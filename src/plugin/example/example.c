#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef _XOPEN_SOURCE
#undef __USE_XOPEN
#include <wchar.h>
#include <jx.h>

/* This plugin demonstrates how to implement settings, functions and commands
 * in a plugin.
 */

/*----------------------------------------------------------------------------*/

/* This function will be callable from jx.  The arguments will be passed
 * into this function as a jx_t array.  The "agdata" is only useful for
 * aggregate functions so we'll ignore it here.  The return value is a jx_t
 * too.
 *
 * You don't need to free the "args" array; that happens automatically.  The
 * result returned by this function should be freshly allocated (don't directly
 * reuse parts of "args", but jx_copy() of args is okay) and will also be
 * automatically freed.  If your function allocates any temporary intermediate
 * data then it should free it explicitly though.
 */
static jx_t *jfn_arity(jx_t *args, void *agdata)
{
	/* Something simple: Count the arguments and return that. */
	return jx_from_int(jx_length(args));
}

/*----------------------------------------------------------------------------*/

/* Here we'll implement a new "example" command.  Commands are implemented as
 * two functions: one to parse the source code and return the parse tree as a
 * jxcmd_t, and one to actually run the parsed command.  We'll do a fairly
 * complex example to demonstrate some of the parse helper functions.
 */

static jxcmdname_t *jcn_example;

/* This parses our "example" command.  The command name has already been
 * parsed, which is how jx_cmd_parse_string() and jx_cmd_parse_file()
 * know to call this function.
 * 
 * "src" is a simple object containing a "str" member which points into
 * a buffer containing the source code.  There are also some other members
 * which help with error reporting, but src->str is the main focus.
 * Initially it will point to the first non-whitespace character after the
 * command name.
 *
 * If an error is detected, then set *referr as appropriate.
 * 
 * This should return the parse tree as a jxcmd_t, or NULL.  Returning NULL
 * does *not* necessarily indicate an error; it can simply mean the command is
 * entirely handled at parse time and doesn't require any action at run time.
 * The "function" command is an example of this.
 */
static jxcmd_t *example_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	char	*text;
	const char *end, *err;
	size_t	len;
	jxcalc_t *expr;
	jxcmd_t *cmd;

	/* We'll use either an expression in parentheses, or literal text. */
	text = NULL;
	expr = NULL;
	jx_cmd_parse_whitespace(src);
	if (*src->str == '(') {
		/* Extract the parenthesized expression, returning it as a
		 * dynamically-allocated string.  This is smart enough to
		 * handled embedded parentheses, quotes, etc.
		 */
		text = jx_cmd_parse_paren(src);
		if (!text) {
			*referr = jx_cmd_src_error(src, 0, "The %s command requires an expression in parentheses", "example");
			return NULL;
		}

		/* Parse the string as an expression */
		expr = jx_calc_parse(text, &end, &err, 0);
		free(text);
		text = NULL;
		if (!expr) {
			*referr = jx_cmd_src_error(src, 0, "Syntax error for %s: %s", "example", err);
			return NULL;
		}
	} else {
		/* collect chars up to the end of the command */
		for (len = 0; &src->str[len] < &src->buf[src->size] && !strchr("\n;}", src->str[len]); len++) {
		}
		text = (char *)malloc(len + 1);
		strncpy(text, src->str, len);
		text[len] = '\0';
		src->str += len;
		if (*src->str == ';')
			src->str++;
	}

	/* Build a command containing the text */
	cmd = jx_cmd(src, jcn_example);
	cmd->key = text;
	cmd->calc = expr;
	return cmd;
}

/* Run an "example" command.  The result of parsing it is passed as "cmd",
 * and a reference to the context is passed via *refcontext.
 */
static jxcmdout_t *example_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	char	*text, *mustfree;
	jx_t	*result;
	jxcmdout_t *out;
	int	color;
	wchar_t	wc;
	int	in;
	mbstate_t state;

	/* If given an expression, evaluate it and convert to a string */
	result = NULL;
	mustfree = NULL;
	if (cmd->calc) {
		/* Evaluate the expression */
		result = jx_calc(cmd->calc, *refcontext, NULL);

		/* If error, then return the error */
		if (result->type == JX_NULL && *result->text) {
			out = jx_cmd_error(cmd->where, "Error in %s: %s", "example", result->text);
			jx_free(result);
			return out;
		}

		/* If the expression is a string, use its value.  Otherwise
		 * convert to a string.
		 */
		if (result->type == JX_STRING)
			text = result->text;
		else
			text = mustfree = jx_serialize(result, NULL);
	} else {
		/* The command uses literal text */
		text = cmd->key;
	}

	/* Output each character in a different color */
	memset(&state, 0, sizeof state);
	for (color = 0; *text; color = (color + 1) & 0x7, text += in) {
		in = mbrtowc(&wc, text, MB_CUR_MAX, &state);
		fprintf(stderr, "\033[3%d;1m%.*s", color, in, text);
	}
	fputs("\033m\n", stderr);

	/* Return NULL to continue to next command */
	jx_free(result);
	if (mustfree)
		free(mustfree);
	return NULL;
}

/*----------------------------------------------------------------------------*/

/* This is the init function.  It registers all of the above functions and
 * commands, and adjusts the settings.
 */
char *pluginexample()
{
	jx_t	*section, *settings;

	/* Add settings. Here we're only defining the names, types, and default
	 * values. The actual values will be loaded later, either from the
	 * command line via "-ssettings" or "-lexample,settings", or scripts
	 * via "set settings" or "plugin example,settings", or from the
	 * ~/.config/jxcalc/jxcalc.json file.and will only be available
	 * when the functions and commands are invoked.
	 */
	settings = jx_parse_string("{\"str\":\"Wow!\",\"num\":4,\"bool\":true}");
	section = jx_by_key(jx_config, "plugin");
	jx_append(section, jx_key("example", settings));

	/* Register the functions */
	jx_calc_function_hook("arity",  "x:any, ...", "number", jfn_arity);

	/* Register the commands.  The first arg is the plugin name. */
	jcn_example = jx_cmd_hook("example", "example", example_parse, example_run);

	/* Success */
	return NULL;
}
