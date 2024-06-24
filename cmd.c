#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <regex.h>
#include <assert.h>
#include "json.h"
#include "calc.h"

/* This handles commands.  Each script is a series of commands, so this is
 * pretty central.  While expressions use a decent LALR parser with operator
 * precedence (implemented in calcparse.c), commands use a simple recursive
 * descent parser.  This is because recursive descent parsers are more modular
 * and therefor easier to extend.  A plugin can register a new command by
 * calling json_cmd_hook() with the name, and pointers to the argument parsing
 * function and run function.
 *
 * A command's parsers can use the json_cmd_parse_whitespace(),
 * json_cmd_parse_key(), json_cmd_parse_paren(), json_cmd_parse_curly()
 * functions.  Also json_calc_parse() of course.
 */

/* Forward declarations for functions that implement the built-in commands */
static jsoncmd_t *if_parse(char **refstr, jsonerror_t **referr);
static jsonerror_t *if_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t *var_parse(char **refstr, jsonerror_t **referr);
static jsonerror_t *var_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t *const_parse(char **refstr, jsonerror_t **referr);
static jsonerror_t *const_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t *global_parse(char **refstr, jsonerror_t **referr);
static jsonerror_t *global_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t *function_parse(char **refstr, jsonerror_t **referr);
static jsonerror_t *function_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t *return_parse(char **refstr, jsonerror_t **referr);
static jsonerror_t *return_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsonerror_t *calc_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);

/* Linked list of command names */
static jsoncmdname_t jsn_if =       {NULL,		"if",		if_parse,	if_run};
static jsoncmdname_t jsn_var =      {&jsn_if,		"var",		var_parse,	var_run};
static jsoncmdname_t jsn_const =    {&jsn_var,		"const",	const_parse,	const_run};
static jsoncmdname_t jsn_global =   {&jsn_const,	"global",	global_parse,	global_run};
static jsoncmdname_t jsn_function = {&jsn_global,	"function",	function_parse,	function_run};
static jsoncmdname_t jsn_return =   {&jsn_function,	"return",	return_parse,	return_run};
static jsoncmdname_t *names = &jsn_return;

/* A command name struct for assignment/output.  This isn't part of the "names"
 * list because assignment/output has no name -- you just give the expression.
 */
static jsoncmdname_t jsn_calc = {NULL, "<<calc>>", NULL, calc_run};



/* Add a new statement name, and its argument parser and runner. */
void json_cmd_hook(char *pluginname, char *cmdname, jsoncmd_t *(*argparser)(char **refstr, jsonerror_t **referr), jsonerror_t *(*run)(jsoncmd_t *cmd, jsoncontext_t **refcontext))
{
	/* Allocate a jsoncmdname_t for it */
	jsoncmdname_t *sn = (jsoncmdname_t *)malloc(sizeof(jsoncmdname_t));

	/* Fill it */
	sn->pluginname = pluginname;
	sn->name = cmdname;
	sn->argparser = argparser;
	sn->run = run;

	/* Add it to the list */
	sn->next = names;
	names = sn;
}

/* Generate an error message */
jsonerror_t *json_cmd_error(char *where, int code, char *fmt, ...)
{
	va_list	ap;
	size_t	size;
	jsonerror_t *err;

	/* !!!Translate the message via catalog using "code" */

	/* Figure out how long the message will be */
	va_start(ap, fmt);
	size = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	/* Allocate the error structure with enough space for the message */
	err = (jsonerror_t *)malloc(sizeof(jsonerror_t) + size);

	/* Fill the error structure */
	err->where = where;
	err->code = code;
	va_start(ap, fmt);
	vsnprintf(err->text, size + 1, fmt, ap);
	va_end(ap);

	/* Return it */
	return err;
}

/*****************************************************************************
 * The following functions parse parts of an expression.  The various
 * {cmdname}_parse functions can call these as they see fit.
 *****************************************************************************/

