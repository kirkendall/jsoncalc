#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <regex.h>
#include <assert.h>
#include <jsoncalc.h>


/* This is by far the largest single source file in the whole library.
 * It defines the json_calc_parse() function, which is responsible for
 * parsing "calc" expressions for later use via the json_calc() function
 * defined in calc.c
 *
 * The parser is a simple shift-reduce parser.  This type of parser works
 * great for operators, so I made nearly all syntax tokens be operators.
 * Things that AREN'T operators: Literals, object generators, array generators,
 * subscripts, and a few oddball things like elements of a SELECT statement. 
 */

/* These macros make jsoncalc_t trees easier to navigate */
#define LEFT u.param.left
#define RIGHT u.param.right
#define JC_IS_STRING(jc)  ((jc)->op == JSONOP_LITERAL && (jc)->u.literal->type == JSON_STRING)

/* This represents a token from the "calc" expression */
typedef struct {
	jsonop_t op;
	int len;
	const char *full;
} token_t;

/* This is used as the expression parsing stack. */
typedef struct {
	jsoncalc_t *stack[100];
	const char	*str[100];
	int	sp;
	int	canassign;
	char	errbuf[100];
} stack_t;

/* These are broad classifications of jsonop_t tokens */
typedef enum {
	JCOP_OTHER,	/* not an operator -- some other type of token */
	JCOP_INFIX,	/* left-associative infix binary operator */
	JCOP_RIGHTINFIX,/* right-associative infix binary operator */
	JCOP_PREFIX,	/* prefix unary operator */
	JCOP_POSTFIX	/* postfix unary operator */
} jcoptype_t;

/* This is used to collect details about a "select" statement */
typedef struct jsonselect_s {
	jsoncalc_t *select;	/* Selected columns as an object generator, or NULL */
	int	distinct;
	jsoncalc_t *from;	/* expression that returns a table, or NULL for first array in context */
	json_t *unroll;		/* list of field names to unroll, or NULL */
	jsoncalc_t *where;	/* expression that selects rows, or NULL for all */
	json_t *groupby;	/* list of field names, or NULL */
	jsoncalc_t *having;	/* expression that selects groups */
	json_t *orderby;	/* list of field names, or NULL */
	jsoncalc_t *limit;	/* expression that limits the returned values */
} jsonselect_t;


/* This table defines the relationship between text and the jsonop_t symbols.
 * It also includes precedence and quirks to help the parser.  As shown here,
 * the items are grouped by how they're related, but the lex() function sorts
 * them by op so you can use operators[jc->op] to find information about jc.
 */
static struct {
	char symbol[11];/* Derived form the JSONOP_xxxx enumerated value */
	char text[5];   /* Text form of the operator */
	short prec;     /* Precedence of the operator (higher is done first) */
	short noexpr;	/* "1" if token can't be in valid expressions */
	jcoptype_t optype;/* token type */
	size_t len;     /* text length (computed at runtime) */
} operators[] = {
	{"ADD",		"+",	210,	0,	JCOP_INFIX},
	{"AG",		"AG",	-1,	1,	JCOP_OTHER},
	{"AND",		"&&",	140,	0,	JCOP_INFIX},
	{"APPEND",	"A[]",	110,	0,	JCOP_INFIX},
	{"ARRAY",	"ARR",	-1,	0,	JCOP_OTHER},
	{"AS",		"AS",	121,	1,	JCOP_INFIX},
	{"ASSIGN",	"ASGN",	110,	0,	JCOP_INFIX},
	{"BETWEEN",	"BTWN",	121,	0,	JCOP_INFIX},
	{"BITAND",	"&",	160,	0,	JCOP_INFIX},
	{"BITNOT",	"~",	240,	0,	JCOP_PREFIX},
	{"BITOR",	"|",	150,	0,	JCOP_INFIX},
	{"BITXOR",	"^",	160,	0,	JCOP_INFIX},
	{"BOOLEAN",	"BOO",	-1,	0,	JCOP_OTHER},
	{"COALESCE",	"??",	130,	0,	JCOP_INFIX},
	{"COLON",	":",	121,	0,	JCOP_RIGHTINFIX}, /* sometimes part of ?: */
	{"COMMA",	",",	110,	0,	JCOP_INFIX},
	{"DESCENDING",	"DES",	3,	1,	JCOP_POSTFIX},
	{"DISTINCT",	"DIS",	2,	1,	JCOP_OTHER},
	{"DIVIDE",	"/",	220,	0,	JCOP_INFIX},
	{"DOT",		".",	270,	0,	JCOP_INFIX},
	{"DOTDOT",	"..",	270,	0,	JCOP_INFIX}, /*!!!*/
	{"EACH",	"##",	115,	0,	JCOP_INFIX}, /*!!!*/
	{"ELLIPSIS",	"...",	127,	0,	JCOP_INFIX},
	{"ENDARRAY",	"]",	0,	1,	JCOP_OTHER},
	{"ENDOBJECT",	"}",	0,	1,	JCOP_OTHER},
	{"ENDPAREN",	")",	0,	1,	JCOP_OTHER},
	{"ENVIRON",	"$",	169,	0,	JCOP_OTHER},
	{"EQ",		"==",	180,	0,	JCOP_INFIX},
	{"EQSTRICT",	"===",	180,	0,	JCOP_INFIX},
	{"FIND",	"@",	116,	0,	JCOP_INFIX},
	{"FNCALL",	"F",	170,	0,	JCOP_OTHER}, /* function call */
	{"FROM",	"FRO",	2,	0,	JCOP_OTHER},
	{"GE",		">=",	190,	0,	JCOP_INFIX},
	{"GROUP",	"#",	115,	0,	JCOP_INFIX},	/*!!!*/
	{"GROUPBY",	"GRO",	2,	1,	JCOP_OTHER},
	{"GT",		">",	190,	0,	JCOP_INFIX},
	{"HAVING",	"HAV",	2,	1,	JCOP_OTHER},
	{"ICEQ",	"=",	180,	0,	JCOP_INFIX},
	{"ICNE",	"<>",	180,	0,	JCOP_INFIX},
	{"IN",		"IN",	175,	0,	JCOP_INFIX},
	{"ISNOTNULL",	"N!",	117,	0,	JCOP_POSTFIX}, /* postfix operator */
	{"ISNULL",	"N=",	117,	0,	JCOP_POSTFIX}, /* postfix operator */
	{"LE",		"<=",	190,	0,	JCOP_INFIX},
	{"LIKE",	"LIK",	180,	0,	JCOP_INFIX},
	{"LIMIT",	"LIM",	2,	1,	JCOP_OTHER},
	{"LITERAL",	"LIT",	-1,	0,	JCOP_OTHER},
	{"LJOIN",	"#<",	117,	0,	JCOP_INFIX},
	{"LT",		"<",	190,	0,	JCOP_INFIX},
	{"MAYBEASSIGN",	"=??",	110,	0,	JCOP_INFIX},
	{"MAYBEMEMBER",	":??",	121,	0,	JCOP_INFIX},
	{"MODULO",	"%",	220,	0,	JCOP_INFIX},
	{"MULTIPLY",	"*",	220,	0,	JCOP_INFIX},
	{"NAME",	"NAM",	-1,	0,	JCOP_OTHER},
	{"NE",		"!=",	180,	0,	JCOP_INFIX},
	{"NEGATE",	"U-",	240,	0,	JCOP_PREFIX},
	{"NESTRICT",	"!==",	180,	0,	JCOP_INFIX},
	{"NJOIN",	"#=",	117,	0,	JCOP_INFIX},
	{"NOT",		"!",	240,	0,	JCOP_PREFIX},
	{"NOTLIKE",	"NLK",	180,	0,	JCOP_INFIX},
	{"NULL",	"NUL",	-1,	0,	JCOP_OTHER},
	{"NUMBER",	"NUM",	-1,	0,	JCOP_OTHER},
	{"OBJECT",	"OBJ",	-1,	0,	JCOP_OTHER},
	{"OR",		"||",	130,	0,	JCOP_INFIX},
	{"ORDERBY",	"ORD",	2,	1,	JCOP_OTHER},
	{"QUESTION",	"?",	121,	0,	JCOP_RIGHTINFIX}, /* right-to-left associative */
	{"REGEX",	"REG",	-1,	0,	JCOP_OTHER},
	{"RJOIN",	"#>",	117,	0,	JCOP_INFIX},
	{"SELECT",	"SEL",	1,	1,	JCOP_OTHER},
	{"STARTARRAY",	"[",	260,	1,	JCOP_OTHER},
	{"STARTOBJECT",	"{",	260,	1,	JCOP_OTHER},
	{"STARTPAREN",	"(",	260,	1,	JCOP_OTHER},
	{"STRING",	"STR",	-1,	0,	JCOP_OTHER},
	{"SUBSCRIPT",	"S[",	170,	0,	JCOP_OTHER},
	{"SUBTRACT",	"-",	210,	0,	JCOP_INFIX}, /* or JSONOP_NEGATE */
	{"VALUES",	"VAL",	125,	0,	JCOP_INFIX},
	{"WHERE",	"WHE",	2,	1,	JCOP_OTHER},
	{"INVALID",	"XXX",	666,	1,	JCOP_OTHER}
};

static int pattern(stack_t *stack, char *want);

/* We can use static copies of some jsoncalc_t's */
static jsoncalc_t startparen = {JSONOP_STARTPAREN};
static jsoncalc_t endparen = {JSONOP_ENDPAREN};
static jsoncalc_t startarray = {JSONOP_STARTARRAY};
static jsoncalc_t endarray = {JSONOP_ENDARRAY};
static jsoncalc_t startobject = {JSONOP_STARTOBJECT};
static jsoncalc_t endobject = {JSONOP_ENDOBJECT};
static jsoncalc_t selectdistinct = {JSONOP_DISTINCT};
static jsoncalc_t selectfrom = {JSONOP_FROM};
static jsoncalc_t selectwhere = {JSONOP_WHERE};
static jsoncalc_t selectgroupby = {JSONOP_GROUPBY};
static jsoncalc_t selecthaving = {JSONOP_HAVING};
static jsoncalc_t selectorderby = {JSONOP_ORDERBY};
static jsoncalc_t selectdesc = {JSONOP_DESCENDING};
static jsoncalc_t selectlimit = {JSONOP_LIMIT};


/* Return the name of an operation, mostly for debugging. */
char *json_calc_op_name(jsonop_t jsonop)
{
	return operators[jsonop].symbol;
}

/* Dump an expression.  This is recursive and doesn't add a newline.  The
 * result isn't pretty, and it couldn't be reparsed to generate the same
 * tree.  It is merely for debugging.
 */
