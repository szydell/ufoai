/**
 * @file g_ai.c
 * @brief Artificial Intelligence.
 */

/*
Copyright (C) 2002-2009 UFO: Alien Invasion.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "g_local.h"
#include "g_ai.h"

/**
 * @brief Check whether friendly units are in the line of fire when shooting
 * @param[in] ent AI that is trying to shoot
 * @param[in] target Shoot to this location
 * @param[in] spread
 */
static qboolean AI_CheckFF (const edict_t * ent, const vec3_t target, float spread)
{
	edict_t *check = NULL;
	vec3_t dtarget, dcheck, back;
	float cosSpread;

	/* spread data */
	if (spread < 1.0)
		spread = 1.0;
	spread *= torad;
	cosSpread = cos(spread);
	VectorSubtract(target, ent->origin, dtarget);
	VectorNormalize(dtarget);
	VectorScale(dtarget, PLAYER_WIDTH / spread, back);

	while ((check = G_EdictsGetNextLivingActorOfTeam(check, ent->team))) {
		if (ent != check) {
			/* found ally */
			VectorSubtract(check->origin, ent->origin, dcheck);
			if (DotProduct(dtarget, dcheck) > 0.0) {
				/* ally in front of player */
				VectorAdd(dcheck, back, dcheck);
				VectorNormalize(dcheck);
				if (DotProduct(dtarget, dcheck) > cosSpread)
					return qtrue;
			}
		}
	}

	/* no ally in danger */
	return qfalse;
}

/**
 * @brief Check whether the fighter should perform the shoot
 * @todo Check whether radius and power of fd are to to big for dist
 * @todo Check whether the alien will die when shooting
 */
static qboolean AI_FighterCheckShoot (const edict_t* ent, const edict_t* check, const fireDef_t* fd, float *dist)
{
	/* check range */
	*dist = VectorDist(ent->origin, check->origin);
	if (*dist > fd->range)
		return qfalse;
	/* don't shoot - we are to close */
	else if (*dist < fd->splrad)
		return qfalse;

	/* check FF */
	if (!G_IsInsane(ent) && AI_CheckFF(ent, check->origin, fd->spread[0]))
		return qfalse;

	return qtrue;
}

/**
 * @brief Checks whether the AI controlled actor wants to use a door
 * @param[in] ent The AI controlled actor
 * @param[in] door The door edict
 * @returns true if the AI wants to use (open/close) that door, false otherwise
 * @note Don't start any new events in here, don't change the actor state
 * @sa Touch_DoorTrigger
 * @todo Finish implementation
 */
qboolean AI_CheckUsingDoor (const edict_t *ent, const edict_t *door)
{
	/* don't try to use the door in every case */
	if (frand() < 0.3)
		return qfalse;

	/* not in the view frustum - don't use the door while not seeing it */
	if (!G_FrustumVis(door, ent->origin))
		return qfalse;

	/* if the alien is trying to hide and the door is
	* still opened, close it */
	if (ent->hiding && door->doorState == STATE_OPENED)
		return qtrue;

	/* aliens and civilians need different handling */
	switch (ent->team) {
	case TEAM_ALIEN: {
		/* only use the door when there is no civilian or phalanx to kill */
		edict_t *check = NULL;

		/* see if there are enemies */
		while ((check = G_EdictsGetNextLivingActor(check))) {
			float actorVis;
			/* don't check for aliens */
			if (check->team == ent->team)
				continue;
			/* check whether the origin of the enemy is inside the
			 * AI actors view frustum */
			if (!G_FrustumVis(check, ent->origin))
				continue;
			/* check whether the enemy is close enough to change the state */
			if (VectorDist(check->origin, ent->origin) > MAX_SPOT_DIST)
				continue;
			actorVis = G_ActorVis(check->origin, ent, qtrue);
			/* there is a visible enemy, don't use that door */
			if (actorVis > ACTOR_VIS_0)
				return qfalse;
		}
		}
		break;
	case TEAM_CIVILIAN:
		/* don't use any door if no alien is inside the viewing angle  - but
		 * try to hide behind the door when there is an alien */
		break;
	default:
		gi.dprintf("Invalid team in AI_CheckUsingDoor: %i for ent type: %i\n",
			ent->team, ent->type);
		break;
	}
	return qtrue;
}

/**
 * @brief Checks whether it would be smart to change the state to STATE_CROUCHED
 * @param[in] ent The AI controlled actor to chech the state change for
 * @returns true if the actor should go into STATE_CROUCHED, false otherwise
 */
static qboolean AI_CheckCrouch (const edict_t *ent)
{
	edict_t *check = NULL;

	/* see if we are very well visible by an enemy */
	while ((check = G_EdictsGetNextLivingActor(check))) {
		float actorVis;
		/* don't check for civilians or aliens */
		if (check->team == ent->team || G_IsCivilian(check))
			continue;
		/* check whether the origin of the enemy is inside the
		 * AI actors view frustum */
		if (!G_FrustumVis(check, ent->origin))
			continue;
		/* check whether the enemy is close enough to change the state */
		if (VectorDist(check->origin, ent->origin) > MAX_SPOT_DIST)
			continue;
		actorVis = G_ActorVis(check->origin, ent, qtrue);
		if (actorVis >= ACTOR_VIS_50)
			return qtrue;
	}
	return qfalse;
}