/* Skip past whitespace and comments.  This may include newlines. */
void json_cmd_parse_whitespace(char **refstr)
{
	do {
		/* Actual whitespace */
		while (isspace(**refstr))
			(*refstr)++;

		/* Comments */
		if ((*refstr)[0] == '/' && (*refstr)[1] == '/') {
			while (**refstr && ** refstr != '\n')
				(*refstr)++;
		}
	} while (isspace(**refstr));
}

/* Parse a key (name) and return it as a dynamically-allocated string.
 * Advance *refstr past the name and any trailing whitespace.  Returns NULL
 * if not a name.  The "quoteable" parameter should be 1 to allow strings
 * quoted with " or ' to be considered keys, or 0 to only allow alphanumeric
 * keys or `-quoted keys.
 */
char *json_cmd_parse_key(char **refstr, int quotable)
{
	size_t	len, unescapedlen;
	char	*key;

	/* Skip leading whitespace */
	json_cmd_parse_whitespace(refstr);

	if (isalpha(**refstr) || **refstr == '_') {
		/* Unquoted alphanumeric name */
		for (len = 1; isalnum((*refstr)[len]) || (*refstr)[len] == '_'; len++){
		}
		key = (char *)malloc(len + 1);
		strncpy(key, *refstr, len);
		key[len] = '\0';
		(*refstr) += len;
	} else if (**refstr == '`') {
		/* Backtick quoted name */
		(*refstr)++;
		for (len = 0; (*refstr)[len] && (*refstr)[len] != '`'; len++){
		}
		key = (char *)malloc(len + 1);
		strncpy(key, *refstr, len);
		key[len] = '\0';
		(*refstr) += len + 1;
	} else if (quotable && (**refstr == '"' || **refstr == '\'')) {
		/* String, acting as a name */
		for (len = 1; (*refstr)[len] && (*refstr)[len] == **refstr; len++) {
			if ((*refstr)[len] == '\\' && (*refstr)[len + 1])
				len++;
		}
		unescapedlen = json_mbs_unescape(NULL, (*refstr)+1, len - 1);
		key = (char *)malloc(unescapedlen + 1);
		json_mbs_unescape(key, (*refstr)+1, len - 1);
		key[unescapedlen] = '\0';
		(*refstr) += len + 1;
	} else
		return NULL;

	/* Skip trailing whitespace */
	json_cmd_parse_whitespace(refstr);

	/* Return the key */
	return key;
}

/* Parse a parenthesized expression, possibly with some other syntax elements
 * mixed in.  This is smart enough to handle nested parentheses, and
 * parentheses in strings.  Returns the contents of the parentheses (without
 * the parentheses themselves) as a dynamically-allocated string, or NULL if
 * not a valid parenthesized expression.
 */
char *json_cmd_parse_paren(char **refstr)
{
	int	nest;
	char	quote;
	char	*scan;
	size_t	len;
	char	*paren;

	/* Skip leading whitespace */
	json_cmd_parse_whitespace(refstr);

	/* If not a parentheses, fail */
	if (**refstr != '(')
		return NULL;

	/* Find the extent of the parenthesized expression */
	for (scan = (*refstr) + 1, nest = 1, quote = '\0'; nest > 0; scan++) {
		if (!*scan)
			return NULL; /* hit end of input without ')' */
		else if (*scan == '\\' && (quote == '"' || quote == '\'') && scan[1])
			scan++;
		else if (*scan == quote)
			quote = '\0';
		else if (!quote && (*scan == '`' || *scan == '"' || *scan == '\''))
			quote = *scan;
		else if (!quote && *scan == '(')
			nest++;
		else if (!quote && *scan == ')')
			nest--;
	}

	/* Copy it into a dynamic string */
	len = (size_t)(scan - *refstr) - 2;
	paren = (char *)malloc(len + 1);
	strncpy(paren, *refstr + 1, len);
	paren[len] = '\0';
	*refstr = scan + 1;

	/* Skip trailing whitespace */
	json_cmd_parse_whitespace(refstr);

	/* Return the contents of the parentheses */
	return paren;
}

