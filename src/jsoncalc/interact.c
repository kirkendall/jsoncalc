#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <json.h>
#include <calc.h>
#include "jsoncalc.h"

/* This catches the SIGINT signal, and uses it to abort an ongoing computation
 * by setting the json_interupt flag.
 */
static void catchinterupt(int signo)
{
	json_interupt = 1;
	fprintf(stderr, "Stopping...\n");
}

/* Interactively read a line from stdin.  This uses GNU readline unless it it
 * inhibitied or not configured.
 */
static char *jcreadline(const char *prompt)
{
	char	*expr;
	int	ch;
	size_t	i, size;

	/* Read a line into a dynamic buffer; */
	if (!inhibit_readline) {
		expr = readline("JsonCalc: ");
		if (!expr)
			return NULL;
		i = strlen(expr);
	} else {
		fputs(prompt, stdout);
		size = 80;
		expr = (char *)malloc(size);
		for (i = 0; (ch = getchar()) != EOF && ch != '\n'; i++) {
			if (size < i + 2) {
				size += 80;
				expr = realloc(expr, size);
			}
			expr[i] = ch;
		}
		if (ch == EOF) {
			free(expr);
			return NULL;
		}
	}

	/* Trim trailing whitespace, possibly including a newline */
	for (; i > 0 && isspace(expr[i - 1]); i--) {
	}
	expr[i] = '\0';

	/* Add to history, if using readline */
	if (!inhibit_readline) {
		add_history(expr);
		write_history(HISTORY_FILE);
	}

	/* Return it */
	return expr;
}

void interact(jsoncontext_t **contextref, jsoncmd_t *initcmds)
{
	char	*expr, *val, *errmsg;
	jsoncmd_t *jc;
	jsoncmdout_t *result;

	/* Enable the use of history and name completion while
	 * inputting expressions.
	 */
	using_history();
	read_history(HISTORY_FILE);
	rl_attempted_completion_function = jsoncalc_completion;
	rl_basic_word_break_characters = " \t\n\"\\'$><=;|&{}()[]#%^*+-:,/?~@";

	/* Catch SIGINT (usually <Ctrl-C>) and use it to stop computation */
	signal(SIGINT, catchinterupt);

	/* Run the initcmds once.  (Not once for each file.) */
	result = json_cmd_run(initcmds, contextref);
	free(result);

	/* Read an expression */
	while ((expr = jcreadline("JsonCalc: ")) != NULL) {
		/* Ignore empty lines */
		if (!expr[0])
			continue;

		/* Maybe -Oformat? */
		if (!strncmp(expr, "-O", 2)) {
			for (val = expr + 2; *val == ' '; val++) {
			}
			if (*val == '?') {
				format_usage();
			} else {
				errmsg = json_format(NULL, val);
				if (errmsg)
					puts(errmsg);
				val = json_format_str(NULL);
				printf("-O%s\n", val);
				free(val);
			}
			free(expr);
			continue;
		}

		/* Maybe -Ccolors? */
		if (!strncmp(expr, "-C", 2)) {
			for (val = expr + 2; *val == ' '; val++) {
			}
			if (*val == '?') {
				color_usage();
			} else {
				errmsg = json_format_color(val);
				if (errmsg)
					puts(errmsg);
				val = json_format_color_str();
				printf("-C%s\n", val);
				free(val);
			}
			free(expr);
			continue;
		}

		/* Compile */
		jc = json_cmd_parse_string(expr);
		free(expr);

		/* Execute */
		json_interupt = 0;
		run(jc, contextref);

		/* Clean up */
		json_cmd_free(jc);
	}

	/* Leave the cursor on the line after the last, unused prompt */
	putchar('\n');

	/* Clean up the history file */
	history_truncate_file(HISTORY_FILE, 100);
}