/**
 * @param ent The alien edict that checks whether it should hide
 * @return true if hide is needed or false if the alien thinks that it is not needed
 */
static qboolean AI_NoHideNeeded (edict_t *ent)
{
	/* only brave aliens are trying to stay on the field if no dangerous actor is visible */
	if (ent->morale > mor_brave->integer) {
		edict_t *from = NULL;
		/* test if check is visible */
		while ((from = G_EdictsGetNextInUse(from))) {
			/** @todo using the visflags of the ai should be faster here */
			if (G_Vis(-ent->team, ent, from, VT_NOFRUSTUM)) {
				const invList_t *invlist = LEFT(from);
				const fireDef_t *fd = NULL;
				if (invlist && invlist->item.t) {
					fd = FIRESH_FiredefForWeapon(&invlist->item);
				} else {
					invlist = LEFT(from);
					if (invlist && invlist->item.t)
						fd = FIRESH_FiredefForWeapon(&invlist->item);
				}
				/* search the (visible) inventory (by just checking the weapon in the hands of the enemy */
				if (fd != NULL && fd->range * fd->range >= VectorDistSqr(ent->origin, from->origin)) {
					const int damage = max(0, fd->damage[0] + (fd->damage[1] * crand()));
					if (damage >= ent->HP / 3)
						return qtrue;
				}
			}
		}
	}
	return qfalse;
}

static const item_t *AI_GetItemForShootType (shoot_types_t shootType, const edict_t *ent)
{
	/* optimization: reaction fire is automatic */
	if (IS_SHOT_REACTION(shootType))
		return NULL;

	if (IS_SHOT_RIGHT(shootType) && RIGHT(ent)
		&& RIGHT(ent)->item.m
		&& RIGHT(ent)->item.t->weapon
		&& (!RIGHT(ent)->item.t->reload
			|| RIGHT(ent)->item.a > 0)) {
		return &RIGHT(ent)->item;
	} else if (IS_SHOT_LEFT(shootType) && LEFT(ent)
		&& LEFT(ent)->item.m
		&& LEFT(ent)->item.t->weapon
		&& (!LEFT(ent)->item.t->reload
			|| LEFT(ent)->item.a > 0)) {
		return &LEFT(ent)->item;
	} else {
		Com_DPrintf(DEBUG_GAME, "AI_FighterCalcBestAction: todo - grenade/knife toss from inventory using empty hand\n");
		/** @todo grenade/knife toss from inventory using empty hand */
		/** @todo evaluate possible items to retrieve and pick one, then evaluate an action against the nearby enemies or allies */
		return NULL;
	}
}

/**
 * @sa AI_ActorThink
 * @todo fill z_align values
 * @todo optimize this
 */
