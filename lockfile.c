/*
 * lockfile.c	Safely creates a lockfile, also over NFS.
 *		This file also holds the implementation for
 *		the Svr4 maillock functions.
 *
 * Version:	@(#)lockfile.c  0.1.1  19-Feb-1998  miquels@cistron.nl
 *
 *		Copyright (C) Miquel van Smoorenburg 1997,1998.
 *
 *		This library is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU Library General Public
 *		License as published by the Free Software Foundation; either
 *		version 2 of the License, or (at your option) any later version.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <utime.h>
#include <paths.h>
#include <errno.h>
#include <lockfile.h>
#include <maillock.h>

static char mlockfile[MAXPATHLEN];
static int  islocked = 0;

/*
 *	Create a lockfile.
 */
int lockfile_create(const char *lockfile, int retries)
{
	struct stat st, st1;
	time_t	now;
	int sleeptime = 5;
	int statfailed = 0;
	int fd;
	int i, e;
	char tmplock[MAXPATHLEN];
	char sysname[256];
	char buf[4];
	char *p;

	/*
	 *	Safety measure.
	 */
	if (strlen(lockfile) + 32 > MAXPATHLEN) {
		errno = ENAMETOOLONG;
		return L_ERROR;
	}

	/*
	 *	Create a temp lockfile (hopefully unique) and
	 *	write 0\0 into the lockfile for svr4 compatibility.
	 */
	if (gethostname(sysname, sizeof(sysname)) < 0)
		return L_ERROR;
	if ((p = strchr(sysname, '.')) != NULL)
		*p = 0;
	strcpy(tmplock, lockfile);
	if ((p = strrchr(tmplock, '/')) == NULL)
		p = tmplock;
	else
		p++;
	sprintf(p, ".lk%05d%x%s",
		getpid(), (int)time(NULL) & 15, sysname);
	i = umask(022);
	fd = open(tmplock, O_WRONLY|O_CREAT|O_EXCL, 0644);
	e = errno;
	umask(i);
	if (fd < 0) {
		errno = e;
		return L_TMPLOCK;
	}
	i = write(fd, "0", 2);
	e = errno;
	if (close(fd) != 0) {
		e = errno;
		i = -1;
	}
	if (i != 2) {
		unlink(tmplock);
		errno = i < 0 ? e : EAGAIN;
		return L_TMPWRITE;
	}

	/*
	 *	Now try to link the temporary lock to the lock.
	 */
	for (i = 0; i < retries && retries > 0; i++) {

		sleeptime = i > 12 ? 60 : 5 * i;
		if (sleeptime > 0)
			sleep(sleeptime);

		/*
		 *	KLUDGE: some people say the return code of
		 *	link() over NFS can't be trusted.
		 *	EXTRA FIX: the value of the nlink field
		 *	can't be trusted (may be cached).
		 */
		(void)link(tmplock, lockfile);

		if (lstat(tmplock, &st1) < 0)
			return L_ERROR; /* Can't happen */

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
			return L_SUCCESS;
		}
		statfailed = 0;

		/*
		 *	Locks are invalid after 5 minutes.
		 *	Use the time of the file system.
		 */
		time(&now);
		if ((fd  = open(lockfile, O_RDONLY)) >= 0) {
			if (read(fd, buf, 1) >= 0 && fstat(fd, &st1) == 0)
				now = st1.st_atime;
			close(fd);
		}
		if (now < st.st_ctime + 300)
			continue;
		(void)unlink(lockfile);
	}
	(void)unlink(tmplock);
	errno = EAGAIN;
	return L_MAXTRYS;
}

/*
 *	Remove a lock.
 */
int lockfile_remove(const char *lockfile)
{
	return unlink(lockfile);
}

/*
 *	Touch a lock.
 */
int lockfile_touch(const char *lockfile)
{
	return utime(lockfile, NULL);
}

/*
 *	Lock a mailfile. This looks a lot like the SVR4 lockmbox() thingy.
 *	Arguments: lusername, retries.
 */
int maillock(const char *name, int retries)
{
	int i;

	if (islocked) return 0;

	snprintf(mlockfile, MAXPATHLEN, "%s/%s.lock", _PATH_MAILDIR, name);
	i = lockfile_create(mlockfile, retries);
	if (i == 0) islocked = 1;

	return i;
}

void mailunlock(void)
{
	if (!islocked) return;
	lockfile_remove(mlockfile);
	islocked = 0;
}

void touchlock(void)
{
	lockfile_touch(mlockfile);
}

