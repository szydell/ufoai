/**
 * @file cl_scrn.c
 * @brief Master for refresh, status bar, console, chat, notify, etc
 *
 * Full screen console.
 * Put up loading plaque.
 * Blanked background with loading plaque.
 * Blanked background with menu.
 * Full screen image for quit and victory.
 * End of unit intermissions.
 */

/*
All original materal Copyright (C) 2002-2007 UFO: Alien Invasion team.

Original file from Quake 2 v3.21: quake2-2.31/client/
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

#include "client.h"
#include "cl_actor.h"
#include "cl_view.h"
#include "../renderer/r_main.h"
#include "../renderer/r_draw.h"
#include "menu/m_inventory.h"
#include "menu/m_draw.h"

static float scr_con_current;			/* aproaches scr_conlines at scr_conspeed */
static float scr_conlines;				/* 0.0 to 1.0 lines of console to display */

static qboolean scr_initialized = qfalse;/* ready to draw */

static int scr_draw_loading = 0;

static cvar_t *scr_conspeed;
static cvar_t *scr_consize;
static cvar_t *scr_rspeed;
static cvar_t *cl_show_cursor_tooltips;

static char cursor_pic[MAX_QPATH];

/**
 * @sa Font_DrawString
 */
static void SCR_DrawString (int x, int y, const char *string, qboolean bitmapFont)
{
	if (!string || !*string)
		return;

	if (bitmapFont) {
		while (*string) {
			R_DrawChar(x, y, *string);
			x += con_fontWidth;
			string++;
		}
	} else
		R_FontDrawString("f_verysmall", ALIGN_UL, x, y, 0, 0, VID_NORM_WIDTH, VID_NORM_HEIGHT, 12, string, 0, 0, NULL, qfalse);
}

/**
 * @sa SCR_DrawLoading
 */
static void SCR_DrawLoadingBar (int x, int y, int w, int h, int percent)
{
	static vec4_t color = {0.3f, 0.3f, 0.3f, 0.7f};
	static vec4_t color_bar = {0.8f, 0.8f, 0.8f, 0.7f};

	R_DrawFill(x, y, w, h, ALIGN_UL, color);

	if (percent != 0)
		R_DrawFill((int)(x+(h*0.2)), (int)(y+(h*0.2)), (int)((w-(h*0.4))*percent*0.01), (int)(h*0.6), ALIGN_UL, color_bar);
}

/**
 * @brief Precache and loading screen at startup
 * @sa CL_InitAfter
 * @param[in] string Draw the loading string - if the scripts are not parsed, this is
 * not possible, so use qfalse for very early calls
 */
void SCR_DrawPrecacheScreen (qboolean string)
{
	R_BeginFrame();
	R_DrawNormPic(0, 0, VID_NORM_WIDTH, VID_NORM_HEIGHT, 0, 0, 0, 0, ALIGN_UL, qfalse, "loading");
	if (string) {
		/* Not used with gettext because it would make removing it too easy. */
		R_FontDrawString("f_menubig", ALIGN_UC,
			(int)(VID_NORM_WIDTH / 2),
			30,
			0, 1, VID_NORM_WIDTH, VID_NORM_HEIGHT, 50, "Download this game for free at http://ufoai.sf.net", 0, 0, NULL, qfalse);
	}
	SCR_DrawLoadingBar((int)(VID_NORM_WIDTH / 2) - 300, VID_NORM_HEIGHT - 30, 600, 20, (int)cls.loadingPercent);
	R_EndFrame();
}

/**
 * @brief Updates needed cvar for loading screens
 */
const char* SCR_SetLoadingBackground (const char *mapString)
{
	char loadingPic[MAX_QPATH];
	char tmpPicName[MAX_VAR];
	const char *mapname;
	size_t len;

	if (!mapString || Com_ServerState())
		mapname = Cvar_VariableString("mapname");
	else {
		mapname = mapString;
		Cvar_Set("mapname", mapString);
	}

	if (!*mapname)
		return NULL;
	else if (*mapname != '+') {
		Q_strncpyz(tmpPicName, mapname, sizeof(tmpPicName));
		len = strlen(tmpPicName);
		/* strip of the day and night letter */
		if (tmpPicName[len - 1] == 'n' || tmpPicName[len - 1] == 'd')
			tmpPicName[len - 1] = '\0';
		if (FS_CheckFile(va("pics/maps/loading/%s.jpg", tmpPicName)) > 0)
			Com_sprintf(loadingPic, sizeof(loadingPic), "maps/loading/%s.jpg", tmpPicName);
		else
			Q_strncpyz(loadingPic, "maps/loading/default.jpg", sizeof(loadingPic));
		Cvar_Set("mn_mappicbig", loadingPic);
	} else {
		Q_strncpyz(loadingPic, "maps/loading/default.jpg", sizeof(loadingPic));
		Cvar_Set("mn_mappicbig", loadingPic);
	}
	return Cvar_VariableString("mn_mappicbig");
}