static float AI_FighterCalcBestAction (edict_t * ent, pos3_t to, aiAction_t * aia)
{
	edict_t *check = NULL;
	int tu;
	pos_t move;
	shoot_types_t shootType;
	float dist, minDist;
	float bestActionPoints, dmg, maxDmg, bestTime = -1, vis;
	const objDef_t *ad;

	bestActionPoints = 0.0;
	memset(aia, 0, sizeof(*aia));
	move = gi.MoveLength(gi.pathingMap, to,
			G_IsCrouched(ent) ? 1 : 0, qtrue);
	tu = ent->TU - move;

	/* test for time */
	if (tu < 0 || move == ROUTING_NOT_REACHABLE)
		return AI_ACTION_NOTHING_FOUND;

	/* set basic parameters */
	VectorCopy(to, ent->pos);
	VectorCopy(to, aia->to);
	VectorCopy(to, aia->stop);
	gi.GridPosToVec(gi.routingMap, ent->fieldSize, to, ent->origin);

	/* shooting */
	maxDmg = 0.0;
	for (shootType = ST_RIGHT; shootType < ST_NUM_SHOOT_TYPES; shootType++) {
		const item_t *item;
		int fdIdx;
		const fireDef_t *fdArray;

		item = AI_GetItemForShootType(shootType, ent);
		if (!item)
			continue;

		fdArray = FIRESH_FiredefForWeapon(item);
		if (fdArray == NULL)
			continue;

		/** @todo timed firedefs that bounce around should not be thrown/shoot about the whole distance */
		for (fdIdx = 0; fdIdx < item->m->numFiredefs[fdArray->weapFdsIdx]; fdIdx++) {
			const fireDef_t *fd = &fdArray[fdIdx];
			const float nspread = SPREAD_NORM((fd->spread[0] + fd->spread[1]) * 0.5 +
				GET_ACC(ent->chr.score.skills[ABILITY_ACCURACY], fd->weaponSkill));
			/* how many shoots can this actor do */
			const int shots = tu / fd->time;
			if (shots) {
				/* search best target */
				while ((check = G_EdictsGetNextLivingActor(check))) {
					if (ent != check && (check->team != ent->team || G_IsInsane(ent))) {
						if (!G_IsVisibleForTeam(check, ent->team))
							continue;

						/* don't shoot civilians in mp */
						if (G_IsCivilian(check) && sv_maxclients->integer > 1 && !G_IsInsane(ent))
							continue;

						if (!AI_FighterCheckShoot(ent, check, fd, &dist))
							continue;

						/* check how good the target is visible */
						vis = G_ActorVis(ent->origin, check, qtrue);
						if (vis == ACTOR_VIS_0)
							continue;

						/* calculate expected damage */
						dmg = vis * (fd->damage[0] + fd->spldmg[0]) * fd->shots * shots;
						if (nspread && dist > nspread)
							dmg *= nspread / dist;

						/* take into account armour */
						if (CONTAINER(check, gi.csi->idArmour)) {
							ad = CONTAINER(check, gi.csi->idArmour)->item.t;
							dmg *= 1.0 - ad->protection[ad->dmgtype] * 0.01;
						}

						if (dmg > check->HP && (check->state & STATE_REACTION))
							/* reaction shooters eradication bonus */
							dmg = check->HP + GUETE_KILL + GUETE_REACTION_ERADICATION;
						else if (dmg > check->HP)
							/* standard kill bonus */
							dmg = check->HP + GUETE_KILL;

						/* ammo is limited and shooting gives away your position */
						if ((dmg < 25.0 && vis < 0.2) /* too hard to hit */
							|| (dmg < 10.0 && vis < 0.6) /* uber-armour */
							|| dmg < 0.1) /* at point blank hit even with a stick */
							continue;

						/* civilian malus */
						if (G_IsCivilian(check) && !G_IsInsane(ent))
							dmg *= GUETE_CIV_FACTOR;

						/* add random effects */
						dmg += GUETE_RANDOM * frand();

						/* check if most damage can be done here */
						if (dmg > maxDmg) {
							maxDmg = dmg;
							bestTime = fd->time * shots;
							aia->shootType = shootType;
							aia->shots = shots;
							aia->target = check;
							aia->fd = fd;
						}
					}
#if 0
				/**
				 * @todo This feature causes the 'aliens shoot at walls'-bug.
				 * I considered adding a visibility check, but that wouldn't prevent aliens
				 * from shooting at the breakable parts of their own ship.
				 * So I disabled it for now. Duke, 23.10.09
				 */
				if (!aia->target) {
					/* search best none human target */
					check = NULL;
					while ((check = G_EdictsGetNextInUse(check))) {
						if (G_IsBreakable(check)) {
							if (!AI_FighterCheckShoot(ent, check, fd, &dist))
								continue;

							/* check whether target is visible enough */
							vis = G_ActorVis(ent->origin, check, qtrue);
							if (vis < ACTOR_VIS_0)
								continue;

							/* don't take vis into account, don't multiply with amout of shots
							 * others (human victims) should be prefered, that's why we don't
							 * want a too high value here */
							maxDmg = (fd->damage[0] + fd->spldmg[0]);
							aia->mode = shootType;
							aia->shots = shots;
							aia->target = check;
							aia->fd = fd;
							bestTime = fd->time * shots;
							/* take the first best breakable or door and try to shoot it */
							break;
						}
					}
				}
#endif
				}
			}
		} /* firedefs */
	}
	/* add damage to bestActionPoints */
	if (aia->target) {
		bestActionPoints += maxDmg;
		assert(bestTime > 0);
		tu -= bestTime;
	}

	if (!G_IsRaged(ent)) {
		/* hide */
		/** @todo Only hide if the visible actors have long range weapons in their hands
		 * otherwise make it depended on the mood (or some skill) of the alien whether
		 * it tries to attack by trying to get as close as possible or to try to hide */
		if (!(G_TestVis(-ent->team, ent, VT_PERISH | VT_NOFRUSTUM) & VIS_YES) || AI_NoHideNeeded(ent)) {
			/* is a hiding spot */
			bestActionPoints += GUETE_HIDE + (aia->target ? GUETE_CLOSE_IN : 0);
		} else if (aia->target && tu >= TU_MOVE_STRAIGHT) {
			byte minX, maxX, minY, maxY;
			const byte crouchingState = G_IsCrouched(ent) ? 1 : 0;
			qboolean stillSearching = qtrue;
			/* reward short walking to shooting spot, when seen by enemies; */
			/** @todo do this decently, only penalizing the visible part of walk
			 * and penalizing much more for reaction shooters around;
			 * now it may remove some tactical options from aliens,
			 * e.g. they may now choose only the closer doors;
			 * however it's still better than going three times around soldier
			 * and only then firing at him */
			bestActionPoints += GUETE_CLOSE_IN - move < 0 ? 0 : GUETE_CLOSE_IN - move;

			/* search hiding spot */
			G_MoveCalc(0, ent, to, crouchingState, HIDE_DIST);
			ent->pos[2] = to[2];
			minX = to[0] - HIDE_DIST > 0 ? to[0] - HIDE_DIST : 0;
			minY = to[1] - HIDE_DIST > 0 ? to[1] - HIDE_DIST : 0;
			/** @todo remove this magic number */
			maxX = to[0] + HIDE_DIST < 254 ? to[0] + HIDE_DIST : 254;
			maxY = to[1] + HIDE_DIST < 254 ? to[1] + HIDE_DIST : 254;

			for (ent->pos[1] = minY; ent->pos[1] <= maxY; ent->pos[1]++) {
				for (ent->pos[0] = minX; ent->pos[0] <= maxX; ent->pos[0]++) {
					/* time */
					const pos_t delta = gi.MoveLength(gi.pathingMap, ent->pos, crouchingState, qfalse);
					if (delta > tu || delta == ROUTING_NOT_REACHABLE)
						continue;

					/* visibility */
					gi.GridPosToVec(gi.routingMap, ent->fieldSize, ent->pos, ent->origin);
					if (G_TestVis(-ent->team, ent, VT_PERISH | VT_NOFRUSTUM) & VIS_YES)
						continue;

					stillSearching = qfalse;
					tu -= delta;
					break;
				}
				if (!stillSearching)
					break;
			}

			if (stillSearching) {
				/* nothing found */
				VectorCopy(to, ent->pos);
				gi.GridPosToVec(gi.routingMap, ent->fieldSize, to, ent->origin);
				/** @todo Try to crouch if no hiding spot was found - randomized */
			} else {
				/* found a hiding spot */
				VectorCopy(ent->pos, aia->stop);
				bestActionPoints += GUETE_HIDE;
				/** @todo also add bonus for fleeing from reaction fire
				 * and a huge malus if more than 1 move under reaction */
			}
		}
	}

	/* reward closing in */
	minDist = CLOSE_IN_DIST;
	check = NULL;
	while ((check = G_EdictsGetNextLivingActor(check))) {
		if (check->team != ent->team) {
			dist = VectorDist(ent->origin, check->origin);
			if (dist < minDist)
				minDist = dist;
		}
	}
	bestActionPoints += GUETE_CLOSE_IN * (1.0 - minDist / CLOSE_IN_DIST);

	return bestActionPoints;
}

