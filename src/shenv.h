/* A simple implementation of a sub-shell environment variable table.

Copyright (C) 2013, 2017 Andrew Makousky

See the file "COPYING" in the top level directory for details.

*/

#ifndef SHENV_H
#define SHENV_H

void shenv_init();
void shenv_destroy();
const char *shenv_getenv(const char *name);
int shenv_setenv(const char *name, const char *value, int replace);
int shenv_unsetenv(const char *name);
void shenv_print_vars(void);

#endif /* not SHENV_H */
