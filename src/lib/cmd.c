#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <regex.h>
#include <assert.h>
#include <jsoncalc.h>

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

/* This array doesn't actually store anything; it just provides a distinct
 * value that can be used to recognize when json_cmd_parse() and
 * json_cmd_parse_string() detect an error.
 */
jsoncmd_t JSON_CMD_ERROR[1];

/* Forward declarations for functions that implement the built-in commands */
static jsoncmd_t    *if_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *if_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *while_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *while_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *for_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *for_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *break_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *break_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *continue_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *continue_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *switch_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *switch_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *case_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *case_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *default_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *default_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *try_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *try_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *throw_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *throw_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *var_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *var_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *const_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *const_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *function_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *function_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *return_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *return_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *void_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *void_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *explain_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *explain_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *file_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *file_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *import_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *import_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *plugin_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *plugin_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *print_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *print_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
static jsoncmd_t    *set_parse(jsonsrc_t *src, jsoncmdout_t **referr);
static jsoncmdout_t *set_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
/* delete lvalue */
/* throw [code],msg[,args] */
/* help topic subtopic */
static jsoncmdout_t *calc_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);

/* Linked list of command names */
static jsoncmdname_t jcn_if =       {NULL,		"if",		if_parse,	if_run};
static jsoncmdname_t jcn_while =    {&jcn_if,		"while",	while_parse,	while_run};
static jsoncmdname_t jcn_for =      {&jcn_while,	"for",		for_parse,	for_run};
static jsoncmdname_t jcn_break =    {&jcn_for,		"break",	break_parse,	break_run};
static jsoncmdname_t jcn_continue = {&jcn_break,	"continue",	continue_parse,	continue_run};
static jsoncmdname_t jcn_switch =   {&jcn_continue,	"switch",	switch_parse,	switch_run};
static jsoncmdname_t jcn_case =     {&jcn_switch,	"case",		case_parse,	case_run};
static jsoncmdname_t jcn_default =  {&jcn_case,		"default",	default_parse,	default_run};
static jsoncmdname_t jcn_try =      {&jcn_default,	"try",		try_parse,	try_run};
static jsoncmdname_t jcn_throw =    {&jcn_try,		"throw",	throw_parse,	throw_run};
static jsoncmdname_t jcn_var =      {&jcn_throw,	"var",		var_parse,	var_run};
static jsoncmdname_t jcn_const =    {&jcn_var,		"const",	const_parse,	const_run};
static jsoncmdname_t jcn_function = {&jcn_const,	"function",	function_parse,	function_run};
static jsoncmdname_t jcn_return =   {&jcn_function,	"return",	return_parse,	return_run};
static jsoncmdname_t jcn_void =     {&jcn_return,	"void",		void_parse,	void_run};
static jsoncmdname_t jcn_explain =  {&jcn_void,		"explain",	explain_parse,	explain_run};
static jsoncmdname_t jcn_file =     {&jcn_explain,	"file",		file_parse,	file_run};
static jsoncmdname_t jcn_import =   {&jcn_file,		"import",	import_parse,	import_run};
static jsoncmdname_t jcn_plugin =   {&jcn_import,	"plugin",	plugin_parse,	plugin_run};
static jsoncmdname_t jcn_print =    {&jcn_plugin,	"print",	print_parse,	print_run};
static jsoncmdname_t jcn_set =	    {&jcn_print,	"set",		set_parse,	set_run};
static jsoncmdname_t *names = &jcn_set;

/* A command name struct for assignment/output.  This isn't part of the "names"
 * list because assignment/output has no name -- you just give the expression.
 */
static jsoncmdname_t jcn_calc = {NULL, "<<calc>>", NULL, calc_run};

/* These are used to indicate special results from a series of commands.
 * Their values are irrelevant; their unique addresses are what matters.
 */
json_t json_cmd_break;		/* "break" statement */
json_t json_cmd_continue;	/* "continue" statement */
json_t json_cmd_case_mismatch;	/* "case" that doesn't match switchcase */

/* Convert a character pointer to a line number.  "buf" is a buffer containing
 * the entire script, and "where" is a point within "buf".
 */
int json_cmd_lineno(jsonsrc_t *src)
{
	int	line;
	char	*scan;

	for (line = 1, scan = src->buf; scan != src->str; scan++)
		if (*scan == '\n')
			line++;
	return line;
}


/* Add a new statement name, and its argument parser and runner. */
void json_cmd_hook(char *pluginname, char *cmdname, jsoncmd_t *(*argparser)(jsonsrc_t *src, jsoncmdout_t **referr), jsoncmdout_t *(*run)(jsoncmd_t *cmd, jsoncontext_t **refcontext))
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
jsoncmdout_t *json_cmd_error(const char *filename, int lineno, int code, const char *fmt, ...)
{
	va_list	ap;
	size_t	size;
	jsoncmdout_t *result;

	/* !!!Translate the message via catalog using "code".  EXCEPT if the
	 * format is "%s" then assume it has already been translated.
	 */

	/* Figure out how long the message will be */
	va_start(ap, fmt);
	size = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	/* Allocate the error structure with enough space for the message */
	result = (jsoncmdout_t *)malloc(sizeof(jsoncmdout_t) + size);

	/* Fill the error structure */
	memset(result, 0, sizeof(jsoncmdout_t) + size);
	result->filename = filename;
	result->lineno = lineno;
	result->code = code;
	va_start(ap, fmt);
	vsnprintf(result->text, size + 1, fmt, ap);
	va_end(ap);

	/* Return it */
	return result;
}

