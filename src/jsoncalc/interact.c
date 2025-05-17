#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <json.h>
#include <calc.h>
#include "jsoncalc.h"

/* This indicates whether we're running something or reading a command line */
static int running;

/* This catches the SIGINT signal, and uses it to abort an ongoing computation
 * by setting the json_interupt flag.
 */
static void catchinterupt(int signo)
{
	if (running) {
		json_interupt = 1;
		fprintf(stderr, "Stopping...\n");
	}
}

/* This is called by readline() when it receives a ^C */
static void catchRLinterupt()
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
		char *dir = json_file_path(NULL, NULL, NULL, 0, 0);
		if (!dir)
			return ".jsoncalc_history";

		/* Append "/history" to it */
		buf = (char *)malloc(strlen(dir) + 9);
		strcpy(buf, dir);
		strcat(buf, "/history");
	}

	return buf;
}



/* Interactively read a line from stdin.  */
static char *jcreadline(const char *prompt)
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

void interact(jsoncontext_t **contextref, jsoncmd_t *initcmds)
{
	char	*expr;
	jsoncmd_t *jc;
	jsoncmdout_t *result;

	/* Enable the use of history and name completion while
	 * inputting expressions.
	 */
	using_history();
	read_history(historyfile());
	rl_attempted_completion_function = jsoncalc_completion;
	rl_basic_word_break_characters = " \t\n\"\\'$><=;|&{}()[]#%^*+-:,/?~@";

	/* Catch SIGINT (usually <Ctrl-C>) and use it to stop computation.
	 * If <Ctrl-C> occurs while entering a line, it'll discard the line
	 * and give you a new prompt.
	 */
	signal(SIGINT, catchinterupt);
	rl_catch_signals = 1;
	rl_signal_event_hook = (rl_hook_func_t *)catchRLinterupt;

	/* Run the initcmds once.  (Not once for each file.) */
	result = json_cmd_run(initcmds, contextref);
	free(result);

	/* Read an expression */
	for (running = 0;
	     (expr = jcreadline("JsonCalc: ")) != NULL;
	     running = 0) {
		/* Ignore empty lines */
		if (!expr[0])
			continue;

		/* Treat all processing of the line as "running */
		running = 1;

		/* Compile */
		jc = json_cmd_parse_string(expr);
		free(expr);
		if (jc != JSON_CMD_ERROR) {
			/* Execute */
			json_interupt = 0;
			run(jc, contextref);

			/* Clean up */
			json_cmd_free(jc);

			/* If the last line was incomplete, then output a
			 * newline.  The readline() library depends on this.
			 */
			if (json_print_incomplete_line) {
				putchar('\n');
				json_print_incomplete_line = 0;
			}
		}
	}

	/* Leave the cursor on the line after the last, unused prompt */
	putchar('\n');

	/* Clean up the history file */
	history_truncate_file(historyfile(), 100);
}