void json_calc_dump(jsoncalc_t *calc)
{
	jsoncalc_t *p;
	char	*str;

	/* Defend against NULL */
	if (!calc)
		return;

	switch (calc->op) {
	  case JSONOP_LITERAL:
		str = json_serialize(calc->u.literal, NULL);
		printf("`%s'", str);
		free(str);
		break;

	  case JSONOP_STRING:
		printf("\"%s\"", calc->u.text);
		break;

	  case JSONOP_NUMBER:
	  case JSONOP_BOOLEAN:
	  case JSONOP_NULL:
	  case JSONOP_NAME:
		printf("%s", calc->u.text);
		break;

	  case JSONOP_ARRAY:
#if 0
		printf("[");
		if (calc->LEFT) {
			json_calc_dump(calc->LEFT);
			for (p = calc->RIGHT; p; p = p->RIGHT) {
				printf(",");
				json_calc_dump(p->LEFT);
			}
		}
		printf("]");
#else
		printf("[array]");
#endif
		break;

	  case JSONOP_OBJECT:
		printf("{");
		if (calc->LEFT) {
			json_calc_dump(calc->LEFT);
			for (p = calc->RIGHT; p; p = p->RIGHT) {
				printf(",");
				json_calc_dump(p->LEFT);
			}
		}
		printf("}");
		break;

	  case JSONOP_EACH:
	  case JSONOP_GROUP:
	  case JSONOP_FIND:
	  case JSONOP_NJOIN:
	  case JSONOP_LJOIN:
	  case JSONOP_RJOIN:
	  case JSONOP_SUBSCRIPT:
	  case JSONOP_DOT:
	  case JSONOP_DOTDOT:
	  case JSONOP_ELLIPSIS:
	  case JSONOP_COALESCE:
	  case JSONOP_QUESTION:
	  case JSONOP_COLON:
	  case JSONOP_MAYBEMEMBER:
	  case JSONOP_AS:
	  case JSONOP_NEGATE:
	  case JSONOP_ISNULL:
	  case JSONOP_ISNOTNULL:
	  case JSONOP_MULTIPLY:
	  case JSONOP_DIVIDE:
	  case JSONOP_MODULO:
	  case JSONOP_ADD:
	  case JSONOP_SUBTRACT:
	  case JSONOP_BITNOT:
	  case JSONOP_BITAND:
	  case JSONOP_BITOR:
	  case JSONOP_BITXOR:
	  case JSONOP_NOT:
	  case JSONOP_AND:
	  case JSONOP_OR:
	  case JSONOP_LT:
	  case JSONOP_LE:
	  case JSONOP_EQ:
	  case JSONOP_NE:
	  case JSONOP_GE:
	  case JSONOP_GT:
	  case JSONOP_ICEQ:
	  case JSONOP_ICNE:
	  case JSONOP_LIKE:
	  case JSONOP_NOTLIKE:
	  case JSONOP_IN:
	  case JSONOP_EQSTRICT:
	  case JSONOP_NESTRICT:
	  case JSONOP_BETWEEN:
	  case JSONOP_ASSIGN:
	  case JSONOP_APPEND:
	  case JSONOP_MAYBEASSIGN:
	  case JSONOP_VALUES:
		if (calc->LEFT) {
			printf("(");
			json_calc_dump(calc->LEFT);
			printf("%s", operators[calc->op].text);
			if (calc->RIGHT)
				json_calc_dump(calc->RIGHT);
			printf(")");
		} else {
			printf("%s", operators[calc->op].text);
			if (calc->RIGHT)
				json_calc_dump(calc->RIGHT);
		}
		break;

	  case JSONOP_COMMA:
		/* Comma expressions can get huge.  Best to hide LEFT */
		printf("...,");
		if (calc->RIGHT)
			json_calc_dump(calc->RIGHT);
		break;

	  case JSONOP_FNCALL:
		printf("%s( ", calc->u.func.jf ? calc->u.func.jf->name : "?");
		json_calc_dump(calc->u.func.args);
		printf(" ) ");
		break;

	  case JSONOP_AG:
		printf(" <<");
		json_calc_dump(calc->u.ag->expr);
		printf(">> ");
		break;

	  case JSONOP_SELECT:
		printf(" SELECT");
		break;

	  case JSONOP_DISTINCT:
		printf(" DISTINCT");
		break;

	  case JSONOP_FROM:
		printf(" FROM");
		break;

	  case JSONOP_WHERE:
		printf(" WHERE");
		break;

	  case JSONOP_GROUPBY:
		printf(" GROUP BY");
		break;

	  case JSONOP_HAVING:
		printf(" HAVING");
		break;

	  case JSONOP_ORDERBY:
		printf(" ORDER BY");
		break;

	  case JSONOP_DESCENDING:
		printf(" DESCENDING");
		break;

	  case JSONOP_LIMIT:
		printf(" LIMIT");
		break;

	  case JSONOP_STARTPAREN:
	  case JSONOP_ENDPAREN:
	  case JSONOP_STARTARRAY:
	  case JSONOP_ENDARRAY:
	  case JSONOP_STARTOBJECT:
	  case JSONOP_ENDOBJECT:
	  case JSONOP_INVALID:
	  case JSONOP_REGEX:
	  case JSONOP_ENVIRON:
		printf(" %s ", json_calc_op_name(calc->op));
		break;
	}
}

/* Dump all entries in the parsing stack */
static int dumpstack(stack_t *stack, const char *format, const char *data)
{
	int     i;
	char	buf[40];

	/* If debugging is off, don't show */
	if (!json_debug_flags.calc)
		return 1;

	/* Print the key */
	snprintf(buf, sizeof buf, format, data);
	strcat(buf, ":");
	printf("%-14s", buf);

	/* Dump the stack */
	if (stack->sp > 0) {
		for (i = 0; i < stack->sp; i++) {
			putchar(' ');
			json_calc_dump(stack->stack[i]);
		}
	}
	putchar('\n');
	return 1;
}

/* Test whether the parsing stack contains an unresolved SELECT keyword */
static int jcselecting(stack_t *stack)
{
	int	i;
	for (i = 0; i < stack->sp; i++)
		if (stack->stack[i]->op == JSONOP_SELECT)
			return 1;
	return 0;
}

/* Test whether the parsing stack is in a context where "/" is the start of a
 * regular expression, not a division operator.
 */
static int jcregex(stack_t *stack)
{
	if (stack->sp == 0
	 || stack->stack[stack->sp - 1]->op == JSONOP_LIKE
	 || stack->stack[stack->sp - 1]->op == JSONOP_NOTLIKE
	 || stack->stack[stack->sp - 1]->op == JSONOP_STARTPAREN
	 || stack->stack[stack->sp - 1]->op == JSONOP_COMMA)
		return 1;
	return 0;
}

/* Test whether the parseing stack is in a context where "=" is an assignment
 * operator, not a comparison operator.  Return JSONOP_ICEQ if it is a
 * comparison, or one of JSON_ASSIGN or JSON_APPEND for assignment.
 *
 * THIS FUNCTION CAN MODIFY THE PARSE STACK!  For JSON_APPEND (denoted by []=)
 * it will remove the [].
 */
static jsonop_t jcisassign(stack_t *stack)
{
	int	sp = stack->sp;
	jsoncalc_t	*jc;
	jsonop_t	op = JSONOP_ASSIGN;

	/* If assignment isn't allowed in this expression, the anwer is "no" */
	if (!stack->canassign)
		return JSONOP_ICEQ;

	/* Any basic l-value can be followed by "[]" to denote appending
	 * to an array.
	 */
	if (sp >= 3 && stack->stack[sp - 2]->op == JSONOP_STARTARRAY && stack->stack[sp - 1]->op == JSONOP_ENDARRAY) {
		op = JSONOP_APPEND;
		sp -= 2;
	}

	/* A name can be an lvalue, except for the names "this" and "that" */
	if (sp == 1 && stack->stack[0]->op == JSONOP_NAME) {
		if (!strcasecmp(stack->stack[0]->u.text, "this")
		 || !strcasecmp(stack->stack[0]->u.text, "that"))
			return JSONOP_ICEQ;
		goto IsAssign;
	}

	/* For more complex assignments, the lvalue can be a name followed by
	 * a series of dot-names and subscripts.  This is complicated slightly
	 * by the fact that the last dot-name or subscript might not be
	 * reduced yet since this function is called from lex().
	 */
	if (sp == 1 ||
	    (sp == 3 && stack->stack[1]->op == JSONOP_DOT && stack->stack[2]->op == JSONOP_NAME) ||
	    (sp == 4 && stack->stack[1]->op == JSONOP_STARTARRAY && stack->stack[3]->op == JSONOP_ENDARRAY)) {
		for (jc = stack->stack[0];
		     jc && ((jc->op == JSONOP_DOT && jc->RIGHT->op == JSONOP_NAME)
			|| jc->op == JSONOP_SUBSCRIPT);
		     jc = jc->LEFT) {
		}
		if (!jc || jc->op != JSONOP_NAME)
			return JSONOP_ICEQ;
		goto IsAssign;
	}
	return JSONOP_ICEQ;

IsAssign:
	/* For JSONOP_APPEND, remove the empty brackets from the stack */
	if (op == JSONOP_APPEND) {
		json_calc_free(stack->stack[sp]); /* JSONOP_SUBSCRIPT */
		json_calc_free(stack->stack[sp + 1]); /* JSONOP_ENDARRAY */
		stack->sp = sp;
	}
	return op;
}

/* Skip whitespace and comments */
static const char *skipwhitespace(const char *str)
{
	/* Skip comment, if any */
	if (str[0] == '/' && str[1] == '/') {
		while (*str && *str != '\n')
			str++;
	}
	while (isspace(*str)) {
		str++;

		/* Maybe another comment? */
		if (str[0] == '/' && str[1] == '/') {
			while (*str && *str != '\n')
				str++;
		}
	}
	return str;
}

/* Given a starting point within a text buffer, parse the next token (storing
 * its details in *token) and return a pointer to the character after the token.
 */
