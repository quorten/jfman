/* A simple implementation of shell globbing.
   Only "MS-DOS" globbing is supported.
   Glob characters must have the high bit set.

Copyright (C) 2017 Andrew Makousky

See the file "COPYING" in the top level directory for details.

*/

#ifndef SHGLOB_H
#define SHGLOB_H

#ifndef GLOB_NOMATCH
#define GLOB_NOMATCH 1
#endif

#ifndef GLOB_ABORTED
#define GLOB_ABORTED 2
#endif

typedef struct shglob_glob_tag shglob_glob_t;

struct shglob_glob_tag
{
	unsigned gl_pathc; /* Number of paths matched */
	char **gl_pathv; /* List of matched pathnames */
	/* Number of leading entries to reserve in gl_pathv.  */
	unsigned gl_offs;
};

int shglob_glob(char *pattern, int flags, void *errfunc,
				shglob_glob_t *pglob);
void shglob_globfree(shglob_glob_t *pglob);

#endif /* not SHGLOB_H */
