/* A simple implementation of a sub-shell environment variable table.

Copyright (C) 2013, 2017 Andrew Makousky

See the file "COPYING" in the top level directory for details.

*/

#include <stdio.h>

#include "xmalloc.h"
#include "exparray.h"
#include "shenv.h"

struct shenv_envvar_tag
{
	char *name;
	char *value;
};
typedef struct shenv_envvar_tag shenv_envvar;
EA_TYPE(shenv_envvar);

static shenv_envvar_array shenv_env;

void shenv_init()
{
	EA_INIT(shenv_env, 16);
}

void shenv_destroy()
{
	unsigned i;
	for (i = 0; i < shenv_env.len; i++)
	{
		xfree(shenv_env.d[i].name);
		xfree(shenv_env.d[i].value);
	}
	EA_DESTROY(shenv_env);
}

const char *shenv_getenv(const char *name)
{
	unsigned i;
	if (shenv_env.d == NULL)
		return NULL;

	for (i = 0; i < shenv_env.len; i++)
	{
		if (!strcmp(shenv_env.d[i].name, name))
			return shenv_env.d[i].value;
	}
	return NULL;
}

int shenv_setenv(const char *name, const char *value, int replace)
{
	unsigned i;
	if (shenv_env.d == NULL)
		return -1;

	/* Check if the variable already exists.  */
	for (i = 0; i < shenv_env.len; i++)
	{
		if (!strcmp(shenv_env.d[i].name, name))
			break;
	}

	if (replace && i < shenv_env.len)
	{
		xfree(shenv_env.d[i].value);
		shenv_env.d[i].value = xstrdup(value);
	}

	if (i == shenv_env.len)
	{
		shenv_env.d[i].name = xstrdup(name);
		shenv_env.d[i].value = xstrdup(value);
		EA_ADD(shenv_env);
	}

	return 0;
}

int shenv_unsetenv(const char *name)
{
	unsigned i;
	if (shenv_env.d == NULL)
		return -1;

	for (i = 0; i < shenv_env.len; i++)
	{
		if (!strcmp(shenv_env.d[i].name, name))
		{
			xfree(shenv_env.d[i].name);
			xfree(shenv_env.d[i].value);
			EA_REMOVE(shenv_env, i);
			EA_NORMALIZE(shenv_env);
			return 0;
		}
	}

	return -1;
}

void shenv_print_vars(void)
{
	unsigned i;
	for (i = 0; i < shenv_env.len; i++)
	{
		fputs(shenv_env.d[i].name, stdout);
		putchar('=');
		puts(shenv_env.d[i].value);
	}
}