/**
 * @brief Calculates possible actions for a civilian.
 * @param[in] ent Pointer to an edict being civilian.
 * @param[in] to The grid position to walk to.
 * @param[in] aia Pointer to aiAction containing informations about possible action.
 * @sa AI_ActorThink
 * @note Even civilians can use weapons if the teamdef allows this
 */
static float AI_CivilianCalcBestAction (edict_t * ent, pos3_t to, aiAction_t * aia)
{
	edict_t *check = NULL;
	int tu;
	pos_t move;
	float minDist, minDistCivilian, minDistFighter;
	float bestActionPoints;
	float reactionTrap = 0.0;
	float delta = 0.0;
	const byte crouchingState = G_IsCrouched(ent) ? 1 : 0;

	/* set basic parameters */
	bestActionPoints = 0.0;
	memset(aia, 0, sizeof(*aia));
	VectorCopy(to, ent->pos);
	VectorCopy(to, aia->to);
	VectorCopy(to, aia->stop);
	gi.GridPosToVec(gi.routingMap, ent->fieldSize, to, ent->origin);

	move = gi.MoveLength(gi.pathingMap, to, crouchingState, qtrue);
	tu = ent->TU - move;

	/* test for time */
	if (tu < 0 || move == ROUTING_NOT_REACHABLE)
		return AI_ACTION_NOTHING_FOUND;

	/* check whether this civilian can use weapons */
	if (ent->chr.teamDef) {
		const teamDef_t* teamDef = ent->chr.teamDef;
		if (!G_IsPaniced(ent) && teamDef->weapons)
			return AI_FighterCalcBestAction(ent, to, aia);
	} else
		gi.dprintf("AI_CivilianCalcBestAction: Error - civilian team with no teamdef\n");

	/* run away */
	minDist = minDistCivilian = minDistFighter = RUN_AWAY_DIST * UNIT_SIZE;

	while ((check = G_EdictsGetNextLivingActor(check))) {
		float dist;
		if (ent == check)
			continue;
		dist = VectorDist(ent->origin, check->origin);
		/* if we are trying to walk to a position that is occupied by another actor already we just return */
		if (!dist)
			return AI_ACTION_NOTHING_FOUND;
		switch (check->team) {
		case TEAM_ALIEN:
			if (dist < minDist)
				minDist = dist;
			break;
		case TEAM_CIVILIAN:
			if (dist < minDistCivilian)
				minDistCivilian = dist;
			break;
		case TEAM_PHALANX:
			if (dist < minDistFighter)
				minDistFighter = dist;
			break;
		}
	}

	minDist /= UNIT_SIZE;
	minDistCivilian /= UNIT_SIZE;
	minDistFighter /= UNIT_SIZE;

	if (minDist < 8.0) {
		/* very near an alien: run away fast */
		delta = 4.0 * minDist;
	} else if (minDist < 16.0) {
		/* near an alien: run away */
		delta = 24.0 + minDist;
	} else if (minDist < 24.0) {
		/* near an alien: run away slower */
		delta = 40.0 + (minDist - 16) / 4;
	} else {
		delta = 42.0;
	}
	/* near a civilian: join him (1/3) */
	if (minDistCivilian < 10.0)
		delta += (10.0 - minDistCivilian) / 3.0;
	/* near a fighter: join him (1/5) */
	if (minDistFighter < 15.0)
		delta += (15.0 - minDistFighter) / 5.0;
	/* don't go close to a fighter to let him move */
	if (minDistFighter < 2.0)
		delta /= 10.0;

	/* try to hide */
	check = NULL;
	while ((check = G_EdictsGetNextLivingActor(check))) {
		if (ent == check)
			continue;
		if (!(G_IsAlien(check) || G_IsInsane(ent)))
			continue;

		if (!G_IsVisibleForTeam(check, ent->team))
			continue;

		if (G_ActorVis(check->origin, ent, qtrue) > 0.25)
			reactionTrap += 25.0;
	}
	delta -= reactionTrap;
	bestActionPoints += delta;

	/* add laziness */
	if (ent->TU)
		bestActionPoints += GUETE_CIV_LAZINESS * tu / ent->TU;
	/* add random effects */
	bestActionPoints += GUETE_CIV_RANDOM * frand();

	return bestActionPoints;
}

