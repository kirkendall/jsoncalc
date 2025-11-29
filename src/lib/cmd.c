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
#include <jx.h>

/* This handles commands.  Each script is a series of commands, so this is
 * pretty central.  While expressions use a decent LALR parser with operator
 * precedence (implemented in calcparse.c), commands use a simple recursive
 * descent parser.  This is because recursive descent parsers are more modular
 * and therefor easier to extend.  A plugin can register a new command by
 * calling jx_cmd_hook() with the name, and pointers to the argument parsing
 * function and run function.
 *
 * A command's parsers can use the jx_cmd_parse_whitespace(),
 * jx_cmd_parse_key(), jx_cmd_parse_paren(), jx_cmd_parse_curly()
 * functions.  Also jx_calc_parse() of course.
 */

/* This array doesn't actually store anything; it just provides a distinct
 * value that can be used to recognize when jx_cmd_parse() and
 * jx_cmd_parse_string() detect an error.
 */
jxcmd_t JX_CMD_ERROR[1];

/* Forward declarations for functions that implement the built-in commands */
static jxcmd_t    *if_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *if_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *while_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *while_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *for_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *for_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *break_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *break_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *continue_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *continue_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *switch_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *switch_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *case_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *case_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *default_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *default_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *try_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *try_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *throw_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *throw_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *var_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *var_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *const_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *const_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *function_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *function_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *return_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *return_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *void_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *void_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *explain_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *explain_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *file_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *file_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *import_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *import_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *plugin_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *plugin_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *print_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *print_run(jxcmd_t *cmd, jxcontext_t **refcontext);
static jxcmd_t    *set_parse(jxsrc_t *src, jxcmdout_t **referr);
static jxcmdout_t *set_run(jxcmd_t *cmd, jxcontext_t **refcontext);
/* delete lvalue */
/* throw [code],msg[,args] */
/* help topic subtopic */
static jxcmdout_t *calc_run(jxcmd_t *cmd, jxcontext_t **refcontext);

/* Linked list of command names */
static jxcmdname_t jcn_if =       {NULL,		"if",		if_parse,	if_run};
static jxcmdname_t jcn_while =    {&jcn_if,		"while",	while_parse,	while_run};
static jxcmdname_t jcn_for =      {&jcn_while,	"for",		for_parse,	for_run};
static jxcmdname_t jcn_break =    {&jcn_for,		"break",	break_parse,	break_run};
static jxcmdname_t jcn_continue = {&jcn_break,	"continue",	continue_parse,	continue_run};
static jxcmdname_t jcn_switch =   {&jcn_continue,	"switch",	switch_parse,	switch_run};
static jxcmdname_t jcn_case =     {&jcn_switch,	"case",		case_parse,	case_run};
static jxcmdname_t jcn_default =  {&jcn_case,		"default",	default_parse,	default_run};
static jxcmdname_t jcn_try =      {&jcn_default,	"try",		try_parse,	try_run};
static jxcmdname_t jcn_throw =    {&jcn_try,		"throw",	throw_parse,	throw_run};
static jxcmdname_t jcn_var =      {&jcn_throw,	"var",		var_parse,	var_run};
static jxcmdname_t jcn_const =    {&jcn_var,		"const",	const_parse,	const_run};
static jxcmdname_t jcn_function = {&jcn_const,	"function",	function_parse,	function_run};
static jxcmdname_t jcn_return =   {&jcn_function,	"return",	return_parse,	return_run};
static jxcmdname_t jcn_void =     {&jcn_return,	"void",		void_parse,	void_run};
static jxcmdname_t jcn_explain =  {&jcn_void,		"explain",	explain_parse,	explain_run};
static jxcmdname_t jcn_file =     {&jcn_explain,	"file",		file_parse,	file_run};
static jxcmdname_t jcn_import =   {&jcn_file,		"import",	import_parse,	import_run};
static jxcmdname_t jcn_plugin =   {&jcn_import,	"plugin",	plugin_parse,	plugin_run};
static jxcmdname_t jcn_print =    {&jcn_plugin,	"print",	print_parse,	print_run};
static jxcmdname_t jcn_set =	    {&jcn_print,	"set",		set_parse,	set_run};
static jxcmdname_t *names = &jcn_set;

/* A command name struct for assignment/output.  This isn't part of the "names"
 * list because assignment/output has no name -- you just give the expression.
 */
static jxcmdname_t jcn_calc = {NULL, "<<calc>>", NULL, calc_run};

/* These are used to indicate special results from a series of commands.
 * Their values are irrelevant; their unique addresses are what matters.
 */
jx_t jx_cmd_break;		/* "break" statement */
jx_t jx_cmd_continue;	/* "continue" statement */
jx_t jx_cmd_case_mismatch;	/* "case" that doesn't match switchcase */

/* Add a new statement name, and its argument parser and runner. */
jxcmdname_t *jx_cmd_hook(char *pluginname, char *cmdname, jxcmd_t *(*argparser)(jxsrc_t *src, jxcmdout_t **referr), jxcmdout_t *(*run)(jxcmd_t *cmd, jxcontext_t **refcontext))
{
	/* Allocate a jxcmdname_t for it */
	jxcmdname_t *sn = (jxcmdname_t *)malloc(sizeof(jxcmdname_t));

	/* Fill it */
	sn->pluginname = pluginname;
	sn->name = cmdname;
	sn->argparser = argparser;
	sn->run = run;

	/* Add it to the list */
	sn->other = names;
	names = sn;

	/* Return it.  The command parser will likely need to know it. */
	return sn;
}