/* Allocate a statement, and initialize it */
jsoncmd_t *json_cmd(char *where, jsoncmdname_t *name)
{
	jsoncmd_t *cmd = (jsoncmd_t *)malloc(sizeof(jsoncmd_t));
	memset(cmd, 0, sizeof(jsoncmd_t));
	cmd->where = where;
	cmd->name = name;
	return cmd;
}

/* Free a statement, and any related statements or data */
void json_cmd_free(jsoncmd_t *cmd)
{
	/* Defend against NULL */
	if (!cmd)
		return;

	/* Free related data */
	if (cmd->key)
		free(cmd->key);
	if (cmd->calc)
		json_calc_free(cmd->calc);
	json_cmd_free(cmd->sub);
	json_cmd_free(cmd->more);
	json_cmd_free(cmd->next);

	/* Free the cmd itself */
	free(cmd);
}

/* Parse a single statement and return it.  If it can't be parsed, then issue
 * an error message and return NULL.  If it is a function definition, return
 * it instead of processing it immediately.
 */
jsoncmd_t *json_cmd_parse_single(char **refstr, jsonerror_t **referr)
{
	jsoncmdname_t	*sn;
	size_t 		len;
	jsoncalc_t	*calc;
	char		*where, *end, *err;
	jsoncmd_t	*cmd;

	/* Skip leading whitespace */
	json_cmd_parse_whitespace(refstr);
	where = *refstr;

	/* All statements begin with a command name, except for assignments
	 * and output expressions.  Start by comparing the start of this
	 * command to all known command names.
	 */
	for (sn = names; sn; sn = sn->next) {
		len = json_mbs_len(sn->name);
		end = (*refstr) + len;
		if (!json_mbs_ncasecmp(sn->name, *refstr, len)
		 && (isspace(*end) || !*end || *end == ';' || *end == '}'))
			break;
	}

	/* If it's a statement, use the statement's parser */
	if (sn) {
		(*refstr) += len;
		return sn->argparser(refstr, referr);
	}

	/* Hopefully it is an assignment or an output expression.  Parse it. */
	end = err = NULL;
	calc = json_calc_parse(*refstr, &end, &err);
	if (!calc || (*end && *end != ';' && *end != '}')) {
		if (calc)
			json_calc_free(calc);
		if (!err) {
			if (isalpha(*where)) {
				*refstr = where;
				err = json_cmd_parse_key(refstr, 0);
				*referr = json_cmd_error(end, 1, "Unknown command \"%s\"", err);
				free(err);
			} else
				*referr = json_cmd_error(end, 1, "expression syntax error");
		} else {
			*referr = json_cmd_error(end, 1, err);
		}
		return NULL;
	}

	/* Stuff it into a jsoncmd_t */
	cmd = json_cmd(*refstr, &jsn_calc);
	cmd->calc = calc;

	/* Move past the end of the statement */
	(*refstr) = end;
	if (**refstr == ';')
		(*refstr)++;

	/* Return it */
	return cmd;
}

/* Parse a statement block, and return it.  If can't be parsed, then issue an
 * error message and return NULL.  Function declarations are not allowed, and
 * should generate an error message.  An empty set of curly braces is allowed,
 * though, and should return a "NO OP" statement.
 */
jsoncmd_t *json_cmd_parse_curly(char **refstr, jsonerror_t **referr)
{
	jsoncmd_t *cmd, *current;

	/* Skip whitespace */
	json_cmd_parse_whitespace(refstr);

	/* Expect a '{'.  For anything else, assume it's a single statement. */
	if (**refstr == '{') {
		(*refstr)++;
		cmd = current = json_cmd_parse_single(refstr, referr);
		while (*referr == NULL && **refstr != '}') {
			current->next = json_cmd_parse_single(refstr, referr);
			if (current->next)
				current = current->next;
			if (*referr)
				break;
		}
		if (**refstr == '}')
			(*refstr)++;

	} else {
		cmd = json_cmd_parse_single(refstr, referr);
	}

	/* Skip trailing whitespace */
	json_cmd_parse_whitespace(refstr);

	/* Return it */
	return cmd;
}