/**
 * @brief Draws the current downloading status
 * @sa SCR_DrawLoadingBar
 * @sa CL_StartHTTPDownload
 * @sa CL_HTTP_Progress
 */
static void SCR_DrawDownloading (void)
{
	const char *dlmsg = va(_("Downloading [%s]"), cls.downloadName);
	R_FontDrawString("f_menubig", ALIGN_UC,
		(int)(VID_NORM_WIDTH / 2),
		(int)(VID_NORM_HEIGHT / 2 - 60),
		(int)(VID_NORM_WIDTH / 2),
		(int)(VID_NORM_HEIGHT / 2 - 60),
		VID_NORM_WIDTH, VID_NORM_HEIGHT, 50, dlmsg, 1, 0, NULL, qfalse);

	SCR_DrawLoadingBar((int)(VID_NORM_WIDTH / 2) - 300, VID_NORM_HEIGHT - 30, 600, 20, (int)cls.downloadPercent);
}

/**
 * @brief Draws the current loading pic of the map from base/pics/maps/loading
 * @sa SCR_DrawLoadingBar
 */
static void SCR_DrawLoading (void)
{
	static const char *loadingPic;
	const vec4_t color = {0.0, 0.7, 0.0, 0.8};
	char *mapmsg;

	if (cls.downloadName[0]) {
		SCR_DrawDownloading();
		return;
	}

	if (!scr_draw_loading) {
		loadingPic = NULL;
		return;
	}

	if (!loadingPic)
		loadingPic = SCR_SetLoadingBackground(selMis ? selMis->def->mapDef->map : cl.configstrings[CS_MAPTITLE]);
	if (!loadingPic)
		return;
	R_DrawNormPic(0, 0, VID_NORM_WIDTH, VID_NORM_HEIGHT, 0, 0, 0, 0, ALIGN_UL, qfalse, loadingPic);
	R_Color(color);

	if (cl.configstrings[CS_TILES][0]) {
		mapmsg = va(_("Loading Map [%s]"), _(cl.configstrings[CS_MAPTITLE]));
		R_FontDrawString("f_menubig", ALIGN_UC,
			(int)(VID_NORM_WIDTH / 2),
			(int)(VID_NORM_HEIGHT / 2 - 60),
			(int)(VID_NORM_WIDTH / 2),
			(int)(VID_NORM_HEIGHT / 2 - 60),
			VID_NORM_WIDTH, VID_NORM_HEIGHT, 50, mapmsg, 1, 0, NULL, qfalse);
	}

	R_FontDrawString("f_menu", ALIGN_UC,
		(int)(VID_NORM_WIDTH / 2),
		(int)(VID_NORM_HEIGHT / 2),
		(int)(VID_NORM_WIDTH / 2),
		(int)(VID_NORM_HEIGHT / 2),
		VID_NORM_WIDTH, VID_NORM_HEIGHT, 50, cls.loadingMessages, 1, 0, NULL, qfalse);

	SCR_DrawLoadingBar((int)(VID_NORM_WIDTH / 2) - 300, VID_NORM_HEIGHT - 30, 600, 20, (int)cls.loadingPercent);
}

/**
 * @brief Allows rendering code to cache all needed sbar graphics
 */
static void SCR_TouchPics (void)
{
	if (cursor->integer) {
		if (cursor->integer > 9 || cursor->integer < 0)
			Cvar_SetValue("cursor", 1);

		R_RegisterPic("wait");
		R_RegisterPic("ducked");
		Com_sprintf(cursor_pic, sizeof(cursor_pic), "cursor%i", cursor->integer);
		if (!R_RegisterPic(cursor_pic)) {
			Com_Printf("SCR_TouchPics: Could not register cursor: %s\n", cursor_pic);
			cursor_pic[0] = 0;
		}
	}
}

static const vec4_t cursorBG = { 0.0f, 0.0f, 0.0f, 0.7f };
/**
 * @brief Draws the 3D-cursor in battlemode and the icons/info next to it.
 */