/* Generate an error message */
jxcmdout_t *jx_cmd_error(const char *where, const char *fmt, ...)
{
	va_list	ap;
	size_t	size;
	jxcmdout_t *result;

	/* !!!Translate the message via catalog using "code".  EXCEPT if the
	 * format is "%s" then assume it has already been translated.
	 */

	/* Figure out how long the message will be */
	va_start(ap, fmt);
	size = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	/* Allocate the error structure with enough space for the message */
	result = (jxcmdout_t *)malloc(sizeof(jxcmdout_t) + size);

	/* Fill the error structure */
	memset(result, 0, sizeof(jxcmdout_t) + size);
	result->where = where;
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
void jx_cmd_parse_whitespace(jxsrc_t *src)
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
void jx_cmd_parse_whitespace_or_type(jxsrc_t *src, char **refstr)
{
	int	nest;
	int	quote;
	int	afterop;
	const char	*start;
	int	len;

	/* Skip whitespace and some comments */
	jx_cmd_parse_whitespace(src);

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
char *jx_cmd_parse_key(jxsrc_t *src, int quotable)
{
	size_t	len, unescapedlen;
	char	*key;

	/* Skip leading whitespace */
	jx_cmd_parse_whitespace(src);

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
		unescapedlen = jx_mbs_unescape(NULL, src->str+1, len - 1);
		key = (char *)malloc(unescapedlen + 1);
		jx_mbs_unescape(key, src->str+1, len - 1);
		key[unescapedlen] = '\0';
		src->str += len + 1;
	} else
		return NULL;

	/* Skip trailing whitespace */
	jx_cmd_parse_whitespace(src);

	/* Return the key */
	return key;
}

/* Parse a parenthesized expression, possibly with some other syntax elements
 * mixed in.  This is smart enough to handle nested parentheses, and
 * parentheses in strings.  Returns the contents of the parentheses (without
 * the parentheses themselves) as a dynamically-allocated string, or NULL if
 * not a valid parenthesized expression.
 */
char *jx_cmd_parse_paren(jxsrc_t *src)
{
	int	nest;
	char	quote;
	const char	*scan;
	size_t	len;
	char	*paren;

	/* Skip leading whitespace */
	jx_cmd_parse_whitespace(src);

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
	jx_cmd_parse_whitespace(src);

	/* Return the contents of the parentheses */
	return paren;
}

/* Allocate a statement, and initialize it */
jxcmd_t *jx_cmd(jxsrc_t *src, jxcmdname_t *name)
{
	jxcmd_t *cmd = (jxcmd_t *)malloc(sizeof(jxcmd_t));
	memset(cmd, 0, sizeof(jxcmd_t));
	cmd->where = src->str;
	cmd->name = name;
	return cmd;
}

/* Free a statement, and any related statements or data */
void jx_cmd_free(jxcmd_t *cmd)
{
	/* Defend against NULL and JX_CMD_ERROR */
	if (!cmd || cmd == JX_CMD_ERROR)
		return;

	/* Free related data */
	if (cmd->key)
		free(cmd->key);
	if (cmd->calc)
		jx_calc_free(cmd->calc);
	jx_cmd_free(cmd->sub);
	jx_cmd_free(cmd->more);
	jx_cmd_free(cmd->nextcmd);

	/* Free the cmd itself */
	free(cmd);
}

/* Parse a single statement and return it.  If it can't be parsed, then issue
 * an error message and return NULL.  If it is a function definition, return
 * it instead of processing it immediately.
 */
jxcmd_t *jx_cmd_parse_single(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcmdname_t	*sn;
	size_t 		len;
	jxcalc_t	*calc;
	const char	*where, *end, *err;
	jxcmd_t	*cmd;

	/* Skip leading whitespace */
	jx_cmd_parse_whitespace(src);
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
	for (sn = names; sn; sn = sn->other) {
		len = strlen(sn->name);
		end = src->str + len;
		if (!jx_mbs_ncasecmp(sn->name, src->str, len)
		 && (!isalnum(*end) && *end != '_'))
			break;
	}

	/* If followed immediately by a "(" then check to see if its a function.
	 * Sometimes functions and commands have the same name, and this helps
	 * us keep them separate.  If it looks like a function call then ignore
	 * the command.
	 */
	if (sn && *end == '(' && jx_calc_function_by_name(sn->name))
		sn = NULL;

	/* If it's a statement, use the statement's parser */
	if (sn) {
		src->str += len;
		return sn->argparser(src, referr);
	}

	/* Hopefully it is an assignment or an output expression.  Parse it. */
	end = err = NULL;
	calc = jx_calc_parse(src->str, &end, &err, 1);
	if (!calc || err || (*end && *end != ';' && *end != '}')) {
		if (calc)
			jx_calc_free(calc);
		if (!err) {
			/* Parsing ended prematurely, but without an error
			 * message.  We need to figure out why it ended
			 * prematurely.  Start by looking for an initial name.
			 */
			char *vagueerr = NULL, afterch = '\0';
			if (isalpha(*where)) {
				/* It started with a name.  Parse the name,
				 * so we can report it as an unknown command.
				 */
				src->str = where;
				vagueerr = jx_cmd_parse_key(src, 0);
				if (src->str < src->buf + src->size)
					afterch = *src->str;
			}

			/* If no name, or a function name, then assume we got
			 * an expression error.  (We'd like to check vars and
			 * consts, but we don't have a context yet.) Otherwise,
			 * treat it as an unknown command.
			 */
			if (vagueerr && afterch == '(' && !jx_calc_function_by_name(vagueerr))
				*referr = jx_cmd_error(where, "unkFunc:Unknown function %s()", vagueerr);
			else if (vagueerr && afterch != '.' && afterch != '[')
				*referr = jx_cmd_error(where, "unkCmd:Unknown command \"%s\"", vagueerr);
			else
				*referr = jx_cmd_error(where, "syntax:Expression syntax error");
			if (vagueerr)
				free(vagueerr);
		} else {
			*referr = jx_cmd_error(where, "%s", err);
		}
		return NULL;
	}

	/* Stuff it into a jxcmd_t */
	cmd = jx_cmd(src, &jcn_calc);
	cmd->calc = calc;

	/* Move past the end of the statement */
	src->str = end;
	if (*src->str == ';')
		src->str++;
	jx_cmd_parse_whitespace(src);

	/* Return it */
	return cmd;
}

/* Parse a statement block, and return it.  If can't be parsed, then store an
 * error message at *referr and return NULL.  Function declarations are not
 * allowed, and should generate an error message.  An empty set of curly braces
 * is allowed, though, and should return a "NO OP" statement.
 */
jxcmd_t *jx_cmd_parse_curly(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcmd_t *cmd, *current;

	/* Skip whitespace */
	jx_cmd_parse_whitespace(src);

	/* Expect a '{'.  For anything else, assume it's a single statement. */
	if (*src->str == '{') {
		src->str++;
		cmd = current = jx_cmd_parse_single(src, referr);
		while (*referr == NULL && *src->str != '}') {
			current->nextcmd = jx_cmd_parse_single(src, referr);
			jx_cmd_parse_whitespace(src);
			if (current->nextcmd)
				current = current->nextcmd;
			if (*referr)
				break;
		}
		if (*src->str == '}')
			src->str++;
	} else {
		cmd = jx_cmd_parse_single(src, referr);
	}

	/* Skip trailing whitespace */
	jx_cmd_parse_whitespace(src);

	/* Return it */
	return cmd;
}

jxcmd_t *jx_cmd_parse(jxsrc_t *src)
{
	jxcmdout_t *result = NULL;
	jxcmd_t *cmd, *firstcmd, *nextcmd;
	jxfile_t *jf;
	int	lineno;

	/* If first line starts with "#!" then skip to second line */
	if (src->str[0] == '#' && src->str[1] == '!') {
		while (*src->str && *src->str != '\n')
			src->str++;
	}

	/* For each statement... */
	jx_cmd_parse_whitespace(src);
	firstcmd = cmd = NULL;
	while (src->str < src->buf + src->size && *src->str) {
		/* Parse it */
		nextcmd = jx_cmd_parse_single(src, &result);

		/* If error then report it and quit */
		if (result) {
			jf = jx_file_containing(result->where, &lineno);
			if (jf)
				jx_user_printf(NULL, "error", "%s:%d: ", jf->filename, lineno);
			jx_user_printf(NULL, "error", "%s\n", result->text);
			free(result);
			jx_cmd_free(firstcmd);
			return JX_CMD_ERROR;
		}

		/* It could be NULL, which is *NOT* an error.  That would be
		 * for things like function definitions, which are processed
		 * by the parser and not at run-time.  Skip NULL */
		if (!nextcmd)
			continue;

		/* Anything else gets added to the statement chain */
		if (cmd)
			cmd->nextcmd = nextcmd;
		else
			firstcmd = nextcmd;
		cmd = nextcmd;

		/* Also, store the filename and line number of this command */
		cmd->where = src->str;

		/* Skip whitespace */
		jx_cmd_parse_whitespace(src);
	}

	/* Return the commands.  Might be NULL. */
	return firstcmd;
}

/* Parse a string as jxcalc commands.  If an error is detected then an
 * error message will be output and this will return NULL.  However, NULL
 * can also be returned if the text is empty, or only contains function
 * definitions, so NULL is *not* an error indication; it just means there's
 * nothing to execute or free.
 */
jxcmd_t *jx_cmd_parse_string(char *text)
{
	jxsrc_t srcbuf;

	/* Fill the src buffer */
	srcbuf.buf = text;
	srcbuf.str = text;
	srcbuf.size = strlen(text);

	/* Parse it */
	return jx_cmd_parse(&srcbuf);
}


/* Parse a file, and return any commands from it. If an error is detected
 * then an error message will be output and this will return NULL.  However,
 * NULL can also be returned if the file is empty, or only contains function
 * definitions, so NULL is *not* an error indication; it just means there's
 * nothing to execute or free.
 */
jxcmd_t *jx_cmd_parse_file(const char *filename) 
{
	jxfile_t *jf;
	jxcmd_t *cmd;
	jxsrc_t srcbuf;

	/* Load the file into memory.  We'll keep it loaded forever, so we can
	 * use it to report error locations and maybe do other debugging.
	 */
	jf = jx_file_load(filename);
	if (!jf) {
		perror(filename);
		return NULL;
	}

	/* Fill in the srcbuf */
	srcbuf.buf = jf->base;
	srcbuf.str = jf->base;
	srcbuf.size = jf->size;

	/* Parse it */
	cmd = jx_cmd_parse(&srcbuf);

	/* Return it */
	return cmd;
}

/* Run a series of statements, and return the result */
jxcmdout_t *jx_cmd_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	jxcmdout_t *result = NULL;

	while (cmd && !result) {
		assert(cmd != JX_CMD_ERROR);

		/* Maybe output trace info */
		if (jx_debug_flags.trace) {
			int lineno;
			jxfile_t *jf = jx_file_containing(cmd->where, &lineno);
			if (jf)
				jx_user_printf(NULL, "debug", "%s:%d: ", jf->filename, lineno);
			if (cmd->key)
				jx_user_printf(NULL, "debug", "%s %s\n", cmd->name->name, cmd->key);
			else
				jx_user_printf(NULL, "debug", "%s\n", cmd->name->name);
		}

		/* Run the command */
		result = (*cmd->name->run)(cmd, refcontext);

		/* If mismatched "case", then skip ahead to the next case */
		if (result && result->ret == &jx_cmd_case_mismatch) {
			/* We're handling this result here.  Free it */
			free(result);
			result = NULL;

			/* Skip to the next "case" or "default" statement */
			while ((cmd = cmd->nextcmd) != NULL
			    && cmd->name != &jcn_case 
			    && cmd->name != &jcn_default) {
			}
		} else {
			/* For NULL, just go to the next command.  If it's
			 * some other value, such as a "return", then we'll
			 * exit the loop so changing "cmd" here is harmless.
			 */
			cmd = cmd->nextcmd;
		}
	}
	return result;
}