/* Generate an error message */
jsoncmdout_t *json_cmd_src_error(jsonsrc_t *src, int code, char *fmt, ...)
{
	va_list	ap;
	size_t	size;
	jsoncmdout_t *result;

	/* !!!Translate the message via catalog using "code".  EXCEPT if the
	 * format is "%s" then assume it has already been translated.
	 */

	/* Figure out how long the message will be */
	va_start(ap, fmt);
	size = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	/* Allocate the error structure with enough space for the message */
	result = (jsoncmdout_t *)malloc(sizeof(jsoncmdout_t) + size);

	/* Fill the error structure */
	memset(result, 0, sizeof(jsoncmdout_t) + size);
	result->filename = src->filename;
	result->lineno = json_cmd_lineno(src);
	result->code = code;
	va_start(ap, fmt);
	vsnprintf(result->text, size + 1, fmt, ap);
	va_end(ap);

	/* Return it */
	return result;
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

/* Skip past whitespace, comments, and an optional type declaration */
void json_cmd_parse_whitespace_or_type(jsonsrc_t *src, char **refstr)
{
	int	nest;
	int	quote;
	int	afterop;
	char	*start;
	int	len;

	/* Skip whitespace and some comments */
	json_cmd_parse_whitespace(src);

	/* If no "?:" or ":" then no type.  We're done. */
	if (*src->str == '?')
		src->str++;
	if (*src->str != ':') {
		if (refstr)
			*refstr = NULL;
		return;
	}

	/* Skip past the ":" */
	src->str++;
	start = src->str;

	/* Skip past the type.  Types consist of names, literals (including
	 * quoted strings), "|" operators, curly braces, square brackets, and
	 * maybe commas within the  brackets/braces.
	 */
	nest = quote = 0;
	afterop = 1;
	for (; *src->str; src->str++) {
		/* Newline ends comments */
		if (quote == '/' && *src->str == '\n')
			quote = '\0';

		/* Letters/numbers always allowed, and reset afterop */
		if (isalnum(*src->str)) {
			afterop = 0;
			continue;
		}

		/* Whitespace always allowed */
		if (isspace(*src->str))
			continue;

		/* A few other characters have special meaning */
		switch (*src->str) {
		case '/': /* Comment, if doubled and not in quoted string */
			if (quote)
				continue;
			if (src->str[1] == '/') {
				quote = '/';
				src->str++;
				afterop = 0;
				continue;
			}
			break;

		case '"':
		case '\'':
		case '`': /* Start of quote, unless we're already quoting */
			if (!quote)
				quote = *src->str;
			else if (quote == *src->str)
				quote = 0;
			afterop = 0;
			continue;

		case '\\': /* Escape within a quoted string */
			if (quote == '"' || quote == '\'' ) {
				if (src->str[1])
					src->str++;
				continue;
			}
			break;

		case '-':
		case '+':
		case '.': /* Parts of numbers, always allowed */
			continue;

		case '|': /* | operator is allowed */
			if (nest == 0)
				afterop = 1;
			continue;

		case '{':
		case '[': /* Start a brace/bracket, but only after operator */
			if (quote)
				continue;
			if (!afterop)
				break;
			nest++;
			continue;

		case '}':
		case ']': /* end a brace/bracket */
			if (!quote && nest > 0)
				nest--;
			continue;

		case ',': /* comma only allowed within brace/bracket */
			if (nest > 0 || quote)
				continue;
			break;

		default:
			/* Other chars only allowed in strings */
			if (quote)
				continue;
		}

		/* If we get here, then we hit the end of the type */
		break;
	}

	/* If we have a refstr, then store a copy of the text there, with the
	 * colon and trailing whitespace removed.
	 */
	if (refstr) {
		for (len = src->str - start; len > 0 && isspace(start[len - 1]); len--) {
		}
		if (len <= 0)
			*refstr = NULL;
		else {
			*refstr = (char *)malloc(len + 1);
			strncpy(*refstr, start, len);
			(*refstr)[len] = '\0';
		}
	}
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
			return NULL; /* Hit end of input without ')' */
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
	cmd->lineno = json_cmd_lineno(src);
	cmd->name = name;
	return cmd;
}

/* Free a statement, and any related statements or data */
void json_cmd_free(jsoncmd_t *cmd)
{
	/* Defend against NULL and JSON_CMD_ERROR */
	if (!cmd || cmd == JSON_CMD_ERROR)
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
jsoncmd_t *json_cmd_parse_single(jsonsrc_t *src, jsoncmdout_t **referr)
{
	jsoncmdname_t	*sn;
	size_t 		len;
	jsoncalc_t	*calc;
	char		*where, *end, *err;
	jsoncmd_t	*cmd;

	/* Skip leading whitespace */
	json_cmd_parse_whitespace(src);
	where = src->str;

	/* If it's an empty command, then return NULL */
	if (*src->str == ';') {
		src->str++;
		return NULL;
	} else if (*src->str == '}')
		return NULL;

	/* All statements begin with a command name, except for assignments
	 * and output expressions.  Start by comparing the start of this
	 * command to all known command names.
	 */
	for (sn = names; sn; sn = sn->next) {
		len = strlen(sn->name);
		end = src->str + len;
		if (!json_mbs_ncasecmp(sn->name, src->str, len)
		 && (!isalnum(*end) && *end != '_'))
			break;
	}

	/* If followed immediately by a "(" then check to see if its a function.
	 * Sometimes functions and commands have the same name, and this helps
	 * us keep them separate.  If it looks like a function call then ignore
	 * the command.
	 */
	if (sn && *end == '(' && json_calc_function_by_name(sn->name))
		sn = NULL;

	/* If it's a statement, use the statement's parser */
	if (sn) {
		src->str += len;
		return sn->argparser(src, referr);
	}

	/* Hopefully it is an assignment or an output expression.  Parse it. */
	end = err = NULL;
	calc = json_calc_parse(src->str, &end, &err, 1);
	if (!calc || err || (*end && *end != ';' && *end != '}')) {
		if (calc)
			json_calc_free(calc);
		if (!err) {
			if (isalpha(*where)) {
				/* It started with a name.  Parse the name,
				 * and report it as an unknown command.
				 */
				src->str = where;
				err = json_cmd_parse_key(src, 0);
			}

			/* If no name, or a function name, then assume we got
			 * an expression error.  (We'd like to check vars and
			 * consts, but we don't have a context yet.) Otherwise,
			 * treat it as an unknown command.
			 */
			if (!err || json_calc_function_by_name(err))
				*referr = json_cmd_src_error(src, 1, "Expression syntax error");
			else
				*referr = json_cmd_src_error(src, 1, "Unknown command \"%s\"", err);
			if (err)
				free(err);
		} else {
			*referr = json_cmd_src_error(src, 1, err);
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
	json_cmd_parse_whitespace(src);

	/* Return it */
	return cmd;
}

/* Parse a statement block, and return it.  If can't be parsed, then store an
 * error message at *referr and return NULL.  Function declarations are not
 * allowed, and should generate an error message.  An empty set of curly braces
 * is allowed, though, and should return a "NO OP" statement.
 */
jsoncmd_t *json_cmd_parse_curly(jsonsrc_t *src, jsoncmdout_t **referr)
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
	jsoncmdout_t *result = NULL;
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
		/* Parse it */
		next = json_cmd_parse_single(src, &result);

		/* If error then report it and quit */
		if (result) {
			if (json_format_default.color)
				fputs(json_format_default.escerror, stderr);
			if (result->filename)
				fprintf(stderr, "%s:", result->filename);
			fprintf(stderr, "%d: %s\n", result->lineno, result->text);
			if (json_format_default.color)
				fputs(json_format_color_end, stderr);

			free(result);
			json_cmd_free(first);
			return JSON_CMD_ERROR;
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
		cmd->lineno = json_cmd_lineno(src);

		/* Skip whitespace */
		json_cmd_parse_whitespace(src);
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

	/* Fill the src buffer */
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
jsoncmd_t *json_cmd_parse_file(const char *filename) 
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

/* Run a series of statements, and return the result */
jsoncmdout_t *json_cmd_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	jsoncmdout_t *result = NULL;

	while (cmd && !result) {
		assert(cmd != JSON_CMD_ERROR);

		/* Maybe output trace info */
		if (json_debug_flags.trace) {
			if (cmd->key)
				fprintf(stderr, "%s%s:%d: %s %s%s\n", json_format_default.escdebug, cmd->filename, cmd->lineno, cmd->name->name, cmd->key, json_format_color_end);
			else
				fprintf(stderr, "%s%s:%d: %s%s\n", json_format_default.escdebug, cmd->filename, cmd->lineno, cmd->name->name, json_format_color_end);
		}

		/* Run the command */
		result = (*cmd->name->run)(cmd, refcontext);

		/* If mismatched "case", then skip ahead to the next case */
		if (result && result->ret == &json_cmd_case_mismatch) {
			/* We're handling this result here.  Free it */
			free(result);
			result = NULL;

			/* Skip to the next "case" or "default" statement */
			while ((cmd = cmd->next) != NULL
			    && cmd->name != &jcn_case 
			    && cmd->name != &jcn_default) {
			}
		} else {
			/* For NULL, just go to the next command.  If it's
			 * some other value, such as a "return", then we'll
			 * exit the loop so changing "cmd" here is harmless.
			 */
			cmd = cmd->next;
		}
	}
	return result;
}

/* Invoke a user-defined function, and return its value */
json_t *json_cmd_fncall(json_t *args, jsonfunc_t *fn, jsoncontext_t *context)
{
	jsoncmdout_t *result;
	json_t	*out;

	assert(fn->user);

	/* Add the call frame to the context stack */
	context = json_context_func(context, fn, args);

	/* Run the body of the function */
	result = json_cmd_run(fn->user, &context);

	/* Decode the "result" response */
	if (!result) /* Function terminated without "return" -- use null */
		out = json_null();
	else if (!result->ret) {
#if 0
		/* Error - convert to error null, but save the filename/lineno
		 * on the context stack, so we can give better error feedback
		 * to the user.
		 */
		context->older->err.filename = result->filename;
		context->older->err.lineno = result->filename;
#endif
		out = json_error_null(result->code, "%s", result->text);
	} else if (result->ret == &json_cmd_break) /* "break" */
		out = json_error_null(1, "Misuse of \"break\"");
	else if (result->ret == &json_cmd_continue) /* "continue" */
		out = json_error_null(1, "Misuse of \"continue\"");
	else /* "return" */
		out = result->ret;

	/* Free "result" but not "result->ret" */
	if (result)
		free(result);

	/* Clean up the context, possibly including local vars and consts */
	while ((context->flags & JSON_CONTEXT_ARGS) == 0)
		context = json_context_free(context);
	context = json_context_free(context);

	/* Return the result */
	return out;

}

/* Append any commands from "added" to the end of "existing".  Either of those
 * can be NULL to represent an empty list.  If context is non-NULL then
 * evaluate any "var" or "const" commands instead of appending them.
 * Either way, the commands from "added" are no longer valid when this
 * function returns; you don't need to store it or free it.
 */
jsoncmd_t *json_cmd_append(jsoncmd_t *existing, jsoncmd_t *added, jsoncontext_t *context)
{
	jsoncmd_t *next, *end;
#if 0
	jsoncmdout_t *result;
#endif

	/* If "existing" is JSON_CMD_ERROR then just return it unchanged. */
	if (existing == JSON_CMD_ERROR)
		return existing;

	/* If "added" is NULL, do nothing */
	if (!added)
		return existing;

	/* If "added" is JSON_CMD_ERROR then free the "existing" list (if any)
	 * and return JSON_CMD_ERROR.
	 */
	if (added == JSON_CMD_ERROR) {
		json_cmd_free(existing);
		return JSON_CMD_ERROR;
	}

	/* If "existing" is non-NULL then move to the end of the list */
	if (existing) {
		end = existing;
		while (end->next)
			end = end->next;
	}

	/* For each command from "added"... */
	for (; added; added = next) {
		next = added->next;
		added->next = NULL;

#if 0
		/* Maybe execute "const" and "var" now */
		if (context && (added->name->run == var_run || added->name->run == const_run)) {
			result = json_cmd_run(added, &context);
			free(result);
			json_cmd_free(added);
			continue;
		}
#endif

		/* Append this command to "existing" */
		if (existing)
			end->next = added;
		else
			existing = added;
		end = added;
	}

	/* Return the combined list */
	return existing;
}



/****************************************************************************/
/* Everything after this is for parsing and running built-in commands.      */
/****************************************************************************/

static jsoncmd_t *if_parse(jsonsrc_t *src, jsoncmdout_t **referr)
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
		*referr = json_cmd_src_error(src, 1, "Missing \"if\" condition");
		return parsed;
	}

	/* Parse the condition */
	parsed->calc = json_calc_parse(str, &end, &err, 0);
	if (err || *end || !parsed->calc) {
		free(str);
		*referr = json_cmd_src_error(src, 1, err ? err : "Syntax error in \"if\" condition");
		return parsed;
	}
	free(str);

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

static jsoncmdout_t *if_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	json_t *jsbool = json_calc(cmd->calc, *refcontext, NULL);
	int	bool = json_is_true(jsbool);
	json_free(jsbool);
	if (bool)
		return json_cmd_run(cmd->sub, refcontext);
	else
		return json_cmd_run(cmd->more, refcontext);
}

static jsoncmd_t *while_parse(jsonsrc_t *src, jsoncmdout_t **referr)
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
		*referr = json_cmd_src_error(src, 1, "Missing \"while\" condition");
		return parsed;
	}

	/* Parse the condition */
	parsed->calc = json_calc_parse(str, &end, &err, 0);
	if (err || *end || !parsed->calc) {
		free(str);
		*referr = json_cmd_src_error(src, 1, err ? err : "Syntax error in \"while\" condition");
		return parsed;
	}
	free(str);

	/* Get the "loop" statements */
	parsed->sub = json_cmd_parse_curly(src, referr);
	if (*referr)
		return parsed;

	/* Return it */
	return parsed;
}

