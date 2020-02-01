/* Command-line processing functions.

Copyright (C) 2013, 2017, 2018 Andrew Makousky

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>

#include "bool.h"
#include "xmalloc.h"
#include "exparray.h"
#include "shenv.h"
#include "shglob.h"
#include "cmdline.h"

typedef char* char_ptr;
EA_TYPE(char);
EA_TYPE(char_ptr);

/* Escape newlines with ${NL} on output?  */
bool escape_newlines = true;
/* Typically external processing errors will use `last_line_num' in
   preference to `line_num'.  */
unsigned last_line_num = 1;
unsigned line_num = 1;

/* Read command-lines from the given file, then call a callback
   function based off of each command name.  */
int proc_cmd_dispatch(FILE *fp, unsigned num_cmds, const char *cmdnames[],
					  const CmdFunc cmdfuncs[], bool free_argv)
{
	int retval = 0;
	int argc; char **argv;
	int read_status;
	shenv_init();
	shenv_setenv("?", "0", 1);
	while ((read_status = read_cmdline(fp, &argc, &argv)) != -1)
	{
		char *cmd_name;
		unsigned i;

		if (argc == 0) /* blank/comment line */
		{
			xfree(argv);
			if (read_status == 0)
				break;
			last_line_num = line_num;
			continue;
		}

		/* Read the command name, then dispatch to the proper function
		   with the command-line arguments.  */
		cmd_name = argv[0];
		for (i = 0; i < num_cmds; i++)
		{
			if (!strcmp(cmd_name, cmdnames[i]))
			{
				int j;
				int cmdret = (*cmdfuncs[i])(argc, argv);
				char strbuf[12]; /* 32-bit int "-2147483647\0" */
				sprintf(strbuf, "%d", cmdret);
				shenv_setenv("?", strbuf, 1);
				if (cmdret)
					fprintf(stderr, "Line %u: error: exit status %d\n",
							last_line_num, cmdret);
				retval = retval || cmdret;
				/* If `free_argv' is `false', then the processing
				   function is assumed to handle freeing `argv'.  */
				if (free_argv)
				{
					for (j = 0; j < argc; j++)
						xfree(argv[j]);
					xfree(argv);
				}
				break;
			}
		}
		if (i == num_cmds)
		{
			int j;
			fprintf(stderr, "Line %u: error: unknown command: %s\n",
					last_line_num, cmd_name);
			shenv_setenv("?", "127", 1);
			for (j = 0; j < argc; j++)
				xfree(argv[j]);
			xfree(argv);
		}

		if (read_status == 0)
			break;
		last_line_num = line_num;
	}
	shenv_destroy();
	if (read_status == -1) /* fatal parse error */
	{
		int i;
		for (i = 0; i < argc; i++)
			xfree(argv[i]);
		xfree(argv);
		return 1;
	}
	return retval;
}

#define PROC_VARSUB \
{ \
	char_array varname; \
	const char *value = NULL; \
	EA_INIT(varname, 16); \
	/* Check if this is an escaped newline.  */ \
	if ((ch = getc(fp)) == '{') \
	{ \
		while ((ch = getc(fp)) != '}' && ch != EOF) \
			EA_APPEND(varname, ch); \
		EA_APPEND(varname, '\0'); \
		if (ch == '}') \
			ch = getc(fp); \
	} \
	else if (ch == '(') \
	{ \
		fatal_error = true; \
		error_desc = "command substitution is not supported"; \
	} \
	else \
	{ \
		do \
			EA_APPEND(varname, ch); \
		while ((isalnum(ch = getc(fp)) || ch == '_') && \
			   ch != EOF); \
		EA_APPEND(varname, '\0'); \
	} \
	value = shenv_getenv(varname.d); \
	if (value) \
		EA_APPEND_MULT(cur_arg, value, strlen(value)); \
	else \
	{ \
		syntax_error = true; \
		error_desc = "unknown substitution"; \
	} \
	EA_DESTROY(varname); \
}

/* PERFORMANCE CRITICAL */
/* Read a command-line from the given file into argument vectors.
   `argc' and `argv' of the vectorized command line are output into
   the given `*argc' and `*argv' arguments.  This function only has
   rudimentary parsing capabilities.  In particular, it only
   recognizes backslashes, single quotes, double quotes, and comments,
   with rudimentary parsing for each.  Command substitution is not
   supported.  `argv' and all of the strings it points to are
   dynamically allocated and must be freed.  */