/* Invoke a user-defined function, and return its value */
jx_t *jx_cmd_fncall(jx_t *args, jxfunc_t *fn, jxcontext_t *context)
{
	jxcmdout_t *result;
	jx_t	*out;

	assert(fn->user);

	/* Add the call frame to the context stack */
	context = jx_context_func(context, fn, args);

	/* Run the body of the function */
	result = jx_cmd_run(fn->user, &context);

	/* Decode the "result" response */
	if (!result) /* Function terminated without "return" -- use null */
		out = jx_null();
	else if (!result->ret)
		out = jx_error_null(result->where, "%s", result->text);
	else if (result->ret == &jx_cmd_break) /* "break" */
		out = jx_error_null(result->where, "break:Misuse of \"break\"");
	else if (result->ret == &jx_cmd_continue) /* "continue" */
		out = jx_error_null(result->where, "continue:Misuse of \"continue\"");
	else /* "return" */
		out = result->ret;

	/* Free "result" but not "result->ret" */
	if (result)
		free(result);

	/* Clean up the context, possibly including local vars and consts */
	while ((context->flags & JX_CONTEXT_ARGS) == 0)
		context = jx_context_free(context);
	context = jx_context_free(context);

	/* Return the result */
	return out;

}

/* Append any commands from "added" to the end of "existing".  Either of those
 * can be NULL to represent an empty list.  If context is non-NULL then
 * evaluate any "var" or "const" commands instead of appending them.
 * Either way, the commands from "added" are no longer valid when this
 * function returns; you don't need to store it or free it.
 */
jxcmd_t *jx_cmd_append(jxcmd_t *existing, jxcmd_t *added, jxcontext_t *context)
{
	jxcmd_t *nextcmd, *end;
#if 0
	jxcmdout_t *result;
#endif

	/* If "existing" is JX_CMD_ERROR then just return it unchanged. */
	if (existing == JX_CMD_ERROR)
		return existing;

	/* If "added" is NULL, do nothing */
	if (!added)
		return existing;

	/* If "added" is JX_CMD_ERROR then free the "existing" list (if any)
	 * and return JX_CMD_ERROR.
	 */
	if (added == JX_CMD_ERROR) {
		jx_cmd_free(existing);
		return JX_CMD_ERROR;
	}

	/* If "existing" is non-NULL then move to the end of the list */
	if (existing) {
		end = existing;
		while (end->nextcmd)
			end = end->nextcmd;
	}

	/* For each command from "added"... */
	for (; added; added = nextcmd) {
		nextcmd = added->nextcmd;
		added->nextcmd = NULL;

#if 0
		/* Maybe execute "const" and "var" now */
		if (context && (added->name->run == var_run || added->name->run == const_run)) {
			result = jx_cmd_run(added, &context);
			free(result);
			jx_cmd_free(added);
			continue;
		}
#endif

		/* Append this command to "existing" */
		if (existing)
			end->nextcmd = added;
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

static jxcmd_t *if_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcmd_t	*parsed;
	char	*str;
	const char	*end, *err = NULL;

	/* Skip leading whitespace */
	jx_cmd_parse_whitespace(src);

	/* Allocate the jxcmd_t for it */
	parsed = jx_cmd(src, &jcn_if);

	/* Get the condition */
	str = jx_cmd_parse_paren(src);
	if (!str) {
		*referr = jx_cmd_error(src->str, "Missing \"%s\" condition", "if");
		return parsed;
	}

	/* Parse the condition */
	parsed->calc = jx_calc_parse(str, &end, &err, 0);
	if (err || *end || !parsed->calc) {
		free(str);
		if (err)
			*referr = jx_cmd_error(src->str, "%s", err);
		else
			*referr = jx_cmd_error(src->str, "Syntax error in \"%s\" condition", "if");
		return parsed;
	}
	free(str);

	/* Get the "then" statements */
	parsed->sub = jx_cmd_parse_curly(src, referr);
	if (*referr)
		return parsed;

	/* If followed by "else" then parse the "else" statements */
	if (!strncmp(src->str, "else", 4) && !isalnum((src->str)[4])) {
		src->str += 4;
		parsed->more = jx_cmd_parse_curly(src, referr);
	}

	/* Return it */
	return parsed;
}

static jxcmdout_t *if_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	jx_t *jsbool = jx_calc(cmd->calc, *refcontext, NULL);
	int	bool = jx_is_true(jsbool);
	jx_free(jsbool);
	if (bool)
		return jx_cmd_run(cmd->sub, refcontext);
	else
		return jx_cmd_run(cmd->more, refcontext);
}

static jxcmd_t *while_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcmd_t	*parsed;
	char	*str;
	const char *end, *err = NULL;

	/* Skip leading whitespace */
	jx_cmd_parse_whitespace(src);

	/* Allocate the jxcmd_t for it */
	parsed = jx_cmd(src, &jcn_while);

	/* Get the condition */
	str = jx_cmd_parse_paren(src);
	if (!str) {
		*referr = jx_cmd_error(src->str, "Missing \"%s\" condition", "while");
		return parsed;
	}

	/* Parse the condition */
	parsed->calc = jx_calc_parse(str, &end, &err, 0);
	if (err || *end || !parsed->calc) {
		free(str);
		if (err)
			*referr = jx_cmd_error(src->str, "%s", err);
		else
			*referr = jx_cmd_error(src->str, "Syntax error in \"while\" condition");
		return parsed;
	}
	free(str);

	/* Get the "loop" statements */
	parsed->sub = jx_cmd_parse_curly(src, referr);
	if (*referr)
		return parsed;

	/* Return it */
	return parsed;
}

static jxcmdout_t *while_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	for (;;) {
		/* Evaluate the condition */
		jx_t *jsbool = jx_calc(cmd->calc, *refcontext, NULL);
		int	bool = jx_is_true(jsbool);
		jx_free(jsbool);

		/* If the condition is false, then terminate the loop */
		if (!bool)
			return NULL;

		/* Run the loop body.  If it has an error, then return the
		 * error; otherwise continue to loop.
		 */
		jxcmdout_t *result = jx_cmd_run(cmd->sub, refcontext);

		/* If we got a "continue" then ignore it and stay in the loop */
		if (result && result->ret == &jx_cmd_continue) {
			free(result);
			result = NULL;
		}

		/* If "breaK', "return", or error then exit the loop */
		if (result) {
			/* If we got a "break", ignore it.  Otherwise ("return"
			 * or an error) return it.
			 */
			if (result && result->ret == &jx_cmd_break) {
				free(result);
				result = NULL;
			}
			return result;
		}
	}
}

static jxcmd_t *for_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcmd_t	*parsed;
	char	*str = NULL;
	const char *end, *err = NULL;
	jxsrc_t	parensrc;

	/* Skip leading whitespace */
	jx_cmd_parse_whitespace(src);

	/* Allocate the jxcmd_t for it */
	parsed = jx_cmd(src, &jcn_for);

	/* Get the loop attributes */
	str = jx_cmd_parse_paren(src);
	if (!str) {
		*referr = jx_cmd_error(src->str, "Missing \"%s\" attributes", "for");
		goto CleanUpAfterError;
	}

	/* Parse the attributes: (var key of expr), (key of expr), or (expr) */
	parensrc.str = str;
	if (!strncasecmp(parensrc.str, "var", 3) && isspace(parensrc.str[3])) {
		parsed->var = 1;
		parensrc.str += 3;
		jx_cmd_parse_whitespace(&parensrc);
	}
	else if (!strncasecmp(parensrc.str, "const", 5) && isspace(parensrc.str[5])) {
		parsed->var = 1;
		parensrc.str += 5;
		jx_cmd_parse_whitespace(&parensrc);
	}
	parsed->key = jx_cmd_parse_key(&parensrc, 1);
	if (parsed->key && parensrc.str[0] == '=') {
		parensrc.str++;
		jx_cmd_parse_whitespace(&parensrc);
	} else if (parsed->key && !strncasecmp(parensrc.str, "of", 2) && !isalnum(parensrc.str[2]) && parensrc.str[2] != '_') {
		parensrc.str += 2;
		jx_cmd_parse_whitespace(&parensrc);
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
	parsed->calc = jx_calc_parse(parensrc.str, &end, &err, 0);
	if (err || *end || !parsed->calc) {
		if (err)
			*referr = jx_cmd_error(src->str, "%s", err);
		else
			*referr = jx_cmd_error(src->str, "Syntax error in \"\" expression", "for");
		goto CleanUpAfterError;
	}

	/* Get the "loop" statements */
	parsed->sub = jx_cmd_parse_curly(src, referr);
	if (*referr)
		goto CleanUpAfterError;

	/* Return it */
	if (str)
		free(str);
	return parsed;

CleanUpAfterError:
	if (str)
		free(str);
	jx_cmd_free(parsed);
	return NULL;
}