/* Convert a character pointer to a line number.  "buf" is a buffer containing
 * the entire script, and "where" is a point within "buf".
 */
static int jsline(char *buf, char *where)
{
	int	line;

	for (line = 1; buf != where; buf++)
		if (*buf == '\n')
			line++;
	return line;
}

/* Parse a string for statements, and return them */
jsoncmd_t *json_cmd_parse_string(char *str)
{
	jsoncmd_t *cmd;
	jsonerror_t *err = NULL;

	cmd = json_cmd_parse_single(&str, &err);
	if (err)
		fprintf(stderr, "%s\n", err->text);
	return cmd;
}

/* Parse a file, and return any scripts from it. */
jsoncmd_t *json_cmd_parse_file(char *filename) 
{
	/* Load the file into memory */

	/* If first line starts with "#!" then skip to second line */

	/* For each statement... */

		/* If it is a function declaration, process it now, and
		 * DON'T add it to the list if commands for the script.
		 */

		/* If it is a variable declaration, process it now as a global
		 * declaration (since this is the top-level of the script).
		 * DON'T add them to the list of commands for the script.
		 */

		/* Anything else gets added to the statement chain */

	/* Clean up and return the remaining statements.  Might be NULL. */
}

/* Run a series of statements */
jsonerror_t *json_cmd_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	jsonerror_t *err = NULL;

	while (cmd && !err) {
		err = (*cmd->name->run)(cmd, refcontext);
		cmd = cmd->next;
	}
	return err;
}

/****************************************************************************/
/* Everything after this is for parsing and running built-in commands.      */
/****************************************************************************/

static jsoncmd_t *if_parse(char **refstr, jsonerror_t **referr)
{
	jsoncmd_t	*parsed;
	char	*str, *end, *err = NULL;
	char	*where;

	/* Skip leading whitespace */
	json_cmd_parse_whitespace(refstr);
	where = *refstr;

	/* Allocate the jsoncmd_t for it */
	parsed = json_cmd(where, &jsn_if);

	/* Get the condition */
	str = json_cmd_parse_paren(refstr);
	if (!str) {
		*referr = json_cmd_error(*refstr, 1, "Missing \"if\" condition");
		return parsed;
	}

	/* Parse the condition */
	parsed->calc = json_calc_parse(str, &end, &err);
	free(str);
	if (err || *end || !parsed->calc) {
		*referr = json_cmd_error(*refstr, 1, err ? err : "Syntax error in \"if\" condition");
		return parsed;
	}

	/* Get the "then" statements */
	parsed->sub = json_cmd_parse_curly(refstr, referr);
	if (*referr)
		return parsed;

	/* If followed by "else" then parse the "else" statements */
	if (!strncmp(*refstr, "else", 4) && !isalnum((*refstr)[4])) {
		(*refstr) += 4;
		parsed->more = json_cmd_parse_curly(refstr, referr);
	}

	/* Return it */
	return parsed;
}

static jsonerror_t *if_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	json_t *jsbool = json_calc(cmd->calc, *refcontext, NULL);
	int	bool = json_is_true(jsbool);
	json_free(jsbool);
	if (bool)
		return json_cmd_run(cmd->sub, refcontext);
	else
		return json_cmd_run(cmd->more, refcontext);
}