/**
 * @brief Searches the map for mission edicts and try to get there
 * @sa AI_PrepBestAction
 * @note The routing table is still valid, so we can still use
 * gi.MoveLength for the given edict here
 */
static int AI_CheckForMissionTargets (const player_t* player, edict_t *ent, aiAction_t *aia)
{
	int bestActionPoints = AI_ACTION_NOTHING_FOUND;
	const byte crouchingState = G_IsCrouched(ent) ? 1 : 0;

	/* reset any previous given action set */
	memset(aia, 0, sizeof(*aia));

	if (ent->team == TEAM_CIVILIAN) {
		edict_t *checkPoint = NULL;
		int length;
		int i = 0;
		/* find waypoints in a closer distance - if civilians are not close enough, let them walk
		 * around until they came close */
		for (checkPoint = ai_waypointList; checkPoint != NULL; checkPoint = checkPoint->groupChain) {
			if (checkPoint->inuse)
				continue;

			/* the lower the count value - the nearer the final target */
			if (checkPoint->count < ent->count) {
				if (VectorDist(ent->origin, checkPoint->origin) <= WAYPOINT_CIV_DIST) {
					const pos_t move = gi.MoveLength(gi.pathingMap, checkPoint->pos, crouchingState, qtrue);
					i++;
					if (move == ROUTING_NOT_REACHABLE)
						continue;

					/* test for time and distance */
					length = ent->TU - move;
					bestActionPoints = GUETE_MISSION_TARGET + length;

					ent->count = checkPoint->count;
					VectorCopy(checkPoint->pos, aia->to);
				}
			}
		}
		/* reset the count value for this civilian to restart the search */
		if (!i)
			ent->count = 100;
	} else if (ent->team == TEAM_ALIEN) {
		/* search for a mission edict */
		edict_t *mission = NULL;
		while ((mission = G_EdictsGetNextInUse(mission))) {
			if (mission->type == ET_MISSION) {
				VectorCopy(mission->pos, aia->to);
				aia->target = mission;
				if (player->pers.team == mission->team) {
					bestActionPoints = GUETE_MISSION_TARGET;
					break;
				} else {
					/* try to prevent the phalanx from reaching their mission target */
					bestActionPoints = GUETE_MISSION_OPPONENT_TARGET;
				}
			}
		}
	}

	return bestActionPoints;
}

#define AI_MAX_DIST	30
/**
 * @brief Attempts to find the best action for an alien. Moves the alien
 * into the starting position for that action and returns the action.
 * @param[in] player The AI player
 * @param[in] ent The AI actor
 */