static jxcmdout_t *for_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	jx_t	*array, *scan;
	jxcontext_t *layer;
	jxcmdout_t *result = NULL;

	/* Evaluate the for-loop's array expression */
	array = jx_calc(cmd->calc, *refcontext, NULL);
	if (!array || array->type != JX_ARRAY) {
		if (jx_is_error(array))
			return jx_cmd_error(cmd->where, "%s", array->text);
		return jx_cmd_error(cmd->where, "forNotArray:\"%s\" expression is not an array", "for");
	}

	/* Without "var", look for an existing variable to use for the loop. */
	if (!cmd->var && cmd->key && jx_context_by_key(*refcontext, cmd->key, &layer) != NULL) {
		/* Make sure the variable isn't a "const" */
		if (layer->flags & JX_CONTEXT_CONST) {
			jx_free(array);
			return jx_cmd_error(cmd->where, "forConst:\"%s\" variable \"%s\" is a %s", "for", cmd->key, "const");
		}

		/* Okay, we have an existing variable! */
		for (scan = jx_first(array); scan; scan = jx_next(scan)) {
			/* Store the value in the variable */
			jx_append(layer->data, jx_key(cmd->key, jx_copy(scan)));

			/* Execute the body of the loop */
			result = jx_cmd_run(cmd->sub, refcontext);

			/* Ignore "continue" and stay in loop.  For anything
			 * else other than NULL, exit the loop.
			 */
			if (result && result->ret == &jx_cmd_continue) {
				free(result);
				result = NULL;
			}
			if (result) {
				jx_break(scan);
				break;
			}
		}
	} else if (cmd->key) {
		/* Add a context for store the variable */
		layer = jx_context(*refcontext, jx_object(), 0);

		/* Loop over the elements */
		for (scan = jx_first(array); scan; scan = jx_next(scan)) {
			/* Store the value in the variable */
			jx_append(layer->data, jx_key(cmd->key, jx_copy(scan)));

			/* Execute the body of the loop */
			result = jx_cmd_run(cmd->sub, &layer);

			/* Ignore "continue" and stay in loop.  For anything
			 * else other than NULL, exit the loop.
			 */
			if (result && result->ret == &jx_cmd_continue) {
				free(result);
				result = NULL;
			}
			if (result) {
				jx_break(scan);
				break;
			}
		}

		/* Clean up */
		jx_context_free(layer);

	} else { /* Anonymous loop */
		/* Loop over the elements */
		for (scan = jx_first(array); scan; scan = jx_next(scan)) {
			/* Add a "this" layer */
			layer = jx_context(*refcontext, scan, JX_CONTEXT_THIS | JX_CONTEXT_NOFREE);

			/* Run the body of the loop */
			result = jx_cmd_run(cmd->sub, &layer);

			/* Ignore "continue" and stay in loop.  For anything
			 * else other than NULL, exit the loop.
			 */
			if (result && result->ret == &jx_cmd_continue) {
				free(result);
				result = NULL;
			}
			if (result) {
				jx_break(scan);
				break;
			}

			/* Remove the "this" layer */
			jx_context_free(layer);
		}
	}

	/* Free the array */
	jx_free(array);

	/* If we got a "break" pseudo-error, ignore it.  Otherwise (real error
	 * or "return" pseudo-error) return it.
	 */
	if (result && result->ret == &jx_cmd_break) {
		free(result);
		result = NULL;
	}
	return result;
}

static jxcmd_t *try_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcmd_t	*parsed;
	char		*str = NULL;
	jxsrc_t	parensrc;

	/* Allocate the jxcmd_t for it */
	parsed = jx_cmd(src, &jcn_try);

	/* Get the "try" statements */
	parsed->sub = jx_cmd_parse_curly(src, referr);
	if (*referr)
		goto CleanUpAfterError;

	/* Expect "catch" */
	if (strncasecmp(src->str, "catch", 5) || !strchr(" \t\n\r({", src->str[5])) {
		*referr = jx_cmd_error(src->str, "Missing \"%s\"", "catch");
		goto CleanUpAfterError;
	}
	src->str += 5;
	jx_cmd_parse_whitespace(src);

	/* Optional name within parentheses */
	if (*src->str == '(') {
		/* Get the parenthesized expression */
		str = jx_cmd_parse_paren(src);

		/* It should be a single name */
		parensrc.str = str;
		parsed->key = jx_cmd_parse_key(&parensrc, 1);
		if (*parensrc.str) {
			*referr = jx_cmd_error(src->str, "The argument to \"%s\" should be a single name", "catch");
			goto CleanUpAfterError;
		}

		/* Free the string */
		free(str);
		str = NULL;
	}

	/* Get the "catch" statements */
	parsed->more = jx_cmd_parse_curly(src, referr);
	if (*referr)
		goto CleanUpAfterError;

	/* !!! I supposed I could test for a "finally" statement */

	/* Return it */
	return parsed;

CleanUpAfterError:
	if (str)
		free(str);
	jx_cmd_free(parsed);
	return NULL;
}

static jxcmdout_t *try_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	jxcmdout_t *result;
	jxcontext_t *caught;
	jx_t *obj, *contextobj;
	jxfile_t *jf;
	int lineno;
	char *scan;

	/* Run the "try" statements.  For any result other than an error,
	 * just return it.
	 */
	result = jx_cmd_run(cmd->sub, refcontext);
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
		obj = jx_object();
		jf = jx_file_containing(result->where, &lineno);
		if (jf) {
			jx_append(obj, jx_key("filename", jx_string(jf->filename, -1)));
			jx_append(obj, jx_key("line", jx_from_int(lineno)));
		}
		for (scan = result->text; isalnum(*scan); scan++) {
		}
		if (*scan == ':') {
			jx_append(obj, jx_key("key", jx_string(result->text, (scan - result->text))));
			scan++;
		} else {
			scan = result->text;
		}
		jx_append(obj, jx_key("message", jx_string(scan, -1)));

		/* Make that object be inside another object, using key as the
		 * the member name.
		 */
		contextobj = jx_object();
		jx_append(contextobj, jx_key(cmd->key, obj));

		/* Stuff it in a context, using the key as the name */
		caught = jx_context(*refcontext, contextobj, 0);

		/* Run the "catch" block with this context */
		result = jx_cmd_run(cmd->more, &caught);

		/* Free the context. This also frees the data allocated above.*/
		jx_context_free(caught);
	} else {
		/* Just run the "catch" block with the same context */
		result = jx_cmd_run(cmd->more, &caught);
	}

	return result;
}


static jxcmd_t *throw_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcmd_t	*parsed;
	const char	*end, *err, *pct;
	jxcalc_t	*jc;

	/* Allocate the jxcmd_t for it */
	parsed = jx_cmd(src, &jcn_throw);

	/* Parse the first (only?) argument. */
	jc = NULL;
	end = err = NULL;
	if (*src->str && *src->str != ';' && *src->str != '}') {
		jc = jx_calc_parse(src->str, &end, &err, 0);
		if (!jc || jc->op != JXOP_LITERAL)
			goto BadArgs;
		src->str = end;

		/* Allow an error code */
		if (jc && jc->u.literal->type == JX_NUMBER) {
			/* Store the number in 'var' */
			parsed->var = jx_int(jc->u.literal);

			/* Don't need this expression anymore */
			jx_calc_free(jc);
			jc = NULL;

			/* Try for another expression, if "," */
			if (*end == ',') {
				src->str = end + 1;
				jc = jx_calc_parse(src->str, &end, &err, 0);
				if (!jc || err || jc->op != JXOP_LITERAL)
					goto BadArgs;
				src->str = end;
			}
		}
	}

	/* Allow error text (a string literal).  If none, then use "Throw" */
	if (!jc)
		parsed->key = strdup("throw");
	else if (jc->u.literal->type != JX_STRING)
		goto BadArgs;
	else {
		/* Store the string in 'key' */
		parsed->key = strdup(jc->u.literal->text);

		/* Don't need this expression anymore */
		jx_calc_free(jc);
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
		jc = jx_calc_parse(src->str, &end, &err, 0);
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
		jx_calc_free(jc);
	jx_cmd_free(parsed);
	*referr = jx_cmd_error(src->str, "Bad parameters to %s", "throw");
	return NULL;
}

