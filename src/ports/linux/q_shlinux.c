/**
 * @file q_shlinux.c
 * @brief Shared linux functions
 * @note Hunk system and some file system functions
 */

/*
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

#include <sys/types.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include "../unix/glob.h"

#include "../../qcommon/qcommon.h"
#if defined(__FreeBSD__) || defined(__NetBSD__)
#include <machine/param.h>
#endif

/*=============================================================================== */

static byte *membase;
static int maxhunksize;
static int curhunksize;

/**
 * @brief
 */
void *Hunk_Begin (int maxsize)
{
	/* reserve a huge chunk of memory, but don't commit any yet */
	maxhunksize = maxsize + sizeof(int);
	curhunksize = 0;
#if (defined __FreeBSD__) || (defined __NetBSD__)
	membase = mmap(0, maxhunksize, PROT_READ|PROT_WRITE,
		MAP_PRIVATE|MAP_ANON, -1, 0);
#else
	membase = mmap(0, maxhunksize, PROT_READ|PROT_WRITE,
		MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
#endif
	if (membase == MAP_FAILED)
		Sys_Error("unable to virtual allocate %d bytes", maxsize);

	*((int *)membase) = curhunksize;

	return membase + sizeof(int);
}

/**
 * @brief
 */
void *Hunk_Alloc (int size, const char *name)
{
	byte *buf;

	if (!size)
		return NULL;

	/* round to cacheline */
	size = (size + 31) & ~31;

	Com_DPrintf("Hunk_Alloc: Allocate %8i / %8i bytes (used: %8i bytes): %s\n",
		size, maxhunksize, curhunksize, name);

	if (curhunksize + size > maxhunksize)
		Sys_Error("Hunk_Alloc overflow");
	buf = membase + sizeof(int) + curhunksize;
	curhunksize += size;
	return buf;
}

/**
 * @brief
 */
int Hunk_End (void)
{
#if defined(__FreeBSD__) || defined(__NetBSD__)
	long pgsz, newsz, modsz;

	pgsz = sysconf(_SC_PAGESIZE);
	if (pgsz == -1)
		Sys_Error("Hunk_End: Sysconf() failed: %s", strerror(errno));

	newsz = curhunksize + sizeof(int);

	if (newsz > maxhunksize)
		Sys_Error("Hunk_End Overflow");
	else if (newsz < maxhunksize) {
		modsz = newsz % pgsz;
		if (modsz)
			newsz += pgsz - modsz;

		if (munmap(membase + newsz, maxhunksize - newsz) == -1)
			Sys_Error("Hunk_End: munmap() failed: %s", strerror(errno));
	}
#endif
#if defined(__linux__)
	byte *n = NULL;

	n = mremap(membase, maxhunksize, curhunksize + sizeof(int), 0);

	if (n != membase)
		Sys_Error("Hunk_End:  Could not remap virtual block (%d)", errno);
#endif

	*((int *)membase) = curhunksize + sizeof(int);

	return curhunksize;
}

/**
 * @brief
 */
void Hunk_Free (void *base)
{
	byte *m;

	if (base) {
		m = ((byte *)base) - sizeof(int);
		if (*((int *)m) && munmap(m, *((int *)m)))
			Sys_Error("Hunk_Free: munmap failed (%d, %s, size: %i)", errno, strerror(errno), *((int *)m));
	}
}

/*=============================================================================== */


int curtime;

/**
 * @brief
 */
int Sys_Milliseconds (void)
{
	struct timeval tp;
	struct timezone tzp;
	static int		secbase;

	gettimeofday(&tp, &tzp);

	if (!secbase) {
		secbase = tp.tv_sec;
		return tp.tv_usec/1000;
	}

	curtime = (tp.tv_sec - secbase)*1000 + tp.tv_usec/1000;

	return curtime;
}

/**
 * @brief Creates a directory with all right (rwx) for user, group and world
 */
void Sys_Mkdir (const char *path)
{
	mkdir(path, 0777);
}

/**
 * @brief Lowers a given string
 */
char *strlwr (char *s)
{
	char* origs = s;
	while (*s) {
		*s = (char)tolower(*s);
		s++;
	}
	return origs;
}

/*============================================ */

static	char	findbase[MAX_OSPATH];
static	char	findpath[MAX_OSPATH];
static	char	findpattern[MAX_OSPATH];
static	DIR		*fdir;

/**
 * @brief
 */
static qboolean CompareAttributes (const char *path, const char *name, unsigned musthave, unsigned canthave)
{
	struct stat st;
	char fn[MAX_OSPATH];

	/* . and .. never match */
	if (Q_strcmp(name, ".") == 0 || Q_strcmp(name, "..") == 0)
		return qfalse;

	Com_sprintf(fn, sizeof(fn), "%s/%s", path, name);
	if (stat(fn, &st) == -1) {
		Com_Printf("CompareAttributes: Warning, stat failed: %s\n", name);
		return qfalse; /* shouldn't happen */
	}

	if ((st.st_mode & S_IFDIR) && (canthave & SFF_SUBDIR))
		return qfalse;

	if ((musthave & SFF_SUBDIR) && !(st.st_mode & S_IFDIR))
		return qfalse;

	return qtrue;
}

/**
 * @brief Opens the directory and returns the first file that matches our searchrules
 * @sa Sys_FindNext
 * @sa Sys_FindClose
 */
char *Sys_FindFirst (const char *path, unsigned musthave, unsigned canhave)
{
	struct dirent *d;
	char *p;

	if (fdir)
		Sys_Error("Sys_BeginFind without close");

/*	COM_FilePath (path, findbase); */
	Q_strncpyz(findbase, path, sizeof(findbase));

	if ((p = strrchr(findbase, '/')) != NULL) {
		*p = 0;
		Q_strncpyz(findpattern, p + 1, sizeof(findpattern));
	} else
		Q_strncpyz(findpattern, "*", sizeof(findpattern));

	if (Q_strcmp(findpattern, "*.*") == 0)
		Q_strncpyz(findpattern, "*", sizeof(findpattern));

	if ((fdir = opendir(findbase)) == NULL)
		return NULL;

	while ((d = readdir(fdir)) != NULL) {
		if (!*findpattern || glob_match(findpattern, d->d_name)) {
/*			if (*findpattern) */
/*				printf("%s matched %s\n", findpattern, d->d_name); */
			if (CompareAttributes(findbase, d->d_name, musthave, canhave)) {
				Com_sprintf(findpath, sizeof(findpath), "%s/%s", findbase, d->d_name);
				return findpath;
			}
		}
	}
	return NULL;
}

/**
 * @brief Returns the next file of the already opened directory (Sys_FindFirst) that matches our search mask
 * @sa Sys_FindClose
 * @sa Sys_FindFirst
 * @sa static var findpattern
 */
char *Sys_FindNext (unsigned musthave, unsigned canhave)
{
	struct dirent *d;

	if (fdir == NULL)
		return NULL;
	while ((d = readdir(fdir)) != NULL) {
		if (!*findpattern || glob_match(findpattern, d->d_name)) {
/*			if (*findpattern) */
/*				printf("%s matched %s\n", findpattern, d->d_name); */
			if (CompareAttributes(findbase, d->d_name, musthave, canhave)) {
				Com_sprintf(findpath, sizeof(findpath), "%s/%s", findbase, d->d_name);
				return findpath;
			}
		}
	}
	return NULL;
}

/**
 * @brief Close of dir handle
 * @sa Sys_FindNext
 * @sa Sys_FindFirst
 */
void Sys_FindClose (void)
{
	if (fdir != NULL)
		closedir(fdir);
	fdir = NULL;
}

/**
 * @brief Breakpoint for debugger sessions
 */
void Sys_DebugBreak (void)
{
#if defined DEBUG
	__asm ("int $3");
#endif
}
