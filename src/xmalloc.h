/* Defines dynamic memory functions that do a few more saftey checks.
   Also defines a convenient memory free macro.  You might decide to
   use "gnulib" in place of this implementation.

Copyright (C) 2013 Andrew Makousky

See the file "COPYING" in the top level directory for details.

*/

#ifndef XMALLOC_H
#define XMALLOC_H

#include <stdlib.h>

void *xmalloc(size_t num);
void *xrealloc(void *mem, size_t num);
void xfree(void *mem);
char *xstrdup(const char *str);

/* Easy Free: free memory and nullify pointer.  */
#define EFREE(mem)			\
{							\
	xfree((mem));			\
	(mem) = NULL;			\
}

#endif /* not XMALLOC_H */