static jxcmdout_t *throw_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	jxcmdout_t *result;
	jx_t	*arg;

	/* If there's an argument, evaluate it. */
	arg = NULL;
	if (cmd->calc) {
		arg = jx_calc(cmd->calc, *refcontext, NULL);
	}

	/* Always return an error -- maybe with an argument */
	result = jx_cmd_error(cmd->where, cmd->key, arg ? arg->text : "");

	/* Clean up */
	if (arg)
		jx_free(arg);

	return result;
}

/* This is a helper function for global/local var/const declarations */
static jxcmd_t *gvc_parse(jxsrc_t *src, jxcmdout_t **referr, jxcmd_t *cmd)
{
	jxcmd_t *first = cmd;
	const char	*end, *err;

	/* Expect a name possibly followed by ":type" and/or "=expr" */
	for (;;) {
		cmd->key = jx_cmd_parse_key(src, 1);
		if (!cmd->key) {
			*referr = jx_cmd_error(src->str, "Name expected after %s", cmd->name->name);
			jx_cmd_free(first);
			return NULL;
		}
		jx_cmd_parse_whitespace_or_type(src, NULL);
		if (*src->str == '=') {
			err = NULL;
			src->str++;
			cmd->calc = jx_calc_parse(src->str, &end, &err, 0);
			src->str = end;
			if (err) {
				*referr = jx_cmd_error(src->str, "%s", err);
				jx_cmd_free(first);
				return NULL;
			}
		}

		/* That may be followed by a comma and another declaration */
		if (*src->str == ',') {
			src->str++;
			jx_cmd_parse_whitespace(src);
			cmd->more = jx_cmd(src, first->name);
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

static jxcmdout_t *gvc_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	jx_t	*value, *error;
	jxcmd_t *each;

	/* A single statement can declare multiple vars/consts */
	error = NULL;
	for (each = cmd; each; each = each->more) {
		/* Evaluate the value. If error, remember it */
		value = NULL;
		if (each->calc) {
			value = jx_calc(each->calc, *refcontext, NULL);
			if (jx_is_error(value)) {
				if (error)
					free(value);
				else
					error = value;
				value = NULL;
			}
		}
		if (!value)
			value = jx_null();

		/* Add it to the context */
		if (!jx_context_declare(refcontext, each->key, value, each->flags)) {
			/* Duplicate! */
			jx_free(value);
			return jx_cmd_error(each->where, "redeclare:Duplicate %s \"%s\"",
				(each->flags & JX_CONTEXT_CONST) ? "const" : "var",
				each->key);
		}
	}

	/* If we encountered an error in an initializer, return it */
	if (error) {
		jxcmdout_t *result;
		result = jx_cmd_error(cmd->where, "%s", error->text);
		jx_free(error);
		return result;
	}

	/* Success! */
	return NULL;
}

static jxcmd_t *break_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcmd_t *cmd = jx_cmd(src, &jcn_break);

	/* No arguments or other components, but we still need to skip ";" */
	jx_cmd_parse_whitespace(src);
	if (*src->str == ';')
		src->str++;
	return cmd;
}

static jxcmdout_t *break_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	/* Return a "break" pseudo-error */
	jxcmdout_t *result = jx_cmd_error(cmd->where, "");
	result->ret = &jx_cmd_break;
	return result;
}

static jxcmd_t *continue_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcmd_t *cmd = jx_cmd(src, &jcn_continue);

	/* No arguments or other components, but we still need to skip ";" */
	jx_cmd_parse_whitespace(src);
	if (*src->str == ';')
		src->str++;
	return cmd;
}

static jxcmdout_t *continue_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	/* Return a "continue" pseudo-error */
	jxcmdout_t *result = jx_cmd_error(cmd->where, "");
	result->ret = &jx_cmd_continue;
	return result;
}

static jxcmd_t *var_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcmd_t *cmd = jx_cmd(src, &jcn_var);
	cmd->flags = JX_CONTEXT_VAR;
	return gvc_parse(src, referr, cmd);
}

static jxcmdout_t *var_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	return gvc_run(cmd, refcontext);
}

static jxcmd_t *const_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcmd_t *cmd = jx_cmd(src, &jcn_var);
	cmd->flags = JX_CONTEXT_CONST;
	return gvc_parse(src, referr, cmd);
}

static jxcmdout_t *const_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	return gvc_run(cmd, refcontext);
}

/* Output a description of a function */
static void describefn(jxfunc_t *f)
{
	jx_t	*params = NULL;

	if (f->fn)
		jx_user_printf(NULL, "normal", "builtin ");
	if (f->agfn)
		jx_user_printf(NULL, "normal", "aggregate ");
	jx_user_printf(NULL, "normal", "function %s", f->name);
	if (f->args)
		jx_user_printf(NULL, "normal", "(%s)", f->args);
	else {
		jx_user_ch('(');
		for (params = f->userparams->first; params; params = params->next) /* undeferred */
			jx_user_printf(NULL, "normal", "%s%s", params->text, params->next ? ", " : ""); /* undeferred */
		jx_user_ch(')');
	}
	if (f->returntype)
		jx_user_printf(NULL, "normal", ":%s", f->returntype);
	jx_user_ch('\n');
}

static jxcmd_t *function_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	char	*fname;
	jxsrc_t paren; /* Used for scanning parameter source */
	jx_t	*params = NULL;
	jxcmd_t *body = NULL;
	char	*returntype = NULL;;

	paren.buf = NULL;

	/* Function name */
	fname = jx_cmd_parse_key(src, 1);
	if (!fname) {
		/* Describe all user-defined functions */
		jxfunc_t *f = jx_calc_function_first();
		for (; f; f = f->other) {
			if (!f->fn)
				describefn(f);
		}
		return NULL;
	}

	/* Parameter list (the parenthesized text) */
	paren.buf = paren.str = jx_cmd_parse_paren(src);
	if (!paren.buf) {
		/* No parameter list, so just describe the named function and
		 * return NULL.
		 */
		jxfunc_t *f = jx_calc_function_by_name(fname);
		if (!f) {
			*referr = jx_cmd_error(src->str, "Unknown function \"%s\"", fname);
			goto Error;
		}

		/* Output a description of the function */
		describefn(f);

		free(fname);
		return NULL;
	}
	paren.size = strlen(paren.buf);

	/* Parameters within the parenthesized text */
	params = jx_object();
	jx_cmd_parse_whitespace(&paren);
	while (*paren.str) {
		char	*pname;
		jx_t	*defvalue;

		/* Parameter name */
		pname = jx_cmd_parse_key(&paren, 0);
		if (!pname) {
			*referr = jx_cmd_error(src->str, "Missing parameter name");
			goto Error;
		}

		/* Possibly a type declaration */
		jx_cmd_parse_whitespace_or_type(&paren, NULL);

		/* If followed by = then use a default */
		if (*paren.str == '=') {
			jxcalc_t *calc;
			const char	*end, *err;

			/* Move past the '=' */
			paren.str++;

			/* Parse the expression */
			err = NULL;
			calc = jx_calc_parse(paren.str, &end, &err, 0);
			if (err) {
				*referr = jx_cmd_error(src->str, "%s in default value", err);
				goto Error;
			}
			if (*end && *end != ',') {
				*referr = jx_cmd_error(src->str, "Syntax error near %.10s", end);
				goto Error;
			}
			paren.str = end;

			/* Evaluate the expression */
			defvalue = jx_calc(calc, NULL, NULL);
			if (jx_is_error(defvalue)) {
				if (defvalue->first)
					*referr = jx_cmd_error((const char *)defvalue->first, "%s", defvalue->text);
				else
					*referr = jx_cmd_error(src->str, "%s", defvalue->text);
				goto Error;
			}

			/* Free the expression */
			jx_calc_free(calc);
		} else {
			/* Use null as the default value */
			defvalue = jx_null();
		}

		/* Add the parameter to the params object */
		jx_append(params, jx_key(pname, defvalue));

		/* Free the name */
		free(pname);

		/* If followed by comma, skip the comma */
		if (*paren.str == ',') {
			paren.str++;
			jx_cmd_parse_whitespace(&paren);
		}
	}

	/* Parentheses may be followed by a return type declaration */
	jx_cmd_parse_whitespace_or_type(src, &returntype);

	/* Body -- if no body, that's okay */
	if (*src->str == '{')
		body = jx_cmd_parse_curly(src, referr);
	else if (jx_calc_function_by_name(fname)) {
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
		if (jx_calc_function_user(fname, params, (char *)paren.buf, returntype, body)) {
			/* Tried to redefine a built-in, which isn't allowed. */
			*referr = jx_cmd_error(src->str, "Can't redefine built-in function \"%s\"", fname);
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
		free((char *)paren.buf);
	if (params)
		jx_free(params);
	if (returntype)
		free(returntype);
	if (body)
		jx_cmd_free(body);
	return NULL;
}

static jxcmdout_t *function_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	/* Can't happen */
	return NULL;
}