static jsoncmdout_t *while_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
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
		jsoncmdout_t *result = json_cmd_run(cmd->sub, refcontext);

		/* If we got a "continue" then ignore it and stay in the loop */
		if (result && result->ret == &json_cmd_continue) {
			free(result);
			result = NULL;
		}

		/* If "breaK', "return", or error then exit the loop */
		if (result) {
			/* If we got a "break", ignore it.  Otherwise ("return"
			 * or an error) return it.
			 */
			if (result && result->ret == &json_cmd_break) {
				free(result);
				result = NULL;
			}
			return result;
		}
	}
}

static jsoncmd_t *for_parse(jsonsrc_t *src, jsoncmdout_t **referr)
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
		*referr = json_cmd_src_error(src, 1, "Missing \"for\" attributes");
		goto CleanUpAfterError;
	}

	/* Parse the attributes: (var key of expr), (key of expr), or (expr) */
	parensrc.str = str;
	if (!strncasecmp(parensrc.str, "var", 3) && isspace(parensrc.str[3])) {
		parsed->var = 1;
		parensrc.str += 3;
		json_cmd_parse_whitespace(&parensrc);
	}
	else if (!strncasecmp(parensrc.str, "const", 5) && isspace(parensrc.str[5])) {
		parsed->var = 1;
		parensrc.str += 5;
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
	parsed->calc = json_calc_parse(parensrc.str, &end, &err, 0);
	if (err || *end || !parsed->calc) {
		*referr = json_cmd_src_error(src, 1, err ? err : "Syntax error in \"for\" expression");
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

static jsoncmdout_t *for_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	json_t	*array, *scan;
	jsoncontext_t *layer;
	jsoncmdout_t *result = NULL;

	/* Evaluate the for-loop's array expression */
	array = json_calc(cmd->calc, *refcontext, NULL);
	if (!array || array->type != JSON_ARRAY) {
		if (json_is_error(array))
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
			result = json_cmd_run(cmd->sub, refcontext);

			/* Ignore "continue" and stay in loop.  For anything
			 * else other than NULL, exit the loop.
			 */
			if (result && result->ret == &json_cmd_continue) {
				free(result);
				result = NULL;
			}
			if (result)
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
			result = json_cmd_run(cmd->sub, &layer);

			/* Ignore "continue" and stay in loop.  For anything
			 * else other than NULL, exit the loop.
			 */
			if (result && result->ret == &json_cmd_continue) {
				free(result);
				result = NULL;
			}
			if (result)
				break;
		}

		/* Clean up */
		json_context_free(layer);

	} else { /* Anonymous loop */
		/* Loop over the elements */
		for (scan = array->first; scan; scan = scan->next) {
			/* Add a "this" layer */
			layer = json_context(*refcontext, scan, JSON_CONTEXT_THIS | JSON_CONTEXT_NOFREE);

			/* Run the body of the loop */
			result = json_cmd_run(cmd->sub, &layer);

			/* Ignore "continue" and stay in loop.  For anything
			 * else other than NULL, exit the loop.
			 */
			if (result && result->ret == &json_cmd_continue) {
				free(result);
				result = NULL;
			}
			if (result)
				break;

			/* Remove the "this" layer */
			json_context_free(layer);
		}
	}

	/* Free the array */
	json_free(array);

	/* If we got a "break" pseudo-error, ignore it.  Otherwise (real error
	 * or "return" pseudo-error) return it.
	 */
	if (result && result->ret == &json_cmd_break) {
		free(result);
		result = NULL;
	}
	return result;
}

static jsoncmd_t *try_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	jsoncmd_t	*parsed;
	char		*str = NULL;
	jsonsrc_t	parensrc;

	/* Allocate the jsoncmd_t for it */
	parsed = json_cmd(src, &jcn_try);

	/* Get the "try" statements */
	parsed->sub = json_cmd_parse_curly(src, referr);
	if (*referr)
		goto CleanUpAfterError;

	/* Expect "catch" */
	if (strncasecmp(src->str, "catch", 5) || !strchr(" \t\n\r({", src->str[5])) {
		*referr = json_cmd_src_error(src, 1, "Missing \"catch\"");
		goto CleanUpAfterError;
	}
	src->str += 5;
	json_cmd_parse_whitespace(src);

	/* Optional name within parentheses */
	if (*src->str == '(') {
		/* Get the parenthesized expression */
		str = json_cmd_parse_paren(src);

		/* It should be a single name */
		parensrc.str = str;
		parsed->key = json_cmd_parse_key(&parensrc, 1);
		if (*parensrc.str) {
			*referr = json_cmd_src_error(src, 1, "The argument to catch should be a single name");
			goto CleanUpAfterError;
		}

		/* Free the string */
		free(str);
		str = NULL;
	}

	/* Get the "catch" statements */
	parsed->more = json_cmd_parse_curly(src, referr);
	if (*referr)
		goto CleanUpAfterError;

	/* !!! I supposed I could test for a "finally" statement */

	/* Return it */
	return parsed;

