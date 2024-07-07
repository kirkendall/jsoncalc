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
static jsoncmd_t   *if_parse(jsonsrc_t *src, jsonerror_t **referr);
static jsonerror_t *if_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t   *while_parse(jsonsrc_t *src, jsonerror_t **referr);
static jsonerror_t *while_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t   *for_parse(jsonsrc_t *src, jsonerror_t **referr);
static jsonerror_t *for_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t   *break_parse(jsonsrc_t *src, jsonerror_t **referr);
static jsonerror_t *break_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t   *try_parse(jsonsrc_t *src, jsonerror_t **referr);
static jsonerror_t *try_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t   *var_parse(jsonsrc_t *src, jsonerror_t **referr);
static jsonerror_t *var_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t   *const_parse(jsonsrc_t *src, jsonerror_t **referr);
static jsonerror_t *const_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t   *function_parse(jsonsrc_t *src, jsonerror_t **referr);
static jsonerror_t *function_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t   *return_parse(jsonsrc_t *src, jsonerror_t **referr);
static jsonerror_t *return_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
/* format */
/* delete */
/* help */
/* include */
static jsonerror_t *calc_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);

/* Linked list of command names */
static jsoncmdname_t jcn_if =       {NULL,		"if",		if_parse,	if_run};
static jsoncmdname_t jcn_while =    {&jcn_if,		"while",	while_parse,	while_run};
static jsoncmdname_t jcn_for =      {&jcn_while,	"for",		for_parse,	for_run};
static jsoncmdname_t jcn_break =    {&jcn_for,		"break",	break_parse,	break_run};
static jsoncmdname_t jcn_try =      {&jcn_break,	"try",		try_parse,	try_run};
static jsoncmdname_t jcn_var =      {&jcn_try,		"var",		var_parse,	var_run};
static jsoncmdname_t jcn_const =    {&jcn_var,		"const",	const_parse,	const_run};
static jsoncmdname_t jcn_function = {&jcn_const,	"function",	function_parse,	function_run};
static jsoncmdname_t jcn_return =   {&jcn_function,	"return",	return_parse,	return_run};
static jsoncmdname_t *names = &jcn_return;

/* A command name struct for assignment/output.  This isn't part of the "names"
 * list because assignment/output has no name -- you just give the expression.
 */
static jsoncmdname_t jcn_calc = {NULL, "<<calc>>", NULL, calc_run};


/* Convert a character pointer to a line number.  "buf" is a buffer containing
 * the entire script, and "where" is a point within "buf".
 */
static int jcmdline(jsonsrc_t *src)
{
	int	line;
	char	*scan;

	for (line = 1, scan = src->buf; scan != src->str; scan++)
		if (*scan == '\n')
			line++;
	return line;
}


