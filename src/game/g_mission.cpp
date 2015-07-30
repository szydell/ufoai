/**
 * @file
 * @brief Mission related code - king of the hill and so on.
 */

/*
All original material Copyright (C) 2002-2015 UFO: Alien Invasion.

Original file from Quake 2 v3.21: quake2-2.31/game/g_spawn.c
Copyright (C) 1997-2001 Id Software, Inc.

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

#include "g_mission.h"
#include "g_actor.h"
#include "g_client.h"
#include "g_edicts.h"
#include "g_inventory.h"
#include "g_match.h"
#include "g_spawn.h"
#include "g_utils.h"

void G_MissionAddVictoryMessage (const char* message)
{
	gi.ConfigString(CS_VICTORY_CONDITIONS, "%s", message);
}

/**
 * @brief Mission trigger
 * @todo use level.nextmap to spawn another map when every living actor has touched the mission trigger
 * @todo use level.actualRound to determine the 'King of the Hill' time
 * @note Don't set a client action here - otherwise the movement event might
 * be corrupted
 */
bool G_MissionTouch (Edict* self, Edict* activator)
{
	if (!self->owner())
		return false;

	if (!G_IsLivingActor(activator))
		return false;
	Actor* actor = makeActor(activator);

	if (self->owner()->isOpponent(actor)) {
		/* reset king of the hill counter */
		gi.BroadcastPrintf(PRINT_HUD, _("Team %i has entered team %i's target zone!"),
				actor->getTeam(), self->owner()->getTeam());
		self->owner()->count = 0;
		return false;
	}
	if (self->owner()->count)
		return false;

	if (self->owner()->isSameTeamAs(actor)) {
		self->owner()->count = level.actualRound;
		if (!self->owner()->item) {
			gi.BroadcastPrintf(PRINT_HUD, _("Team %i has occupied its target zone!"),
					actor->getTeam());
			return true;
		}
	}

	/* search the item in the activator's inventory */
	/* ignore items linked from any temp container the actor must have this in his hands */
	const Container* cont = nullptr;
	while ((cont = actor->chr.inv.getNextCont(cont))) {
		Item* item = nullptr;
		while ((item = cont->getNextItem(item))) {
			const objDef_t* od = item->def();
			/* check whether we found the searched item in the actor's inventory */
			if (!Q_streq(od->id, self->owner()->item))
				continue;

			/* drop the weapon - even if out of TUs */
			G_ActorInvMove(actor, cont->def(), item, INVDEF(CID_FLOOR), NONE, NONE, false);
			gi.BroadcastPrintf(PRINT_HUD, _("Item was placed."));
			self->owner()->count = level.actualRound;
			return true;
		}
	}

	return true;
}

void G_MissionReset (Edict* self, Edict* activator)
{
	/* Don't reset the mission timer for 'bring item' missions G_MissionThink will handle that */
	if (!self->owner() || !self->owner()->time || self->owner()->item)
		return;
	linkedList_t* touched = self->touchedList;
	while (touched) {
		const Edict* const ent = static_cast<const Edict* const>(touched->data);
		if (self->owner()->isSameTeamAs(ent) && !(G_IsDead(ent) || ent == activator)) {
			return;
		}
		touched = touched->next;
	}
	/* All team actors are gone, reset counter */
	gi.BroadcastPrintf(PRINT_HUD, _("Target zone is unoccupied!"));
	self->owner()->count = 0;
}

/**
 * @brief Mission trigger destroy function
 */
bool G_MissionDestroy (Edict* self)
{
	return true;
}

/**
 * @brief Mission trigger use function
 */
bool G_MissionUse (Edict* self, Edict* activator)
{
	Edict* target = G_EdictsFindTargetEntity(self->target);
	if (!target) {
		gi.DPrintf("Target '%s' wasn't found for misc_mission\n", self->target);
		G_FreeEdict(self);
		return false;
	}

	if (target->destroy) {
		/* set this to zero to determine that this is a triggered destroy call */
		target->HP = 0;
		target->destroy(target);
		/* freed when the level changes */
		self->target = nullptr;
		self->use = nullptr;
	} else if (target->use)
		target->use(target, activator);

	return true;
}