/* This is a helper function for global/local var/const declarations */
static jsoncmd_t *gvc_parse(char **refstr, jsonerror_t **referr, jsoncmd_t *cmd)
{
	jsoncmd_t *first = cmd;
	char	*end, *err;

	/* The "global" command name may be followed by "var" or "const" */
	if (cmd->name == &jsn_global) {
		if (!strncasecmp(*refstr, "var", 3) && isspace((*refstr)[3])) {
			cmd->flags = JSON_CONTEXT_GLOBAL | JSON_CONTEXT_VAR;
			(*refstr) += 3;
			json_cmd_parse_whitespace(refstr);
		} else if (!strncasecmp(*refstr, "const", 5) && isspace((*refstr)[5])) {
			cmd->flags = JSON_CONTEXT_GLOBAL | JSON_CONTEXT_CONST;
			(*refstr) += 5;
			json_cmd_parse_whitespace(refstr);
		}
	} else if (cmd->name == &jsn_var) {
		cmd->flags = JSON_CONTEXT_VAR;
	} else /* jsn_const */ {
		cmd->flags = JSON_CONTEXT_CONST;
	}

	/* After that, expect a name possibly followed by '=' and an expression */
	for (;;) {
		cmd->key = json_cmd_parse_key(refstr, 1);
		if (!cmd->key) {
			*referr = json_cmd_error(*refstr, 1, "Name expected after %s", cmd->name->name);
			json_cmd_free(first);
			return NULL;
		}
		if (**refstr == '=') {
			err = NULL;
			(*refstr)++;
			cmd->calc = json_calc_parse(*refstr, &end, &err);
			(*refstr) = end;
			if (err) {
				json_cmd_error(*refstr, 1, "Error in expression (%s)", err);
				json_cmd_free(first);
				return NULL;
			}
		}

		/* That may be followed by a comma and another declaration */
		if (**refstr == ',') {
			(*refstr)++;
			json_cmd_parse_whitespace(refstr);
			cmd->more = json_cmd(*refstr, first->name);
			cmd = cmd->more;
			cmd->flags = first->flags;
		}
		else
			break;

	}

	/* Probably followed by a ';' */
	if (**refstr == ';')
		(*refstr)++;

	return first;
}
static jsonerror_t *gvc_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	json_t	*value;

	/* A single statement can declare multiple vars/consts */
	while (cmd) {
		/* Evaluate the value */
		if (cmd->calc)
			value = json_calc(cmd->calc, *refcontext, NULL);
		else
			value = json_null();

		/* Add it to the context */
		if (!json_context_declare(refcontext, cmd->key, value, cmd->flags)) {
			/* Duplicate! */
			json_free(value);
			return json_cmd_error(cmd->where, 1, "Duplicate %s %s \"%s\"",
				(cmd->flags & JSON_CONTEXT_GLOBAL) ? "global" : "local",
				(cmd->flags & JSON_CONTEXT_CONST) ? "const" : "var",
				cmd->key);
		}

		/* Move to the next */
		cmd = cmd->more;
	}

	/* Success! */
	return NULL;
}

static jsoncmd_t *var_parse(char **refstr, jsonerror_t **referr)
{
	return gvc_parse(refstr, referr, json_cmd(*refstr, &jsn_var));
}

static jsonerror_t *var_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	return gvc_run(cmd, refcontext);
}

static jsoncmd_t *const_parse(char **refstr, jsonerror_t **referr)
{
	return gvc_parse(refstr, referr, json_cmd(*refstr, &jsn_const));
}

static jsonerror_t *const_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	return gvc_run(cmd, refcontext);
}

static jsoncmd_t *global_parse(char **refstr, jsonerror_t **referr)
{
	return gvc_parse(refstr, referr, json_cmd(*refstr, &jsn_global));
}

static jsonerror_t *global_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	return gvc_run(cmd, refcontext);
}

static jsoncmd_t *function_parse(char **refstr, jsonerror_t **referr)
{
	return  NULL;
}

static jsonerror_t *function_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	return NULL;
}

static jsoncmd_t *return_parse(char **refstr, jsonerror_t **referr)
{
	return  NULL;
}

static jsonerror_t *return_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	return NULL;
}

/* Handle an assignment or output expression */
static jsonerror_t *calc_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	/* Calculate the result of the expression.   If it's an assignment,
	 * then this will do the assignment too.
	 */
	json_t *result = json_calc(cmd->calc, *refcontext, NULL);

	/* If not an assignment, then it's an output.  Output it! */
	if (cmd->calc->op != JSONOP_ASSIGN && cmd->calc->op != JSONOP_APPEND)
		json_print(result, stdout, NULL);

	/* Either way, free the result */
	json_free(result);

	return NULL;
}