static void SCR_DrawCursor (void)
{
	int icon_offset_x = 16;	/* Offset of the first icon on the x-axis. */
	int icon_offset_y = 16;	/* Offset of the first icon on the y-axis. */
	int icon_spacing = 2;	/* the space between different icons. */

	if (!cursor->integer || cls.playingCinematic == CIN_STATUS_FULLSCREEN)
		return;

	if (cursor->modified) {
		cursor->modified = qfalse;
		SCR_TouchPics();
	}

	if (!cursor_pic[0])
		return;

	if (mouseSpace != MS_DRAG) {
		if (cls.state == ca_active && cls.team != cl.actTeam)
			R_DrawNormPic(mousePosX, mousePosY, 0, 0, 0, 0, 0, 0, ALIGN_CC, qtrue, "wait");
		else {
			R_DrawNormPic(mousePosX, mousePosY, 0, 0, 0, 0, 0, 0, ALIGN_CC, qtrue, cursor_pic);
			if (cl_showCoords->integer)
				SCR_DrawString(mousePosX * viddef.rx, mousePosY * viddef.rx, va("%i:%i", mousePosX, mousePosY), qtrue);
		}

		if (cls.state == ca_active && mouseSpace == MS_WORLD) {
			if (selActor) {
				/* Display 'crouch' icon if actor is crouched. */
				if (selActor->state & STATE_CROUCHED)
					R_DrawNormPic(mousePosX + icon_offset_x, mousePosY + icon_offset_y, 0, 0, 0, 0, 0, 0, ALIGN_CC, qtrue, "ducked");
				icon_offset_y += 16;	/* Height of 'crouched' icon. */
				icon_offset_y += icon_spacing;

				/* Display 'Reaction shot' icon if actor has it activated. */
				if (selActor->state & STATE_REACTION_ONCE)
					R_DrawNormPic(mousePosX + icon_offset_x, mousePosY + icon_offset_y, 0, 0, 0, 0, 0, 0, ALIGN_CC, qtrue, "reactionfire");
				else if (selActor->state & STATE_REACTION_MANY)
					R_DrawNormPic(mousePosX + icon_offset_x, mousePosY + icon_offset_y, 0, 0, 0, 0, 0, 0, ALIGN_CC, qtrue, "reactionfiremany");
				icon_offset_y += 16;	/* Height of 'reaction fire' icon. ... just in case we add further icons below.*/
				icon_offset_y += icon_spacing;

				/* Display weaponmode (text) heR_ */
				if (mn.menuText[TEXT_MOUSECURSOR_RIGHT] && cl_show_cursor_tooltips->integer)
					SCR_DrawString(mousePosX + icon_offset_x, mousePosY - 16, mn.menuText[TEXT_MOUSECURSOR_RIGHT], qfalse);
			}

			/* playernames */
			if (mn.menuText[TEXT_MOUSECURSOR_PLAYERNAMES] && cl_show_cursor_tooltips->integer) {
				/*@todo: activate this:
				R_DrawFill(mx + icon_offset_x - 1, my - 33, 20, 128, 0, cursorBG);
				*/
				SCR_DrawString(mousePosX + icon_offset_x, mousePosY - 32, mn.menuText[TEXT_MOUSECURSOR_PLAYERNAMES], qfalse);
				MN_MenuTextReset(TEXT_MOUSECURSOR_PLAYERNAMES);
			}
		}
	} else {
		const vec3_t org = { mousePosX, mousePosY, -50 };
		const vec3_t scale = { 3.5, 3.5, 3.5 };
		const vec4_t color = { 1, 1, 1, 1 };
		MN_DrawItem(org, dragItem, 0, 0, 0, 0, scale, color);
	}
}


/**
 * @brief Scroll it up or down
 */
void SCR_RunConsole (void)
{
	/* decide on the height of the console */
	if (cls.key_dest == key_console)
		scr_conlines = scr_consize->value;	/* half screen */
	else
		scr_conlines = 0;		/* none visible */

	if (scr_conlines < scr_con_current) {
		scr_con_current -= scr_conspeed->value * cls.frametime;
		if (scr_conlines > scr_con_current)
			scr_con_current = scr_conlines;

	} else if (scr_conlines > scr_con_current) {
		scr_con_current += scr_conspeed->value * cls.frametime;
		if (scr_conlines < scr_con_current)
			scr_con_current = scr_conlines;
	}
}

/**
 * @sa SCR_UpdateScreen
 * @sa Con_DrawConsole
 * @sa Con_DrawNotify
 */
static void SCR_DrawConsole (void)
{
	Con_CheckResize();

	if (!viddef.viewWidth || !viddef.viewHeight) {
		/* active full screen menu */
		/* draw the console like in game */
		if (scr_con_current)
			Con_DrawConsole(scr_con_current);
		/* allow chat in waiting dialoges */
		if (cls.key_dest == key_message)
			Con_DrawNotify(); /* only draw notify in game */
		return;
	}

#if 0
	if (cls.state == ca_connecting || cls.state == ca_connected) {	/* forced full screen console */
		Con_DrawConsole(1.0);
		return;
	}
#endif

	if (scr_con_current) {
		Con_DrawConsole(scr_con_current);
	} else {
		if ((cls.key_dest == key_game || cls.key_dest == key_message) && cls.state != ca_sequence)
			Con_DrawNotify(); /* only draw notify in game */
	}
}