const char *lex(const char *str, token_t *token, stack_t *stack)
{
	jsonop_t op, best;
	char *end;

	/* Fix the operators[] array, if we haven't already done so.
	 * This just means counting the lengths of the operators
	 */
	if (!operators[0].len) {
		/* Verify that the JSONOP_INVALID symbol is in the right
		 * place.  That strongly suggests that the rest of them are
		 * all correct too.
		 */
		assert(operators[JSONOP_INVALID].prec == 666);

		/* Compute lengths of names */
		for (op = 0; op <= JSONOP_INVALID; op++)
			operators[op].len = strlen(operators[op].text);
	}

	/* Skip whitespace */
	str = skipwhitespace(str);

	/* Start with some common defaults */
	token->op = JSONOP_INVALID;
	token->full = str;
	token->len = 1;

	/* If no tokens, return NULL */
	if (!*str) {
		if (json_debug_flags.calc)
			printf("lex(): NULL\n");
		return NULL;
	}

	/* Numbers */
	if (isdigit(*str) || (*str == '.' && isdigit(str[1]))) {
		token->op = JSONOP_NUMBER;
		if (*str == '0' && str[1] && strchr("0123456789XxOoBb", str[1])) {
			int	radix;
			token->full = str;
			if (str[1] == 'x' || str[1] == 'X')
				radix = 16, str += 2;
			else if (str[1] == 'o' || str[1] == 'O')
				radix = 8, str += 2;
			else if (str[1] == 'b' || str[1] == 'B')
				radix = 2, str += 2;
			else /* 0nnn, assume octal (deprecated in JavaScript) */
				radix = 8;
			(void)strtol(str, &end, radix);
			token->len = end - token->full;
			str = end;
		} else {
			while (isdigit(token->full[token->len]))
				token->len++;
			if (token->full[token->len] == '.' && isdigit(token->full[token->len + 1])) {
				token->len++;
				while (isdigit(token->full[token->len]))
					token->len++;
			}
			if (token->full[token->len] == 'e' || token->full[token->len] == 'E') {
				token->len++;
				if (token->full[token->len] == '+' || token->full[token->len] == '-')
					token->len++;
				while (isdigit(token->full[token->len]))
					token->len++;
			}
			str += token->len;
		}
		if (json_debug_flags.calc)
			printf("lex(): number JSONOP_NUMBER \"%.*s\"\n", token->len, token->full);
		return str;
	}

	/* Quoted strings */
	if (strchr("\"'`", *str)) {
		token->op = JSONOP_STRING;
		token->full = str;
		token->len = 1;
		while (token->full[token->len] && token->full[token->len] != *token->full)
		{
			if (token->full[token->len] == '\\' && token->full[token->len + 1])
				token->len++;
			token->len++;
		}
		token->len++;
		str += token->len;

		/* `...` is a quoted name, not a string */
		if (*token->full == '`') {
			token->op = JSONOP_NAME;
			token->full++;
			token->len -= 2;
		}
		if (json_debug_flags.calc)
			printf("lex(): string JSONOP_%s \"%.*s\"\n", json_calc_op_name(token->op), token->len, token->full);
		return str;
	}

	/* Names or alphanumeric keywords */
	if (isalpha(*str) || *str == '_') {
		token->op = JSONOP_NAME;
		token->full = str;
		token->len = 1;
		while (isalnum(token->full[token->len]) || token->full[token->len] == '_')
			token->len++;

		/* Distinguish keywords from names */
		if ((token->len == 4 && !strncmp(token->full, "true", 4))
		 || (token->len == 5 && !strncmp(token->full, "false", 5)))
			token->op = JSONOP_BOOLEAN;
		else if (token->len == 4 && !strncmp(token->full, "null", 4))
			token->op = JSONOP_NULL;
		else if (token->len == 4 && !strncasecmp(token->full, "like", 4))
			token->op = JSONOP_LIKE;
		else if (token->len == 3 && !strncasecmp(token->full, "not like", 8)) {
			token->len = 8;
			token->op = JSONOP_NOTLIKE;
		} else if (token->len == 2 && !strncasecmp(token->full, "in", 2))
			token->op = JSONOP_IN;
		else if (token->len == 3 && !strncasecmp(token->full, "and", 3))
			token->op = JSONOP_AND;
		else if (token->len == 2 && !strncasecmp(token->full, "or", 2))
			token->op = JSONOP_OR;
		else if (token->len == 3 && !strncasecmp(token->full, "not", 3))
			token->op = JSONOP_NOT;
		else if (token->len == 7 && !strncasecmp(token->full, "between", 7))
			token->op = JSONOP_BETWEEN;
		else if (token->len == 2 && !strncasecmp(token->full, "is null", 7)) {
			token->len = 7;
			token->op = JSONOP_ISNULL;
		} else if (token->len == 2 && !strncasecmp(token->full, "is not null", 7)) {
			token->len = 11;
			token->op = JSONOP_ISNOTNULL;
		} else if (token->len == 2 && !strncasecmp(token->full, "as", 2)) {
			token->op = JSONOP_AS;
		} else if (token->len == 6 && !strncasecmp(token->full, "values", 6)) {
			token->op = JSONOP_VALUES;
		} else if (token->len == 6 && !strncasecmp(token->full, "select", 6)) {
			token->len = 6;
			token->op = JSONOP_SELECT;
		} else if (jcselecting(stack)) {
			/* The following SQL keywords are only recognized as
			 * part of a "select" clause.  This is because some of
			 * them such as "from" and "desc" are often used as
			 * member names, and "distinct" is a jsoncalc function
			 * name.  You could wrap them in backticks to prevent
			 * them from being taken as keywords, but that'd be
			 * inconvenient.
			 */
			if (token->len == 8 && !strncasecmp(token->full, "distinct", 8)) {
				token->op = JSONOP_DISTINCT;
			} else if (token->len == 4 && !strncasecmp(token->full, "from", 4)) {
				token->op = JSONOP_FROM;
			} else if (token->len == 5 && !strncasecmp(token->full, "where", 5)) {
				token->op = JSONOP_WHERE;
			} else if (token->len == 5 && !strncasecmp(token->full, "group by", 8)) {
				token->len = 8;
				token->op = JSONOP_GROUPBY;
			} else if (token->len == 6 && !strncasecmp(token->full, "having", 6)) {
				token->op = JSONOP_HAVING;
			} else if (token->len == 5 && !strncasecmp(token->full, "order by", 8)) {
				token->len = 8;
				token->op = JSONOP_ORDERBY;
			} else if (token->len == 10 && !strncasecmp(token->full, "descending", 10)) {
				token->len = 10;
				token->op = JSONOP_DESCENDING;
			} else if (token->len == 4 && !strncasecmp(token->full, "desc", 4)) {
				token->len = 4;
				token->op = JSONOP_DESCENDING;
			} else if (token->len == 5 && !strncasecmp(token->full, "limit", 5)) {
				token->len = 5;
				token->op = JSONOP_LIMIT;
			}
		}
		str += token->len;
		if (json_debug_flags.calc)
			printf("lex(): keyword JSONOP_%s \"%.*s\"\n", json_calc_op_name(token->op), token->len, token->full);
		return str;
	}

	/* Regular expression */
	if (*str == '/' && jcregex(stack)) {
		/* skip to the terminating '/' */
		token->full = str;
		while (*++str && *str != '/') {
			if (*str == '\\' && str[1])
				str++;
		}

		/* Also include any trailing flags */
		while (*++str && isalnum(*str)) {
		}

		/* Finish filling in token */
		token->op = JSONOP_REGEX;
		token->len = (int)(str - token->full);
		return str;
	}

	/* Operators - find the longest matching name */
	best = JSONOP_INVALID;
	for (op = 0; op < JSONOP_INVALID; op++) {
		if (!strncmp(str, operators[op].text, operators[op].len)
		 && (best == JSONOP_INVALID || operators[best].len < operators[op].len))
			best = op;
	}
	if (best == JSONOP_ENDOBJECT) {
		/* An unmatched } can end an expression, for example if a
		 * function definition ends with "return expr}".  To the
		 * expression parser, this use of "}" should be treated
		 * much like a ";" -- it should be JSONOP_INVALID.
		 */
		int i;
		for (i = stack->sp - 1; i >= 0; i--)
			if (stack->stack[i]->op == JSONOP_STARTOBJECT)
				break;
		if (i < 0)
			best = JSONOP_INVALID;
	}
	if (best != JSONOP_INVALID) {
		/* Use this operator for this token */
		token->op = best;
		token->full = str;
		token->len = operators[best].len;
		str += token->len;

		/* SUBTRACT could be NEGATE -- depends on context */
		if (token->op == JSONOP_SUBTRACT && (pattern(stack, "^") || pattern(stack, "+")))
			token->op = JSONOP_NEGATE;

		/* ICEQ could be ASSIGN -- depends on context */
		if (token->op == JSONOP_ICEQ)
			token->op = jcisassign(stack);

		if (json_debug_flags.calc)
			printf("lex(): operator JSONOP_%s \"%.*s\"\n", json_calc_op_name(token->op), token->len, token->full);
		return str;
	}

	/* Invalid */
	if (json_debug_flags.calc)
		printf("lex(): invalid JSONOP_%s \"%.*s\"\n", json_calc_op_name(token->op), token->len, token->full);
	str++;
	return str;
} 


/* Allocate a jsoncalc_t structure. Type and (if appropriate) text come from
 * a token_t struct.
 */
static jsoncalc_t *jcalloc(token_t *token)
{
	jsoncalc_t *jc;
	size_t len, size;

	/* Some tokens don't need to be allocated dynamically */
	switch (token->op) {
	  case JSONOP_STARTPAREN: return &startparen;
	  case JSONOP_ENDPAREN: return &endparen;
	  case JSONOP_STARTARRAY: return &startarray;
	  case JSONOP_ENDARRAY: return &endarray;
	  case JSONOP_STARTOBJECT: return &startobject;
	  case JSONOP_ENDOBJECT: return &endobject;
	  case JSONOP_DISTINCT:	return &selectdistinct;
	  case JSONOP_FROM: return &selectfrom;
	  case JSONOP_WHERE: return &selectwhere;
	  case JSONOP_GROUPBY: return &selectgroupby;
	  case JSONOP_HAVING: return &selecthaving;
	  case JSONOP_ORDERBY: return &selectorderby;
	  case JSONOP_DESCENDING: return &selectdesc;
	  case JSONOP_LIMIT: return &selectlimit;
	  default:;
	}

	/* Allocate it.  If long "name" then add extra space. */
	len = 0;
	if (token->op == JSONOP_NAME)
		len = token->len;
	if (len + 1 < sizeof(jc->u))
		size = sizeof(*jc);
	else
		size = sizeof(*jc) - sizeof(jc->u) + len + 1;
	jc = (jsoncalc_t *)malloc(size);

	/* Initialize it to all zeroes */
	memset((void *)jc, 0, size);
	jc->op = token->op;

	/* Copy names u.text, other literals into a json_t */
	if (token->op == JSONOP_NAME)
		strncpy(jc->u.text, token->full, token->len);
	else if (token->op == JSONOP_STRING) {
		jc->op = JSONOP_LITERAL;
		len = json_mbs_unescape(NULL, token->full + 1, token->len - 2);
		jc->u.literal = json_string("", len);
		if (len > 0)
			json_mbs_unescape(jc->u.literal->text, token->full + 1, token->len - 2);
	} else if (token->op == JSONOP_NUMBER) {
		jc->op = JSONOP_LITERAL;
		if (*token->full == '0' && token->full[1] && strchr("0123456789XxOoBb", token->full[1])) {
			long	value;
			int	radix;
			const char	*digits = token->full;
			switch (token->full[1]) {
			case 'x': case 'X': radix = 16;	digits += 2; break;
			case 'o': case 'O': radix = 8;	digits += 2; break;
			case 'b': case 'B': radix = 2;	digits += 2; break;
			default:	    radix = 8;
			}
			value = strtol(digits, NULL, radix);
			jc->u.literal = json_from_int((int)value);
		} else if (strchr(token->full, '.') || strchr(token->full, 'e') || strchr(token->full, 'E')) {
			jc->u.literal = json_from_double(atof(token->full));
		} else {
			jc->u.literal = json_from_int(atoi(token->full));
		}
	} else if (token->op == JSONOP_BOOLEAN) {
		jc->op = JSONOP_LITERAL;
		jc->u.literal = json_boolean(*token->full == 't');
	} else if (token->op == JSONOP_NULL) {
		jc->op = JSONOP_LITERAL;
		jc->u.literal = json_null();
	}

	/* SELECT needs some extra space allocated during parsing. */
	if (token->op == JSONOP_SELECT) {
		jc->u.select = (jsonselect_t *)calloc(1, sizeof(jsonselect_t));
	}

	/* REGEX needs a buffer allocated, and then the text and flags need to
	 * be parsed.  Big stuff.
	 */
	if (token->op == JSONOP_REGEX) {
		int	ignorecase = 0;
		char	*tmp, *build;
		const char *scan;
		int	err;

		/* Allocate a regex_t buffer */
		jc->u.regex.preg = malloc(sizeof(regex_t));

		/* Extract the regex source from the token */
		tmp = (char *)malloc(token->len);
		for (scan = token->full + 1, build = tmp; *scan && *scan != '/'; ) {
			if (*scan == '\\' && scan[1] == '/')
				scan++;
			*build++ = *scan++;
		}
		*build = '\0';

		/* Scan flags for "i" and/or "g" */
		while (++scan < &token->full[token->len]) {
			ignorecase |= (*scan == 'i');
			jc->u.regex.global |= (*scan == 'g');
		}

		/* Compile the regex */
		err = regcomp((regex_t *)jc->u.regex.preg, tmp, ignorecase ? REG_ICASE : 0);
		if (err) {
			char	buf[200];
			/* Fetch the error messaged */
			regerror(err, (regex_t *)jc->u.regex.preg, buf, sizeof buf);
			/* Stuff it into a null */
			jc->op = JSONOP_LITERAL;
			jc->u.literal = json_error_null(NULL, "regex:%s", buf);
		}
	}

	/* return it */
	return jc;
}

/* Allocate a jsoncalc_t structure that uses u.param.left and u.param.right */
static jsoncalc_t *jcleftright(jsonop_t op, jsoncalc_t *left, jsoncalc_t *right)
{
	token_t	t;
	jsoncalc_t *jc;

	t.op = op;
	jc = jcalloc(&t);
	jc->LEFT = left;
	jc->RIGHT = right;
	return jc;
}

/* Allocate a jsoncalc_t structure for a function call, and up to 3 parameters.
 * The first parameter, p1, is required; the others may be NULL to skip them.
 */
static jsoncalc_t *jcfunc(char *name, jsoncalc_t *p1, jsoncalc_t *p2, jsoncalc_t *p3)
{
	token_t	t;
	jsoncalc_t *jc;

	/* Allocate the jsoncalc_t for the function call */
	t.op = JSONOP_FNCALL;
	jc = jcalloc(&t);

	/* Link it to the named function */
	jc->u.func.jf = json_calc_function_by_name(name);

	/* The args are an array generator */
	t.op = JSONOP_ARRAY;
	jc->u.func.args = jcalloc(&t);
	jc->u.func.args->LEFT = p1;
	if (p2) {
		jc->u.func.args->RIGHT = jcleftright(JSONOP_ARRAY, p2, NULL);
		if (p3)
			jc->u.func.args->RIGHT->RIGHT = jcleftright(JSONOP_ARRAY, p3, NULL);
	}

	return jc;
}

