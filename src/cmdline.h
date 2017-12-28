/* Command-line processing functions.

Copyright (C) 2013 Andrew Makousky

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

#ifndef CMDLINE_H
#define CMDLINE_H

typedef int (*CmdFunc)(int, char *[]);

/* Escape newlines with ${NL} on output?  */
extern bool escape_newlines;
/* Typically external processing errors will use `last_line_num' in
   preference to `line_num'.  */
extern unsigned last_line_num;
extern unsigned line_num;

int proc_cmd_dispatch(FILE *fp, unsigned num_cmds, const char *commands[],
		      const CmdFunc cmdfuncs[], bool free_argv);
int read_cmdline(FILE *fp, int *argc, char ***argv);
void write_cmdline(FILE *fp, int argc, char *argv[], int num_file_args);

#endif /* not CMDLINE_H */