static aiAction_t AI_PrepBestAction (const player_t *player, edict_t * ent)
{
	aiAction_t aia, bestAia;
	pos3_t oldPos, to;
	vec3_t oldOrigin;
	int xl, yl, xh, yh;
	float bestActionPoints, best;
	const byte crouchingState = G_IsCrouched(ent) ? 1 : 0;

	/* calculate move table */
	G_MoveCalc(0, ent, ent->pos, crouchingState, MAX_ROUTE);
	Com_DPrintf(DEBUG_ENGINE, "AI_PrepBestAction: Called MoveMark.\n");
	gi.MoveStore(gi.pathingMap);

	/* set borders */
	xl = (int) ent->pos[0] - AI_MAX_DIST;
	if (xl < 0)
		xl = 0;
	yl = (int) ent->pos[1] - AI_MAX_DIST;
	if (yl < 0)
		yl = 0;
	xh = (int) ent->pos[0] + AI_MAX_DIST;
	if (xh > PATHFINDING_WIDTH)
		xl = PATHFINDING_WIDTH;
	yh = (int) ent->pos[1] + AI_MAX_DIST;
	if (yh > PATHFINDING_WIDTH)
		yh = PATHFINDING_WIDTH;

	/* search best action */
	best = AI_ACTION_NOTHING_FOUND;
	VectorCopy(ent->pos, oldPos);
	VectorCopy(ent->origin, oldOrigin);

	/* evaluate moving to every possible location in the search area,
	 * including combat considerations */
	for (to[2] = 0; to[2] < PATHFINDING_HEIGHT; to[2]++)
		for (to[1] = yl; to[1] < yh; to[1]++)
			for (to[0] = xl; to[0] < xh; to[0]++) {
				const pos_t move = gi.MoveLength(gi.pathingMap, to, crouchingState, qtrue);
				if (move != ROUTING_NOT_REACHABLE && move <= ent->TU) {
					if (G_IsCivilian(ent) || G_IsPaniced(ent))
						bestActionPoints = AI_CivilianCalcBestAction(ent, to, &aia);
					else
						bestActionPoints = AI_FighterCalcBestAction(ent, to, &aia);

					if (bestActionPoints > best) {
						bestAia = aia;
						best = bestActionPoints;
					}
				}
			}

	VectorCopy(oldPos, ent->pos);
	VectorCopy(oldOrigin, ent->origin);

	bestActionPoints = AI_CheckForMissionTargets(player, ent, &aia);
	if (bestActionPoints > best) {
		bestAia = aia;
		best = bestActionPoints;
	}

	/* nothing found to do */
	if (best == AI_ACTION_NOTHING_FOUND) {
		bestAia.target = NULL;
		return bestAia;
	}

	/* check if the actor is in crouched state and try to stand up before doing the move */
	if (G_IsCrouched(ent))
		G_ClientStateChange(player, ent, STATE_CROUCHED, qtrue);

	/* do the move */
	G_ClientMove(player, 0, ent, bestAia.to);

	/* test for possible death during move. reset bestAia due to dead status */
	if (G_IsDead(ent))
		memset(&bestAia, 0, sizeof(bestAia));

	return bestAia;
}

edict_t *ai_waypointList;

void G_AddToWayPointList (edict_t *ent)
{
	int i = 1;

	if (!ai_waypointList)
		ai_waypointList = ent;
	else {
		edict_t *e = ai_waypointList;
		while (e->groupChain) {
			e = e->groupChain;
			i++;
		}
		i++;
		e->groupChain = ent;
	}
}

/**
 * @brief This function will turn the AI actor into the direction that is needed to walk
 * to the given location
 * @param[in] aiActor The actor to turn
 * @param[in] pos The position to set the direction for
 */
void AI_TurnIntoDirection (edict_t *aiActor, const pos3_t pos)
{
	int dv;
	const byte crouchingState = G_IsCrouched(aiActor) ? 1 : 0;

	G_MoveCalc(aiActor->team, aiActor, pos, crouchingState, MAX_ROUTE);

	dv = gi.MoveNext(gi.routingMap, aiActor->fieldSize, gi.pathingMap, pos, crouchingState);
	if (dv != ROUTING_UNREACHABLE) {
		const byte dir = getDVdir(dv);
		/* Only attempt to turn if the direction is not a vertical only action */
		if (dir < CORE_DIRECTIONS || dir >= FLYING_DIRECTIONS)
			G_ActorDoTurn(aiActor, dir & (CORE_DIRECTIONS - 1));
	}
}

/**
 * @brief if a weapon can be reloaded we attempt to do so if TUs permit, otherwise drop it
 */
static void AI_TryToReloadWeapon (edict_t *ent, containerIndex_t containerID)
{
	if (G_ClientCanReload(G_PLAYER_FROM_ENT(ent), ent, containerID)) {
		G_ActorReload(ent, INVDEF(containerID));
	} else {
		G_ActorInvMove(ent, INVDEF(containerID), CONTAINER(ent, containerID), INVDEF(gi.csi->idFloor), NONE, NONE, qtrue);
	}
}

/**
 * @brief The think function for the ai controlled aliens
 * @param[in] player
 * @param[in] ent
 * @sa AI_FighterCalcBestAction
 * @sa AI_CivilianCalcBestAction
 * @sa G_ClientMove
 * @sa G_ClientShoot
 */