/* Build an array generator, one item at a time.  For the first call, "list"
 * should be NULL; in subsequent calls, it should be the value returned by
 * the previous call.  This function is used in some command parsers in cmd.c
 */
jsoncalc_t *json_calc_list(jsoncalc_t *list, jsoncalc_t *item)
{
	jsoncalc_t *tail;

	/* Allocate a JSONOP_ARRAY with "item" on the left */
	item = jcleftright(JSONOP_ARRAY, item, NULL);

	/* If no list, then just return the JSONOP_ARRAY containing item */
	if (!list)
		return item;

	/* Find the tail of the list */
	for (tail = list; tail->RIGHT; tail = tail->RIGHT) {
	}

	/* Append the new item to the tail, and return the whole list */
	tail->RIGHT = item;
	return list;
}

/* Free a jsoncalc_t tree that was allocated via json_calc_parse()  ... or
 * ultimately the internal jcalloc() function.
 */
void json_calc_free(jsoncalc_t *jc)
{
	/* defend against NULL */
	if (!jc)
		return;

	/* Some jsoncalc_t values aren't dynamically allocated, so not freed */
	if (jc == &startparen || jc == &endparen
	 || jc == &startarray || jc == &endarray
	 || jc == &startobject || jc == &endobject)
		return;

	/* Recursively work down through the expression tree */
	switch (jc->op) {
	  case JSONOP_STRING:
	  case JSONOP_NUMBER:
	  case JSONOP_BOOLEAN:
	  case JSONOP_NULL:
	  case JSONOP_NAME:
		break;

	  case JSONOP_LITERAL:
		json_free(jc->u.literal);
		break;

	  case JSONOP_DOT:
	  case JSONOP_DOTDOT:
	  case JSONOP_ELLIPSIS:
	  case JSONOP_ARRAY:
	  case JSONOP_OBJECT:
	  case JSONOP_SUBSCRIPT:
	  case JSONOP_COALESCE:
	  case JSONOP_QUESTION:
	  case JSONOP_COLON:
	  case JSONOP_MAYBEMEMBER:
	  case JSONOP_AS:
	  case JSONOP_EACH:
	  case JSONOP_GROUP:
	  case JSONOP_FIND:
	  case JSONOP_NJOIN:
	  case JSONOP_LJOIN:
	  case JSONOP_RJOIN:
	  case JSONOP_NEGATE:
	  case JSONOP_ISNULL:
	  case JSONOP_ISNOTNULL:
	  case JSONOP_MULTIPLY:
	  case JSONOP_DIVIDE:
	  case JSONOP_MODULO:
	  case JSONOP_ADD:
	  case JSONOP_SUBTRACT:
	  case JSONOP_BITNOT:
	  case JSONOP_BITAND:
	  case JSONOP_BITOR:
	  case JSONOP_BITXOR:
	  case JSONOP_NOT:
	  case JSONOP_AND:
	  case JSONOP_OR:
	  case JSONOP_LT:
	  case JSONOP_LE:
	  case JSONOP_EQ:
	  case JSONOP_NE:
	  case JSONOP_GE:
	  case JSONOP_GT:
	  case JSONOP_ICEQ:
	  case JSONOP_ICNE:
	  case JSONOP_LIKE:
	  case JSONOP_NOTLIKE:
	  case JSONOP_IN:
	  case JSONOP_EQSTRICT:
	  case JSONOP_NESTRICT:
	  case JSONOP_COMMA:
	  case JSONOP_BETWEEN:
	  case JSONOP_ENVIRON:
	  case JSONOP_ASSIGN:
	  case JSONOP_APPEND:
	  case JSONOP_MAYBEASSIGN:
	  case JSONOP_VALUES:
		json_calc_free(jc->LEFT);
		json_calc_free(jc->RIGHT);
		break;

	  case JSONOP_SELECT:
		json_calc_free(jc->u.select->select);
		json_calc_free(jc->u.select->from);
		json_calc_free(jc->u.select->where);
		json_free(jc->u.select->groupby);
		json_free(jc->u.select->orderby);
		free(jc->u.select);
		break;

	  case JSONOP_FNCALL:
		json_calc_free(jc->u.func.args);
		break;

	  case JSONOP_AG:
		json_calc_free(jc->u.ag->expr);
		free(jc->u.ag);
		break;

	  case JSONOP_REGEX:
		/* jc->u.regex.preg is a pointer to a regex_t buffer.  We need
		 * to call regfree() on that buffer to release the data
		 * associated with the buffer, and also free() to release
		 * the memory for the buffer itself.
		 */
		regfree((regex_t *)jc->u.regex.preg);
		free(jc->u.regex.preg);
		break;

	  case JSONOP_DISTINCT:
	  case JSONOP_FROM:
	  case JSONOP_WHERE:
	  case JSONOP_GROUPBY:
	  case JSONOP_HAVING:
	  case JSONOP_ORDERBY:
	  case JSONOP_DESCENDING:
	  case JSONOP_LIMIT:
		/* These SQL keywords are harmless, and never need to be freed*/
		return;

	  case JSONOP_STARTPAREN:
	  case JSONOP_ENDPAREN:
	  case JSONOP_STARTARRAY:
	  case JSONOP_ENDARRAY:
	  case JSONOP_STARTOBJECT:
	  case JSONOP_ENDOBJECT:
	  case JSONOP_INVALID:
		/* None of these should appear in a parsed expression */
		abort();
	}

	/* Finally, free this jsoncalt_t */
	free(jc);
}


/* Convert a left-associative list of comma operators into a right-associative
 * list of array elements or object members.  In other words, for a non-empty
 * array, fixcomma(...)->left is the first element of the array, and
 * fixcomma(...)->right is the remainder of the array, or NULL at the end
 * of the array.  For an empty array, both left & right are NULL.
 *
 * (If we parsed commas as right-associative then the size of the parsing stack
 * would limit the length of comma lists we could support.  That isn't an issue
 * for left-associative operators.)
 */
static jsoncalc_t *fixcomma(jsoncalc_t *jc, jsonop_t op)
{
	jsoncalc_t *top, *parent;

	/* We could get a single item, which is not a not a comma operator
	 * but should be a single item in the returned list.  We'll need to
	 * allocate an array struct to make it into a list of 1.
	 */
	if (jc->op != JSONOP_COMMA)
		return jcleftright(op, jc, NULL);

	/* Okay, this is a comma -- the last comma in a left-associative list.
	 * "left" is the earlier commas (using "left" to form a linked list)
	 * or the first item (if not a comma), and "right" is the last item.
	 *
	 * We want to convert that into a linked list of array or object nodes
	 * using "right" for links and "left" to point to data.  A NULL "right"
	 * marks the end of the list.  Note that we'll end up with one more
	 * array/object node than we had commas, but the above single-item case
	 * should take care of that.
	 */
	parent = jc->LEFT;
	if (parent->op == JSONOP_COMMA)
		top = fixcomma(parent, op);
	else
		parent = top = fixcomma(parent, op);
	parent->RIGHT = jc;
	jc->LEFT = jc->RIGHT;
	jc->RIGHT = NULL;
	jc->op = op;
	return top;
}

/* The top item on the stack should be a "name:expr" operator.  If it isn't
 * then convert it to one.  The two other possibilities are that it could be
 * a "expr AS name" operator, or a nameless expression in which case we want
 * to use the expression's source text as the name.
 */
static jsoncalc_t *fixcolon(stack_t *stack, const char *srcend)
{
	jsoncalc_t *jc = stack->stack[stack->sp - 1];
	if (jc->op == JSONOP_AS) {
		/* Convert "expr AS name" to "name:expr" */
		jsoncalc_t *swapper = jc->LEFT;
		jc->LEFT = jc->RIGHT;
		jc->RIGHT = swapper;
		jc->op = JSONOP_COLON;
	} else if (jc->op != JSONOP_COLON && jc->op != JSONOP_MAYBEMEMBER) {
		/* Use the source text as the name */
		token_t t;
		t.op = JSONOP_NAME;
		t.full = stack->str[stack->sp - 1];
		if (strchr("\"'`", *t.full))
			t.full++;
		while (srcend - 1 > t.full && strchr("\"'` ", srcend[-1]))
			srcend--;
		t.len = (int)(srcend - t.full);

		/* Make it a COLON expression */
		jc = jcleftright(JSONOP_COLON, jcalloc(&t), jc);
	} /* Else it is already "name:expr" or "name:??expr" */

	return jc;
}

/* Test whether jc uses an aggregate function */
static int jcisag(jsoncalc_t *jc)
{
	/* Defend against NULL */
	if (!jc)
		return 0;

	/* Recursively check subexpressions */
	switch (jc->op) {
	  case JSONOP_LITERAL:
	  case JSONOP_STRING:
	  case JSONOP_NUMBER:
	  case JSONOP_BOOLEAN:
	  case JSONOP_NULL:
	  case JSONOP_NAME:
	  case JSONOP_FROM:
	  case JSONOP_REGEX:
		return 0;

	  case JSONOP_DOT:
	  case JSONOP_DOTDOT:
	  case JSONOP_ELLIPSIS:
	  case JSONOP_ARRAY:
	  case JSONOP_OBJECT:
	  case JSONOP_SUBSCRIPT:
	  case JSONOP_COALESCE:
	  case JSONOP_QUESTION:
	  case JSONOP_COLON:
	  case JSONOP_MAYBEMEMBER:
	  case JSONOP_NJOIN:
	  case JSONOP_LJOIN:
	  case JSONOP_RJOIN:
	  case JSONOP_NEGATE:
	  case JSONOP_ISNULL:
	  case JSONOP_ISNOTNULL:
	  case JSONOP_MULTIPLY:
	  case JSONOP_DIVIDE:
	  case JSONOP_MODULO:
	  case JSONOP_ADD:
	  case JSONOP_SUBTRACT:
	  case JSONOP_BITNOT:
	  case JSONOP_BITAND:
	  case JSONOP_BITOR:
	  case JSONOP_BITXOR:
	  case JSONOP_NOT:
	  case JSONOP_AND:
	  case JSONOP_OR:
	  case JSONOP_LT:
	  case JSONOP_LE:
	  case JSONOP_EQ:
	  case JSONOP_NE:
	  case JSONOP_GE:
	  case JSONOP_GT:
	  case JSONOP_ICEQ:
	  case JSONOP_ICNE:
	  case JSONOP_LIKE:
	  case JSONOP_NOTLIKE:
	  case JSONOP_IN:
	  case JSONOP_EQSTRICT:
	  case JSONOP_NESTRICT:
	  case JSONOP_COMMA:
	  case JSONOP_BETWEEN:
	  case JSONOP_ASSIGN:
	  case JSONOP_APPEND:
	  case JSONOP_MAYBEASSIGN:
	  case JSONOP_EACH:
	  case JSONOP_GROUP:
	  case JSONOP_FIND:
	  case JSONOP_VALUES:
		return jcisag(jc->LEFT) || jcisag(jc->RIGHT);

	  case JSONOP_ENVIRON:
		return jcisag(jc->RIGHT);

	  case JSONOP_FNCALL:
		/* If this is an aggregate function, that's our answer */
		if (jc->u.func.jf->agfn)
			return 1;

		/* Check arguments */
		return jcisag(jc->u.func.args);

	  case JSONOP_AG:
	  case JSONOP_STARTPAREN:
	  case JSONOP_ENDPAREN:
	  case JSONOP_STARTARRAY:
	  case JSONOP_ENDARRAY:
	  case JSONOP_STARTOBJECT:
	  case JSONOP_ENDOBJECT:
	  case JSONOP_SELECT:
	  case JSONOP_DISTINCT:
	  case JSONOP_AS:
	  case JSONOP_WHERE:
	  case JSONOP_GROUPBY:
	  case JSONOP_HAVING:
	  case JSONOP_ORDERBY:
	  case JSONOP_DESCENDING:
	  case JSONOP_LIMIT:
	  case JSONOP_INVALID:
		/* None of these should appear in a parsed expression */
		abort();
	}

	/*NOTREACHED*/
	return 0;
}