static jxcmd_t *return_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcalc_t *calc;
	const char	*end, *err;
	jxcmd_t *cmd;
	jxsrc_t start;

	/* Allocate a cmd */
	start = *src;

	/* The return value is optional */
	jx_cmd_parse_whitespace(src);
	if (*src->str && *src->str != ';' && *src->str != '}') {
		/* Parse the expression */
		err = NULL;
		calc = jx_calc_parse(src->str, &end, &err, 0);
		if (err) {
			*referr = jx_cmd_error(src->str, "%s", err);
			if (calc)
				jx_calc_free(calc);
			return NULL;
		}
		if (*end && (*end != ';' && *end != '}')) {
			*referr = jx_cmd_error(src->str, "Syntax error near %.10s", end);
			if (calc)
				jx_calc_free(calc);
			return NULL;
		}
		src->str = end;

	} else {
		/* With no expression, assume "null */
		calc = jx_calc_parse("null", NULL, NULL, 0);
	}

	/* Move past the ';', if there is one */
	if (*src->str == ';')
		src->str++;
	jx_cmd_parse_whitespace(src);

	/* Build the command, and return it */
	cmd = jx_cmd(&start, &jcn_return);
	cmd->calc = calc;
	return cmd;
}

static jxcmdout_t *return_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	/* Return a 'return" pseudo-error, returning whatever jx_calc() gives
	 * us.  If jx_calc() returns an actual error, so be it.  If that
	 * error doesn't include the position of the error, then use the
	 * position of this "return" command.
	 */
	jxcmdout_t *err = jx_cmd_error(cmd->where, "");
	err->ret = jx_calc(cmd->calc, *refcontext, NULL);
	if (jx_is_error(err->ret) && !err->ret->first)
		err->ret->first = (jx_t *)cmd->where;
	return err;
}

static jxcmd_t *switch_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcmd_t	*parsed;
	char	*str;
	const char *end, *err = NULL;

	/* Skip leading whitespace */
	jx_cmd_parse_whitespace(src);

	/* Allocate the jxcmd_t for it */
	parsed = jx_cmd(src, &jcn_switch);

	/* Get the condition */
	str = jx_cmd_parse_paren(src);
	if (!str) {
		*referr = jx_cmd_error(src->str, "Missing \"%s\" expression", "switch");
		return parsed;
	}

	/* Parse the expression */
	parsed->calc = jx_calc_parse(str, &end, &err, 0);
	if (err || *end || !parsed->calc) {
		free(str);
		if (err)
			*referr = jx_cmd_error(src->str, "%s", err);
		else
			*referr = jx_cmd_error(src->str, "Syntax error in \"%s\" expression", "switch");
		return parsed;
	}
	free(str);

	/* Get the "body" statements */
	parsed->sub = jx_cmd_parse_curly(src, referr);
	if (*referr)
		return parsed;

	/* Return it */
	return parsed;
}

static jxcmdout_t *switch_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	jxcontext_t *layer;
	jx_t	*switchcase;
	jxcmdout_t *result;

	/* Evaluate the expression */
	switchcase = jx_calc(cmd->calc, *refcontext, NULL);

	/* Add a context for store the "switchcase" variable */
	layer = jx_context(*refcontext, jx_object(), 0);

	/* Store the value in the variable */
	jx_append(layer->data, jx_key("switchcase", switchcase));

	/* Execute the body of the switch */
	result = jx_cmd_run(cmd->sub, &layer);

	/* If exited with a "break", ignore it */
	if (result && result->ret == &jx_cmd_break) {
		free(result);
		result = NULL;
	}

	/* Clean up */
	jx_context_free(layer);

	return result;
}

static jxcmd_t *case_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcmd_t	*parsed;
	char	*str;
	const char *end, *err = NULL;
	int	len, quote, nest, escape;

	/* Skip leading whitespace */
	jx_cmd_parse_whitespace(src);

	/* Allocate the jxcmd_t for it */
	parsed = jx_cmd(src, &jcn_case);

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
		*referr = jx_cmd_error(src->str, "Missing or malformed \"%s\" expression", "case");
		return parsed;
	}
	str = (char *)malloc(len + 1);
	strncpy(str, src->str, len);
	str[len] = '\0';

	/* Parse the case.  */
	parsed->calc = jx_calc_parse(str, &end, &err, 0);
	if (err || *end || !parsed->calc) {
		free(str);
		if (err)
			*referr = jx_cmd_error(src->str, "%s", err);
		else
			*referr = jx_cmd_error(src->str, "Syntax error in \"%s\" expression", "case");
		return parsed;
	}
	free(str);

	/* Move past the ":" */
	src->str += len + 1;

	/* Return it */
	return parsed;
}

static jxcmdout_t *case_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	jx_t	*switchcase;
	int	match;

	/* Fetch the "switchcase" value.  Note that we only look in the top
	 * context layer so that if switch statements are nested, we don't see
	 * the outer one.  Use jx_by_key() instead of jx_context_by_key().
	 */
	switchcase = jx_by_key((*refcontext)->data, "switchcase");
	if (!switchcase)
		return jx_cmd_error(cmd->where, "case:Can't use \"%s\" outside of \"%s\"", "case", "switch");

	/* If "null" then continue with next command */
	if (jx_is_null(switchcase))
		return NULL;

	/* Compare the case value to switchcase.
	 * 
	 * One optimization: If the value is a literal then we don't bother
	 * calling jx_calc(), mostly so we can avoid allocating and freeing
	 * the value.
	 */
	if (cmd->calc->op == JXOP_LITERAL) {
		match = jx_equal(cmd->calc->u.literal, switchcase);
	} else {
		jx_t *thiscase = jx_calc(cmd->calc, *refcontext, NULL);
		match = jx_equal(thiscase, switchcase);
		jx_free(thiscase);
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
		jx_append((*refcontext)->data, jx_key("switchcase", jx_null()));
		return NULL;
	} else {
		/* No match!  Leave "switchcase" unchanged, and skip to the
		 * next "case" or "default" statement.
		 */
		jxcmdout_t *result = jx_cmd_error(cmd->where, "");
		result->ret = &jx_cmd_case_mismatch;
		return result;
	}
}

static jxcmd_t *default_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcmd_t	*parsed;

	/* Skip leading whitespace */
	jx_cmd_parse_whitespace(src);

	/* Allocate the jxcmd_t for it */
	parsed = jx_cmd(src, &jcn_default);

	/* Ends with a colon */
	if (*src->str != ':') {
		*referr = jx_cmd_error(src->str, "Syntax error in \"%s\"", "default");
		return parsed;
	}
	src->str++;

	/* Return it */
	return parsed;
}

static jxcmdout_t *default_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	/* The "default" command always continues to the next command */
	return NULL;
}

static jxcmd_t *void_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcalc_t *calc;
	const char	*end, *err;
	jxcmd_t *cmd;
	jxsrc_t start;

	/* Allocate a cmd */
	start = *src;

	/* The expression is mandatory */
	jx_cmd_parse_whitespace(src);
	if (!*src->str || *src->str == ';' || *src->str == '}') {
		jx_cmd_error(src->str, "The \"%s\" command requires an expression", "void");
	}

	/* Parse the expression */
	err = NULL;
	calc = jx_calc_parse(src->str, &end, &err, 0);
	if (err) {
		*referr = jx_cmd_error(src->str, "%s", err);
		if (calc)
			jx_calc_free(calc);
		return NULL;
	}
	if (*end && (*end != ';' && *end != '}')) {
		*referr = jx_cmd_error(src->str, "Syntax error near %.10s", end);
		if (calc)
			jx_calc_free(calc);
		return NULL;
	}
	src->str = end;

	/* Move past the ';', if there is one */
	if (*src->str == ';')
		src->str++;
	jx_cmd_parse_whitespace(src);

	/* Build the command, and return it */
	cmd = jx_cmd(&start, &jcn_void);
	cmd->calc = calc;
	return cmd;
}