/**
 * @sa SCR_UpdateScreen
 * @sa SCR_EndLoadingPlaque
 * @sa SCR_DrawLoading
 */
void SCR_BeginLoadingPlaque (void)
{
	if (developer->integer)
		return;

	scr_draw_loading = 1;

	SCR_UpdateScreen();
	cls.disable_screen = cls.realtime;
}

/**
 * @sa SCR_BeginLoadingPlaque
 */
void SCR_EndLoadingPlaque (void)
{
	cls.disable_screen = 0;
	scr_draw_loading = 0;
	SCR_DrawLoading(); /* reset the loadingPic pointer */
	/* clear any lines of console text */
	Con_ClearNotify();
}

static void SCR_TimeRefresh_f (void)
{
	int i;
	int start, stop;
	float time;

	if (cls.state != ca_active)
		return;

	start = Sys_Milliseconds();

	if (Cmd_Argc() == 2) {		/* run without page flipping */
		R_BeginFrame();
		for (i = 0; i < 128; i++) {
			refdef.viewangles[1] = i / 128.0 * 360.0;
			R_RenderFrame();
		}
		R_EndFrame();
	} else {
		for (i = 0; i < 128; i++) {
			refdef.viewangles[1] = i / 128.0 * 360.0;

			R_BeginFrame();
			R_RenderFrame();
			R_EndFrame();
		}
	}

	stop = Sys_Milliseconds();
	time = (stop - start) / 1000.0;
	Com_Printf("%f seconds (%f fps)\n", time, 128 / time);
}

/**
 * @brief This is called every frame, and can also be called explicitly to flush text to the screen
 * @sa MN_DrawMenus
 * @sa V_RenderView
 * @sa SCR_DrawConsole
 * @sa SCR_DrawCursor
 */
void SCR_UpdateScreen (void)
{
	/* if the screen is disabled (loading plaque is up, or vid mode changing)
	 * do nothing at all */
	if (cls.disable_screen) {
		if (cls.realtime - cls.disable_screen > 120000 && cl.refresh_prepped) {
			cls.disable_screen = 0;
			Com_Printf("Loading plaque timed out.\n");
			return;
		}
	}

	/* not initialized yet */
	if (!scr_initialized || !con.initialized)
		return;

	R_BeginFrame();

	if (cls.state == ca_disconnected && !scr_draw_loading)
		SCR_EndLoadingPlaque();

	if (scr_draw_loading)
		SCR_DrawLoading();
	else {
		MN_SetViewRect(MN_GetActiveMenu());

		if (cls.playingCinematic == CIN_STATUS_FULLSCREEN) {
			CIN_RunCinematic();
		} else {
			/* draw scene */
			V_RenderView();

			/* draw the menus on top of the render view (for hud and so on) */
			MN_DrawMenus();
		}

		SCR_DrawConsole();

		if (cl_fps->integer)
			SCR_DrawString(viddef.width - con_fontWidth * 10, 4, va("fps: %3.1f", cls.framerate), qtrue);
		if (scr_rspeed->integer) {
			if (cls.state == ca_active)
				SCR_DrawString(viddef.width - con_fontWidth * 30, 20, va("brushes: %6i alias: %6i\n", refdef.brush_count, refdef.alias_count), qtrue);
			else
				SCR_DrawString(viddef.width - con_fontWidth * 14, 20, va("alias: %6i\n", refdef.alias_count), qtrue);
		}

		if (cls.state != ca_sequence)
			SCR_DrawCursor();
	}

	R_DrawChars();  /* draw all chars accumulated above */

	R_EndFrame();
}

/**
 * @sa CL_Init
 */
void SCR_Init (void)
{
	scr_conspeed = Cvar_Get("scr_conspeed", "3", 0, "Console open/close speed");
	scr_consize = Cvar_Get("scr_consize", "1.0", 0, "Console size");
	scr_rspeed = Cvar_Get("r_speeds", "0", 0, "Show some rendering stats");
	cl_show_cursor_tooltips = Cvar_Get("cl_show_cursor_tooltips", "1", CVAR_ARCHIVE, "Show cursor tooltips in tactical game mode");

	/* register our commands */
	Cmd_AddCommand("timerefresh", SCR_TimeRefresh_f, "Run a benchmark");

	SCR_TouchPics();

	scr_initialized = qtrue;
}

/**
 * @brief Displays a message on the hud.
 *
 * @param[in] time is a ms values
 * @param[in] text text is already translated here
 */
void SCR_DisplayHudMessage (const char *text, int time)
{
	cl.msgTime = cl.time + time;
	Q_strncpyz(cl.msgText, text, sizeof(cl.msgText));
}