void AI_ActorThink (player_t * player, edict_t * ent)
{
	aiAction_t bestAia;

	/* if a weapon can be reloaded we attempt to do so if TUs permit, otherwise drop it */
	if (!G_IsPaniced(ent)) {
		if (RIGHT(ent) && RIGHT(ent)->item.t->reload && RIGHT(ent)->item.a == 0)
			AI_TryToReloadWeapon(ent, gi.csi->idRight);
		if (LEFT(ent) && LEFT(ent)->item.t->reload && LEFT(ent)->item.a == 0)
			AI_TryToReloadWeapon(ent, gi.csi->idLeft);
	}

	/* if both hands are empty, attempt to get a weapon out of backpack if TUs permit */
	if (ent->chr.teamDef->weapons && !LEFT(ent) && !RIGHT(ent))
		G_ClientGetWeaponFromInventory(player, ent);

	bestAia = AI_PrepBestAction(player, ent);

	/* shoot and hide */
	if (bestAia.target) {
		const int fdIdx = bestAia.fd ? bestAia.fd->fdIdx : 0;
		/* shoot until no shots are left or target died */
		while (bestAia.shots) {
			G_ClientShoot(player, ent, bestAia.target->pos, bestAia.shootType, fdIdx, NULL, qtrue, bestAia.z_align);
			bestAia.shots--;
			/* died by our own shot? */
			if (G_IsDead(ent))
				return;
			/* check for target's death */
			if (G_IsDead(bestAia.target)) {
				/* search another target now */
				bestAia = AI_PrepBestAction(player, ent);
				/* no other target found - so no need to hide */
				if (!bestAia.target)
					return;
			}
		}
		ent->hiding = qtrue;

		/* now hide - for this we use the team of the alien actor because a phalanx soldier
		 * might become visible during the hide movement */
		G_ClientMove(player, ent->team, ent, bestAia.stop);
		/* no shots left, but possible targets left - maybe they shoot back
		 * or maybe they are still close after hiding */

		/* decide whether the actor maybe wants to go crouched */
		if (AI_CheckCrouch(ent))
			G_ClientStateChange(player, ent, STATE_CROUCHED, qfalse);

		/* actor is still alive - try to turn into the appropriate direction to see the target
		 * actor once he sees the ai, too */
		AI_TurnIntoDirection(ent, bestAia.target->pos);

		/** @todo If possible targets that can shoot back (check their inventory for weapons, not for ammo)
		 * are close, go into reaction fire mode, too */
		/* G_ClientStateChange(player, ent->number, STATE_REACTION_ONCE, qtrue); */

		ent->hiding = qfalse;
	}
}


/**
 * @brief Every server frame one single actor is handled - always in the same order
 * @sa G_RunFrame
 */
void AI_Run (void)
{
	player_t *player;
	int i;

	/* don't run this too often to prevent overflows */
	if (level.framenum % 10)
		return;

	/* set players to ai players and cycle over all of them */
	for (i = 0, player = game.players + game.sv_maxplayersperteam; i < game.sv_maxplayersperteam; i++, player++)
		if (player->inuse && G_IsAIPlayer(player) && level.activeTeam == player->pers.team) {
			/* find next actor to handle */
			edict_t *ent = player->pers.last;

			while ((ent = G_EdictsGetNextLivingActorOfTeam(ent, player->pers.team))) {
				if (ent->TU) {
					if (g_ailua->integer)
						AIL_ActorThink(player, ent);
					else
						AI_ActorThink(player, ent);
					player->pers.last = ent;
					return;
				}
			}

			/* nothing left to do, request endround */
			G_ClientEndRound(player);
			player->pers.last = NULL;
			return;
		}
}

/**
 * @brief Initializes the actor's stats like morals, strength and so on.
 * @param ent Actor to set the stats for.
 * @param[in] team Team to which actor belongs.
 */
static void AI_SetStats (edict_t * ent, int team)
{
	CHRSH_CharGenAbilitySkills(&ent->chr, sv_maxclients->integer >= 2);

	ent->HP = ent->chr.HP;
	ent->morale = ent->chr.morale;
	ent->STUN = 0;
}


/**
 * @brief Sets an actor's ingame model and character values.
 * @param ent Actor to set the model for.
 * @param[in] team Team to which actor belongs.
 */
static void AI_SetModelAndCharacterValues (edict_t * ent, int team)
{
	/* Set model. */
	const char *teamDefintion;
	if (team != TEAM_CIVILIAN) {
		if (gi.csi->numAlienTeams) {
			const int alienTeam = rand() % gi.csi->numAlienTeams;
			assert(gi.csi->alienTeams[alienTeam]);
			teamDefintion = gi.csi->alienTeams[alienTeam]->id;
		} else
			teamDefintion = gi.Cvar_String("ai_alien");
	} else if (team == TEAM_CIVILIAN) {
		teamDefintion = gi.Cvar_String("ai_civilian");
	}
	gi.GetCharacterValues(teamDefintion, &ent->chr);
	ent->body = gi.ModelIndex(CHRSH_CharGetBody(&ent->chr));
	ent->head = gi.ModelIndex(CHRSH_CharGetHead(&ent->chr));
	if (!ent->chr.teamDef)
		gi.error("Could not set teamDef for character: '%s'", teamDefintion);
}


/**
 * @brief Sets the actor's equipment.
 * @param ent Actor to give equipment to.
 * @param[in] team Team to which the actor belongs.
 * @param[in] ed Equipment definition for the new actor.
 */