CleanUpAfterError:
	if (str)
		free(str);
	json_cmd_free(parsed);
	return NULL;
}

static jsoncmdout_t *try_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	jsoncmdout_t *result;
	jsoncontext_t *caught;
	json_t *obj, *contextobj;

	/* Run the "try" statements.  For any result other than an error,
	 * just return it.
	 */
	result = json_cmd_run(cmd->sub, refcontext);
	if (!result || result->ret)
		return result;

	/* If no "catch" block, we're done */
	if (!cmd->more)
		return NULL;

	/* If there's a key, then we need to add a context where that name
	 * is an object describing the error.
	 */
	if (cmd->key) {

		/* Build the object describing the error */
		obj = json_object();
		if (result->filename)
			json_append(obj, json_key("filename", json_string(result->filename, -1)));
		json_append(obj, json_key("line", json_from_int(result->lineno)));
		json_append(obj, json_key("code", json_from_int(result->code)));
		json_append(obj, json_key("message", json_string(result->text, -1)));

		/* Make that object be inside another object, using key as the
		 * the member name.
		 */
		contextobj = json_object();
		json_append(contextobj, json_key(cmd->key, obj));

		/* Stuff it in a context, using the key as the name */
		caught = json_context(*refcontext, contextobj, 0);

		/* Run the "catch" block with this context */
		result = json_cmd_run(cmd->more, &caught);

		/* Free the context. This also frees the data allocated above.*/
		json_context_free(caught);
	} else {
		/* Just run the "catch" block with the same context */
		result = json_cmd_run(cmd->more, &caught);
	}

	return result;
}


static jsoncmd_t *throw_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	jsoncmd_t	*parsed;
	char		*end, *err, *pct;
	jsoncalc_t	*jc;

	/* Allocate the jsoncmd_t for it */
	parsed = json_cmd(src, &jcn_throw);

	/* Parse the first (only?) argument. */
	jc = NULL;
	end = err = NULL;
	if (*src->str && *src->str != ';' && *src->str != '}') {
		jc = json_calc_parse(src->str, &end, &err, 0);
		if (!jc || jc->op != JSONOP_LITERAL)
			goto BadArgs;
		src->str = end;

		/* Allow an error code */
		if (jc && jc->u.literal->type == JSON_NUMBER) {
			/* Store the number in 'var' */
			parsed->var = json_int(jc->u.literal);

			/* Don't need this expression anymore */
			json_calc_free(jc);
			jc = NULL;

			/* Try for another expression, if "," */
			if (*end == ',') {
				src->str = end + 1;
				jc = json_calc_parse(src->str, &end, &err, 0);
				if (!jc || err || jc->op != JSONOP_LITERAL)
					goto BadArgs;
				src->str = end;
			}
		}
	}

	/* Allow error text (a string literal).  If none, then use "Throw" */
	if (!jc)
		parsed->key = strdup("throw");
	else if (jc->u.literal->type != JSON_STRING)
		goto BadArgs;
	else {
		/* Store the string in 'key' */
		parsed->key = strdup(jc->u.literal->text);

		/* Don't need this expression anymore */
		json_calc_free(jc);
		jc = NULL;
	}

	/* The message is allowed to contain one %s.  If it does, then we
	 * expect exactly one additional argument.  If it doesn't then we
	 * don't allow any more arguments.
	 */
	pct = strchr(parsed->key, '%');
	if (pct) {
		/* Only allow one %s formatter -- no second %s, no %d, etc */
		if (pct[1] != 's' || strchr(pct + 1, '%'))
			goto BadArgs;

		/* Must be followed by another expression */
		if (*src->str != ',')
			goto BadArgs;

		/* Parse it */
		src->str++;
		jc = json_calc_parse(src->str, &end, &err, 0);
		if (!jc || err)
			goto BadArgs;
		src->str = end;

		/* Store it as 'calc' */
		parsed->calc = jc;
		jc = NULL;
	}

	/* Must not be anymore arguments */
	if (*src->str && *src->str != ';' && *src->str != '}')
		goto BadArgs;

	/* Skip over ';' at end of cmd */
	if (*src->str == ';')
		src->str++;

	/* Return it */
	return parsed;

BadArgs:
	if (jc)
		json_calc_free(jc);
	json_cmd_free(parsed);
	*referr = json_cmd_src_error(src, 0, "Bad parameters to %s", "throw");
	return NULL;
}

static jsoncmdout_t *throw_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	jsoncmdout_t *result;
	json_t	*arg;

	/* If there's an argument, evaluate it. */
	arg = NULL;
	if (cmd->calc) {
		arg = json_calc(cmd->calc, *refcontext, NULL);
	}

	/* Always return an error -- maybe with an argument */
	result = json_cmd_error(cmd->filename, cmd->lineno, cmd->var & 0xff, cmd->key, arg ? arg->text : "");

	/* Clean up */
	if (arg)
		json_free(arg);

	return result;
}

/* This is a helper function for global/local var/const declarations */
static jsoncmd_t *gvc_parse(jsonsrc_t *src, jsoncmdout_t **referr, jsoncmd_t *cmd)
{
	jsoncmd_t *first = cmd;
	char	*end, *err;

	/* Expect a name possibly followed by ":type" and/or "=expr" */
	for (;;) {
		cmd->key = json_cmd_parse_key(src, 1);
		if (!cmd->key) {
			*referr = json_cmd_src_error(src, 1, "Name expected after %s", cmd->name->name);
			json_cmd_free(first);
			return NULL;
		}
		json_cmd_parse_whitespace_or_type(src, NULL);
		if (*src->str == '=') {
			err = NULL;
			src->str++;
			cmd->calc = json_calc_parse(src->str, &end, &err, 0);
			src->str = end;
			if (err) {
				*referr = json_cmd_src_error(src, 1, "Error in expression (%s)", err);
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

static jsoncmdout_t *gvc_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	json_t	*value, *error;
	jsoncmd_t *each;

	/* A single statement can declare multiple vars/consts */
	error = NULL;
	for (each = cmd; each; each = each->more) {
		/* Evaluate the value. If error, remember it */
		value = NULL;
		if (each->calc) {
			value = json_calc(each->calc, *refcontext, NULL);
			if (json_is_error(value)) {
				if (error)
					free(value);
				else
					error = value;
				value = NULL;
			}
		}
		if (!value)
			value = json_null();

		/* Add it to the context */
		if (!json_context_declare(refcontext, each->key, value, each->flags)) {
			/* Duplicate! */
			json_free(value);
			return json_cmd_error(each->filename, each->lineno, 1, "Duplicate %s \"%s\"",
				(each->flags & JSON_CONTEXT_CONST) ? "const" : "var",
				each->key);
		}
	}

	/* If we encountered an error in an initializer, return it */
	if (error) {
		jsoncmdout_t *result;
		result = json_cmd_error(cmd->filename, cmd->lineno, (int)(size_t)error->first, "%s", error->text);
		json_free(error);
		return result;
	}

	/* Success! */
	return NULL;
}

static jsoncmd_t *break_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	jsoncmd_t *cmd = json_cmd(src, &jcn_break);

	/* No arguments or other components, but we still need to skip ";" */
	json_cmd_parse_whitespace(src);
	if (*src->str == ';')
		src->str++;
	return cmd;
}

static jsoncmdout_t *break_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	/* Return a 'break" pseudo-error */
	jsoncmdout_t *result = json_cmd_error(cmd->filename, cmd->lineno, 0, "");
	result->ret = &json_cmd_break;
	return result;
}

static jsoncmd_t *continue_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	jsoncmd_t *cmd = json_cmd(src, &jcn_continue);

	/* No arguments or other components, but we still need to skip ";" */
	json_cmd_parse_whitespace(src);
	if (*src->str == ';')
		src->str++;
	return cmd;
}

