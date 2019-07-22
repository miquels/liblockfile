/*
 * dotlockfile.c	Command line version of liblockfile.
 *			Runs setgid mail so is able to lock mailboxes
 *			as well. Liblockfile can call this command.
 *
 * Version:	@(#)dotlockfile.c  1.14  17-Jan-2017  miquels@cistron.nl
 *
 *		Copyright (C) Miquel van Smoorenburg 1999-2017
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
#include <sys/wait.h>
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

void ignore_signal(int sig)
{
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
	strncpy(tmp, lockfile,sizeof(tmp));
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

void perror_exit(const char *why) {
	fprintf(stderr, "dotlockfile: ");
	perror(why);
	exit(L_ERROR);
}

/*
 *	Print usage mesage and exit.
 */
void usage(void)
{
	fprintf(stderr, "Usage:  dotlockfile -l [-r retries] [-p] <-m|lockfile>\n");
	fprintf(stderr, "        dotlockfile -l [-r retries] [-p] <-m|lockfile> command args...\n");
	fprintf(stderr, "        dotlockfile -u|-t\n");
	exit(1);
}

int check_access(char *dir, gid_t gid, gid_t egid, char *lockfile, struct passwd *pwd, struct stat *st) {

	/*
	 *	Try with normal perms first.
	 */
	int r = eaccess_write(dir, gid, st);
	if (gid == egid)
		return r;
	if (r == 0 || errno == ENOENT) {
		if (setregid(gid, gid) < 0)
			perror_exit("setregid(gid, gid)");
		return r;
	}

	/*
	 *	Perhaps with the effective group.
	 */
	r = eaccess_write(dir, egid, st);
	if (r < 0 && errno == ENOENT)
		return -1;
	if (r == 0) {
		if (!ismaillock(lockfile, pwd->pw_name)) {
			errno = EPERM;
			return -1;
		}
		if (setregid(-1, egid) < 0)
			perror_exit("setregid(-1, egid)");
		return 0;
	}

	/*
	 *	Once more with saved group-id cleared.
	 */
	if (setregid(gid, gid) < 0)
		perror_exit("setregid(gid, gid)");
	return eaccess_write(dir, gid, st);
}

int main(int argc, char **argv)
{
	struct passwd	*pwd;
	struct stat	st, st2;
	gid_t		gid, egid;
	char		*dir, *file, *lockfile = NULL;
	char		**cmd = NULL;
	int 		c, r, l;
	int		retries = 5;
	int		flags = 0;
	int		lock = 0;
	int		unlock = 0;
	int		check = 0;
	int		quiet = 0;
	int		touch = 0;
	int		writepid = 0;

	/*
	 *	Remember real and effective gid, and
	 *	drop privs for now.
	 */
	if ((gid = getgid()) < 0)
		perror_exit("getgid");
	if ((egid = getegid()) < 0)
		perror_exit("getegid");
	if (setregid(-1, gid) < 0)
		perror_exit("setregid(-1, gid)");

	set_signal(SIGINT, got_signal);
	set_signal(SIGQUIT, got_signal);
	set_signal(SIGHUP, got_signal);
	set_signal(SIGTERM, got_signal);
	set_signal(SIGPIPE, got_signal);

	/*
	 *	Get username for mailbox-locks.
	 */
	if ((pwd = getpwuid(geteuid())) == NULL) {
		fprintf(stderr, "dotlockfile: You don't exist. Go away.\n");
		return L_ERROR;
	}

	/*
	 *	Process the options.
	 */
	while ((c = getopt(argc, argv, "+qpNr:mluct")) != EOF) switch(c) {
		case 'q':
			quiet = 1;
			break;
		case 'p':
			writepid = 1;
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
				retries = 0x7ffffff0;
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
			lock = 1;
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
	 * next argument may be lockfile name
	 */
	if (!lockfile) {
		if (optind == argc)
			usage();
		lockfile = argv[optind++];
	}

	/*
	 * next arguments may be command [args...]
	 */
	if (optind < argc)
		cmd = argv + optind;

	/*
	 *	Options sanity check
	 */
	if ((cmd || lock) && (touch || check || unlock))
		usage();

	if (writepid)
		flags |= (cmd ? L_PID : L_PPID);

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

	r = check_access(dir, gid, egid, lockfile, pwd, &st);
	if (r < 0) {
		if (!quiet) {
			if (errno == ENOENT) {
				fprintf(stderr,
				"dotlockfile: %s: no such directory\n", dir);
			} else {
				fprintf(stderr,
				"dotlockfile: %s: permission denied\n", lockfile);
			}
		}
		return L_TMPLOCK;
	}

	/*
	 *	Remember directory.
	 */
	char oldpwd[PATH_MAX];
	if (getcwd(oldpwd, sizeof(oldpwd)) == NULL)
		perror_exit("getcwd");

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
	r = lockfile_create_save_tmplock(file, tmplock, l, retries, flags);
	if (r != 0 || !cmd)
		return r;

	/*
	 *	Spawn command
	 */
	set_signal(SIGINT, ignore_signal);
	set_signal(SIGQUIT, ignore_signal);
	set_signal(SIGHUP, ignore_signal);
	set_signal(SIGALRM, ignore_signal);

	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		lockfile_remove(file);
		exit(L_ERROR);
	}
	if (pid == 0) {
		if (gid != egid && setregid(gid, gid) < 0) {
			perror("setregid(gid, gid)");
			exit(127);
		}
		if (chdir(oldpwd) < 0) {
			perror(oldpwd);
			exit(127);
		}
		execvp(cmd[0], cmd);
		perror(cmd[0]);
		exit(127);
	}

	/* wait for child */
	int e, wstatus;
	while (1) {
		if (!writepid)
			alarm(30);
		e = waitpid(pid, &wstatus, 0);
		if (e >= 0 || errno != EINTR)
			break;
		if (!writepid)
			lockfile_touch(file);
	}

	alarm(0);
	lockfile_remove(file);

	return 0;
}

