#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <jx.h>
#include "jxprog.h"

/* This indicates whether we're running something or reading a command line */
static int running;

/* This catches the SIGINT signal, and uses it to abort an ongoing computation
 * by setting the jx_interrupt flag.
 */
static void catchinterrupt(int signo)
{
	if (running) {
		jx_interrupt = 1;
		fprintf(stderr, "Stopping...\n");
	}
}

/* This is called by readline() when it receives a ^C */
static void catchRLinterrupt()
{
	/* Wipe out the partially-entered line */
	rl_free_line_state();
	rl_cleanup_after_signal();
	rl_replace_line("", 0);

	/* Start a new line */
	puts("");
	rl_on_new_line();
	rl_redisplay();
}


static char *historyfile(void)
{
	static char	*buf = NULL;

	/* First time, generate it */
	if (!buf) {
		/* Get the config directory */
		char *dir = jx_file_path(NULL, NULL, NULL);
		if (!dir)
			return ".jx.history";

		/* Append "/history" to it */
		buf = (char *)malloc(strlen(dir) + 9);
		strcpy(buf, dir);
		strcat(buf, "/history");
	}

	return buf;
}



/* Interactively read a line from stdin.  */
static char *jxreadline(const char *prompt)
{
	char	*expr;
	size_t	i;

	/* Read a line into a dynamic buffer; */
	expr = readline(prompt);
	if (!expr)
		return NULL;
	i = strlen(expr);

	/* Trim trailing whitespace, possibly including a newline */
	for (; i > 0 && isspace(expr[i - 1]); i--) {
	}
	expr[i] = '\0';

	/* Add to history */
	add_history(expr);
	write_history(historyfile());

	/* Return it */
	return expr;
}

/* Built a colorized prompt string.  This is basically the given prompt with
 * a couple of escape sequences added to start and stop the prompt colors,
 * and also a couple of special characters that ReadLine uses to indicate
 * certain parts of the prompt (namely those escape sequences) are zero-width.
 */
static char *jxprompt(const char *prompt)
{
	static char	buf[50];

	sprintf(buf, "%c%s%c%s%c%s%c ",
		RL_PROMPT_START_IGNORE,
		"\033[32;1m",
		RL_PROMPT_END_IGNORE,
		prompt,
		RL_PROMPT_START_IGNORE,
		"\033[m",
		RL_PROMPT_END_IGNORE);

	return buf;
}


void interact(jxcontext_t **contextref, jxcmd_t *initcmds)
{
	char	*expr;
	jxcmd_t *jc;
	jxcmdout_t *result;

	/* Enable the use of history and name completion while
	 * inputting expressions.
	 */
	using_history();
	read_history(historyfile());
	rl_attempted_completion_function = jx_completion;
	rl_basic_word_break_characters = " \t\n\"\\'$><=;|&{}()[]#%^*+-:,/?~@";

	/* Catch SIGINT (usually <Ctrl-C>) and use it to stop computation.
	 * If <Ctrl-C> occurs while entering a line, it'll discard the line
	 * and give you a new prompt.
	 */
	signal(SIGINT, catchinterrupt);
	rl_catch_signals = 1;
	rl_signal_event_hook = (rl_hook_func_t *)catchRLinterrupt;

	/* Run the initcmds once.  (Not once for each file.) */
	result = jx_cmd_run(initcmds, contextref);
	free(result);

	/* Read an expression */
	for (running = 0;
	     (expr = jxreadline(jxprompt("jx:"))) != NULL;
	     running = 0) {
		/* Ignore empty lines */
		if (!expr[0])
			continue;

		/* Treat all processing of the line as "running */
		running = 1;

		/* Compile */
		jc = jx_cmd_parse_string(expr);
		free(expr);
		if (jc != JX_CMD_ERROR) {
			/* Execute */
			jx_interrupt = 0;
			run(jc, contextref);

			/* Clean up */
			jx_cmd_free(jc);

			/* If the last line was incomplete, then output a
			 * newline.  The readline() library depends on this.
			 */
			if (jx_print_incomplete_line) {
				putchar('\n');
				jx_print_incomplete_line = 0;
			}
		}
	}

	/* Leave the cursor on the line after the last, unused prompt */
	putchar('\n');

	/* Clean up the history file */
	history_truncate_file(historyfile(), 100);
}
