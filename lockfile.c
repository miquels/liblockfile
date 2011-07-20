/*
 * lockfile.c 	Safely creates a lockfile, also over NFS.
 *		This file also holds the implementation for
 *		the Svr4 maillock functions.
 *
 * Version:	@(#)lockfile.c  1.06  04-Jun-2004  miquels@cistron.nl
 *
 *		Copyright (C) Miquel van Smoorenburg 1997,1998,1999,2004.
 *
 *		This library is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU Library General Public
 *		License as published by the Free Software Foundation; either
 *		version 2 of the License, or (at your option) any later version.
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
#include <signal.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <lockfile.h>
#include <maillock.h>

#ifdef HAVE_UTIME
#include <utime.h>
#endif

#ifdef LIB
static char *mlockfile;
static int  islocked = 0;
#endif

#ifndef LIB
extern int check_sleep(int);
#endif

#if !defined(LIB) || defined(MAILGROUP)
/*
 *	See if we can write to the directory.
 *	Returns: -1 fail
 *		  0 OK writable
 */
#ifdef LIB
static
#endif
int eaccess_write(char *fn, gid_t gid, struct stat *st)
{
	struct stat	tmp;
	uid_t		uid = geteuid();
	gid_t		*aux;
	int		n, i;

	if (st == NULL) st = &tmp;

	if (stat(fn, st) != 0)
		return -1;
	errno = EPERM;

	if (uid == 0) return 0;

	if (st->st_uid == uid)
		return (st->st_mode & 0200) ? 0 : -1;

	if (st->st_gid == gid)
		return (st->st_mode & 0020) ? 0 : -1;

	if ((n = getgroups(0, NULL)) > 0) {
		aux = malloc(n * sizeof(gid_t));
		if (aux && getgroups(n, aux) == 0) {
			for (i = 0; i < n; i++)
				if (st->st_gid == aux[i]) {
					free(aux);
					return (st->st_mode & 0020) ? 0 : -1;
				}
		}
		free(aux);
	}

	return (st->st_mode & 0002) ? 0 : -1;
}
#endif

#if defined(LIB) && defined(MAILGROUP)
/*
 *	Can we write to the directory of the lockfile ?
 */
static int need_extern(const char *file)
{
	gid_t		egid = getegid();
	struct stat	st;
	static gid_t	mailgid = -1;
	char		*dir;
	char		*p;
	int             ret;

	/*
	 *	Find directory.
	 */
	if ((dir = (char *)malloc(strlen(file) + 1)) == NULL)
		return L_ERROR;
	strcpy(dir, file);
	if ((p = strrchr(dir, '/')) != NULL)
		*p = 0;
	else
		strcpy(dir, ".");
	if (eaccess_write(dir, egid, NULL) >= 0) {
		free(dir);
		return 0;
	}

	/*
	 *	See if accessible for group mail. We find out what
	 *	"group mail" is by statting LOCKPROG, that saves us
	 *	from having to call getgrgid() in a library.
	 */
	if (mailgid == (gid_t)-1) {
		if (stat(LOCKPROG, &st) < 0 || !(st.st_mode & S_ISGID))
			return 0;
		mailgid = st.st_gid;
	}
	ret = eaccess_write(dir, mailgid, NULL) >= 0;
	free (dir);
	return ret;
}

/*
 *	Call external program to do the actual locking.
 */
static int do_extern(char *opt, const char *lockfile, int retries, int flags)
{
	sigset_t	set, oldset;
	char		buf[4];
	pid_t		pid, n;
	int		st;

	/*
	 *	Block SIGCHLD. The main program might have installed
	 *	handlers we don't want to call.
	 */
	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	sigprocmask(SIG_BLOCK, &set, &oldset);

	/*
	 *	Fork, execute locking program and wait.
	 */
	if ((pid = fork()) < 0)
		return L_ERROR;
	if (pid == 0) {
		sprintf(buf, "%d", retries % 1000);
		execl(LOCKPROG, LOCKPROG, opt, "-r", buf, "-q",
			(flags & L_PID) ? "-p" : "-N", lockfile, NULL);
		_exit(L_ERROR);
	}

	/*
	 *	Wait for return status - do something appropriate
	 *	if program died or returned L_ERROR.
	 */
	while ((n = waitpid(pid, &st, 0)) != pid)
		if (n < 0 && errno != EINTR)
			break;
	if (!sigismember(&oldset, SIGCHLD))
		sigprocmask(SIG_UNBLOCK, &set, NULL);
	if (n < 0)
		return L_ERROR;
	if (!WIFEXITED(st) || WEXITSTATUS(st) == L_ERROR) {
		errno = EINTR;
		return L_ERROR;
	}

	return WEXITSTATUS(st);
}

#endif

