#ifndef _LOCKFILE_H
#define _LOCKFILE_H

#ifdef  __cplusplus
extern "C" {
#endif

/*
 *	Prototypes.
 */
int	lockfile_create(const char *lockfile, int retries);
int	lockfile_remove(const char *lockfile);
int	lockfile_touch(const char *lockfile);

/*
 *	Constants.
 */
#define	L_SUCCESS	0	/* Lockfile created			*/
#define L_NAMELEN	1	/* Recipient name too long (> 13 chars)	*/
#define L_TMPLOCK	2	/* Error creating tmp lockfile		*/
#define L_TMPWRITE	3	/* Can't write pid int tmp lockfile	*/
#define L_MAXTRYS	4	/* Failed after max. number of attempts	*/
#define L_ERROR		5	/* Unknown error; check errno		*/

#ifdef  __cplusplus
}
#endif

#endif /* _LOCKFILE_H */