static jsoncmdout_t *continue_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	/* Return a 'break" pseudo-error */
	jsoncmdout_t *result = json_cmd_error(cmd->filename, cmd->lineno, 0, "");
	result->ret = &json_cmd_continue;
	return result;
}

static jsoncmd_t *var_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	jsoncmd_t *cmd = json_cmd(src, &jcn_var);
	cmd->flags = JSON_CONTEXT_VAR;
	return gvc_parse(src, referr, cmd);
}

static jsoncmdout_t *var_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	return gvc_run(cmd, refcontext);
}

static jsoncmd_t *const_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	jsoncmd_t *cmd = json_cmd(src, &jcn_var);
	cmd->flags = JSON_CONTEXT_CONST;
	return gvc_parse(src, referr, cmd);
}

static jsoncmdout_t *const_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	return gvc_run(cmd, refcontext);
}

static jsoncmd_t *function_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	char	*fname;
	jsonsrc_t paren; /* Used for scanning parameter source */
	json_t	*params = NULL;
	jsoncmd_t *body = NULL;
	char	*returntype = NULL;;

	paren.buf = NULL;

	/* Function name */
	fname = json_cmd_parse_key(src, 1);
	if (!fname) {
		*referr = json_cmd_src_error(src, 1, "Missing function name");
		goto Error;
	}

	/* Parameter list (the parenthesized text) */
	paren.filename = src->filename;
	paren.buf = paren.str = json_cmd_parse_paren(src);
	if (!paren.buf) {
		/* No parameter list, so just describe the named function and
		 * return NULL.
		 */
		jsonfunc_t *f = json_calc_function_by_name(fname);
		if (!f) {
			*referr = json_cmd_src_error(src, 1, "Unknown function \"%s\"", fname);
			goto Error;
		}

		/* Output a description of the function */
		if (f->fn)
			printf("builtin ");
		if (f->agfn)
			printf("aggregate ");
		printf("function %s", f->name);
		if (f->args)
			printf("(%s)", f->args);
		else {
			putchar('(');
			for (params = f->userparams->first; params; params = params->next)
				printf("%s%s", params->text, params->next ? ", " : "");
			putchar(')');
		}
		if (f->returntype)
			printf(":%s", f->returntype);
		putchar('\n');
		free(fname);
		return NULL;
	}
	paren.size = strlen(paren.buf);

	/* Parameters within the parenthesized text */
	params = json_object();
	json_cmd_parse_whitespace(&paren);
	while (*paren.str) {
		char	*pname;
		json_t	*defvalue;

		/* Parameter name */
		pname = json_cmd_parse_key(&paren, 0);
		if (!pname) {
			*referr = json_cmd_src_error(src, 1, "Missing parameter name");
			goto Error;
		}

		/* Possibly a type declaration */
		json_cmd_parse_whitespace_or_type(&paren, NULL);

		/* If followed by = then use a default */
		if (*paren.str == '=') {
			jsoncalc_t *calc;
			char	*end, *err;

			/* Move past the '=' */
			paren.str++;

			/* Parse the expression */
			err = NULL;
			calc = json_calc_parse(paren.str, &end, &err, 0);
			if (err) {
				*referr = json_cmd_src_error(src, 1, "Syntax error - %s", err);
				goto Error;
			}
			if (*end && *end != ',') {
				*referr = json_cmd_src_error(src, 1, "Syntax error near %.10s", end);
				goto Error;
			}
			paren.str = end;

			/* Evaluate the expression */
			defvalue = json_calc(calc, NULL, NULL);
			if (json_is_error(defvalue)) {
				*referr = json_cmd_src_error(src, (int)(long)defvalue->first, "%s", defvalue->text);
				goto Error;
			}

			/* Free the expression */
			json_calc_free(calc);
		} else {
			/* Use null as the default value */
			defvalue = json_null();
		}

		/* Add the parameter to the params object */
		json_append(params, json_key(pname, defvalue));

		/* Free the name */
		free(pname);

		/* If followed by comma, skip the comma */
		if (*paren.str == ',') {
			paren.str++;
			json_cmd_parse_whitespace(&paren);
		}
	}

	/* Parentheses may be followed by a return type declaration */
	json_cmd_parse_whitespace_or_type(src, &returntype);

	/* Body -- if no body, that's okay */
	if (*src->str == '{')
		body = json_cmd_parse_curly(src, referr);
	else if (json_calc_function_by_name(fname)) {
		/* No body but the function is already defined -- we were
		 * just redundantly declaring an already-defined function.
		 */
		free(fname);
		free(returntype);
		return NULL;
	} else
		body = NULL;

	/* Define it! */
	if (!*referr) {
		if (json_calc_function_user(fname, params, paren.buf, returntype, body)) {
			/* Tried to redefine a built-in, which isn't allowed. */
			*referr = json_cmd_src_error(src, 1, "Can't redefine built-in function \"%s\"", fname);
			goto Error;
		}

		/* We're done.  Nothing more will be required at runtime.
		 * The fname, params, and body remain allocated since the
		 * function needs them.
		 */
		return  NULL;
	}

Error:
	if (fname)
		free(fname);
	if (paren.buf)
		free(paren.buf);
	if (params)
		json_free(params);
	if (returntype)
		free(returntype);
	if (body)
		json_cmd_free(body);
	return NULL;
}

static jsoncmdout_t *function_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	/* Can't happen */
	return NULL;
}

static jsoncmd_t *return_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	jsoncalc_t *calc;
	char	*end, *err;
	jsoncmd_t *cmd;
	jsonsrc_t start;

	/* Allocate a cmd */
	start = *src;

	/* The return value is optional */
	json_cmd_parse_whitespace(src);
	if (*src->str && *src->str != ';' && *src->str != '}') {
		/* Parse the expression */
		err = NULL;
		calc = json_calc_parse(src->str, &end, &err, 0);
		if (err) {
			*referr = json_cmd_src_error(src, 1, "Syntax error - %s", err);
			if (calc)
				json_calc_free(calc);
			return NULL;
		}
		if (*end && (*end != ';' && *end != '}')) {
			*referr = json_cmd_src_error(src, 1, "Syntax error near %.10s", end);
			if (calc)
				json_calc_free(calc);
			return NULL;
		}
		src->str = end;

	} else {
		/* With no expression, assume "null */
		calc = json_calc_parse("null", NULL, NULL, 0);
	}

	/* Move past the ';', if there is one */
	if (*src->str == ';')
		src->str++;
	json_cmd_parse_whitespace(src);

	/* Build the command, and return it */
	cmd = json_cmd(&start, &jcn_return);
	cmd->calc = calc;
	return cmd;
}

static jsoncmdout_t *return_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	/* Return a 'return" pseudo-error */
	jsoncmdout_t *err = json_cmd_error(cmd->filename, cmd->lineno, 0, "");
	err->ret = json_calc(cmd->calc, *refcontext, NULL);
	return err;
}

static jsoncmd_t *switch_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	jsoncmd_t	*parsed;
	char	*str, *end, *err = NULL;

	/* Skip leading whitespace */
	json_cmd_parse_whitespace(src);

	/* Allocate the jsoncmd_t for it */
	parsed = json_cmd(src, &jcn_switch);

	/* Get the condition */
	str = json_cmd_parse_paren(src);
	if (!str) {
		*referr = json_cmd_src_error(src, 1, "Missing \"switch\" expression");
		return parsed;
	}

	/* Parse the expression */
	parsed->calc = json_calc_parse(str, &end, &err, 0);
	if (err || *end || !parsed->calc) {
		free(str);
		*referr = json_cmd_src_error(src, 1, err ? err : "Syntax error in \"switch\" expression");
		return parsed;
	}
	free(str);

	/* Get the "body" statements */
	parsed->sub = json_cmd_parse_curly(src, referr);
	if (*referr)
		return parsed;

	/* Return it */
	return parsed;
}