/* Convert a select statement to "native" jsoncalc_t */
static jsoncalc_t *jcselect(jsonselect_t *sel)
{
	jsoncalc_t *jc, *ja;
	token_t t;
	int	anyselectag;

	/* If there's a column list in the SELECT clause, convert it to an
	 * object generator.
	 */
	anyselectag = 0;
	if (sel->select) {
		sel->select = fixcomma(sel->select, JSONOP_OBJECT);

		/* If there's no "distinct" flag but all columns use aggregates
		 * then there should be an implied "distinct".  This is why
		 * "SELECT count(*) FROM table" returns just one count.
		 */
		if (!sel->distinct && sel->select) {
			sel->distinct = 1;
			for (jc = sel->select; jc; jc = jc->u.param.right) {
				if (!jcisag(jc->u.param.left)) {
					sel->distinct = 0;
					break;
				}
			}
		}

		/* We also need to know if ANY of the columns use aggregates.
		 * This will help us organize the right hand side of the #
		 * expression.
		 */
		for (jc = sel->select; jc && !anyselectag; jc = jc->u.param.right)
			anyselectag = jcisag(jc->u.param.left);
	}

	/* If there's a having clause without a groupby clause, then use it
	 * as part of the where clause.
	 */
	if (sel->having && !sel->groupby) {
		if (sel->where)
			sel->where = jcleftright(JSONOP_AND, sel->where, sel->having);
		else
			sel->where = sel->having;
		sel->having = NULL;
	}

	/* Maybe dump some debugging info */
	if (json_debug_flags.calc) {
		char *tmp; 
		printf("jcselect(\n");
		printf("   distinct=%s\n", sel->distinct ? "true" : "false");
		printf("   select=");json_calc_dump(sel->select);putchar('\n');
		printf("   from=");json_calc_dump(sel->from);putchar('\n');
		tmp = json_serialize(sel->groupby, 0);
		printf("   groupby=%s\n", tmp);
		free(tmp);
		tmp = json_serialize(sel->orderby, 0);
		printf("   orderby=%s\n", tmp);
		free(tmp);
		printf("   limit=");json_calc_dump(sel->limit);putchar('\n');
		printf(")\n");
	}

	/* Start building the "native" version of the SELECT in variable "jc",
	 * starting with the table in the FROM clause, or with the default
	 * table if there was no FROM clause.
	 */
	if (sel->from) {
		/* Yes, use it.  If there's also an unroll list, then add an
		 * unroll() function call around it.
		 */
		if (sel->unroll) {
			/* Make the unroll list be a jsoncalc_t literal */
			t.op = JSONOP_LITERAL;
			ja = jcalloc(&t);
			ja->u.literal = sel->unroll;

			/* Generate an unroll() function call */
			jc = jcfunc("unroll", sel->from, ja, NULL);
		} else {
			/* just the one table */
			jc = sel->from;
		}
	} else {
		/* No, arrange for us to choose a default at runtime */
		jc = &selectfrom;
	}

	/* If there's a GROUP BY list, add a groupBy() function call */
	if (sel->groupby) {
		/* If there's a where clause, do it before groupby */
		if (sel->where)
			jc = jcleftright(JSONOP_EACH, jc, sel->where);

		/* The list is already json_t array of strings.  Make it be
		 * a jsoncalc_t literal.
		 */
		t.op = JSONOP_LITERAL;
		ja = jcalloc(&t);
		ja->u.literal = sel->groupby;

		/* Add groupBy() function call */
		jc = jcfunc("groupBy", jc, ja, NULL);

		/* If there's a having clause and/or a select list, that's
		 * next.  Even without it, we still need an # operator even
		 * if the right operand is just "this".
		 */
		if (sel->having && sel->select) {
			ja = jcleftright(JSONOP_QUESTION, sel->having, sel->select);
		} else if (sel->having) {
			ja = sel->having;
		} else if (sel->select) {
			ja = sel->select;
		} else {
			/* use "this" as the right operator */
			t.op = JSONOP_NAME;
			t.full = "this";
			t.len = 4;
			ja = jcalloc(&t);
		}
		jc = jcleftright(JSONOP_GROUP, jc, ja);
	} else if (sel->where || sel->select) {
		/* We want to add a ##where?select or just part of that */
		if (sel->where && sel->select)
			ja = jcleftright(JSONOP_QUESTION, sel->where, sel->select);
		else if (sel->where)
			ja = sel->where;
		else
			ja = sel->select;
		jc = jcleftright(JSONOP_EACH, jc, ja);
	}

	/* If there's an ORDER BY clause, add an orderBy() function call */
	if (sel->orderby) {
		/* The list is already json_t array of strings.  Make it be
		 * a jsoncalc_t literal.
		 */
		t.op = JSONOP_LITERAL;
		ja = jcalloc(&t);
		ja->u.literal = sel->orderby;

		/* Add an orderBy() function call */
		jc = jcfunc("orderBy", jc, ja, NULL);
	}

	/* If there's a DISTINCT keyword, add a distinct() function call */
	if (sel->distinct) {
		t.op = JSONOP_LITERAL;
		ja = jcalloc(&t);
		ja->u.literal = json_boolean(1);
		jc = jcfunc("distinct", jc, ja, NULL);
	}

	/* If there's a LIMIT number, add a .slice(0,limit) function call */
	if (sel->limit) {
		jsoncalc_t *jzero;
		token_t	t;
		t.op = JSONOP_NUMBER;
		t.full = "0";
		t.len = 1;
		jzero = jcalloc(&t);
		jc = jcfunc("slice", jc, jzero, sel->limit);
	}

	return jc;
}

/* Check a single character against a token.  The characters used are:
 *   ^  Start of expression, or parenthesis/bracket/brace
 *   (  JSONOP_STARTPAREN (parenthesis)
 *   )  JSONOP_ENDPAREN
 *   [  JSONOP_STARTARRAY (array generator)
 *   ]  JSONOP_ENDARRAY
 *   {  JSONOP_STARTOBJECT (object generator)
 *   }  JSONOP_ENDOBJECT
 *   @  JSONOP_SUBSCRIPT (array subscript)
 *   $  JSONOP_ENVIRON
 *   ?  JSONOP_QUESTION
 *   :  JSONOP_COLON or JSONOP_MAYBEMEMBER
 *   &  JSONOP_AND
 *   b  JSONOP_BETWEEN
 *   i  JSONOP_IN
 *   N  JSONOP_ISNULL or JSONOP_ISNOTNULL
 *   l  Literal
 *   n  Name or string literal
 *   m  Object member -- a complete name:value clause or just a name.
 *   M  Object member -- a complete name:value clause.
 *   S  JSONOP_SELECT
 *   D  JSONOP_DISTINCT
 *   A  JSONOP_AS
 *   F  JSONOP_FROM
 *   W  JSONOP_WHERE
 *   G  JSONOP_GROUPBY
 *   H  JSONOP_HAVING
 *   O  JSONOP_ORDERBY
 *   L  JSONOP_LIMIT
 *   d  JSONOP_DESCENDING
 *   .  JSONOP_DOT
 *   ,  JSONOP_COMMA
 *   *  JSONOP_MULTIPLY
 *   +  Binary operator (needs right param)
 *   -  Unary operator (needs right param)
 *   x  Literal or completed operator
 */
static int pattern_single(jsoncalc_t *jc, char pchar)
{
	/* Check the jsoncalc node against a single character */
	switch (pchar) {
	  case '^': /* handled in pattern() because it needs offsetsp */

	  case '(': /* JSONOP_STARTPAREN (parenthesis) */
		if (jc->op != JSONOP_STARTPAREN)
			return FALSE;
		break;

	  case ')': /* JSONOP_ENDPAREN (parenthesis) */
		if (jc->op != JSONOP_ENDPAREN)
			return FALSE;
		break;

	  case '[': /* JSONOP_STARTARRAY (array generator) */
		if (jc->op != JSONOP_STARTARRAY)
			return FALSE;
		break;

	  case ']': /* JSONOP_ENDARRAY */
		if (jc->op != JSONOP_ENDARRAY)
			return FALSE;
		break;

	  case '{': /* JSONOP_STARTOBJECT (object generator) */
		if (jc->op != JSONOP_STARTOBJECT)
			return FALSE;
		break;

	  case '}': /* JSONOP_ENDOBJECT */
		if (jc->op != JSONOP_ENDOBJECT)
			return FALSE;
		break;

	  case '@': /* JSONOP_SUBSCRIPT (array subscript) */
		if (jc->op != JSONOP_SUBSCRIPT)
			return FALSE;
		break;

	  case '$': /* JSONOP_ENVIRON ($ for environ variable, without name) */
		if (jc->op != JSONOP_ENVIRON || jc->LEFT)
			return FALSE;
		break;

	  case '?': /* JSONOP_QUESTION (array subscript) */
		if (jc->op != JSONOP_QUESTION)
			return FALSE;
		break;

	  case ':': /* JSONOP_COLON or JSONOP_MAYBEMEMBER */
		if (jc->op != JSONOP_COLON && jc->op != JSONOP_MAYBEMEMBER)
			return FALSE;
		break;

	  case 'b': /* JSONOP_BETWEEN */
		if (jc->op != JSONOP_BETWEEN)
			return FALSE;
		break;

	  case 'i': /* JSONOP_IN */
		if (jc->op != JSONOP_IN)
			return FALSE;
		break;

	  case 'N': /* JSONOP_ISNULL or JSONOP_ISNOTNULL */
		if (jc->op != JSONOP_ISNULL && jc->op != JSONOP_ISNOTNULL)
			return FALSE;
		break;

	  case '&': /* JSONOP_AND */
		if (jc->op != JSONOP_AND)
			return FALSE;
		break;

	  case 'l': /* Literal */
		if (jc->op != JSONOP_LITERAL)
			return FALSE;
		break;

	  case 'n': /* Name */
		if (jc->op != JSONOP_NAME
		 && !JC_IS_STRING(jc)
		 && jc->op != JSONOP_DOT)
			return FALSE;
		break;

	  case 'm': /* Member or list of members (name or name:expr)*/
		if (jc->op != JSONOP_NAME
		 && (jc->op != JSONOP_COLON
		  || !jc->LEFT
		  || (jc->LEFT->op != JSONOP_NAME && !JC_IS_STRING(jc->LEFT))
		  || !jc->RIGHT)) {
			/* If we get here then it isn't the first
			 * member in a list, but maybe it's the comma
			 * for a later member in the list?
			 */
			if (jc->op != JSONOP_COMMA
			 || !jc->LEFT
			 || (jc->LEFT->op != JSONOP_COLON
			  && (jc->LEFT->op != JSONOP_NAME && jc->LEFT->op != JSONOP_COMMA))
			 || !jc->RIGHT)
				return FALSE;
		}
		break;

	  case 'M': /* Stricter member (name:expr) */
		if (jc->op != JSONOP_COLON
		 || !jc->LEFT
		 || (jc->LEFT->op != JSONOP_NAME && !JC_IS_STRING(jc->LEFT))
		 || !jc->RIGHT) {
			return FALSE;
		}
		break;

	  case 'S': /* JSONOP_SELECT */
		if (jc->op != JSONOP_SELECT)
			return FALSE;
		break;

	  case 'D': /* JSONOP_DISTINCT */
		if (jc->op != JSONOP_DISTINCT)
			return FALSE;
		break;

	  case 'A': /* JSONOP_AS */
		if (jc->op != JSONOP_AS)
			return FALSE;
		break;

	  case 'F': /* JSONOP_FROM */
		if (jc->op != JSONOP_FROM)
			return FALSE;
		break;

	  case 'W': /* JSONOP_WHERE */
		if (jc->op != JSONOP_WHERE)
			return FALSE;
		break;

	  case 'G': /* JSONOP_GROUPBY */
		if (jc->op != JSONOP_GROUPBY)
			return FALSE;
		break;

	  case 'H': /* JSONOP_HAVING */
		if (jc->op != JSONOP_HAVING)
			return FALSE;
		break;

	  case 'O': /* JSONOP_ORDERBY */
		if (jc->op != JSONOP_ORDERBY)
			return FALSE;
		break;

	  case 'L': /* JSONOP_LIMIT */
		if (jc->op != JSONOP_LIMIT)
			return FALSE;
		break;

	  case 'd': /* JSONOP_DESCENDING */
		if (jc->op != JSONOP_DESCENDING)
			return FALSE;
		break;

	  case '.': /* JSONOP_DOT */
		if (jc->op != JSONOP_DOT)
			return FALSE;
		break;

	  case ',': /* JSONOP_COMMA */
		if (jc->op != JSONOP_COMMA)
			return FALSE;
		break;

	  case '*': /* JSONOP_MULTIPLY */
		if (jc->op != JSONOP_MULTIPLY)
			return FALSE;
		break;

	  case '+': /* Incomplete binary operator */
		if ((operators[jc->op].optype != JCOP_INFIX && operators[jc->op].optype != JCOP_RIGHTINFIX)
		 || jc->RIGHT != NULL
		 || jc->op == JSONOP_COMMA)
			return FALSE;
		break;

	  case '-': /* Incomplete prefix unary operator */
		if (operators[jc->op].optype != JCOP_PREFIX
		 || jc->RIGHT != NULL)
			return FALSE;
		break;

	  case 'x': /* Literal or completed operator */
		if (jc->op != JSONOP_LITERAL
		 && jc->op != JSONOP_NAME
		 && jc->op != JSONOP_ARRAY
		 && jc->op != JSONOP_OBJECT
		 && jc->op != JSONOP_SUBSCRIPT
		 && jc->op != JSONOP_FNCALL
		 && jc->op != JSONOP_REGEX
		 && (jc->op != JSONOP_ENVIRON || jc->LEFT == NULL)
		 && ((jc->op != JSONOP_NEGATE
		   && jc->op != JSONOP_NOT)
		  || jc->RIGHT == NULL)
		 && (operators[jc->op].prec < 0
		  || jc->LEFT == NULL
		  || jc->RIGHT == NULL))
			return FALSE;
		if (jc->op == JSONOP_FNCALL && !jc->u.func.args)
			return FALSE;
		break;

	  default:
		abort();
	}

	return TRUE;
}