/*
 *	Create a lockfile.
 */
#ifdef LIB
static
#endif
int lockfile_create_save_tmplock(const char *lockfile,
		char *tmplock, int tmplocksz, int retries, int flags)
{
	struct stat	st, st1;
	char		sysname[256];
	char		buf[8];
	char		*p;
	int		sleeptime = 0;
	int		statfailed = 0;
	int		fd;
	int		i, e, len;
	int		dontsleep = 1;
	int		tries = retries + 1;

#ifdef MAXPATHLEN
	/*
	 *	Safety measure.
	 */
	if (strlen(lockfile) + 32 > MAXPATHLEN) {
		errno = ENAMETOOLONG;
		return L_ERROR;
	}
#endif

	if (strlen(lockfile) + 32 + 1 > tmplocksz) {
		errno = EINVAL;
		return L_ERROR;
	}

#if defined(LIB) && defined(MAILGROUP)
	if (need_extern(lockfile))
		return do_extern("-l", lockfile, retries, flags);
#endif

	/*
	 *	Create a temp lockfile (hopefully unique) and write
	 *	either our pid/ppid in it, or 0\0 for svr4 compatibility.
	 */
	if (gethostname(sysname, sizeof(sysname)) < 0)
		return L_ERROR;
	if ((p = strchr(sysname, '.')) != NULL)
		*p = 0;
	/* strcpy is safe: length-check above, limited at sprintf below */
	strcpy(tmplock, lockfile);
	if ((p = strrchr(tmplock, '/')) == NULL)
		p = tmplock;
	else
		p++;
	sprintf(p, ".lk%05d%x%s",
		(int)getpid(), (int)time(NULL) & 15, sysname);
	i = umask(022);
	fd = open(tmplock, O_WRONLY|O_CREAT|O_EXCL, 0644);
	e = errno;
	umask(i);
	if (fd < 0) {
		tmplock[0] = 0;
		errno = e;
		return L_TMPLOCK;
	}
	if (flags & (L_PID | L_PPID)) {
		sprintf(buf, "%d\n",
			(flags & L_PID) ? (int)getpid() : (int)getppid());
		p = buf;
		len = strlen(buf);
	} else {
		p = "0\n";
		len = 2;
	}
	i = write(fd, p, len);
	e = errno;
	if (close(fd) != 0) {
		e = errno;
		i = -1;
	}
	if (i != len) {
		unlink(tmplock);
		tmplock[0] = 0;
		errno = i < 0 ? e : EAGAIN;
		return L_TMPWRITE;
	}

	/*
	 *	Now try to link the temporary lock to the lock.
	 */
	for (i = 0; i < tries && tries > 0; i++) {

		if (!dontsleep) {
			sleeptime += 5;
			if (sleeptime > 60) sleeptime = 60;
#ifdef LIB
			sleep(sleeptime);
#else
			if ((e = check_sleep(sleeptime)) != 0) {
				unlink(tmplock);
				tmplock[0] = 0;
				return e;
			}
#endif
		}
		dontsleep = 0;


		/*
		 *	Now lock by linking the tempfile to the lock.
		 *
		 *	KLUDGE: some people say the return code of
		 *	link() over NFS can't be trusted.
		 *	EXTRA FIX: the value of the nlink field
		 *	can't be trusted (may be cached).
		 */
		(void)link(tmplock, lockfile);

		if (lstat(tmplock, &st1) < 0) {
			tmplock[0] = 0;
			return L_ERROR; /* Can't happen */
		}

		if (lstat(lockfile, &st) < 0) {
			if (statfailed++ > 5) {
				/*
				 *	Normally, this can't happen; either
				 *	another process holds the lockfile or
				 *	we do. So if this error pops up
				 *	repeatedly, just exit...
				 */
				e = errno;
				(void)unlink(tmplock);
				tmplock[0] = 0;
				errno = e;
				return L_MAXTRYS;
			}
			continue;
		}

		/*
		 *	See if we got the lock.
		 */
		if (st.st_rdev == st1.st_rdev &&
		    st.st_ino  == st1.st_ino) {
			(void)unlink(tmplock);
			tmplock[0] = 0;
			return L_SUCCESS;
		}
		statfailed = 0;

		/*
		 *	If there is a lockfile and it is invalid,
		 *	remove the lockfile.
		 */
		if (lockfile_check(lockfile, flags) == -1) {
			unlink(lockfile);
			dontsleep = 1;
			/*
			 *	If the lockfile was invalid, then the first
			 *	try wasn't valid either - make sure we
			 *	try at least once more.
			 */
			if (tries == 1) tries++;
		}

	}
	(void)unlink(tmplock);
	tmplock[0] = 0;
	errno = EAGAIN;
	return L_MAXTRYS;
}