int read_cmdline(FILE *fp, int *argc, char ***argv)
{
	bool process_globs = false;
	bool no_quotes = true;
	bool var_assign = false;
	bool found_comment = false;
	bool syntax_error = false;
	bool fatal_error = false;
	char *error_desc = "";
	char_ptr_array loc_argv;
	char_array cur_arg;
	int ch;
	EA_INIT(loc_argv, 16);
	EA_INIT(cur_arg, 16);
	ch = getc(fp);

	/* Skip leading whitespace.  */
	while ((ch == ' ' || ch == '\t') &&
		   ch != '\n' && ch != EOF && ch != ';')
		ch = getc(fp);

	while (ch != '\n' && ch != EOF && ch != ';')
	{
		bool backslash_newline = false;
		switch (ch)
		{
		/* Process any quoted sequences.  */
		case '\'':
			no_quotes = false;
			ch = getc(fp);
			while (ch != '\'' && ch != EOF)
			{
				if (ch == '\n')
					line_num++;
				EA_APPEND(cur_arg, ch);
				ch = getc(fp);
			}
			if (ch == '\'')
				ch = getc(fp);
			else
			{
				fatal_error = true;
				error_desc = "missing quote";
			}
			break;
		case '"':
			no_quotes = false;
			ch = getc(fp);
			while (ch != '"' && ch != EOF)
			{
				bool backslash_in_quotes = false;
				switch (ch)
				{
				case '\n': line_num++; break;
				case '\\':
					backslash_in_quotes = true;
					ch = getc(fp);
					if (ch == '\n')
					{
						line_num++;
						ch = getc(fp);
					}
					break;
				case '$':
					PROC_VARSUB;
					break;
				}
				if (backslash_in_quotes || ch != '"')
				{
					EA_APPEND(cur_arg, ch);
					ch = getc(fp);
				}
				if (syntax_error)
				{
					fprintf(stderr, "Line %u: syntax error: %s\n",
							line_num, error_desc);
					syntax_error = false; /* recoverable error */
				}
			}
			if (ch == '"')
				ch = getc(fp);
			else
			{
				fatal_error = true;
				error_desc = "missing quote";
			}
			break;
		case '`':
			fatal_error = true;
			error_desc = "command substitution is not supported";
			break;

		/* Process any escape sequences.  */
		case '\\':
			ch = getc(fp);
			if (ch == EOF)
				break;
			if (ch != '\n')
			{
				EA_APPEND(cur_arg, ch);
				ch = getc(fp);
			}
			else /* (ch == '\n') */
			{
				backslash_newline = true;
				line_num++;
				ch = getc(fp);
			}
			break;

		/* Process variable assignment triggers.  */
		case '=':
			if (no_quotes)
				var_assign = true;
			EA_APPEND(cur_arg, ch);
			ch = getc(fp);
			break;

		/* Process variable substitutions.  */
		case '$':
			PROC_VARSUB;
			if (syntax_error)
			{
				fprintf(stderr, "Line %u: syntax error: %s\n",
						line_num, error_desc);
				syntax_error = false; /* recoverable error */
			}
			break;

		/* Process shell globbing.  */
		case '*':
		case '?':
			/* Note that shell globbing can only expand when there is
			   a filesystem in context.  */
			process_globs = true;
			/* TODO FIXME: We set the high bit to tag that this is a
			   glob character.  However, this can conflict with UTF-8
			   sequences.  The "right" way to do this is to escape all
			   previous unescaped wildcard characters and make sure
			   all new such characters are escaped as they are
			   added.  */
			ch |= 0x80;
			EA_APPEND(cur_arg, ch);
			ch = getc(fp);
			break;

		/* Process any comments.  */
		case '#':
			found_comment = true;
			while (ch != '\n' && ch != EOF)
				ch = getc(fp);
			break;

		case ' ':
		case '\t':
			/* Handled outside of switch statement.  */
			break;

		case '>':
		case '<':
		case '|':
		case '&':
		case '(':
		case ')':
			fatal_error = true;
			error_desc = "unsupported shell grammar found";
			break;
		case '\0':
			fatal_error = true;
			error_desc = "null character found in input";
			break;

		/* Process unquoted sequences.  */
		default:
			EA_APPEND(cur_arg, ch);
			ch = getc(fp);
			break;
		}

		if (fatal_error)
			break;
		if (ch != ' ' && ch != '\t' &&
			ch != '\n' && ch != EOF && ch != ';')
			continue;
		if (found_comment)
			break;

		if (var_assign)
		{
			/* Process a variable assignment.  */
			char *varname;
			char *value;
			EA_APPEND(cur_arg, '\0');
			value = varname = cur_arg.d;

			while (*value != '\0' && *value != '=')
				value++;
			if (*value == '=')
			{ *value = '\0'; value++; }

			if (*value != '\0')
				shenv_setenv(varname, value, 1);
			else
				shenv_unsetenv(varname);

			/* Skip consecutive whitespace.  */
			while (ch == ' ' || ch == '\t')
				ch = getc(fp);
			if (ch == '\n' || ch == EOF || ch == ';')
				break;

			cur_arg.len = 0;
			var_assign = false;
			continue;
		}

		/* Do not add an empty argument if there is a backslash before
		   a newline, not surrounded by quotes, and the current
		   argument is of length zero.  */
		if (backslash_newline && cur_arg.len == 0)
			xfree(cur_arg.d);
		else
		{
			/* Add the current argument to the argument array.  */
			EA_APPEND(cur_arg, '\0');
			/* If the glob expression has matches, then add all
			   matching results as arguments.  Otherwise, unflag the
			   glob characters and just add the single argument.  */
			if (process_globs)
			{
				shglob_glob_t res_globs = { 0, NULL, 0 };
				if (!shglob_glob(cur_arg.d, 0, NULL, &res_globs))
				{
					EA_APPEND_MULT(loc_argv, res_globs.gl_pathv,
								   res_globs.gl_pathc);
					xfree(res_globs.gl_pathv);
					xfree(cur_arg.d);
				}
				else
				{
					/* Unescape characters.  */
					unsigned i;
					for (i = 0; i < cur_arg.len; i++)
					{
						if (cur_arg.d[i] == (char)(0x80 | '?'))
							cur_arg.d[i] = '?';
						else if (cur_arg.d[i] == (char)(0x80 | '*'))
							cur_arg.d[i] = '*';
					}
					EA_APPEND(loc_argv, cur_arg.d);
					shglob_globfree(&res_globs);
				}
			}
			else
				EA_APPEND(loc_argv, cur_arg.d);
			cur_arg.d = NULL; /* Memory ownership has passed on.  */
			process_globs = false;
			if (ch == '\n' || ch == EOF || ch == ';')
				break;
		}

		/* Skip consecutive whitespace.  */
		while (ch == ' ' || ch == '\t')
			ch = getc(fp);
		if (ch == '\n' || ch == EOF || ch == ';')
			break;
		/* Prepare for adding the next argument vector.  */
		EA_INIT(cur_arg, 16);
	}
	if (ch == '\n')
		line_num++;

	EA_DESTROY(cur_arg);
	*argc = loc_argv.len;
	EA_APPEND(loc_argv, NULL);
	*argv = loc_argv.d;
	if (*argc < 0)
	{
		unsigned i;
		fprintf(stderr,
				"Line %u: ERROR: Over-sized command line detected!\n",
				last_line_num);
		for (i = INT_MAX; i < loc_argv.len; i++)
			xfree(loc_argv.d[i]);
		EA_SET_SIZE(loc_argv, INT_MAX);
		*argc = loc_argv.len;
		*argv = loc_argv.d;
	}
	if (syntax_error || fatal_error)
	{
		fprintf(stderr, "Line %u: syntax error: %s\n",
				line_num, error_desc);
		if (fatal_error)
		{
			fputs("Cannot continue.\n", stderr);
			fprintf(stderr, "Try checking downward from line %u.\n",
					last_line_num);
			return -1;
		}
	}
	if (ch == EOF)
		return 0;
	return 1;
}

