/*
 * dotlockfile.c	Command line version of liblockfile.
 *			Runs setgid mail so is able to lock mailboxes
 *			as well. Liblockfile can call this command.
 *
 * Version:	@(#)dotlockfile.c  1.0  10-Jun-1999  miquels@cistron.nl
 *
 *		Copyright (C) Miquel van Smoorenburg 1999.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version 2
 *		of the License, or (at your option) any later version.
 */

#include "autoconf.h"

#include <sys/types.h>
#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <maillock.h>
#include <lockfile.h>

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#ifndef HAVE_GETOPT_H
extern int getopt();
extern char *optarg;
extern int optind;
#endif

extern int eaccess(char *, gid_t, struct stat *);

/*
 *	Sleep for an amout of time while regulary checking if
 *	our parent is still alive.
 */
int check_sleep(int sleeptime)
{
	int		i;
	static int	ppid = 0;

	if (ppid == 0) ppid = getppid();

	for (i = 0; i < sleeptime; i += 5) {
		sleep(5);
		if (kill(ppid, 0) < 0 && errno == ESRCH)
			return L_ERROR;
	}
	return 0;
}


/*
 *	Is this a lock for a mailbox? We check if the filename
 *	is in ..../USERNAME.lock format, and if we own the file
 *	that we want to lock.
 */
int ismaillock(char *lockfile, char *username)
{
	struct stat	st;
	char		*p;
	char		tmp[1024];
	int		len;

	sprintf(tmp, "%.120s.lock", username);
	if ((p = strrchr(lockfile, '/')) != NULL)
		p++;
	else
		p = lockfile;

	if (strcmp(p, tmp) != 0)
		return 0;

	len = strlen(lockfile);
	if (len > sizeof(tmp) || len < 5)
		return 0;
	strncpy(tmp, lockfile, len - 5);
	tmp[len - 5] = 0;
	if (stat(tmp, &st) != 0 || st.st_uid != geteuid())
		return 0;

	return 1;
}


/*
 *	Split a filename up in  file and directory.
 */
void fn_split(char *fn, char **fn_p, char **dir_p)
{
	static char	buf[MAXPATHLEN];
	char		*p;

	strcpy(buf, fn);
	if ((p = strrchr(buf, '/')) != NULL) {
		*p++   = 0;
		*fn_p  = p;
		*dir_p = buf;
	} else {
		*fn_p  = fn;
		*dir_p = ".";
	}
}


/*
 *	Return name of lockfile for mail.
 */
char *mlockname(char *user)
{
	static char	buf[MAXPATHLEN];
	char		*e;

	if ((e = getenv("MAIL")) != NULL && strlen(e) + 6 < MAXPATHLEN)
		sprintf(buf, "%s.lock", e);
	else
		sprintf(buf, "%s%.120s.lock", MAILDIR, user);
	return buf;
}


/*
 *	Print usage mesage and exit.
 */
void usage(void)
{
	fprintf(stderr, "Usage: dotlockfile [-p] [-l|-u] [-r retries] [-c] [-m|lockfile]\n");
	exit(1);
}


int main(int argc, char **argv)
{
	struct passwd	*pwd;
	struct stat	st, st2;
	gid_t		gid;
	char		*dir, *file, *lockfile = NULL;
	int 		c, r;
	int		retries = 5;
	int		flags = 0;
	int		unlock = 0;
	int		check = 0;
	int		quiet = 0;

	/*
	 *	Get username for mailbox-locks.
	 */
	if ((pwd = getpwuid(getuid())) == NULL) {
		fprintf(stderr, "dotlockfile: You don't exist. Go away.\n");
		return L_ERROR;
	}

	/*
	 *	Process the options.
	 */
	while ((c = getopt(argc, argv, "qpNr:mlu")) != EOF) switch(c) {
		case 'q':
			quiet = 1;
			break;
		case 'p':
			flags |= L_PPID;
			break;
		case 'N':
			/* NOP */
			break;
		case 'r':
			retries = atoi(optarg);
			break;
		case 'm':
			lockfile = mlockname(pwd->pw_name);
			break;
		case 'l':
			/* default: lock */
			break;
		case 'u':
			unlock = 1;
			break;
		case 'c':
			check = 1;
			break;
		default:
			usage();
			break;
	}

	/*
	 *	Need a lockfile, ofcourse.
	 */
	if (lockfile && optind < argc) usage();
	if (lockfile == NULL) {
		if (optind != argc - 1) usage();
		lockfile = argv[optind];
	}
	if (strlen(lockfile) >= MAXPATHLEN) {
		fprintf(stderr, "dotlockfile: %s: name too long\n", lockfile);
		return L_NAMELEN;
	}

	/*
	 *	See if we can write into the lock directory.
	 */
	fn_split(lockfile, &file, &dir);
	gid = getgid();
	if (eaccess(dir, gid, &st) < 0) {
		if (errno == ENOENT) {
enoent:
			if (!quiet) fprintf(stderr,
				"dotlockfile: %s: no such directory\n", dir);
			return L_TMPLOCK;
		}
		if ((r = eaccess(dir, getegid(), &st) < 0) && errno == ENOENT)
			goto enoent;
		if (r < 0 || !ismaillock(lockfile, pwd->pw_name)) {
			if (!quiet) fprintf(stderr,
				"dotlockfile: %s: permission denied\n", lockfile);
			return L_TMPLOCK;
		}
	} else
		setgid(gid);

	/*
	 *	Now we should be able to chdir() to the lock directory.
	 *	When we stat("."), it should be the same as at the
	 *	eaccess() check or someone played symlink() games on us.
	 */
	if (chdir(dir) < 0 || stat(".", &st2) < 0) {
		if (!quiet) fprintf(stderr,
			"dotlockfile: %s: cannot access directory\n", dir);
		return L_TMPLOCK;
	}
	if (st.st_ino != st2.st_ino || st.st_dev != st2.st_dev) {
		if (!quiet) fprintf(stderr,
			"dotlockfile: %s: directory changed underneath us!\n", dir);
		return L_TMPLOCK;
	}

	/*
	 *	Simple check for a valid lockfile ?
	 */
	if (check)
		return (lockfile_check(file, flags) < 0) ? 0 : 1;

	/*
	 *	Remove lockfile?
	 */
	if (unlock)
		return (lockfile_remove(file) == 0) ? 0 : 1;

	/*
	 *	No, lock.
	 */
	return lockfile_create(file, retries, flags);
}

