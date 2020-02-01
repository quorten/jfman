/* Empty stubs for programs that don't require globbing.

Copyright (C) 2017 Andrew Makousky

See the file "COPYING" in the top level directory for details.

*/

#include <stdlib.h>

#include "shglob.h"

int shglob_glob(char *pattern, int flags, void *errfunc,
				shglob_glob_t *pglob)
{
	if (pattern == NULL || pglob == NULL)
		return GLOB_ABORTED;
	return GLOB_NOMATCH;
}

void shglob_globfree(shglob_glob_t *pglob)
{
}