/* Recognize a pattern at the top of the stack. The pattern is given as a
 * string in which each character represents a type of jsonop_t, except that
 * a space splits it into a single-token-per-character segment and an
 * any-of-these-at-the-end segment.  The characters recognized are:
 */
static int pattern(stack_t *stack, char *want)
{
	char *pat;
	int  offsetsp;
	jsoncalc_t *jc;

	/* No special processing for the last token.  The number of
	 * characters should match the number of tokens
	 */
	offsetsp = stack->sp - strlen(want);

	/* Check everything on the stack */
	for (pat = want; *pat; pat++, offsetsp++) {
		/* Copy the top of the stack into variables, for convenience */
		if (offsetsp >= 0)
			jc = stack->stack[offsetsp];
		else
			jc = NULL;

		if (*pat == '^') {
			if (offsetsp >= 0
			 && jc->op != JSONOP_STARTPAREN
			 && jc->op != JSONOP_STARTARRAY
			 && jc->op != JSONOP_STARTOBJECT
			 && jc->op != JSONOP_SUBSCRIPT
			 && jc->op != JSONOP_VALUES
			 && jc->op != JSONOP_COLON
			 && jc->op != JSONOP_MAYBEMEMBER
			 && jc->op != JSONOP_ASSIGN
			 && jc->op != JSONOP_APPEND
			 && jc->op != JSONOP_COMMA
			 && jc->op != JSONOP_FROM
			 && jc->op != JSONOP_WHERE
			 && jc->op != JSONOP_GROUPBY
			 && jc->op != JSONOP_HAVING
			 && jc->op != JSONOP_ORDERBY
			 && jc->op != JSONOP_LIMIT)
				return FALSE;
			continue;
		} else if (!jc || !pattern_single(jc, *pat))
			return FALSE;
	}

	/* If we get here, it matched */
	return TRUE;
}


static int pattern_verbose(stack_t *stack, char *want, jsoncalc_t *next)
{
	int result = pattern(stack, want);
	dumpstack(stack, "pattern %s", want);
	if (json_debug_flags.calc && result) {
		if (next)
			printf(" -> TRUE, if prec>=%d (%s next.prec)\n", operators[next->op].prec, json_calc_op_name(next->op));
		else
			puts(" -> TRUE, unconditionally");
	}
	return result;
}

#define PATTERN(x)	(pattern_verbose(stack, match = (x), next))
#define PREC(o)		(!next || operators[o].prec >= operators[next->op].prec + (operators[next->op].optype == JCOP_RIGHTINFIX ? 1 : 0))


/* Reduce the parse state, back to a given precedence level or the most
 * recent incomplete parenthesis/bracket/brace.  Return NULL if successful,
 * or an error message if error.
 */