static jxcmdout_t *void_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	/* Evaluate the expression but return NULL */
	jx_free(jx_calc(cmd->calc, *refcontext, NULL));
	return NULL;
}

static jxcmd_t *explain_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	const char	*end, *err;
	jxcmd_t *cmd;

	/* Allocate a cmd */
	cmd = jx_cmd(src, &jcn_explain);

	/* Three ways to go: "explain" explains the default table, "explain?"
	 * says where the default table is located, and "explain expr" explains
	 * the result of an expression.
	 */
	jx_cmd_parse_whitespace(src);
	if (!*src->str || *src->str == ';' || *src->str == '}') {
		/* Use the default */
	} else if (*src->str == '?') {
		/* Use the default, but suppress the actual "explain" table */
		cmd->var = 1;
		src->str++;
		jx_cmd_parse_whitespace(src);
	} else {
		/* Use an expression */
		err = NULL;
		cmd->calc = jx_calc_parse(src->str, &end, &err, 0);
		if (err) {
			*referr = jx_cmd_error(src->str, "%s", err);
			jx_cmd_free(cmd);
			return NULL;
		}
		src->str = end;
	}

	/* Detect cruft after the arguments */
	if (*src->str && (*src->str != ';' && *src->str != '}')) {
		*referr = jx_cmd_error(src->str, "Syntax error near %.10s", end);
		jx_cmd_free(cmd);
		return NULL;
	}

	/* Move past the ';', if there is one */
	if (*src->str == ';')
		src->str++;
	jx_cmd_parse_whitespace(src);

	/* Return the command */
	return cmd;
}

static jxcmdout_t *explain_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	jx_t	*table, *mustfree, *columns;
	char	*expr;

	/* Is there an expression, explicitly naming a table? */
	if (!cmd->calc) {
		/* No, so look for a default table */
		table = jx_context_default_table(*refcontext, &expr);
		mustfree = NULL;

		/* If no table, say so */
		if (!table)
			return jx_cmd_error(cmd->where, "noDefTable:No default table");
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
		table = jx_calc(cmd->calc, *refcontext, NULL);
		mustfree = table;
		expr = NULL;
	}

	/* Detect errors */
	if (jx_is_error(table)) {
		jxcmdout_t *out = jx_cmd_error(cmd->where, "%s", table->text);
		jx_free(table);
		return out;
	}
	if (!jx_is_table(table)) {

		jxcmdout_t *out = jx_cmd_error(cmd->where, "explainNotTable:Not a table");
		jx_free(table);
		return out;
	}

	/* Output the explain results, unless the parameter text was just "?" */
	columns = NULL;
	if (!cmd->var) {
		/* If it is a deferred array, then we might want to check only
		 * some of the rows.
		 */
		if (jx_is_deferred_array(table)) {
			int deferexplain = 0;
			jx_t *jc = jx_by_key(jx_config, "deferexplain");
			if (jc && jc->type == JX_NUMBER)
				deferexplain = jx_int(jc);
			if (deferexplain >= 1) {
				for (table = jx_first(table); deferexplain > 0 && table; deferexplain--, table = jx_next(table))
					columns = jx_explain(columns, table, 0);
				jx_break(table);
			}
		}
		if (!columns) {
			for (table = jx_first(table); table; table = jx_next(table))
				columns = jx_explain(columns, table, 0);
		}
		jx_print(columns, NULL);
	}

	/* If we have an expr for the default table, output it */
	if (expr)
		jx_user_printf(NULL, "normal", "%s\n", expr);

	/* Clean up */
	if (columns)
		jx_free(columns);
	if (mustfree)
		jx_free(mustfree);
	if (expr)
		free(expr);

	/* Done! */
	return NULL;
}

static jxcmd_t *file_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	const char	*end, *err;
	jxcmd_t *cmd;

	/* Allocate a cmd */
	cmd = jx_cmd(src, &jcn_file);

	/* Many possible ways to invoke this.  Most commands have a strict
	 * syntax, but file is intended mostly for interactive use and should
	 * be user-friendly.  The main ways to invoke it are:
	 *   file       List all files, with the current one highlighted
	 *   file +	Move to the next file
	 *   file -	Move to the previous file
	 *   file word	Move to the named file.  If new, append it.
	 *   file (expr)Move to the result of expr
	 */
	jx_cmd_parse_whitespace(src);
	if (!*src->str || *src->str == ';' || *src->str == '}') {
		/* "file" with no arguments */
	} else if (*src->str == '+' || *src->str == '-') {
		/* Verify that it's ONLY + or -, not part of a calc expression*/
		char ch[2];
		ch[0] = *src->str++;
		ch[1] = '\0';
		jx_cmd_parse_whitespace(src);
		if (*src->str && *src->str != ';' && *src->str != '}') {
			*referr = jx_cmd_error(src->str, "Bad use of \"%s\" or \"%s\"", "file+", "file-");
			return NULL;
		}
		cmd->key = strdup(ch);
	} else if (*src->str == '(') {
		/* Get the expression */
		char *str = jx_cmd_parse_paren(src);
		if (!str) {
			*referr = jx_cmd_error(src->str, "Missing ) in \"%s\" expression", "file");
			return cmd;
		}

		/* Parse the expression */
		cmd->calc = jx_calc_parse(str, &end, &err, 0);
		if (err || *end || !cmd->calc) {
			free(str);
			if (err)
				*referr = jx_cmd_error(src->str, "%s", err);
			else
				*referr = jx_cmd_error(src->str, "Syntax error in \"%s\" expression", "file");
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
		jx_cmd_parse_whitespace(src);
	}

	/* Move past the ';', if there is one */
	if (*src->str == ';')
		src->str++;
	jx_cmd_parse_whitespace(src);

	/* Return the command */
	return cmd;
}

static jxcmdout_t *file_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	jx_t *files, *elem;
	int	current = JX_CONTEXT_FILE_SAME;

	/* Determine what type of "file" invocation this is */
	if (cmd->calc) {
		/* "file (calc) -- Evaluate the expression. */
		jx_t *result = jx_calc(cmd->calc, *refcontext, NULL);

		/* If we got an error, then return the error */
		if (jx_is_error(result)) {
			jxcmdout_t *err = jx_cmd_error(cmd->where, "%s", result->text);
			jx_free(result);
			return err;
		}

		/* If it's a number, then select a file by index */
		if (result->type == JX_NUMBER) {
			current = jx_int(result);
			files = jx_context_file(*refcontext, NULL, 0, &current);
			jx_free(result);
		} else if (result->type == JX_STRING) {
			current = JX_CONTEXT_FILE_NEXT;
			files = jx_context_file(*refcontext, result->text, 0, &current);
			jx_free(result);
		} else {
			jx_free(result);
			return jx_cmd_error(cmd->where, "fileExpr:file expressions should return a number or string.");
		}
	} else if (!cmd->key) {
		/* "file" with no args -- display the current filename */
		files = jx_context_file(*refcontext, NULL, 0, &current);
	} else if (!strcmp(cmd->key, "+")) {
		/* "file +" -- Move to the next file in the list */
		current = JX_CONTEXT_FILE_NEXT;
		files = jx_context_file(*refcontext, NULL, 0, &current);
	} else if (!strcmp(cmd->key, "-")) {
		/* "file -" -- Move to the previous file in the list */
		current = JX_CONTEXT_FILE_PREVIOUS;
		files = jx_context_file(*refcontext, NULL, 0, &current);
	} else {
		/* "file filename" -- Move to the named file */
		current = JX_CONTEXT_FILE_NEXT;
		files = jx_context_file(*refcontext, cmd->key, 0, &current);
	}

	/* After all that, display the current file's name */
	elem = jx_by_index(files, current);
	files = jx_by_key(elem, "filename");
	if (!files || files->type != JX_STRING)
		jx_user_printf(NULL, "normal", "%s\n", "(no files)");
	else
		jx_user_printf(NULL, "normal", "%s\n", files->text);
	jx_break(files);
	jx_break(elem);

	/* Return success always */
	return NULL;
}


