#ifndef _MAILLOCK_H
#define _MAILLOCK_H

#ifdef  __cplusplus
extern "C" {
#endif

/*
 *	Prototypes.
 */
int	maillock(const char *name, int retries);
void	touchlock();
void	mailunlock();

#ifdef  __cplusplus
}
#endif

#include <lockfile.h>

#endif /* _MAILLOCK_H */