static jsoncmdout_t *switch_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	jsoncontext_t *layer;
	json_t	*switchcase;
	jsoncmdout_t *result;

	/* Evaluate the expression */
	switchcase = json_calc(cmd->calc, *refcontext, NULL);

	/* Add a context for store the "switchcase" variable */
	layer = json_context(*refcontext, json_object(), 0);

	/* Store the value in the variable */
	json_append(layer->data, json_key("switchcase", switchcase));

	/* Execute the body of the switch */
	result = json_cmd_run(cmd->sub, &layer);

	/* If exited with a "break", ignore it */
	if (result && result->ret == &json_cmd_break) {
		free(result);
		result = NULL;
	}

	/* Clean up */
	json_context_free(layer);

	return result;
}

static jsoncmd_t *case_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	jsoncmd_t	*parsed;
	char	*str, *end, *err = NULL;
	int	len, quote, nest, escape;

	/* Skip leading whitespace */
	json_cmd_parse_whitespace(src);

	/* Allocate the jsoncmd_t for it */
	parsed = json_cmd(src, &jcn_case);

	/* Extract the case.  This is trickier than it sounds -- we can't just
	 * parse it because it should end with a ":", and ":" is a valid
	 * operator.
	 */
	 nest = quote = escape = 0;
	 for (len = 0;
	      src->str[len] >= ' ' && (nest || quote || src->str[len] != ':');
	      len++) {
		if (escape)
			escape = 0;
		else if (quote && src->str[len] == quote)
			quote = 0;
		else if (!quote && (src->str[len] == '"' || src->str[len] == '\''))
			quote = src->str[len];
		else if (quote && src->str[len] == '\\')
			escape = 1;
		else if (!quote && src->str[len] == '(')
			nest++;
		else if (!quote && src->str[len] == ')')
			nest--;
	}
	if (len == 0 || src->str[len] != ':') {
		*referr = json_cmd_src_error(src, 1, "Missing or malformed \"case\" expression");
		return parsed;
	}
	str = (char *)malloc(len + 1);
	strncpy(str, src->str, len);
	str[len] = '\0';

	/* Parse the case.  */
	parsed->calc = json_calc_parse(str, &end, &err, 0);
	if (err || *end || !parsed->calc) {
		free(str);
		*referr = json_cmd_src_error(src, 1, err ? err : "Syntax error in \"case\" expression");
		return parsed;
	}
	free(str);

	/* Move past the ":" */
	src->str += len + 1;

	/* Return it */
	return parsed;
}

static jsoncmdout_t *case_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	json_t	*switchcase;
	int	match;

	/* Fetch the "switchcase" value.  Note that we only look in the top
	 * context layer so that if switch statements are nested, we don't see
	 * the outer one.  Use json_by_key() instead of json_context_by_key().
	 */
	switchcase = json_by_key((*refcontext)->data, "switchcase");
	if (!switchcase)
		return json_cmd_error(cmd->filename, cmd->lineno, 1, "Can't use \"case\" outside of \"switch\"");

	/* If "null" then continue with next command */
	if (json_is_null(switchcase))
		return NULL;

	/* Compare the case value to switchcase.
	 * 
	 * One optimization: If the value is a literal then we don't bother
	 * calling json_calc(), mostly so we can avoid allocating and freeing
	 * the value.
	 */
	if (cmd->calc->op == JSONOP_LITERAL) {
		match = json_equal(cmd->calc->u.literal, switchcase);
	} else {
		json_t *thiscase = json_calc(cmd->calc, *refcontext, NULL);
		match = json_equal(thiscase, switchcase);
		json_free(thiscase);
	}

	/* Anything else should match exactly.  If this does, then
	 * set switchcase to null and return NULL so we continue to the
	 * next statement.  If it does NOT match then we leave switchcase
	 * unchanged, and continue to the next "case" or "default" statement.
	 */
	if (match) {
		/* Match!  Change "switchcase" to null, and continue on to
		 * the next statement.
		 */
		json_append((*refcontext)->data, json_key("switchcase", json_null()));
		return NULL;
	} else {
		/* No match!  Leave "switchcase" unchanged, and skip to the
		 * next "case" or "default" statement.
		 */
		jsoncmdout_t *result = json_cmd_error(cmd->filename, cmd->lineno, 0, "");
		result->ret = &json_cmd_case_mismatch;
		return result;
	}
}

static jsoncmd_t *default_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	jsoncmd_t	*parsed;

	/* Skip leading whitespace */
	json_cmd_parse_whitespace(src);

	/* Allocate the jsoncmd_t for it */
	parsed = json_cmd(src, &jcn_default);

	/* Ends with a colon */
	if (*src->str != ':') {
		*referr = json_cmd_src_error(src, 1, "Syntax error in \"default\"");
		return parsed;
	}
	src->str++;

	/* Return it */
	return parsed;
}

static jsoncmdout_t *default_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	/* The "default" command always continues to the next command */
	return NULL;
}

static jsoncmd_t *void_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	jsoncalc_t *calc;
	char	*end, *err;
	jsoncmd_t *cmd;
	jsonsrc_t start;

	/* Allocate a cmd */
	start = *src;

	/* The expression is mandatory */
	json_cmd_parse_whitespace(src);
	if (!*src->str || *src->str == ';' || *src->str == '}') {
		json_cmd_src_error(src, 1, "The void command requires an expression");
	}

	/* Parse the expression */
	err = NULL;
	calc = json_calc_parse(src->str, &end, &err, 0);
	if (err) {
		*referr = json_cmd_src_error(src, 1, "Syntax error - %s", err);
		if (calc)
			json_calc_free(calc);
		return NULL;
	}
	if (*end && (*end != ';' && *end != '}')) {
		*referr = json_cmd_src_error(src, 1, "Syntax error near %.10s", end);
		if (calc)
			json_calc_free(calc);
		return NULL;
	}
	src->str = end;

	/* Move past the ';', if there is one */
	if (*src->str == ';')
		src->str++;
	json_cmd_parse_whitespace(src);

	/* Build the command, and return it */
	cmd = json_cmd(&start, &jcn_void);
	cmd->calc = calc;
	return cmd;
}

static jsoncmdout_t *void_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	/* Evaluate the expression but return NULL */
	json_free(json_calc(cmd->calc, *refcontext, NULL));
	return NULL;
}

static jsoncmd_t *explain_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	char	*end, *err;
	jsoncmd_t *cmd;

	/* Allocate a cmd */
	cmd = json_cmd(src, &jcn_explain);

	/* Three ways to go: "explain" explains the default table, "explain?"
	 * says where the default table is located, and "explain expr" explains
	 * the result of an expression.
	 */
	json_cmd_parse_whitespace(src);
	if (!*src->str || *src->str == ';' || *src->str == '}') {
		/* Use the default */
	} else if (*src->str == '?') {
		/* Use the default, but suppress the actual "explain" table */
		cmd->var = 1;
		src->str++;
		json_cmd_parse_whitespace(src);
	} else {
		/* Use an expression */
		err = NULL;
		cmd->calc = json_calc_parse(src->str, &end, &err, 0);
		if (err) {
			*referr = json_cmd_src_error(src, 1, "Syntax error - %s", err);
			json_cmd_free(cmd);
			return NULL;
		}
		src->str = end;
	}

	/* Detect cruft after the arguments */
	if (*src->str && (*src->str != ';' && *src->str != '}')) {
		*referr = json_cmd_src_error(src, 1, "Syntax error near %.10s", end);
		json_cmd_free(cmd);
		return NULL;
	}

	/* Move past the ';', if there is one */
	if (*src->str == ';')
		src->str++;
	json_cmd_parse_whitespace(src);

	/* Return the command */
	return cmd;
}