static char *reduce(stack_t *stack, jsoncalc_t *next, const char *srcend)
{
	token_t t;
	jsoncalc_t *jc, *jn;
	jsonfunc_t *jf;
	int     startsp = stack->sp;
	jsoncalc_t **top;
	char *match;

	/* Keep reducing until you can't */
	for (;; (void)(json_debug_flags.calc && dumpstack(stack, "Reduce %s", match))) {
		/* For convenience, set "top" to the top of the stack.
		 * We can then use top[-1] for the last item on the stack,
		 * top[-2] for the item before that, and so on.
		 */
		top = &stack->stack[stack->sp];

		/* The "between" operator has an odd precedence */
		if (PATTERN("xbx&x") && PREC(JSONOP_BETWEEN)) {
			top[-4]->LEFT = top[-5];
			top[-4]->RIGHT = top[-2];
			top[-2]->LEFT = top[-3];
			top[-2]->RIGHT = top[-1];
			top[-5] = top[-4];
			stack->sp -= 4;
			continue;
		}

		/* The ?: operator is weird */
		if (PATTERN("x?x:x") && PREC(JSONOP_QUESTION)) {
			top[-4]->LEFT = top[-5];
			top[-4]->RIGHT = top[-2];
			top[-2]->LEFT = top[-3];
			top[-2]->RIGHT = top[-1];
			top[-5] = top[-4];
			stack->sp -= 4;
			continue;
		}

		/* All unary expressions have high precedence */
		if ((PATTERN("^-x") || PATTERN("+-x")) && PREC(top[-2]->op)) {
			/* Use x as the parameter */
			top[-2]->RIGHT = top[-1];
			stack->sp--;
			continue;
		}

		/* The $name thing is like a unary, except that it might have
		 * a subscript that's handled first.
		 */
		if (PATTERN("$n") && (!next || next->op != JSONOP_STARTARRAY)) {
			/* Make the name be LHS of $ */
			top[-2]->LEFT = top[-1];
			stack->sp--;
			continue;
		} else if (PATTERN("$@")) {
			/* Convert the subscript to a $env[n] */
			json_calc_free(top[-2]);
			top[-2] = top[-1];
			top[-2]->op = JSONOP_ENVIRON;
			stack->sp--;
			continue;
		}

		/* In an array generator or SELECT clause, commas are treated
		 * specially because we want to convert "expr AS name" to
		 * "name:expr", and for any other expr we want to use the
		 * expression's source code as its name.  (So"SELECT count(*)"
		 * uses "count(*)" as the column label.)
		 */
		if ((PATTERN("Sx") && PREC(JSONOP_COMMA))
		 || (PATTERN("{x") && PREC(JSONOP_COMMA))) {
			/* First element.  Convert it */
			top[-1] = fixcolon(stack, srcend);
		}
		else if ((PATTERN("Sx,x") || PATTERN("{x,x")) && PREC(JSONOP_COMMA)) {
			/* Convert the next element, and reduce the comma */
			top[-1] = fixcolon(stack, srcend);
			top[-2]->LEFT = top[-3];
			top[-2]->RIGHT = top[-1];
			top[-3] = top[-2];
			stack->sp -= 2;
			top -= 2;
		}

		/* SQL SELECT */
		if (PATTERN("S*")) {
			/* Drop the "*" */
			stack->sp--;
			continue;
		} else if (PATTERN("Sx") && PREC(JSONOP_FROM)) {
			/* Save the whole expression (a comma list) as the
			 * column list.
			 */
			top[-2]->u.select->select = top[-1];
			stack->sp--;
			continue;
		} else if (PATTERN("SFx,n") && PREC(JSONOP_COMMA)) {
			/* Comma in a FROM clause adds to an unroll list */
			jc = top[-5];
			if (!jc->u.select->unroll)
				jc->u.select->unroll = json_array();
			json_append(jc->u.select->unroll, json_string(top[-1]->u.text, -1));
			/* Remove the name and comma, keep "SFx" */
			json_calc_free(top[-1]);
			json_calc_free(top[-2]);
			stack->sp -= 2;
			continue;
		} else if (PATTERN("SFx") && PREC(JSONOP_FROM)) {
			/* Save the table in select.from */
			top[-3]->u.select->from = top[-1];
			stack->sp -= 2;
			continue;
		} else if (PATTERN("SWx") && PREC(JSONOP_WHERE)) {
			/* Save the condition in select.where */
			top[-3]->u.select->where = top[-1];
			stack->sp -= 2;
			continue;
		} else if (PATTERN("SGn,n") && PREC(JSONOP_COMMA)) {
			/* Add the first name to an array of names */
			jc = top[-5];
			if (!jc->u.select->groupby)
				jc->u.select->groupby = json_array();
			json_append(jc->u.select->groupby, json_string(top[-3]->u.text, -1));

			/* Remove the first name and comma, keep "SG" and "n" */
			json_calc_free(top[-3]);
			json_calc_free(top[-2]);
			top[-3] = top[-1];
			stack->sp -= 2; /* keep "SGn" */
			continue;
		} else if (PATTERN("SGn") && PREC(JSONOP_GROUPBY)) {
			/* Add the name to an array of names */
			jc = top[-3];
			if (!jc->u.select->groupby)
				jc->u.select->groupby = json_array();
			json_append(jc->u.select->groupby, json_string(top[-1]->u.text, -1));
			json_calc_free(top[-1]);
			stack->sp--; /* keep "SG" */
			continue;
		} else if (PATTERN("SG") && PREC(JSONOP_GROUPBY)) {
			stack->sp--; /* keep "S" */
			continue;
		} else if (PATTERN("SHx") && PREC(JSONOP_HAVING)) {
			/* Save the condition in select.having */
			top[-3]->u.select->having = top[-1];
			stack->sp -= 2;
			continue;
		} else if (PATTERN("SOnd,n") && PREC(JSONOP_COMMA)) {
			/* Add the name to an array of names */
			jc = top[-6];
			if (!jc->u.select->orderby)
				jc->u.select->orderby = json_array();
			json_append(jc->u.select->orderby, json_boolean(1));
			json_append(jc->u.select->orderby, json_string(top[-4]->u.text, -1));

			/* Discard first name and comma, but keep "SO" and "n"*/
			json_calc_free(top[-4]);
			json_calc_free(top[-2]);
			top[-4] = top[-1];
			stack->sp -= 3; /* keep "SOn" */
			continue;
		} else if (PATTERN("SOn,n") && PREC(JSONOP_COMMA)) {
			/* Add the name to an array of names */
			jc = top[-5];
			if (!jc->u.select->orderby)
				jc->u.select->orderby = json_array();
			json_append(jc->u.select->orderby, json_string(top[-3]->u.text, -1));

			/* Discard first name and comma, but keep "SO" and "n"*/
			json_calc_free(top[-3]);
			json_calc_free(top[-2]);
			top[-3] = top[-1];
			stack->sp -= 2;
			continue;
		} else if (PATTERN("SOnd") && PREC(JSONOP_ORDERBY)) {
			/* Add the name to an array of names */
			jc = top[-4];
			if (!jc->u.select->orderby)
				jc->u.select->orderby = json_array();
			json_append(jc->u.select->orderby, json_boolean(1));
			json_append(jc->u.select->orderby, json_string(top[-2]->u.text, -1));
			json_calc_free(top[-2]);
			stack->sp -= 2; /* keep "SO" */
			continue;
		} else if (PATTERN("SOn") && PREC(JSONOP_ORDERBY)) {
			/* Add the name to an array of names */
			jc = top[-3];
			if (!jc->u.select->orderby)
				jc->u.select->orderby = json_array();
			json_append(jc->u.select->orderby, json_string(top[-1]->u.text, -1));
			json_calc_free(top[-1]);
			stack->sp--;
			continue;
		} else if (PATTERN("SO") && PREC(JSONOP_ORDERBY)) {
			stack->sp--; /* keep "S" */
			continue;
		} else if (PATTERN("SLl") && PREC(JSONOP_LIMIT)) {
			top[-3]->u.select->limit = top[-1];

			/* Remove "LIMIT" and the number, but keep "SELECT" */
			stack->sp -= 2;
			continue;
		} else if (PATTERN("S") && PREC(JSONOP_SELECT)) {
			/* All parts of the SELECT have now been parsed.  All
			 * we need to do now is convert it to a "normal"
			 * jsoncalc expression.
			 */
			jc = top[-1];
			top[-1] = jcselect(jc->u.select);
			free(jc->u.select);
			free(jc);
			continue;
		}


		/* Binary operators should be resolved if high precedence */
		if (PATTERN("x+x") && PREC(top[-2]->op)) {
			/* Some special rules */
			if (top[-2]->op == JSONOP_DOT) {
				if (JC_IS_STRING(top[-1])) {
					/* Convert the string to a name */
					t.op = JSONOP_NAME;
					t.full = top[-1]->u.literal->text;
					t.len = strlen(t.full);
					jn = jcalloc(&t);
					json_calc_free(top[-1]);
					top[-1] = jn;
				} else if (top[-1]->op != JSONOP_NAME) {
					return "The . operator requires a name on the right";
				}
			}

			/* Use x's as parameters */
			top[-2]->LEFT = top[-3];
			top[-2]->RIGHT = top[-1];
			top[-3] = top[-2];
			stack->sp -= 2;
			continue;
		}

		/* Commas can be handled like operators above, except for
		 * members of an object generator since "name:expr" isn't
		 * exactly an expression.  And that forces us to handle
		 * commas specially in array generators and function calls
		 * too.
		 */
		if ((PATTERN("{m,M") || PATTERN("x(x,x") || PATTERN("[x,x"))
		 && PREC(JSONOP_COMMA)) {
			top[-2]->LEFT = top[-3];
			top[-2]->RIGHT = top[-1];
			top[-3] = top[-2];
			stack->sp -= 2;
			continue;
		}

		/* Function calls */
		if (PATTERN("x()") || (PATTERN("x(*)") && !top[-2]->RIGHT)) {
			/* Function call with no extra parameters.  If x is
			 * a dotted expression then the left-hand-side object
			 * is a parameter, otherwise we use "this".
			 */
			if (top[-2]->op == JSONOP_MULTIPLY) /* asterisk */
				startsp = stack->sp - 4;
			else
				startsp = stack->sp - 3;
			if (stack->stack[startsp]->op == JSONOP_DOT) {
				jc = stack->stack[startsp]->LEFT;
				jn = stack->stack[startsp]->RIGHT;
			} else {
				jc = NULL; /* we'll make it "this" later */
				jn = stack->stack[startsp];
			}
			if (jn->op != JSONOP_NAME)
				return "Syntax error - Name expected";
			jf = json_calc_function_by_name(jn->u.text);
			if (jf == NULL) {
				snprintf(stack->errbuf, sizeof stack->errbuf, "Unknown function %s", jn->u.text);
				return stack->errbuf;
			}
			if (jc == NULL) {
				if (top[-2]->op == JSONOP_MULTIPLY) {
					/* For function(*) use true as the argument */
					t.op = JSONOP_BOOLEAN;
					t.full = "true";
					t.len = 4;
					jc = jcalloc(&t);
				} else {
					/* For function() use this as the argument */
					t.op = JSONOP_NAME;
					t.full = "this";
					t.len = 4;
					jc = jcalloc(&t);
				}
			}
			stack->stack[startsp]->op = JSONOP_FNCALL;
			stack->stack[startsp]->u.func.jf = jf;
			stack->stack[startsp]->u.func.args = fixcomma(jc, JSONOP_ARRAY);
			stack->stack[startsp]->u.func.agoffset = 0;
			stack->sp = startsp + 1;
			continue;
		} else if (PATTERN("x(x)")) {
			/* Function call with extra parameters */

			/* May be name(args) or arg1.name(args) */
			jn = top[-4];
			if (jn->op == JSONOP_DOT)
				jn = jn->RIGHT;
			if (jn->op != JSONOP_NAME)
				return "Syntax error - Function name expected";
			jf = json_calc_function_by_name(jn->u.text);
			if (!jf) {
				snprintf(stack->errbuf, sizeof stack->errbuf, "Unknown function \"%s\"", jn->u.text);
				return stack->errbuf;
			}

			/* Convert parameters to an array generator.  If the
			 * arg1.name(args...) notation was used, convert to
			 * name(arg1, args...).
			 */
			jc = fixcomma(top[-2], JSONOP_ARRAY);
			if (top[-4]->op == JSONOP_DOT) {
				jsoncalc_t *tmp = top[-4];
				tmp->op = JSONOP_ARRAY;
				tmp->RIGHT = jc;
				jc = tmp;
			}

			/* Store it - convert name to function call */
			jn->op = JSONOP_FNCALL;
			jn->u.func.jf = jf;
			jn->u.func.args = jc;
			jn->u.func.agoffset = 0;
			top[-4] = jn;
			stack->sp -= 3;
			continue;
		}

		/* Parentheses (must be after function call pattern) */
		if (PATTERN("(x)")) {
			top[-3] = top[-2];
			stack->sp -= 2;
			continue;
		}

		/* Subscripts on a value */
		if (PATTERN("x[x]")) {
			/* Subscript with a value */
			t.op = JSONOP_SUBSCRIPT;
			jc = jcalloc(&t);
			jc->LEFT = top[-4];
			jc->RIGHT = top[-2];
			top[-4] = jc;
			stack->sp -= 3;
			continue;
		}

		/* Array generators (must be checked after subscript pattern) */
		if (PATTERN("^[]") || PATTERN("xi[)")) { /*!!!*/
			/* Empty array generator, convert from STARTARRAY and
			 * ENDARRAY to just ARRAY
			 */
			t.op = JSONOP_ARRAY;
			top[-2] = jcalloc(&t);
			stack->sp--;
			continue;
		} else if (PATTERN("[x]") || PATTERN("xi[x)")) { /*!!!*/
			/* Non-empty array generator.  All elements are in
			 * a comma expression in top[-2].  Convert comma to
			 * array.
			 */
			top[-3] = fixcomma(top[-2], JSONOP_ARRAY);
			stack->sp -= 2;
			continue;
		} else if (PATTERN("[x,]")) {
			/* Superfluous trailing comma in array generator */
			top[-4] = fixcomma(top[-3], JSONOP_ARRAY);
			stack->sp -= 3;
			continue;
		}

		/* Treat "x is null" and "x is not null" like "x === null"
		 * and "x !== null"
		 */
		if (PATTERN("xN")) {
			top[-1]->LEFT = top[-2];
			t.op = JSONOP_NULL;
			t.full = "null";
			t.len = 4;
			top[-1]->RIGHT = jcalloc(&t);
			if (top[-1]->op == JSONOP_ISNULL)
				top[-1]->op = JSONOP_EQSTRICT;
			else
				top[-1]->op = JSONOP_NESTRICT;
			top[-2] = top[-1];
			stack->sp--;
			continue;
		}

		/* Object generators */
		if (PATTERN("n:x") && PREC(JSONOP_COLON)) {
			/* colon expression */
			top[-3]->op = JSONOP_NAME; /* string to name */
			top[-2]->LEFT = top[-3];
			top[-2]->RIGHT = top[-1];
			top[-3] = top[-1];
			stack->sp -= 2;
			continue;
		} else if (PATTERN("{}")) {
			/* Empty object generator, convert from STARTOBJECT
			 * ENDOBJECT to just OBJECT
			 */
			t.op = JSONOP_OBJECT;
			top[-2] = jcalloc(&t);
			stack->sp--;
			continue;
		} else if (PATTERN("{m}") || PATTERN("{x}")) {
			/* Non-empty object.  All elements are in a comma
			 * expression in top[-2].  Convert comma to object.
			 */
			top[-3] = fixcomma(top[-2], JSONOP_OBJECT);
			stack->sp -= 2;
			top -= 2;

			/* Convert any simple names to colon expressions. Also
			 * convert any strings to names, where unambiguous.
			 */
			for (jn = top[-1]; jn; jn = jn->RIGHT) {
				if (jn->LEFT->op == JSONOP_COLON || jn->LEFT->op == JSONOP_MAYBEMEMBER) {
					/* If left of colon is a string instead
					 * of a name, fix it
					 */
					if (JC_IS_STRING(jn->LEFT->LEFT)) {
						jsoncalc_t *name;
						t.op = JSONOP_NAME;
						t.full = jn->LEFT->LEFT->u.literal->text;
						t.len = strlen(t.full);
						name = jcalloc(&t);
						json_calc_free(jn->LEFT->LEFT);
						jn->LEFT->LEFT = name;
					}
				} else {
					return "Object generators use a series of name:expr pairs";
				}
			}

			continue;
		}

		/* If we get here, we ran out of things to reduce */
		break;
	}
	return NULL;
}

/* Incorporate the next lexical token into the parse state */
static void shift(stack_t *stack, jsoncalc_t *jc, const char *str)
{
	stack->stack[stack->sp] = jc;
	stack->str[stack->sp] = str;
	stack->sp++;
	dumpstack(stack, "Shift %.1s", str);
}