/**
 * @note Think functions are only executed when the match is running
 * or in other word, the game has started
 */
void G_MissionThink (Edict* self)
{
	if (!G_MatchIsRunning())
		return;

	/* when every player has joined the match - spawn the mission target
	 * particle (if given) to mark the trigger */
	if (self->particle) {
		self->link = G_SpawnParticle(self->origin, self->spawnflags, self->particle);

		/* This is automatically freed on map shutdown */
		self->particle = nullptr;
	}

	Edict* chain = self->groupMaster;
	if (!chain)
		chain = self;
	while (chain) {
		if (chain->type == ET_MISSION) {
			if (chain->item) {
				const Item* ic;
				G_GetFloorItems(chain);
				ic = chain->getFloor();
				if (!ic) {
					/* reset the counter if there is no item */
					chain->count = 0;
					return;
				}
				for (; ic; ic = ic->getNext()) {
					const objDef_t* od = ic->def();
					assert(od);
					/* not the item we are looking for */
					if (Q_streq(od->id, chain->item))
						break;
				}
				if (!ic) {
					/* reset the counter if it's not the searched item */
					chain->count = 0;
					return;
				}
			}
			if (chain->time) {
				/* Check that the target zone is still occupied (last defender might have died) */
				if (chain->child()) {
					int numTouched = 0;
					linkedList_t* touched = chain->child()->touchedList;
					while (touched) {
						const Edict* const ent = static_cast<const Edict* const>(touched->data);
						if (chain->isSameTeamAs(ent) && !G_IsDead(ent))
							++numTouched;
						touched = touched->next;
					}
					/* No one occupies the target anymore */
					if (numTouched < 1)
						chain->count = 0;
				}

				const int endTime = level.actualRound - chain->count;
				const int spawnIndex = (chain->getTeam() + level.teamOfs) % MAX_TEAMS;
				const int currentIndex = (level.activeTeam + level.teamOfs) % MAX_TEAMS;
				/* not every edict in the group chain has
				 * been occupied long enough */
				if (!chain->count || endTime < chain->time ||
						(endTime == level.actualRound && spawnIndex <= currentIndex))
					return;
			}
		}
		chain = chain->groupChain;
	}

	G_UseEdict(self, nullptr);

	/* store team before the edict is released */
	const int team = self->getTeam();
	chain = self->groupMaster;
	if (!chain)
		chain = self;
	while (chain) {
		if (chain->item != nullptr) {
			Edict* item = G_GetEdictFromPos(chain->pos, ET_ITEM);
			if (item != nullptr) {
				if (!G_InventoryRemoveItemByID(chain->item, item, CID_FLOOR)) {
					Com_Printf("Could not remove item '%s' from floor edict %i\n", chain->item, item->getIdNum());
				} else if (!item->getFloor()) {
					G_EventPerish(*item);
					G_FreeEdict(item);
				}
			}
		}
		if (chain->link != nullptr) {
			Edict* particle = G_GetEdictFromPos(chain->pos, ET_PARTICLE);
			if (particle != nullptr) {
				G_AppearPerishEvent(G_VisToPM(particle->visflags), false, *particle, nullptr);
				G_FreeEdict(particle);
			}
			chain->link = nullptr;
		}

		Edict* ent = chain->groupChain;
		/* free the trigger */
		if (chain->child())
			G_FreeEdict(chain->child());
		/* free the group chain */
		G_FreeEdict(chain);
		chain = ent;
	}
	self = nullptr;

	/* still active mission edicts left */
	Edict* ent = nullptr;
	while ((ent = G_EdictsGetNextInUse(ent)))
		if (ent->type == ET_MISSION && ent->getTeam() == team)
			return;

	G_MatchEndTrigger(team, 10);
}