int lockfile_create(const char *lockfile, int retries, int flags)
{
	char *tmplock;
	int l, r, e;

	l = strlen(lockfile)+32+1;
	if ((tmplock = (char *)malloc(l)) == NULL)
		return L_ERROR;
	tmplock[0] = 0;
	r = lockfile_create_save_tmplock(lockfile,
						tmplock, l, retries, flags);
	e = errno;
	free(tmplock);
	errno = e;
	return r;
}

/*
 *	See if a valid lockfile is present.
 *	Returns 0 if so, -1 if not.
 */
int lockfile_check(const char *lockfile, int flags)
{
	struct stat	st, st2;
	char		buf[16];
	time_t		now;
	pid_t		pid;
	int		fd, len, r;

	if (stat(lockfile, &st) < 0)
		return -1;

	/*
	 *	Get the contents and mtime of the lockfile.
	 */
	time(&now);
	pid = 0;
	if ((fd = open(lockfile, O_RDONLY)) >= 0) {
		/*
		 *	Try to use 'atime after read' as now, this is
		 *	the time of the filesystem. Should not get
		 *	confused by 'atime' or 'noatime' mount options.
		 */
		len = 0;
		if (fstat(fd, &st) == 0 &&
		    (len = read(fd, buf, sizeof(buf))) >= 0 &&
		    fstat(fd, &st2) == 0 &&
		    st.st_atime != st2.st_atime)
			now = st.st_atime;
		close(fd);
		if (len > 0 && (flags & (L_PID|L_PPID))) {
			buf[len] = 0;
			pid = atoi(buf);
		}
	}

	if (pid > 0) {
		/*
		 *	If we have a pid, see if the process
		 *	owning the lockfile is still alive.
		 */
		r = kill(pid, 0);
		if (r == 0 || errno == EPERM)
			return 0;
		if (r < 0 && errno == ESRCH)
			return -1;
		/* EINVAL - FALLTHRU */
	}

	/*
	 *	Without a pid in the lockfile, the lock
	 *	is valid if it is newer than 5 mins.
	 */

	if (now < st.st_mtime + 300)
		return 0;

	return -1;
}

/*
 *	Remove a lock.
 */
int lockfile_remove(const char *lockfile)
{
#if defined(LIB) && defined(MAILGROUP)
	if (need_extern(lockfile))
		return do_extern("-u", lockfile, 0, 0);
#endif
	return (unlink(lockfile) < 0 && errno != ENOENT) ? -1 : 0;
}

/*
 *	Touch a lock.
 */
int lockfile_touch(const char *lockfile)
{
#ifdef HAVE_UTIME
	return utime(lockfile, NULL);
#else
	return utimes(lockfile, NULL);
#endif
}

#ifdef LIB
/*
 *	Lock a mailfile. This looks a lot like the SVR4 function.
 *	Arguments: lusername, retries.
 */
int maillock(const char *name, int retries)
{
	char		*p, *mail;
	char		*newlock;
	int		i, e;
	int             len, newlen;

	if (islocked) return 0;

#ifdef MAXPATHLEN
	if (strlen(name) + sizeof(MAILDIR) + 6 > MAXPATHLEN) {
		errno = ENAMETOOLONG;
		return L_NAMELEN;
	}
#endif

	/*
	 *	If $MAIL is for the same username as "name"
	 *	then use $MAIL instead.
	 */

	len = strlen(name)+strlen(MAILDIR)+6;
	mlockfile = (char *)malloc(len);
	if (!mlockfile)
		return L_ERROR;
	sprintf(mlockfile, "%s%s.lock", MAILDIR, name);
	if ((mail = getenv("MAIL")) != NULL) {
		if ((p = strrchr(mail, '/')) != NULL)
			p++;
		else
			p = mail;
		if (strcmp(p, name) == 0) {
			newlen = strlen(mail)+6;
#ifdef MAXPATHLEN
			if (newlen > MAXPATHLEN) {
				errno = ENAMETOOLONG;
				return L_NAMELEN;
			}
#endif
			if (newlen > len) {
				newlock = (char *)realloc (mlockfile, newlen);
				if (newlock == NULL) {
					e = errno;
					free (mlockfile);
					mlockfile = NULL;
					errno = e;
					return L_ERROR;
				}
				mlockfile = newlock;
			}
			sprintf(mlockfile, "%s.lock", mail);
		}
	}
	i = lockfile_create(mlockfile, retries, 0);
	if (i == 0) islocked = 1;

	return i;
}

void mailunlock(void)
{
	if (!islocked) return;
	lockfile_remove(mlockfile);
	free (mlockfile);
	islocked = 0;
}

void touchlock(void)
{
	lockfile_touch(mlockfile);
}
#endif

