/*
 * nfslock.c	This file contains code to do a safe open(file, O_EXCL, ..)
 *		over NFS. That's especially useful for mailspools shared over
 *		NFS, until all MTAs and MUAs use code similar to that here.
 *
 *		You just compile this file into a shared library and
 *		put it in /lib as nfslock.so. Then add the line
 *		"/lib/nfslock.so" to /etc/ld.so.preload. That's all.
 *
 *		To compile: cc -fPIC -shared -o nfslock.so nfslock.c
 *
 * Version:	@(#)nfslock.c  1.20  30-Nov-1998  miquels@cistron.nl
 *
 */

#include "autoconf.h"

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef __linux__
#  error This is really only meant for Linux systems, sorry.
#endif

extern int __libc_open(const char *, int, ...);
static struct utsname uts;

/*
 *	See if the directory where is certain file is in
 *	is located on an NFS mounted volume.
 */
static int is_nfs(const char *file)
{
	char dir[1024];
	char *s;
	struct stat st;

	strncpy(dir, file, sizeof(dir));
	if ((s = strrchr(dir, '/')) != NULL)
		*s = 0;
	else
		strcpy(dir, ".");

	if (stat(dir, &st) < 0)
		return 0;

	return ((st.st_dev & 0xFF00) == 0);
}

/*
 *	See if this is a temporary file of Exim/procmail/qpopper.
 *	In that case, we do not run the special algorithm.
 */
static int istmplock(const char *file)
{
	char *p, *s;
	int len, i;

	if ((p = strrchr(file, '/')) != NULL)
		p++;
	else
		p = (char *)file;

	/* Our own temp files. */
	if (strncmp(p, ".nfs", 4) == 0)
		return 1;

	/* Qpopper. */
	if (strncmp(p, ".locktmp", 8) == 0)
		return 1;

	/* liblockfile. */
	if (strncmp(p, ".lk", 3) == 0)
		return 1;

#if 1
	/* Procmail is hard to detect... */
	if (strncmp(p, "_", 1) == 0)
		return 1;
#endif

	/* I see this on our local mail spool often */
	if (*(s = p) == '.') {
		s++;
		while(*s && *s != '.') s++;
		if (*s == '.') {
			s++;
			while(*s && ((*s >= '0' && *s <= '9') ||
			             (*s >= 'a' && *s <= 'f')))
				s++;
			if (*s == 0)
				return 1;
		}
	}

	/* Exim. */
	len = strlen(p);
	if (len > 18) {
		s = p - 18;
		if (s[0] == '.' && s[8] == '.') {
			for(i = 0; i < 18; i++) {
				if (i == 0 || i == 8) continue;
				if ((s[i] >= '0' && s[i] <= '9') ||
				    (s[i] >= 'a' && s[i] <= 'f'))
					continue;
				i = -1;
				break;
			}
			if (i > 0) return 1;
		}
	}

	/* Mutt. */
	if ((s = strrchr(p, '.')) != NULL && s[1] >= '0' && s[1] <= '9')
		if (atoi(s+1) == getpid())
			return 1;

	/* Doesn't look like a temp lockfile */
	return 0;
}

/*
 *	Put our process ID into a string.
 */
static void putpid(char *s)
{
	static char pidstr[6];
	pid_t pid;
	int i;

	if (pidstr[0] == 0) {
		pid = getpid();
		for(i = 0; i < 5; i++) {
			pidstr[4 - i] = (pid % 10) + '0';
			pid /= 10;
		}
		pidstr[5] = 0;
	}
	strcpy(s, pidstr);
}

int open(const char *file, int flags, ...)
{
	char tmp[1024];
	char *s;
	int mode, i, e, error;
	va_list ap;
	struct stat st1, st2;

	if (!(flags & O_CREAT))
		return __libc_open(file, flags);

	va_start(ap, flags);
	mode = va_arg(ap, int);
	va_end(ap);

	/*
	 *	NFS has no atomic creat-if-not-exist (O_EXCL) but we
	 *	can emulate it by creating the file under a temporary
	 *	name and then renaming it to the final destination.
	 */
	if ((flags & O_EXCL) && !istmplock(file) && is_nfs(file)) {
		/*
		 *	Try to make a unique temp name, network-wide.
		 */
		if (strlen(file) > sizeof(tmp) - 16) {
			errno = ENAMETOOLONG;
			return -1;
		}
		strcpy(tmp, file);
		if ((s = strrchr(tmp, '/')) != NULL)
			s++;
		else
			s = tmp;
		*s = 0;
		strcpy(s, ".nfs");
		if (uts.nodename[0] == 0) uname(&uts);
		for(i = 0; i < 5 && uts.nodename[i] &&
			uts.nodename[i] != '.'; i++)
				s[4 + i] = uts.nodename[i];
		putpid(s + 4 + i);
		if ((i = __libc_open(tmp, flags, mode)) < 0)
			return i;

		/*
		 *	We don't just check the result code from link()
		 *	but we stat() both files as well to see if they're
		 *	the same just to be sure.
		 */
		error = link(tmp, file);
		e = errno;
		if (error < 0) {
			(void)unlink(tmp);
			close(i);
			errno = e;
			return error;
		}

		error = stat(tmp, &st1);
		e = errno;
		(void)unlink(tmp);
		if (error < 0) {
			close(i);
			errno = e;
			return -1;
		}
		if (stat(file, &st2) < 0) {
			close(i);
			errno = e;
			return -1;
		}
		if (st1.st_ino != st2.st_ino) {
			close(i);
			errno = EEXIST;
			return -1;
		}

		return i;
	}
	return __libc_open(file, flags, mode);
}

int creat(const char *file, mode_t mode)
{
	return open(file, O_CREAT|O_WRONLY|O_TRUNC, mode);
}