/* Turn a vectorized command line into a textual command line with
   special shell characters in any arguments escaped as proper.  The
   textual command line is written to standard output.  Any arguments
   with special characters in them will be in double quotes.
   `num_file_args' specifies how many of the last arguments will be
   treated specially by not having the very first `$' character
   escaped.  This feature is primarily used by `asup' to specify file
   names in terms of generic prefix variables.  */
void write_cmdline(FILE *fp, int argc, char *argv[], int num_file_args)
{
	int i;
	for (i = 0; i < argc; i++)
	{
		unsigned j;
		bool quote_arg = false;

		/* Check if this argument needs to be quoted.  */
		for (j = 0; argv[i][j] != '\0' && !quote_arg; j++)
		{
			switch (argv[i][j])
			{
			case '\'': case '"':  case '`':
			case '\\':
			case '$':
			case '*': case '?':
			case '#':
			case ' ':  case '\t': case '\n':
			case '>': case '<': case '|': case '&': case '(': case ')':
				quote_arg = true;
			}
		}

		if (!quote_arg)
			fputs(argv[i], fp);
		else
		{
			putc('"', fp);
			for (j = 0; argv[i][j] != '\0'; j++)
			{
				switch (argv[i][j])
				{
				case '$':
					/* Don't escape intentional dollar signs.  */
					if (j == 0 && i >= argc - num_file_args)
						break;
				case '\\':
				case '"':
				case '`':
					putc('\\', fp);
				break;
				case '\n':
					if (escape_newlines)
					{
						fputs("${NL}", fp);
						continue; /* Restart at outer loop */
					}
					break;
				}
				putc(argv[i][j], fp);
			}
			putc('"', fp);
		}

		putc((i < argc - 1) ? ' ' : '\n', fp);
	}
}