static jxcmd_t *import_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	const char	*end;
	char	*filename;
	FILE	*fp;
	jxcmd_t *code, *cmd;
	jxsrc_t start = *src;

	/* Parse the name. */
	jx_cmd_parse_whitespace(src);
	for (end = src->str; *end && *end != ';' && *end != '}'; end++){
	}
	while (end > src->str && end[-1] == ' ')
		end--;
	filename = (char *)malloc(end - src->str + 4);
	strncpy(filename, src->str, end - src->str);
	filename[end - src->str] = '\0';
	src->str = end;

	/* If the filename has no extension, then assume ".jx" */
	end = strrchr(filename, '/');
	if (end)
		end++;
	else
		end = filename;
	end = strchr(end, '.');
	if (!end)
		strcat(filename, ".jx");

	/* For security's sake, make sure the name doesn't start with "/"
	 * or contain "../"
	 */
	if (filename[0] == '/' || strstr(filename, "../")) {
		*referr = jx_cmd_error(start.str, "Unsafe file name to import: \"%s\"", filename);
		return NULL;
	}

	/* If the file doesn't exist or is unreadable, fail */
	if (access(filename, F_OK) < 0) {
		*referr = jx_cmd_error(start.str, "Import file \"%s\" does not exist", filename);
		return NULL;
	}
	fp = fopen(filename, "r");
	if (!fp) {
		*referr = jx_cmd_error(start.str, "Import file \"%s\" is unreadable", filename);
		return NULL;
	}
	fclose(fp);

	/* Load the file. If it contains any code other than function
	 * definitions, then stow it in a cmd to run later; this is necessary
	 * for declaring variables and constants, because those get stored in
	 * the context but we don't have a context at parse time.
	 */
	cmd = NULL;
	code = jx_cmd_parse_file(filename);
	if (code && code != JX_CMD_ERROR) {
		cmd = jx_cmd(&start, &jcn_import);
		cmd->sub = code;
	}

	/* Move past the ';', if there is one */
	jx_cmd_parse_whitespace(src);
	if (*src->str == ';')
		src->str++;
	jx_cmd_parse_whitespace(src);

	/* Probably nothing left to do at runtime... except maybe vars */
	return cmd;
}

static jxcmdout_t *import_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	/* Declare variables and such */
	return jx_cmd_run(cmd->sub, refcontext);
}


static jxcmd_t *plugin_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	size_t len;
	char	quote;
	char	*str, *settings;
	jx_t	*err, *section;

	/* Find the end of the command */
	jx_cmd_parse_whitespace(src);
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
	err = jx_plugin_load(str);
	if (err) {
		if (err->first)
			*referr = jx_cmd_error((char *)err->first, "%s", err->text);
		else
			*referr = jx_cmd_error(src->str, "%s", err->text);
		return NULL;
	}

	/* Process the settings, if any */
	if (*settings) {
		/* Find where this plugins settings are stored */
		section = jx_by_key(jx_config, "plugin");
		section = jx_by_key(section, str);
		if (!section) {
			*referr = jx_cmd_error(src->str, "The \"%s\" plugin doesn't use settings", str);
			return NULL;
		}

		/* Adjust the settings */
		err = jx_config_parse(section, settings, NULL);
		if (err) {
			if (err->first)
				*referr = jx_cmd_error((char *)err->first, "%s", err->text);
			else
				*referr = jx_cmd_error(src->str, "%s", err->text);
			return NULL;
		}

	}

	/* No action needed at runtime */
	return NULL;
}

static jxcmdout_t *plugin_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	/* Plugins are loaded at parse time, not run time */
	return NULL;
}

/* Print a value.  If it's a string, print it without quotes or backslashes. */
static jxcmd_t *print_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcmd_t *cmd;
	jxsrc_t start;
	jxcalc_t *item, *list;
	const char	*err;

	start = *src;
	jx_cmd_parse_whitespace(src);
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
		item = jx_calc_parse(src->str, &src->str, &err, FALSE);
		if (!item || err || (*src->str && !strchr(";},", *src->str))) {
			if (list)
				jx_calc_free(list);
			*referr = jx_cmd_error(start.str, err ? err : "printSyntax:Syntax error in \"%s\" expression", "print");
			return NULL;
		}
		list = jx_calc_list(list, item);
	} while (*src->str++ == ',');

	/* Build the command */
	cmd = jx_cmd(&start, &jcn_print);
	cmd->calc = list;
	return cmd;
}

static jxcmdout_t *print_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	jx_t	*list, *scan;
	char	lastchar;

	/* Evaluate the expression list */
	list = jx_calc(cmd->calc, *refcontext, NULL);

	/* If error, then return the error */
	if (jx_is_error(list)) {
		jxcmdout_t *err = jx_cmd_error(cmd->where, "%s", list->text);
		jx_free(list);
		return err;
	}

	/* Otherwise output the results, all strung together without any
	 * added spaces or anything.  For strings, output the string literally.
	 */
	lastchar = '\n';
	for (scan = jx_first(list); scan; scan = jx_next(scan)) {
		if (scan->type == JX_STRING) {
			jx_user_printf(NULL, "normal", "%s", scan->text, stdout);
			if (*scan->text)
				lastchar = scan->text[strlen(scan->text) - 1];
		} else {
			char *tmp = jx_serialize(scan, NULL);
			jx_user_printf(NULL, "normal", "%s", tmp);
			free(tmp);
			lastchar = 'x'; /* Never empty, never '\n' */
		}
	}

	/* If the last character wasn't a newline, remember that. */
	jx_print_incomplete_line = (lastchar != '\n');

	/* Clean up */
	jx_free(list);
	return NULL;
}

/* Set an option. */
static jxcmd_t *set_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxcmd_t *cmd;
	jxsrc_t start;
	jxcalc_t *calc;
	char	*str;
	const char *end, *err;
	size_t	len;

	start = *src;
	jx_cmd_parse_whitespace(src);

	/* The options settings can be either explicit text, or a parenthesized
	 * expression that returns a string.
	 */
	if (*src->str == '(') {
		/* Parenthesized expression -- Get it in a string */
		str = jx_cmd_parse_paren(src);
		if (!str) {
			*referr = jx_cmd_error(src->str, "Missing ) in \"%s\" expression", "set");
			return NULL;
		}

		/* Parse it */
		calc = jx_calc_parse(str, &end, &err, 0);
		if (!calc || err || (*src->str && !strchr(";},", *src->str))) {
			free(str);
			if (err)
				*referr = jx_cmd_error(start.str, "%s", err);
			else
				*referr = jx_cmd_error(start.str, "setSyntax:Syntax error in \"%s\" expression", "set");
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
	cmd = jx_cmd(&start, &jcn_set);
	cmd->calc = calc;
	cmd->key = str;
	return cmd;
}

static jxcmdout_t *set_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	jx_t *result, *section, *conferr;
	char	*str;

	/* Are we using an expression to generate the settings on-the-fly? */
	if (cmd->calc) {
		/* Evaluate it */
		result = jx_calc(cmd->calc, *refcontext, NULL);
		if (jx_is_error(result)) {
			jxcmdout_t *err = jx_cmd_error(cmd->where, "%s", result->text);
			jx_free(result);
			return err;
		}

		/* Value must be a string */
		if (result->type != JX_STRING) {
			jx_free(result);
			return jx_cmd_error(cmd->where, "setString:set expression must return a string");
		}

		/* Use the string's text */
		str = result->text;
	} else {
		/* Use the literal text */
		str = cmd->key;
		result = NULL;
	}

	/* Parse it, and store the changes.  */
	section = jx_by_key(jx_config, jx_text_by_key(jx_system, "runmode"));
	conferr = jx_config_parse(section, str, NULL);
	if (conferr) {
		jxcmdout_t *err = jx_cmd_error(cmd->where, "%s", conferr->text);
		jx_free(conferr);
		if (result)
			jx_free(result);
		return err;
	}

	/* Make the changes effective in the format */
	jx_format_set(NULL, NULL);

	/* Clean up */
	if (result)
		jx_free(result);
	return NULL;
}


/* Handle an assignment or output expression */
static jxcmdout_t *calc_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	/* Calculate the result of the expression.   If it's an assignment,
	 * then this will do the assignment too.
	 */
	jx_t *result = jx_calc(cmd->calc, *refcontext, NULL);

	/* If we got an error ("null" with text), then convert to jxcmdout_t */
	if (jx_is_error(result)) {
		jxcmdout_t *err = jx_cmd_error(result->first ? (const char *)result->first : cmd->where, "%s", result->text);
		jx_free(result);
		return err;
	}

	/* If not an assignment, then it's an output.  Output it! */
	if (cmd->calc->op != JXOP_ASSIGN
	 && cmd->calc->op != JXOP_APPEND
	 && cmd->calc->op != JXOP_MAYBEASSIGN) {
		/* Print the result */
		jx_print(result, NULL);

		/* Give the user interface a chance to save the result.  If
		 * it doesn't want to do that, then free it.
		 */
		if (!jx_user_result(result))
			jx_free(result);
	} else {
		/* For assignment, a copy of the result is already saved to
		 * we can discard it.
		 */
		jx_free(result);
	}

	return NULL;
}