static jsoncmdout_t *explain_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	json_t	*table, *mustfree, *columns;
	char	*expr;

	/* Is there an expression, explicitly naming a table? */
	if (!cmd->calc) {
		/* No, so look for a default table */
		table = json_context_default_table(*refcontext, &expr);
		mustfree = NULL;

		/* If no table, say so */
		if (!table)
			return json_cmd_error(cmd->filename, cmd->lineno, 1, "No default table");
	} else {
		/* Yes, so evaluate it.
		 *
		 * NOTE: It'd be nice if we could do this without copying the
		 * table, since some tables are large.  However, even a huge
		 * table can be copied in a fraction of a second, and this
		 * command is really only useful when used interactively.
		 * For this reason, I'm not going to bother trying to optimize
		 * this for simple expressions.
		 */
		table = json_calc(cmd->calc, *refcontext, NULL);
		mustfree = table;
		expr = NULL;
	}

	/* Detect errors */
	if (json_is_error(table))
		return json_cmd_error(cmd->filename, cmd->lineno, (int)(long)table->first, "%s", table->text);
	if (!json_is_table(table))
		return json_cmd_error(cmd->filename, cmd->lineno, 1, "Not a table");

	/* Output the explain results, unless the parameter text was just "?" */
	columns = NULL;
	if (!cmd->var) {
		for (table = table->first; table; table = table->next)
			columns = json_explain(columns, table, 0);
		json_print(columns, NULL);
	}

	/* If we have an expr for the default table, output it */
	if (expr)
		puts(expr);

	/* Clean up */
	if (columns)
		json_free(columns);
	if (mustfree)
		json_free(mustfree);
	if (expr)
		free(expr);

	/* Done! */
	return NULL;
}

static jsoncmd_t *file_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	char	*end, *err;
	jsoncmd_t *cmd;

	/* Allocate a cmd */
	cmd = json_cmd(src, &jcn_file);

	/* Many possible ways to invoke this.  Most commands have a strict
	 * syntax, but file is intended mostly for interactive use and should
	 * be user-friendly.  The main ways to invoke it are:
	 *   file       List all files, with the current one highlighted
	 *   file +	Move to the next file
	 *   file -	Move to the previous file
	 *   file word	Move to the named file.  If new, append it.
	 *   file (expr)Move to the result of expr
	 */
	json_cmd_parse_whitespace(src);
	if (!*src->str || *src->str == ';' || *src->str == '}') {
		/* "file" with no arguments */
	} else if (*src->str == '+' || *src->str == '-') {
		/* Verify that it's ONLY + or -, not part of a calc expression*/
		char ch[2];
		ch[0] = *src->str++;
		ch[1] = '\0';
		json_cmd_parse_whitespace(src);
		if (*src->str && *src->str != ';' && *src->str != '}') {
			*referr = json_cmd_src_error(src, 1, "Bad use of file+ or file-");
			return NULL;
		}
		cmd->key = strdup(ch);
	} else if (*src->str == '(') {
		/* Get the expression */
		char *str = json_cmd_parse_paren(src);
		if (!str) {
			*referr = json_cmd_src_error(src, 1, "Missing ) in \"%s\" expression", "file");
			return cmd;
		}

		/* Parse the expression */
		cmd->calc = json_calc_parse(str, &end, &err, 0);
		if (err || *end || !cmd->calc) {
			free(str);
			*referr = json_cmd_src_error(src, 1, err ? err : "Syntax error in \"%s\" expression", "file");
			return cmd;
		}
		free(str);
	} else {
		/* Assume it is a filename.  Find the end of it, and copy it
		 * into the cmd->key.
		 */
		for (end = src->str; *end && *end != ';' && *end != '}'; end++){
		}
		while (end > src->str && end[-1] == ' ')
			end--;
		cmd->key = (char *)malloc(end - src->str + 1);
		strncpy(cmd->key, src->str, end - src->str);
		cmd->key[end - src->str] = '\0';

		/* Move past the end of the name */
		src->str = end;
		json_cmd_parse_whitespace(src);
	}

	/* Move past the ';', if there is one */
	if (*src->str == ';')
		src->str++;
	json_cmd_parse_whitespace(src);

	/* Return the command */
	return cmd;
}

static jsoncmdout_t *file_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	json_t *files;
	int	current = JSON_CONTEXT_FILE_SAME;

	/* Determine what type of "file" invocation this is */
	if (cmd->calc) {
		/* "file (calc) -- Evaluate the expression. */
		json_t *result = json_calc(cmd->calc, *refcontext, NULL);

		/* If we got an error, then return the error */
		if (json_is_error(result)) {
			jsoncmdout_t *err = json_cmd_error(cmd->filename, cmd->lineno, (int)(long)result->first, "%s", result->text);
			json_free(result);
			return err;
		}

		/* If it's a number, then select a file by index */
		if (result->type == JSON_NUMBER) {
			current = json_int(result);
			files = json_context_file(*refcontext, NULL, 0, &current);
			json_free(result);
		} else if (result->type == JSON_STRING) {
			current = JSON_CONTEXT_FILE_NEXT;
			files = json_context_file(*refcontext, result->text, 0, &current);
			json_free(result);
		} else {
			json_free(result);
			return json_cmd_error(cmd->filename, cmd->lineno, 1, "file expressions should return a number or string.");
		}
	} else if (!cmd->key) {
		/* "file" with no args -- display the current filename */
		files = json_context_file(*refcontext, NULL, 0, &current);
	} else if (!strcmp(cmd->key, "+")) {
		/* "file +" -- Move to the next file in the list */
		current = JSON_CONTEXT_FILE_NEXT;
		files = json_context_file(*refcontext, NULL, 0, &current);
	} else if (!strcmp(cmd->key, "-")) {
		/* "file -" -- Move to the previous file in the list */
		current = JSON_CONTEXT_FILE_PREVIOUS;
		files = json_context_file(*refcontext, NULL, 0, &current);
	} else {
		/* "file filename" -- Move to the named file */
		current = JSON_CONTEXT_FILE_NEXT;
		files = json_context_file(*refcontext, cmd->key, 0, &current);
	}

	/* After all that, display the current file's name */
	files = json_by_index(files, current);
	files = json_by_key(files, "filename");
	if (!files || files->type != JSON_STRING)
		puts("(no files)");
	else
		puts(files->text);

	/* Return success always */
	return NULL;
}


static jsoncmd_t *import_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	char	*end, *filename;
	FILE	*fp;
	jsoncmd_t *code, *cmd;
	jsonsrc_t start = *src;

	/* Parse the name. */
	json_cmd_parse_whitespace(src);
	for (end = src->str; *end && *end != ';' && *end != '}'; end++){
	}
	while (end > src->str && end[-1] == ' ')
		end--;
	filename = (char *)malloc(end - src->str + 4);
	strncpy(filename, src->str, end - src->str);
	filename[end - src->str] = '\0';
	src->str = end;

	/* If the filename has no extension, then assume ".jc" */
	end = strrchr(filename, '/');
	if (end)
		end++;
	else
		end = filename;
	end = strchr(end, '.');
	if (!end)
		strcat(filename, ".jc");

	/* For security's sake, make sure the name doesn't start with "/"
	 * or contain "../"
	 */
	if (filename[0] == '/' || strstr(filename, "../")) {
		*referr = json_cmd_src_error(&start, 1, "Unsafe file name to import: \"%s\"", filename);
		return NULL;
	}

	/* If the file doesn't exist or is unreadable, fail */
	if (access(filename, F_OK) < 0) {
		*referr = json_cmd_src_error(&start, 1, "Import file \"%s\" does not exist", filename);
		return NULL;
	}
	fp = fopen(filename, "r");
	if (!fp) {
		*referr = json_cmd_src_error(&start, 1, "Import file \"%s\" is unreadable", filename);
		return NULL;
	}
	fclose(fp);

	/* Load the file. If it contains any code other than function
	 * definitions, then stow it in a cmd to run later; this is necessary
	 * for declaring variables and constants, because those get stored in
	 * the context but we don't have a context at parse time.
	 */
	cmd = NULL;
	code = json_cmd_parse_file(filename);
	if (code) {
		cmd = json_cmd(&start, &jcn_import);
		cmd->sub = code;
	}

	/* Move past the ';', if there is one */
	json_cmd_parse_whitespace(src);
	if (*src->str == ';')
		src->str++;
	json_cmd_parse_whitespace(src);

	/* Probably nothing left to do at runtime... except maybe vars */
	return cmd;
}

static jsoncmdout_t *import_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	/* Declare variables and such */
	return json_cmd_run(cmd->sub, refcontext);
}


