#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef _XOPEN_SOURCE
#undef __USE_XOPEN
#include <wchar.h>
#include <jsoncalc.h>

/* This plugin demonstrates how to implement settings, functions and commands
 * in a plugin.
 */

/*----------------------------------------------------------------------------*/

/* This function will be callable from JsonCalc.  The arguments will be passed
 * into this function as a json_t array.  The "agdata" is only useful for
 * aggregate functions so we'll ignore it here.  The return value is a json_t
 * too.
 *
 * You don't need to free the "args" array; that happens automatically.  The
 * result returned by this function should be freshly allocated (don't directly
 * reuse parts of "args", but json_copy() of args is okay) and will also be
 * automatically freed.  If your function allocates any temporary intermediate
 * data then it should free it explicitly though.
 */
static json_t *jfn_arity(json_t *args, void *agdata)
{
	/* Something simple: Count the arguments and return that. */
	return json_from_int(json_length(args));
}

/*----------------------------------------------------------------------------*/

/* Here we'll implement a new "example" command.  Commands are implemented as
 * two functions: one to parse the source code and return the parse tree as a
 * jsoncmd_t, and one to actually run the parsed command.  We'll do a fairly
 * complex example to demonstrate some of the parse helper functions.
 */

extern jsoncmd_t *example_parse(jsonsrc_t *src, jsoncmdout_t **referr);
extern jsoncmdout_t *example_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
jsoncmdname_t jcn_example = {NULL, "example", example_parse, example_run};

/* This parses our "example" command.  The command name has already been
 * parsed, which is how json_cmd_parse_string() and json_cmd_parse_file()
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
 * This should return the parse tree as a jsoncmd_t, or NULL.  Returning NULL
 * does *not* necessarily indicate an error; it can simply mean the command is
 * entirely handled at parse time and doesn't require any action at run time.
 * The "function" command is an example of this.
 */
jsoncmd_t *example_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	char	*text, *end, *err;
	size_t	len;
	jsoncalc_t *expr;
	jsoncmd_t *cmd;

	/* We'll use either an expression in parentheses, or literal text. */
	text = NULL;
	expr = NULL;
	json_cmd_parse_whitespace(src);
	if (*src->str == '(') {
		/* Extract the parenthesized expression, returning it as a
		 * dynamically-allocated string.  This is smart enough to
		 * handled embedded parentheses, quotes, etc.
		 */
		text = json_cmd_parse_paren(src);
		if (!text) {
			*referr = json_cmd_src_error(src, 0, "The %s command requires an expression in parentheses", "example");
			return NULL;
		}

		/* Parse the string as an expression */
		expr = json_calc_parse(text, &end, &err, 0);
		free(text);
		text = NULL;
		if (!expr) {
			*referr = json_cmd_src_error(src, 0, "Syntax error for %s: %s", "example", err);
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
	cmd = json_cmd(src, &jcn_example);
	cmd->key = text;
	cmd->calc = expr;
	return cmd;
}

/* Run an "example" command.  The result of parsing it is passed as "cmd",
 * and a reference to the context is passed via *refcontext.
 */
jsoncmdout_t *example_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	char	*text, *mustfree;
	json_t	*result;
	jsoncmdout_t *out;
	int	color;
	wchar_t	wc;
	int	in;
	mbstate_t state;

	/* If given an expression, evaluate it and convert to a string */
	result = NULL;
	mustfree = NULL;
	if (cmd->calc) {
		/* Evaluate the expression */
		result = json_calc(cmd->calc, *refcontext, NULL);

		/* If error, then return the error */
		if (result->type == JSON_NULL && *result->text) {
			out = json_cmd_error(cmd->filename, cmd->lineno, 0, "Error in %s: %s", "example", result->text);
			json_free(result);
			return out;
		}

		/* If the expression is a string, use its value.  Otherwise
		 * convert to a string.
		 */
		if (result->type == JSON_STRING)
			text = result->text;
		else
			text = mustfree = json_serialize(result, NULL);
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
	json_free(result);
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
	json_t	*section, *settings;

	/* Add settings. Here we're only defining the names, types, and default
	 * values. The actual values will be loaded later, either from the
	 * command line via "-ssettings" or "-lexample,settings", or scripts
	 * via "set settings" or "plugin example,settings", or from the
	 * ~/.config/jsoncalc/jsoncalc.json file.and will only be available
	 * when the functions and commands are invoked.
	 */
	settings = json_parse_string("{\"str\":\"Wow!\",\"num\":4,\"bool\":true}");
	section = json_by_key(json_config, "plugin");
	json_append(section, json_key("example", settings));

	/* Register the functions */
	json_calc_function_hook("arity",  "x:any, ...", "number", jfn_arity);

	/* Register the commands.  The first arg is the plugin name. */
	json_cmd_hook("example", "example", example_parse, example_run);

	/* Success */
	return NULL;
}