/* Check whether JSONOP_COLON is misused anywhere */
static int parsecolon(jsoncalc_t *jc)
{
	/* Defend against NULL */
	if (!jc)
		return 0;

	switch (jc->op) {
	  case JSONOP_COLON:
		return 1;

	  case JSONOP_SUBSCRIPT:
		if (parsecolon(jc->LEFT))
			return 1;
		if (jc->RIGHT->op == JSONOP_COLON)
			return parsecolon(jc->RIGHT->RIGHT);
		else
			return parsecolon(jc->RIGHT);
		break;

	  case JSONOP_QUESTION:
		if (parsecolon(jc->LEFT))
			return 1;
		if (jc->RIGHT->op == JSONOP_COLON)
			return parsecolon(jc->RIGHT->LEFT) || parsecolon(jc->RIGHT->RIGHT);
		else
			return parsecolon(jc->RIGHT);

	  case JSONOP_OBJECT:
		return 0;

	  case JSONOP_LITERAL:
	  case JSONOP_STRING:
	  case JSONOP_NUMBER:
	  case JSONOP_BOOLEAN:
	  case JSONOP_NULL:
	  case JSONOP_NAME:
	  case JSONOP_FROM:
	  case JSONOP_REGEX:
		break;

	  case JSONOP_DOT:
	  case JSONOP_DOTDOT:
	  case JSONOP_ELLIPSIS:
	  case JSONOP_ARRAY:
	  case JSONOP_COALESCE:
	  case JSONOP_MAYBEMEMBER:
	  case JSONOP_NJOIN:
	  case JSONOP_LJOIN:
	  case JSONOP_RJOIN:
	  case JSONOP_NEGATE:
	  case JSONOP_ISNULL:
	  case JSONOP_ISNOTNULL:
	  case JSONOP_MULTIPLY:
	  case JSONOP_DIVIDE:
	  case JSONOP_MODULO:
	  case JSONOP_ADD:
	  case JSONOP_SUBTRACT:
	  case JSONOP_BITNOT:
	  case JSONOP_BITAND:
	  case JSONOP_BITOR:
	  case JSONOP_BITXOR:
	  case JSONOP_NOT:
	  case JSONOP_AND:
	  case JSONOP_OR:
	  case JSONOP_LT:
	  case JSONOP_LE:
	  case JSONOP_EQ:
	  case JSONOP_NE:
	  case JSONOP_GE:
	  case JSONOP_GT:
	  case JSONOP_ICEQ:
	  case JSONOP_ICNE:
	  case JSONOP_LIKE:
	  case JSONOP_NOTLIKE:
	  case JSONOP_IN:
	  case JSONOP_EQSTRICT:
	  case JSONOP_NESTRICT:
	  case JSONOP_COMMA:
	  case JSONOP_BETWEEN:
	  case JSONOP_ENVIRON:
	  case JSONOP_ASSIGN:
	  case JSONOP_APPEND:
	  case JSONOP_MAYBEASSIGN:
	  case JSONOP_VALUES:
	  case JSONOP_EACH:
	  case JSONOP_GROUP:
	  case JSONOP_FIND:
		return parsecolon(jc->LEFT) || parsecolon(jc->RIGHT);

	  case JSONOP_FNCALL:
		return parsecolon(jc->u.func.args);

	  case JSONOP_AG:
	  case JSONOP_STARTPAREN:
	  case JSONOP_ENDPAREN:
	  case JSONOP_STARTARRAY:
	  case JSONOP_ENDARRAY:
	  case JSONOP_STARTOBJECT:
	  case JSONOP_ENDOBJECT:
	  case JSONOP_SELECT:
	  case JSONOP_DISTINCT:
	  case JSONOP_AS:
	  case JSONOP_WHERE:
	  case JSONOP_GROUPBY:
	  case JSONOP_HAVING:
	  case JSONOP_ORDERBY:
	  case JSONOP_DESCENDING:
	  case JSONOP_LIMIT:
	  case JSONOP_INVALID:
		/* None of these should appear in a parsed expression */
		abort();
	}

	return 0;
}


/* Search for aggregate functions, and add JSONOP_AG to the expression where
 * appropriate.  Return the altered expression.
 */
static jsoncalc_t *parseag(jsoncalc_t *jc, jsonag_t *ag)
{
	int     addhere;
	token_t t;

	/* Defend against NULL */
	if (!jc)
		return NULL;

	/* If no aggregate list passed in, make one now.  Make it big */
	addhere = 0;
	if (!ag) {
		ag = (jsonag_t *)calloc(1, sizeof(*ag) + 100 * sizeof(jsoncalc_t *));
		addhere = 1;
	}

	/* Recursively check subexpressions */
	switch (jc->op) {
	  case JSONOP_LITERAL:
	  case JSONOP_STRING:
	  case JSONOP_NUMBER:
	  case JSONOP_BOOLEAN:
	  case JSONOP_NULL:
	  case JSONOP_NAME:
	  case JSONOP_FROM:
	  case JSONOP_REGEX:
		break;

	  case JSONOP_DOT:
	  case JSONOP_DOTDOT:
	  case JSONOP_ELLIPSIS:
	  case JSONOP_ARRAY:
	  case JSONOP_OBJECT:
	  case JSONOP_SUBSCRIPT:
	  case JSONOP_COALESCE:
	  case JSONOP_QUESTION:
	  case JSONOP_COLON:
	  case JSONOP_MAYBEMEMBER:
	  case JSONOP_NJOIN:
	  case JSONOP_LJOIN:
	  case JSONOP_RJOIN:
	  case JSONOP_NEGATE:
	  case JSONOP_ISNULL:
	  case JSONOP_ISNOTNULL:
	  case JSONOP_MULTIPLY:
	  case JSONOP_DIVIDE:
	  case JSONOP_MODULO:
	  case JSONOP_ADD:
	  case JSONOP_SUBTRACT:
	  case JSONOP_BITNOT:
	  case JSONOP_BITAND:
	  case JSONOP_BITOR:
	  case JSONOP_BITXOR:
	  case JSONOP_NOT:
	  case JSONOP_AND:
	  case JSONOP_OR:
	  case JSONOP_LT:
	  case JSONOP_LE:
	  case JSONOP_EQ:
	  case JSONOP_NE:
	  case JSONOP_GE:
	  case JSONOP_GT:
	  case JSONOP_ICEQ:
	  case JSONOP_ICNE:
	  case JSONOP_LIKE:
	  case JSONOP_NOTLIKE:
	  case JSONOP_IN:
	  case JSONOP_EQSTRICT:
	  case JSONOP_NESTRICT:
	  case JSONOP_COMMA:
	  case JSONOP_BETWEEN:
	  case JSONOP_ENVIRON:
	  case JSONOP_ASSIGN:
	  case JSONOP_APPEND:
	  case JSONOP_MAYBEASSIGN:
	  case JSONOP_VALUES:
	  case JSONOP_FIND:
		jc->LEFT = parseag(jc->LEFT, ag);
		jc->RIGHT = parseag(jc->RIGHT, ag);
		break;

	  case JSONOP_EACH:
	  case JSONOP_GROUP:
		jc->LEFT = parseag(jc->LEFT, ag);
		jc->RIGHT = parseag(jc->RIGHT, NULL); /* gets its own list */
		break;

	  case JSONOP_FNCALL:
		/* If this is an aggregate function, add it */
		if (jc->u.func.jf->agfn) {
			ag->ag[ag->nags++] = jc;
			jc->u.func.agoffset = ag->agsize;
			ag->agsize += jc->u.func.jf->agsize;
		}

		/* Check arguments */
		jc->u.func.args = parseag(jc->u.func.args, ag);
		break;

	  case JSONOP_AG:
	  case JSONOP_STARTPAREN:
	  case JSONOP_ENDPAREN:
	  case JSONOP_STARTARRAY:
	  case JSONOP_ENDARRAY:
	  case JSONOP_STARTOBJECT:
	  case JSONOP_ENDOBJECT:
	  case JSONOP_SELECT:
	  case JSONOP_DISTINCT:
	  case JSONOP_AS:
	  case JSONOP_WHERE:
	  case JSONOP_GROUPBY:
	  case JSONOP_HAVING:
	  case JSONOP_ORDERBY:
	  case JSONOP_DESCENDING:
	  case JSONOP_LIMIT:
	  case JSONOP_INVALID:
		/* None of these should appear in a parsed expression */
		abort();
	}

	/* If not supposed to add here, just return jc unchanged */
	if (!addhere)
		return jc;

	/* If no aggregates, then free ag and return jc */
	if (ag->nags == 0) {
		free(ag);
		return jc;
	}

	/* Resize ag to the right size, and insert it above jc */
	ag = (jsonag_t *)realloc(ag, sizeof(*ag) + ag->nags * sizeof(jsoncalc_t *));
	ag->expr = jc;
	t.op = JSONOP_AG;
	jc = jcalloc(&t);
	jc->u.ag = ag;
	return jc;
}


/* Parse a calc expression, and return it as a jsonop_t tree.  "str" is the
 * string to parse, and "end" is the end of the string or NULL to end at '\0'.
 * *refend will be set to the point where parsing stopped, which may be before
 * the end, e.g. if the string ends with a semicolon or other non-operator.
 * *referr will be set to an error message in a dynamically-allocated string.
 * canassign enables parsing "=" as an assignment operator.
 */
jsoncalc_t *json_calc_parse(const char *str, const char **refend, const char **referr, int canassign)
{
	const char    *c = str;
	jsoncalc_t *jc;
	char *err;
	token_t token;
	stack_t stack;

	/* Initialize the stack */
	stack.sp = 0;
	stack.canassign = canassign;
	err = NULL;

	/* Scan tokens through the end of the expression */
	while ((c = lex(c, &token, &stack)) != NULL && token.op != JSONOP_INVALID) {
		/* Convert token to a jsoncalc_t */
		jc = jcalloc(&token);

		/* Try to reduce */
		if (stack.sp == 0)
			err = NULL; /* can't reduce an empty stack */
		else if (stack.stack[stack.sp - 1]->op == JSONOP_NAME && jc->op == JSONOP_STARTPAREN)
			err = reduce(&stack, jc, token.full);
		else if (jc->op == JSONOP_STARTARRAY && !pattern(&stack, "^") && !pattern(&stack, "+"))
			err = reduce(&stack, jc, token.full);
		else if (jc == &selectdistinct) {
			if (stack.stack[stack.sp - 1]->op == JSONOP_SELECT) {
				stack.stack[stack.sp - 1]->u.select->distinct = 1;
				continue;
			}
		} else if (operators[jc->op].prec >= 0)
			err = reduce(&stack, jc, token.full);
		if (err)
			break;

		/* push it onto the stack */
		shift(&stack, jc, token.full);
	}

	/* Defend against calls where the expression is completely missing */
	if (!err && stack.sp == 0) {
		err = "Expression is missing";
	}

	/* One last reduce */
	if (!err) {
		if (json_debug_flags.calc)
			puts("Doing the final reduce...");
		err = reduce(&stack, NULL, token.full + token.len);
	}

	/* Some operators (tokens, really) are only used during parsing.
	 * If the stack still contains one of those, then we got an
	 * incomplete parse.
	 */
	if (!err && operators[stack.stack[0]->op].noexpr)
		err = "Syntax error - Incomplete expression";

	/* If this leaves an operator without operands, complain */
	switch (stack.sp > 0 ? operators[stack.stack[0]->op].optype : JCOP_OTHER) {
	case JCOP_OTHER:
		break;
	case JCOP_INFIX:
	case JCOP_RIGHTINFIX:
		if (!stack.stack[0]->LEFT || !stack.stack[0]->RIGHT)
			err = "Missing operand";
		break;
	case JCOP_PREFIX:
		if (!stack.stack[0]->RIGHT)
			err = "Missing operand of unary operator";
		break;
	case JCOP_POSTFIX:
		if (!stack.stack[0]->LEFT)
			err = "Missing operand of postfix operator";
		break;
	}

	/* Watch for misuse of a ":".  That token is used for several things,
	 * and its easier to check after parsing than during parsing.
	 */
	if (!err && parsecolon(stack.stack[0]))
		err = "Misuse of \":\"";

	/* If it compiled cleanly, look for aggregate functions */
	if (!err && stack.sp == 1)
		stack.stack[0] = parseag(stack.stack[0], NULL);

	/* Store the error message (or lack thereof) */
	if (referr)
		*referr = err ? strdup(err) : NULL;

	/* Store the end of the parse.  If there are surplus items on the
	 * stack, then that's where parsing really ended, otherwise use
	 * the end of the last token (or the start of it, if invalid).
	 */
	if (refend)
	{
		if (stack.sp >= 2)
			*refend = stack.str[1];
		else if (token.op == JSONOP_INVALID)
			*refend = token.full;
		else {
			*refend = token.full + token.len;

			/* If the last token was a quoted string or name,
			 * then the closing quote should *NOT* be included
			 * in the tail.
			 */
			if ((token.op == JSONOP_STRING || token.op == JSONOP_NAME) && **refend && strchr("\"'`", **refend))
				(*refend)++;
		}

		/* Skip past any trailing whitespace */
		*refend = skipwhitespace(*refend);
	}

	/* Clean up any extra stack items */
	while (stack.sp >= 2)
		json_calc_free(stack.stack[--stack.sp]);
	if (stack.sp == 1 && err)
		json_calc_free(stack.stack[--stack.sp]);

	return stack.sp == 0 ? NULL : stack.stack[0];
}