static jsoncmd_t *plugin_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	size_t len;
	char	quote;
	char	*str, *settings;
	json_t	*err, *section;

	/* Find the end of the command */
	json_cmd_parse_whitespace(src);
	for (len = 0, quote = 0; src->str[len] && (quote || !strchr(";\n}", src->str[len])); len++) {
		/* Handle backslash in a quoted string */
		if (quote && src->str[len] == '\\' && src->str[len + 1])
			len++;

		/* Start/end quoting */
		if (src->str[len] == quote)
			quote = 0;
		else if (!quote && strchr("\"'`", src->str[len]))
			quote = src->str[len];
	}

	/* Make a temp copy of the arguments */
	str = (char *)malloc(len + 1);
	strncpy(str, src->str, len);
	str[len] = '\0';
	src->str += len;

	/* Separate the plugin name from settings */
	settings = strchr(str, ',');
	if (settings) {
		*settings++ = '\0';
		while (*settings == ' ')
			settings++;
	} else
		settings = "";

	/* Load the plugin */
	err = json_plugin_load(str, 0, 0);
	if (err) {
		*referr = json_cmd_src_error(src, (int)(size_t)err->first, "%s", err->text);
		return NULL;
	}

	/* Process the settings, if any */
	if (*settings) {
		/* Find where this plugins settings are stored */
		section = json_by_key(json_config, "plugin");
		section = json_by_key(section, str);
		if (!section) {
			*referr = json_cmd_src_error(src, 0, "The \"%s\" plugin doesn't use settings", str);
			return NULL;
		}

		/* Adjust the settings */
		err = json_config_parse(section, settings, NULL);
		if (err) {
			*referr = json_cmd_src_error(src, (int)(size_t)err->first, "%s", err->text);
			return NULL;
		}

	}

	/* No action needed at runtime */
	return NULL;
}

static jsoncmdout_t *plugin_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	/* Plugins are loaded at parse time, not run time */
	return NULL;
}

/* Print a value.  If it's a string, print it without quotes or backslashes. */
static jsoncmd_t *print_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	jsoncmd_t *cmd;
	jsonsrc_t start;
	jsoncalc_t *item, *list;
	char	*err;

	start = *src;
	json_cmd_parse_whitespace(src);
	if (!*src->str || *src->str == ';' || *src->str == '}') {
		/* "print" with no arguments does nothing */
		return NULL;
	}

	/* Parse a comma-delimited list of expressions to output, as an
	 * array generator expression.
	 */
	list = NULL;
	err = NULL;
	do {
		item = json_calc_parse(src->str, &src->str, &err, FALSE);
		if (!item || err || (*src->str && !strchr(";},", *src->str))) {
			if (list)
				json_calc_free(list);
			*referr = json_cmd_error(start.filename, json_cmd_lineno(&start), 1, err ? err : "Syntax error in \"%s\" expression", "print");
			return NULL;
		}
		list = json_calc_list(list, item);
	} while (*src->str++ == ',');

	/* Build the command */
	cmd = json_cmd(&start, &jcn_print);
	cmd->calc = list;
	return cmd;
}

static jsoncmdout_t *print_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	json_t	*list, *scan;
	char	lastchar;

	/* Evaluate the expression list */
	list = json_calc(cmd->calc, *refcontext, NULL);

	/* If error, then return the error */
	if (json_is_error(list)) {
		jsoncmdout_t *err = json_cmd_error(cmd->filename, cmd->lineno, (int)(long)list->first, "%s", list->text);
		json_free(list);
		return err;
	}

	/* Otherwise output the results, all strung together without any
	 * added spaces or anything.  For strings, output the string literally.
	 */
	lastchar = '\n';
	for (scan = list->first; scan; scan = scan->next) {
		if (scan->type == JSON_STRING) {
			fputs(scan->text, stdout);
			if (*scan->text)
				lastchar = scan->text[strlen(scan->text) - 1];
		} else {
			char *tmp = json_serialize(scan, NULL);
			fputs(tmp, stdout);
			free(tmp);
			lastchar = 'x'; /* Never empty, never '\n' */
		}
	}

	/* If the last character wasn't a newline, remember that. */
	json_print_incomplete_line = (lastchar != '\n');

	/* Clean up */
	json_free(list);
	return NULL;
}

/* Set an option. */
static jsoncmd_t *set_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	jsoncmd_t *cmd;
	jsonsrc_t start;
	jsoncalc_t *calc;
	char	*str, *end, *err;
	size_t	len;

	start = *src;
	json_cmd_parse_whitespace(src);

	/* The options settings can be either explicit text, or a parenthesized
	 * expression that returns a string.
	 */
	if (*src->str == '(') {
		/* Parenthesized expression -- Get it in a string */
		str = json_cmd_parse_paren(src);
		if (!str) {
			*referr = json_cmd_src_error(src, 1, "Missing ) in \"%s\" expression", "set");
			return NULL;
		}

		/* Parse it */
		calc = json_calc_parse(str, &end, &err, 0);
		if (!calc || err || (*src->str && !strchr(";},", *src->str))) {
			free(str);
			*referr = json_cmd_error(start.filename, json_cmd_lineno(&start), 1, err ? err : "Syntax error in \"%s\" expression", "set");
			return NULL;
		}

		/* CLean up */
		free(str);
		str = NULL;
	} else {
		/* Explicit text -- Parsing this is tricky because the parser
		 * wants to store the change immediately, but we want to save
		 * those changes until the command is actually run.
		 */
		for (len = 0; src->str[len] && !strchr(";\n{", src->str[len]); len++) {
		}
		str = (char *)malloc(len + 1);
		strncpy(str, src->str, len);
		str[len] = '\0';
		src->str += len;
		calc = NULL;
	}

	/* Build the command */
	cmd = json_cmd(&start, &jcn_set);
	cmd->calc = calc;
	cmd->key = str;
	return cmd;
}

static jsoncmdout_t *set_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	json_t *result, *section, *conferr;
	char	*str;

	/* Are we using an expression to generate the settings on-the-fly? */
	if (cmd->calc) {
		/* Evaluate it */
		result = json_calc(cmd->calc, *refcontext, NULL);
		if (json_is_error(result)) {
			jsoncmdout_t *err = json_cmd_error(cmd->filename, cmd->lineno, (int)(long)result->first, "%s", result->text);
			json_free(result);
			return err;
		}

		/* Value must be a string */
		if (result->type != JSON_STRING) {
			json_free(result);
			return json_cmd_error(cmd->filename, cmd->lineno, (int)(long)result->first, "set expression must return a string");
		}

		/* Use the string's text */
		str = result->text;
	} else {
		/* Use the literal text */
		str = cmd->key;
		result = NULL;
	}

	/* Parse it, and store the changes.  */
	section = json_by_key(json_config, json_text_by_key(json_system, "runmode"));
	conferr = json_config_parse(section, str, NULL);
	if (conferr) {
		jsoncmdout_t *err = json_cmd_error(cmd->filename, cmd->lineno, (int)(long)conferr->first, "%s", conferr->text);
		json_free(conferr);
		if (result)
			json_free(result);
		return err;
	}

	/* Make the changes effective in the format */
	json_format_set(NULL, NULL);

	/* Clean up */
	if (result)
		json_free(result);
	return NULL;
}


/* Handle an assignment or output expression */
static jsoncmdout_t *calc_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	/* Calculate the result of the expression.   If it's an assignment,
	 * then this will do the assignment too.
	 */
	json_t *result = json_calc(cmd->calc, *refcontext, NULL);

	/* If we got an error ("null" with text), then convert to jsoncmdout_t */
	if (json_is_error(result)) {
		jsoncmdout_t *err = json_cmd_error(cmd->filename, cmd->lineno, (int)(long)result->first, "%s", result->text);
		json_free(result);
		return err;
	}

	/* If not an assignment, then it's an output.  Output it! */
	if (cmd->calc->op != JSONOP_ASSIGN
	 && cmd->calc->op != JSONOP_APPEND
	 && cmd->calc->op != JSONOP_MAYBEASSIGN)
		json_print(result, NULL);

	/* Either way, free the result */
	json_free(result);

	return NULL;
}
