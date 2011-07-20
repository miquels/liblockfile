/*
 * dotlockfile.c	Command line version of liblockfile.
 *			Runs setgid mail so is able to lock mailboxes
 *			as well. Liblockfile can call this command.
 *
 * Version:	@(#)dotlockfile.c  1.1  15-May-2003  miquels@cistron.nl
 *
 *		Copyright (C) Miquel van Smoorenburg 1999,2003
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

extern int eaccess_write(char *, gid_t, struct stat *);
extern int lockfile_create_save_tmplock(const char *lockfile,
                char *tmplock, int tmplocksz, int retries, int flags);

char *tmplock;

/*
 *	If we got SIGINT, SIGQUIT, SIGHUP, remove the
 *	tempfile and re-raise the signal.
 */
void got_signal(int sig)
{
	if (tmplock && tmplock[0])
		unlink(tmplock);
	signal(sig, SIG_DFL);
	raise(sig);
}

/*
 *	Install signal handler only if the signal was
 *	not ignored already.
 */
int set_signal(int sig, void (*handler)(int))
{
	struct sigaction sa;

	if (sigaction(sig, NULL, &sa) < 0)
		return -1;
	if (sa.sa_handler == SIG_IGN)
		return 0;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	return sigaction(sig, &sa, NULL);
}

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
int fn_split(char *fn, char **fn_p, char **dir_p)
{
	static char	*buf = NULL;
	char		*p;

	if (buf)
		free (buf);
	buf = (char *) malloc (strlen (fn) + 1);
	if (! buf)
		return L_ERROR;
	strcpy(buf, fn);
	if ((p = strrchr(buf, '/')) != NULL) {
		*p++   = 0;
		*fn_p  = p;
		*dir_p = buf;
	} else {
		*fn_p  = fn;
		*dir_p = ".";
	}
	return L_SUCCESS;
}


/*
 *	Return name of lockfile for mail.
 */
char *mlockname(char *user)
{
	static char	*buf = NULL;
	char		*e;

	if (buf)
		free(buf);

	e = getenv("MAIL");
	if (e) {
		buf = (char *)malloc(strlen(e)+6);
		if (!buf)
			return NULL;
		sprintf(buf, "%s.lock", e);
	} else {
		buf = (char *)malloc(strlen(MAILDIR)+strlen(user)+6);
		if (!buf)
			return NULL;
		sprintf(buf, "%s%s.lock", MAILDIR, user);
	}
	return buf;
}


/*
 *	Print usage mesage and exit.
 */
void usage(void)
{
	fprintf(stderr, "Usage:  dotlockfile [-l [-r retries] |-u|-t|-c] [-p] [-m|lockfile]\n");
	exit(1);
}


int main(int argc, char **argv)
{
	struct passwd	*pwd;
	struct stat	st, st2;
	gid_t		gid;
	char		*dir, *file, *lockfile = NULL;
	int 		c, r, l;
	int		retries = 5;
	int		flags = 0;
	int		unlock = 0;
	int		check = 0;
	int		quiet = 0;
	int		touch = 0;

	set_signal(SIGINT, got_signal);
	set_signal(SIGQUIT, got_signal);
	set_signal(SIGHUP, got_signal);
	set_signal(SIGTERM, got_signal);
	set_signal(SIGPIPE, got_signal);

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
	while ((c = getopt(argc, argv, "qpNr:mluct")) != EOF) switch(c) {
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
			if (retries <= 0 &&
			    retries != -1 && strcmp(optarg, "0") != 0) {
				fprintf(stderr,
				    "dotlockfile: -r %s: invalid argument\n",
						optarg);
				return L_ERROR;
			}
			if (retries == -1) {
				/* 4000 years */
				retries = 2147483647;
			}
			break;
		case 'm':
			lockfile = mlockname(pwd->pw_name);
			if (!lockfile) {
				perror("dotlockfile");
				return L_ERROR;
			}
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
		case 't':
			touch = 1;
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

#ifdef MAXPATHLEN
	if (strlen(lockfile) >= MAXPATHLEN) {
		fprintf(stderr, "dotlockfile: %s: name too long\n", lockfile);
		return L_NAMELEN;
	}
#endif

	/*
	 *	See if we can write into the lock directory.
	 */
	r = fn_split(lockfile, &file, &dir);
	if (r != L_SUCCESS) {
		perror("dotlockfile");
		return L_ERROR;
	}
	gid = getgid();
	if (eaccess_write(dir, gid, &st) < 0) {
		if (errno == ENOENT) {
enoent:
			if (!quiet) fprintf(stderr,
				"dotlockfile: %s: no such directory\n", dir);
			return L_TMPLOCK;
		}
		if ((r = eaccess_write(dir, getegid(), &st) < 0) && errno == ENOENT)
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
	 *	eaccess_write() check or someone played symlink() games on us.
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
		return (lockfile_check(file, flags) < 0) ? 1 : 0;

	/*
	 *	Touch lock ?
	 */
	if (touch)
		return (lockfile_touch(file) < 0) ? 1 : 0;

	/*
	 *	Remove lockfile?
	 */
	if (unlock)
		return (lockfile_remove(file) == 0) ? 0 : 1;

	/*
	 *	No, lock.
	 */
	l = strlen(file) + 32 + 1;
	tmplock = malloc(l);
	if (tmplock == NULL) {
		perror("malloc");
		exit(L_ERROR);
	}
	return lockfile_create_save_tmplock(file, tmplock, l, retries, flags);
}