static void AI_SetEquipment (edict_t * ent, int team, equipDef_t * ed)
{
	/* Pack equipment. */
	if (team != TEAM_CIVILIAN) { /** @todo Give civilians gear. */
		if (ent->chr.teamDef->weapons)
			game.i.EquipActor(&game.i, &ent->i, ed, &ent->chr);
		else if (ent->chr.teamDef)
			/* actor cannot handle equipment but no weapons */
			game.i.EquipActorMelee(&game.i, &ent->i, &ent->chr);
		else
			gi.dprintf("AI_InitPlayer: actor with no equipment\n");
	}
}


/**
 * @brief Initializes the actor.
 * @param[in] player Player to which this actor belongs.
 * @param[in,out] ent Pointer to edict_t representing actor.
 * @param[in] ed Equipment definition for the new actor.
 */
static void AI_InitPlayer (player_t * player, edict_t * ent, equipDef_t * ed)
{
	const int team = player->pers.team;

	/* Set Actor state. */
	ent->type = ET_ACTOR;
	ent->pnum = player->num;

	/* Set the model and chose alien race. */
	AI_SetModelAndCharacterValues(ent, team);

	/* Calculate stats. */
	AI_SetStats(ent, team);

	/* Give equipment. */
	AI_SetEquipment(ent, team, ed);

	/* More tweaks */
	if (team != TEAM_CIVILIAN) {
		/* no need to call G_SendStats for the AI - reaction fire is serverside only for the AI */
		G_ClientStateChange(player, ent, STATE_REACTION_ONCE, qfalse);
	}

	/* initialize the LUA AI now */
	if (team == TEAM_CIVILIAN)
		AIL_InitActor(ent, "civilian", "default");
	else if (team == TEAM_ALIEN)
		AIL_InitActor(ent, "alien", "default");
	else
		gi.dprintf("AI_InitPlayer: unknown team AI\n");

	/* link the new actor entity */
	gi.LinkEdict(ent);
}

#define MAX_SPAWNPOINTS		64
static edict_t * spawnPoints[MAX_SPAWNPOINTS];


/**
 * @brief Spawn civilians and aliens
 * @param[in] player
 * @param[in] numSpawn
 * @sa AI_CreatePlayer
 */
static void G_SpawnAIPlayer (player_t * player, int numSpawn)
{
	edict_t *ent = NULL;
	equipDef_t *ed = &gi.csi->eds[0];
	int i, j, numPoints, team;
	char name[MAX_VAR];

	/* search spawn points */
	team = player->pers.team;
	numPoints = 0;
	while ((ent = G_EdictsGetNextInUse(ent)))
		if (ent->type == ET_ACTORSPAWN && ent->team == team)
			spawnPoints[numPoints++] = ent;

	/* check spawn point number */
	if (numPoints < numSpawn) {
		gi.dprintf("Not enough spawn points for team %i\n", team);
		numSpawn = numPoints;
	}

	/* prepare equipment */
	if (team != TEAM_CIVILIAN) {
		Q_strncpyz(name, gi.Cvar_String("ai_equipment"), sizeof(name));
		for (i = 0, ed = gi.csi->eds; i < gi.csi->numEDs; i++, ed++)
			if (!strcmp(name, ed->name))
				break;
		if (i == gi.csi->numEDs)
			ed = &gi.csi->eds[0];
	}

	/* spawn players */
	for (j = 0; j < numSpawn; j++) {
		assert(numPoints > 0);
		ent = spawnPoints[rand() % numPoints];
		/* select spawnpoint */
		while (ent->type != ET_ACTORSPAWN)
			ent = spawnPoints[rand() % numPoints];

		/* spawn */
		level.num_spawned[team]++;
		level.num_alive[team]++;
		/* initialize the new actor */
		AI_InitPlayer(player, ent, ed);
	}
	/* show visible actors */
	G_ClearVisFlags(team);
	G_CheckVis(NULL, qfalse);

	/* give time */
	G_GiveTimeUnits(team);
}


/**
 * @brief Spawn civilians and aliens
 * @param[in] team
 * @sa G_SpawnAIPlayer
 * @return player_t pointer
 * @note see cvars ai_numaliens, ai_numcivilians, ai_numactors
 */
player_t *AI_CreatePlayer (int team)
{
	player_t *p;
	int i;

	if (!sv_ai->integer) {
		gi.dprintf("AI deactivated - set sv_ai cvar to 1 to activate it\n");
		return NULL;
	}

	/* set players to ai players and cycle over all of them */
	for (i = 0, p = game.players + game.sv_maxplayersperteam; i < game.sv_maxplayersperteam; i++, p++)
		if (!p->inuse) {
			memset(p, 0, sizeof(*p));
			p->inuse = qtrue;
			p->num = p - game.players;
			p->pers.ai = qtrue;
			G_SetTeamForPlayer(p, team);
			if (p->pers.team == TEAM_CIVILIAN)
				G_SpawnAIPlayer(p, ai_numcivilians->integer);
			else if (sv_maxclients->integer == 1)
				G_SpawnAIPlayer(p, ai_numaliens->integer);
			else
				G_SpawnAIPlayer(p, ai_numactors->integer);
			gi.dprintf("Created AI player (team %i)\n", p->pers.team);
			return p;
		}

	/* nothing free */
	return NULL;
}