/* Add a new statement name, and its argument parser and runner. */
void json_cmd_hook(char *pluginname, char *cmdname, jsoncmd_t *(*argparser)(jsonsrc_t *src, jsonerror_t **referr), jsonerror_t *(*run)(jsoncmd_t *cmd, jsoncontext_t **refcontext))
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
jsonerror_t *json_cmd_error(char *filename, int lineno, int code, char *fmt, ...)
{
	va_list	ap;
	size_t	size;
	jsonerror_t *err;

	/* !!!Translate the message via catalog using "code".  EXCEPT if the
	 * format is "%s" then assume it has already been translated.
	 */

	/* Figure out how long the message will be */
	va_start(ap, fmt);
	size = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	/* Allocate the error structure with enough space for the message */
	err = (jsonerror_t *)malloc(sizeof(jsonerror_t) + size);

	/* Fill the error structure */
	memset(err, 0, sizeof(jsonerror_t) + size);
	err->filename = filename;
	err->lineno = lineno;
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
void json_cmd_parse_whitespace(jsonsrc_t *src)
{
	do {
		/* Actual whitespace */
		while (isspace(*src->str))
			src->str++;

		/* Comments */
		if (src->str[0] == '/' && src->str[1] == '/') {
			while (*src->str && *src->str != '\n')
				src->str++;
		}
	} while (isspace(*src->str));
}

/* Parse a key (name) and return it as a dynamically-allocated string.
 * Advance src->str past the name and any trailing whitespace.  Returns NULL
 * if not a name.  The "quoteable" parameter should be 1 to allow strings
 * quoted with " or ' to be considered keys, or 0 to only allow alphanumeric
 * keys or `-quoted keys.
 */
char *json_cmd_parse_key(jsonsrc_t *src, int quotable)
{
	size_t	len, unescapedlen;
	char	*key;

	/* Skip leading whitespace */
	json_cmd_parse_whitespace(src);

	if (isalpha(*src->str) || *src->str == '_') {
		/* Unquoted alphanumeric name */
		for (len = 1; isalnum(src->str[len]) || src->str[len] == '_'; len++){
		}
		key = (char *)malloc(len + 1);
		strncpy(key, src->str, len);
		key[len] = '\0';
		src->str += len;
	} else if (*src->str == '`') {
		/* Backtick quoted name */
		src->str++;
		for (len = 0; src->str[len] && src->str[len] != '`'; len++){
		}
		key = (char *)malloc(len + 1);
		strncpy(key, src->str, len);
		key[len] = '\0';
		src->str += len + 1;
	} else if (quotable && (*src->str == '"' || *src->str == '\'')) {
		/* String, acting as a name */
		for (len = 1; src->str[len] && src->str[len] == *src->str; len++) {
			if (src->str[len] == '\\' && src->str[len + 1])
				len++;
		}
		unescapedlen = json_mbs_unescape(NULL, src->str+1, len - 1);
		key = (char *)malloc(unescapedlen + 1);
		json_mbs_unescape(key, src->str+1, len - 1);
		key[unescapedlen] = '\0';
		src->str += len + 1;
	} else
		return NULL;

	/* Skip trailing whitespace */
	json_cmd_parse_whitespace(src);

	/* Return the key */
	return key;
}

/* Parse a parenthesized expression, possibly with some other syntax elements
 * mixed in.  This is smart enough to handle nested parentheses, and
 * parentheses in strings.  Returns the contents of the parentheses (without
 * the parentheses themselves) as a dynamically-allocated string, or NULL if
 * not a valid parenthesized expression.
 */
char *json_cmd_parse_paren(jsonsrc_t *src)
{
	int	nest;
	char	quote;
	char	*scan;
	size_t	len;
	char	*paren;

	/* Skip leading whitespace */
	json_cmd_parse_whitespace(src);

	/* If not a parentheses, fail */
	if (*src->str != '(')
		return NULL;

	/* Find the extent of the parenthesized expression */
	for (scan = src->str + 1, nest = 1, quote = '\0'; nest > 0; scan++) {
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
	len = (size_t)(scan - src->str) - 2;
	paren = (char *)malloc(len + 1);
	strncpy(paren, src->str + 1, len);
	paren[len] = '\0';
	src->str = scan;

	/* Skip trailing whitespace */
	json_cmd_parse_whitespace(src);

	/* Return the contents of the parentheses */
	return paren;
}

/* Allocate a statement, and initialize it */
jsoncmd_t *json_cmd(jsonsrc_t *src, jsoncmdname_t *name)
{
	jsoncmd_t *cmd = (jsoncmd_t *)malloc(sizeof(jsoncmd_t));
	memset(cmd, 0, sizeof(jsoncmd_t));
	cmd->filename = src->filename;
	cmd->lineno = jcmdline(src);
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
jsoncmd_t *json_cmd_parse_single(jsonsrc_t *src, jsonerror_t **referr)
{
	jsoncmdname_t	*sn;
	size_t 		len;
	jsoncalc_t	*calc;
	char		*where, *end, *err;
	jsoncmd_t	*cmd;

	/* Skip leading whitespace */
	json_cmd_parse_whitespace(src);
	where = src->str;

	/* All statements begin with a command name, except for assignments
	 * and output expressions.  Start by comparing the start of this
	 * command to all known command names.
	 */
	for (sn = names; sn; sn = sn->next) {
		len = json_mbs_len(sn->name);
		end = src->str + len;
		if (!json_mbs_ncasecmp(sn->name, src->str, len)
		 && (isspace(*end) || !*end || *end == ';' || *end == '}'))
			break;
	}

	/* If it's a statement, use the statement's parser */
	if (sn) {
		src->str += len;
		return sn->argparser(src, referr);
	}

	/* Hopefully it is an assignment or an output expression.  Parse it. */
	end = err = NULL;
	calc = json_calc_parse(src->str, &end, &err);
	if (!calc || (*end && *end != ';' && *end != '}')) {
		if (calc)
			json_calc_free(calc);
		if (!err) {
			if (isalpha(*where)) {
				/* It started with a name.  Parse the name,
				 * and report it as an known command.
				 */
				src->str = where;
				err = json_cmd_parse_key(src, 0);
				*referr = json_cmd_error(src->filename, jcmdline(src), 1, "Unknown command \"%s\"", err);
				free(err);
			} else
				*referr = json_cmd_error(src->filename, jcmdline(src), 1, "expression syntax error");
		} else {
			*referr = json_cmd_error(src->filename, jcmdline(src), 1, err);
		}
		return NULL;
	}

	/* Stuff it into a jsoncmd_t */
	cmd = json_cmd(src, &jcn_calc);
	cmd->calc = calc;

	/* Move past the end of the statement */
	src->str = end;
	if (*src->str == ';')
		src->str++;

	/* Return it */
	return cmd;
}

/* Parse a statement block, and return it.  If can't be parsed, then issue an
 * error message and return NULL.  Function declarations are not allowed, and
 * should generate an error message.  An empty set of curly braces is allowed,
 * though, and should return a "NO OP" statement.
 */
jsoncmd_t *json_cmd_parse_curly(jsonsrc_t *src, jsonerror_t **referr)
{
	jsoncmd_t *cmd, *current;

	/* Skip whitespace */
	json_cmd_parse_whitespace(src);

	/* Expect a '{'.  For anything else, assume it's a single statement. */
	if (*src->str == '{') {
		src->str++;
		cmd = current = json_cmd_parse_single(src, referr);
		while (*referr == NULL && *src->str != '}') {
			current->next = json_cmd_parse_single(src, referr);
			json_cmd_parse_whitespace(src);
			if (current->next)
				current = current->next;
			if (*referr)
				break;
		}
		if (*src->str == '}')
			src->str++;

	} else {
		cmd = json_cmd_parse_single(src, referr);
	}

	/* Skip trailing whitespace */
	json_cmd_parse_whitespace(src);

	/* Return it */
	return cmd;
}

jsoncmd_t *json_cmd_parse(jsonsrc_t *src)
{
	jsonerror_t *err = NULL;
	jsoncmd_t *cmd, *first, *next;

	/* If first line starts with "#!" then skip to second line */
	if (src->str[0] == '#' && src->str[1] == '!') {
		while (*src->str && *src->str != '\n')
			src->str++;
	}

	/* For each statement... */
	json_cmd_parse_whitespace(src);
	first = cmd = NULL;
	while (src->str < src->buf + src->size && *src->str) {
		/* Find the line number of this command */
		json_cmd_parse_whitespace(src);

		/* Parse it */
		next = json_cmd_parse_single(src, &err);

		/* If error then report it and quit */
		if (err) {
			if (json_format_default.color)
				fputs(json_format_color_error, stderr);
			if (err->filename)
				fprintf(stderr, "%s:", err->filename);
			fprintf(stderr, "%d: %s\n", err->lineno, err->text);
			if (json_format_default.color)
				fputs(json_format_color_end, stderr);

			free(err);
			json_cmd_free(first);
			return NULL;
		}

		/* It could be NULL, which is *NOT* an error.  That would be
		 * for things like function definitions, which are processed
		 * by the parser and not at run-time.  Skip NULL */
		if (!next)
			continue;

		/* Anything else gets added to the statement chain */
		if (cmd)
			cmd->next = next;
		else
			first = next;
		cmd = next;

		/* Also, store the filename and line number of this command */
		cmd->filename = src->filename;
		cmd->lineno = jcmdline(src);
	}

	/* Return the commands.  Might be NULL. */
	return first;
}

/* Parse a string as jsoncalc commands.  If an error is detected then an
 * error message will be output and this will return NULL.  However, NULL
 * can also be returned if the text is empty, or only contains function
 * definitions, so NULL is *not* an error indication; it just means there's
 * nothing to execute or free.
 */
jsoncmd_t *json_cmd_parse_string(char *text)
{
	jsonsrc_t srcbuf;

	/* fill the src buffer */
	srcbuf.filename = NULL;
	srcbuf.buf = text;
	srcbuf.str = text;
	srcbuf.size = strlen(text);

	/* Parse it */
	return json_cmd_parse(&srcbuf);
}


/* Parse a file, and return any commands from it. If an error is detected
 * then an error message will be output and this will return NULL.  However,
 * NULL can also be returned if the file is empty, or only contains function
 * definitions, so NULL is *not* an error indication; it just means there's
 * nothing to execute or free.
 */
jsoncmd_t *json_cmd_parse_file(char *filename) 
{
	char	*buf;
	size_t	size, offset;
	ssize_t	nread;
	jsoncmd_t *cmd;
	jsonsrc_t srcbuf;

	/* Load the file into memory */
	FILE *fp = fopen(filename, "r");
	if (!fp) {
		perror(filename);
		return NULL;
	}
	size = 1024;
	buf = (char *)malloc(size);
	offset = 0;
	while ((nread = fread(buf + offset, 1, size - offset - 1, fp)) > 0) {
		offset += nread;
		if (offset + 1 >= size) {
			size *= 2;
			buf = (char *)realloc(buf, size);
		}
	}
	buf[offset] = '\0';
	fclose(fp);

	/* Fill in the srcbuf */
	srcbuf.filename = filename;
	srcbuf.buf = buf;
	srcbuf.str = buf;
	srcbuf.size = offset;

	/* Parse it */
	cmd = json_cmd_parse(&srcbuf);

	/* Free the buffer */
	free(buf);

	/* Return it */
	return cmd;
}

/* Run a series of statements.  Returns one of four things:
 *  NULL indicates no error or other exceptions, and execution should continue.
 *  err->ret=1 indicates "break" - skip execution to exit a loop then continue.
 *  err->ret=value indicates "return" - skip to end of function then use value.
 *  err->ret=NULL indicates an error - maybe try/catch it, maybe stop & report.
 */
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

static jsoncmd_t *if_parse(jsonsrc_t *src, jsonerror_t **referr)
{
	jsoncmd_t	*parsed;
	char	*str, *end, *err = NULL;

	/* Skip leading whitespace */
	json_cmd_parse_whitespace(src);

	/* Allocate the jsoncmd_t for it */
	parsed = json_cmd(src, &jcn_if);

	/* Get the condition */
	str = json_cmd_parse_paren(src);
	if (!str) {
		*referr = json_cmd_error(src->filename, jcmdline(src), 1, "Missing \"if\" condition");
		return parsed;
	}

	/* Parse the condition */
	parsed->calc = json_calc_parse(str, &end, &err);
	free(str);
	if (err || *end || !parsed->calc) {
		*referr = json_cmd_error(src->filename, jcmdline(src), 1, err ? err : "Syntax error in \"if\" condition");
		return parsed;
	}

	/* Get the "then" statements */
	parsed->sub = json_cmd_parse_curly(src, referr);
	if (*referr)
		return parsed;

	/* If followed by "else" then parse the "else" statements */
	if (!strncmp(src->str, "else", 4) && !isalnum((src->str)[4])) {
		src->str += 4;
		parsed->more = json_cmd_parse_curly(src, referr);
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

static jsoncmd_t *while_parse(jsonsrc_t *src, jsonerror_t **referr)
{
	jsoncmd_t	*parsed;
	char	*str, *end, *err = NULL;

	/* Skip leading whitespace */
	json_cmd_parse_whitespace(src);

	/* Allocate the jsoncmd_t for it */
	parsed = json_cmd(src, &jcn_while);

	/* Get the condition */
	str = json_cmd_parse_paren(src);
	if (!str) {
		*referr = json_cmd_error(src->filename, jcmdline(src), 1, "Missing \"while\" condition");
		return parsed;
	}

	/* Parse the condition */
	parsed->calc = json_calc_parse(str, &end, &err);
	free(str);
	if (err || *end || !parsed->calc) {
		*referr = json_cmd_error(src->filename, jcmdline(src), 1, err ? err : "Syntax error in \"while\" condition");
		return parsed;
	}

	/* Get the "loop" statements */
	parsed->sub = json_cmd_parse_curly(src, referr);
	if (*referr)
		return parsed;

	/* Return it */
	return parsed;
}

static jsonerror_t *while_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	for (;;) {
		/* Evaluate the condition */
		json_t *jsbool = json_calc(cmd->calc, *refcontext, NULL);
		int	bool = json_is_true(jsbool);
		json_free(jsbool);

		/* If the condition is false, then terminate the loop */
		if (!bool)
			return NULL;

		/* Run the loop body.  If it has an error, then return the
		 * error; otherwise continue to loop.
		 */
		jsonerror_t *err = json_cmd_run(cmd->sub, refcontext);
		if (err) {
			/* If we got a "break" pseudo-error, ignore it.
			 * Otherwise (real error or "return" pseudo-error)
			 * return it.
			 */
			if (err && err->ret == (json_t *)1) {
				free(err);
				err = NULL;
			}
			return err;
		}
	}
}

static jsoncmd_t *for_parse(jsonsrc_t *src, jsonerror_t **referr)
{
	jsoncmd_t	*parsed;
	char	*str = NULL, *end, *err = NULL;
	jsonsrc_t	parensrc;

	/* Skip leading whitespace */
	json_cmd_parse_whitespace(src);

	/* Allocate the jsoncmd_t for it */
	parsed = json_cmd(src, &jcn_for);

	/* Get the loop attributes */
	str = json_cmd_parse_paren(src);
	if (!str) {
		*referr = json_cmd_error(src->filename, jcmdline(src), 1, "Missing \"for\" attributes");
		goto CleanUpAfterError;
	}

	/* Parse the attributes: (var key of expr), (key of expr), or (expr) */
	parensrc.str = str;
	if (!strncasecmp(parensrc.str, "var", 3) && isspace(parensrc.str[3])) {
		parsed->var = 1;
		parensrc.str += 3;
		json_cmd_parse_whitespace(&parensrc);
	}
	parsed->key = json_cmd_parse_key(&parensrc, 1);
	if (parsed->key && parensrc.str[0] == '=') {
		parensrc.str++;
		json_cmd_parse_whitespace(&parensrc);
	} else if (parsed->key && !strncasecmp(parensrc.str, "of", 2) && !isalnum(parensrc.str[2]) && parensrc.str[2] != '_') {
		parensrc.str += 2;
		json_cmd_parse_whitespace(&parensrc);
	} else {
		/* If we parsed a key, it wasn't part of "key =/of expr",
		 * it was the first word of "expr".  Clean up!
		 */
		if (parsed->key) {
			free(parsed->key);
			parsed->key = NULL;
		}
		parsed->var = 0;
		parensrc.str = str;
	}
	parsed->calc = json_calc_parse(parensrc.str, &end, &err);
	if (err || *end || !parsed->calc) {
		*referr = json_cmd_error(src->filename, jcmdline(src), 1, err ? err : "Syntax error in \"for\" expression");
		goto CleanUpAfterError;
	}

	/* Get the "loop" statements */
	parsed->sub = json_cmd_parse_curly(src, referr);
	if (*referr)
		goto CleanUpAfterError;

	/* Return it */
	if (str)
		free(str);
	return parsed;

CleanUpAfterError:
	if (str)
		free(str);
	json_cmd_free(parsed);
	return NULL;
}

static jsonerror_t *for_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	json_t	*array, *scan;
	jsoncontext_t *layer;
	jsonerror_t *err = NULL;

	/* Evaluate the for-loop's array expression */
	array = json_calc(cmd->calc, *refcontext, NULL);
	if (!array || array->type != JSON_ARRAY) {
		if (array->type == JSON_NULL && array->text[0])
			return json_cmd_error(cmd->filename, cmd->lineno, (int)(long)array->first, "%s", array->text);
		return json_cmd_error(cmd->filename, cmd->lineno, 1, "\"for\" expression is not an array");
	}

	/* Without "var", look for an existing variable to use for the loop. */
	if (!cmd->var && cmd->key && json_context_by_key(*refcontext, cmd->key, &layer) != NULL) {
		/* Make sure the variable isn't a "const" */
		if (layer->flags & JSON_CONTEXT_CONST) {
			json_free(array);
			return json_cmd_error(cmd->filename, cmd->lineno, 1, "\"for\" variable \"%s\" is a const", cmd->key);
		}

		/* Okay, we have an existing variable! */
		for (scan = array->first; scan; scan = scan->next) {
			/* Store the value in the variable */
			json_append(layer->data, json_key(cmd->key, json_copy(scan)));

			/* Execute the body of the loop */
			err = json_cmd_run(cmd->sub, refcontext);
			if (err)
				break;
		}
	} else if (cmd->key) {
		/* Add a context for store the variable */
		layer = json_context(*refcontext, json_object(), 0);

		/* Loop over the elements */
		for (scan = array->first; scan; scan = scan->next) {
			/* Store the value in the variable */
			json_append(layer->data, json_key(cmd->key, json_copy(scan)));

			/* Execute the body of the loop */
			err = json_cmd_run(cmd->sub, &layer);
			if (err)
				break;
		}

		/* Clean up */
		json_free(layer->data);
		json_context_free(layer, 0);

	} else { /* anonymous loop */
		/* Loop over the elements */
		for (scan = array->first; scan; scan = scan->next) {
			/* Add a "this" layer */
			layer = json_context(*refcontext, scan, JSON_CONTEXT_THIS);

			/* Run the body of the loop */
			err = json_cmd_run(cmd->sub, &layer);
			if (err)
				break;

			/* Remove the "this" layer */
			json_context_free(layer, 0);
		}
	}

	/* Free the array */
	json_free(array);

	/* If we got a "break" pseudo-error, ignore it.  Otherwise (real error
	 * or "return" pseudo-error) return it.
	 */
	if (err && err->ret == (json_t *)1) {
		free(err);
		err = NULL;
	}
	return err;
}

static jsoncmd_t *try_parse(jsonsrc_t *src, jsonerror_t **referr)
{
	return NULL;
}

static jsonerror_t *try_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	return NULL;
}


/* This is a helper function for global/local var/const declarations */
static jsoncmd_t *gvc_parse(jsonsrc_t *src, jsonerror_t **referr, jsoncmd_t *cmd)
{
	jsoncmd_t *first = cmd;
	char	*end, *err;

	/* Expect a name possibly followed by '=' and an expression */
	for (;;) {
		cmd->key = json_cmd_parse_key(src, 1);
		if (!cmd->key) {
			*referr = json_cmd_error(src->filename, jcmdline(src), 1, "Name expected after %s", cmd->name->name);
			json_cmd_free(first);
			return NULL;
		}
		if (*src->str == '=') {
			err = NULL;
			src->str++;
			cmd->calc = json_calc_parse(src->str, &end, &err);
			src->str = end;
			if (err) {
				json_cmd_error(src->filename, jcmdline(src), 1, "Error in expression (%s)", err);
				json_cmd_free(first);
				return NULL;
			}
		}

		/* That may be followed by a comma and another declaration */
		if (*src->str == ',') {
			src->str++;
			json_cmd_parse_whitespace(src);
			cmd->more = json_cmd(src, first->name);
			cmd = cmd->more;
			cmd->flags = first->flags;
		}
		else
			break;

	}

	/* Probably followed by a ';' */
	if (*src->str == ';')
		src->str++;

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
			return json_cmd_error(cmd->filename, cmd->lineno, 1, "Duplicate %s %s \"%s\"",
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

static jsoncmd_t *break_parse(jsonsrc_t *src, jsonerror_t **referr)
{
	jsoncmd_t *cmd = json_cmd(src, &jcn_break);

	/* No arguments or other components, but we still need to skip ";" */
	json_cmd_parse_whitespace(src);
	if (*src->str == ';')
		src->str++;
	return cmd;
}

static jsonerror_t *break_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	/* Return a 'break" pseudo-error */
	jsonerror_t *err = json_cmd_error(cmd->filename, cmd->lineno, 0, "");
	err->ret = (json_t *)1;
	return err;
}

static jsoncmd_t *var_parse(jsonsrc_t *src, jsonerror_t **referr)
{
	return gvc_parse(src, referr, json_cmd(src, &jcn_var));
}

static jsonerror_t *var_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	return gvc_run(cmd, refcontext);
}

static jsoncmd_t *const_parse(jsonsrc_t *src, jsonerror_t **referr)
{
	return gvc_parse(src, referr, json_cmd(src, &jcn_const));
}

static jsonerror_t *const_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	return gvc_run(cmd, refcontext);
}

static jsoncmd_t *function_parse(jsonsrc_t *src, jsonerror_t **referr)
{
	/* function name */
	/* arguments */
	/* body */
	/* define it! */

	/* We're done.  Nothing more will be required at runtime */
	return  NULL;
}

static jsonerror_t *function_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	/* Can't happen */
	return NULL;
}

static jsoncmd_t *return_parse(jsonsrc_t *src, jsonerror_t **referr)
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

	/* If we got an error ("null" with text), then convert to jsonerror_t */
	if (result && result->type == JSON_NULL && *result->text) {
		jsonerror_t *err = json_cmd_error(cmd->filename, cmd->lineno, (int)(long)result->first, "%s", result->text);
		json_free(result);
		return err;
	}

	/* If not an assignment, then it's an output.  Output it! */
	if (cmd->calc->op != JSONOP_ASSIGN && cmd->calc->op != JSONOP_APPEND)
		json_print(result, NULL);

	/* Either way, free the result */
	json_free(result);

	return NULL;
}
