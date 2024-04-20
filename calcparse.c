#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <assert.h>
#include "json.h"
#include "calc.h"

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
	char *full;
} token_t;

/* This is used as the expression parsing stack. */
typedef struct {
	jsoncalc_t *stack[100];
	char *str[100];
	int sp;
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

/* This table defines the relationship between text and the jsonop_t symbols.
 * It also includes precedence and quirks to help the parser.  As shown here,
 * the items are grouped by how they're related, but the lex() function sorts
 * them by op so you can use operators[jc->op] to find information about jc.
 */
static struct {
	char symbol[11];/* Derived form the JSONOP_xxxx enumerated value */
	char text[5];   /* text form of the operator */
	short prec;     /* precedence of the operator, or negative for non-operatos */
	jcoptype_t optype;/* token type */
	size_t len;     /* text length (computed at runtime) */
} operators[] = {
	{"ADD",		"+",	210,	JCOP_INFIX},
	{"AG",		"AG",	-1,	JCOP_OTHER},
	{"AND",		"&&",	140,	JCOP_INFIX},
	{"ARRAY",	"ARR",	-1,	JCOP_OTHER},
	{"AS",		"AS",	121,	JCOP_INFIX},
	{"ASSIGN",	"ASGN",	121,	JCOP_INFIX},
	{"BETWEEN",	"BTWN",	121,	JCOP_INFIX},
	{"BITAND",	"&",	170,	JCOP_INFIX},
	{"BITNOT",	"~",	240,	JCOP_INFIX},
	{"BITOR",	"|",	150,	JCOP_INFIX},
	{"BITXOR",	"^",	160,	JCOP_INFIX},
	{"BOOLEAN",	"BOO",	-1,	JCOP_OTHER},
	{"COALESCE",	"??",	130,	JCOP_INFIX},
	{"COLON",	":",	121,	JCOP_RIGHTINFIX}, /* sometimes part of ?: */
	{"COMMA",	",",	110,	JCOP_INFIX},
	{"CONST",	"CONS",	110,	JCOP_OTHER},
	{"DESCENDING",	"DES",	3,	JCOP_POSTFIX},
	{"DISTINCT",	"DIS",	2,	JCOP_OTHER},
	{"DIVIDE",	"/",	220,	JCOP_INFIX},
	{"DOT",		".",	270,	JCOP_INFIX},
	{"EACH",	"@@",	5,	JCOP_INFIX}, /*!!!*/
	{"ELIPSIS",	"..",	100,	JCOP_INFIX}, /*!!!*/
	{"ENDARRAY",	"]",	0,	JCOP_OTHER},
	{"ENDOBJECT",	"}",	0,	JCOP_OTHER},
	{"ENDPAREN",	")",	0,	JCOP_OTHER},
	{"EQ",		"==",	180,	JCOP_INFIX},
	{"EQSTRICT",	"===",	180,	JCOP_INFIX},
	{"EXPLAIN",	"EXP",	1,	JCOP_PREFIX},
	{"FNCALL",	"F",	170,	JCOP_OTHER}, /* function call */
	{"FROM",	"FRO",	2,	JCOP_OTHER},
	{"FUNCTION",	"FN",	-1,	JCOP_OTHER}, /* function definition */
	{"GE",		">=",	190,	JCOP_INFIX},
	{"GROUP",	"@",	5,	JCOP_INFIX},	/*!!!*/
	{"GROUPBY",	"GRO",	2,	JCOP_OTHER},
	{"GT",		">",	190,	JCOP_INFIX},
	{"ICEQ",	"=",	180,	JCOP_INFIX},
	{"ICNE",	"<>",	180,	JCOP_INFIX},
	{"IN",		"IN",	175,	JCOP_INFIX},
	{"ISNOTNULL",	"N!",	250,	JCOP_POSTFIX}, /* postfix operator */
	{"ISNULL",	"N=",	250,	JCOP_POSTFIX}, /* postfix operator */
	{"LE",		"<=",	190,	JCOP_INFIX},
	{"LIKE",	"LIK",	180,	JCOP_INFIX},
	{"LITERAL",	"LIT",	-1,	JCOP_OTHER},
	{"LJOIN",	"@<",	10,	JCOP_INFIX}, /*!!!*/
	{"LT",		"<",	190,	JCOP_INFIX},
	{"MODULO",	"%",	220,	JCOP_INFIX},
	{"MULTIPLY",	"*",	220,	JCOP_INFIX},
	{"NAME",	"NAM",	-1,	JCOP_OTHER},
	{"NE",		"!=",	180,	JCOP_INFIX},
	{"NEGATE",	"U-",	240,	JCOP_PREFIX},
	{"NESTRICT",	"!==",	180,	JCOP_INFIX},
	{"NJOIN",	"@=",	10,	JCOP_INFIX}, /*!!!*/
	{"NOT",		"!",	240,	JCOP_PREFIX},
	{"NOTLIKE",	"NLK",	180,	JCOP_INFIX},
	{"NULL",	"NUL",	-1,	JCOP_OTHER},
	{"NUMBER",	"NUM",	-1,	JCOP_OTHER},
	{"OBJECT",	"OBJ",	-1,	JCOP_OTHER},
	{"OR",		"||",	130,	JCOP_INFIX},
	{"ORDERBY",	"ORD",	2,	JCOP_OTHER},
	{"QUESTION",	"?",	121,	JCOP_RIGHTINFIX}, /* right-to-left associative */
	{"RETURN",	"RET",	-1,	JCOP_OTHER},
	{"RJOIN",	"@>",	10,	JCOP_INFIX}, /*!!!*/
	{"SELECT",	"SEL",	1,	JCOP_OTHER},
	{"SEMICOLON",	";",	-1,	JCOP_OTHER},
	{"STARTARRAY",	"[",	260,	JCOP_OTHER},
	{"STARTOBJECT",	"{",	260,	JCOP_OTHER},
	{"STARTPAREN",	"(",	260,	JCOP_OTHER},
	{"STRING",	"STR",	-1,	JCOP_OTHER},
	{"SUBSCRIPT",	"S[",	170,	JCOP_OTHER},
	{"SUBTRACT",	"-",	210,	JCOP_INFIX}, /* or JSONOP_NEGATE */
	{"VAR",		"VAR",	-1,	JCOP_OTHER},
	{"WHERE",	"WHE",	2,	JCOP_OTHER},
	{"INVALID",	"XXX",	666,	JCOP_OTHER}
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
static jsoncalc_t selectorderby = {JSONOP_ORDERBY};
static jsoncalc_t selectdesc = {JSONOP_DESCENDING};


/* Return the name of an operation, mostly for debugging. */
char *json_calc_op_name(jsonop_t jsonop)
{
	return operators[jsonop].symbol;
}

/* Return the name of an operator, mostly for debugging.  This differs from
 * json_op_calc_name() in that here we try to use the punctuation symbols for
 * operators instead of their name.
 */
static char *operatorname(jsonop_t op)
{
	return operators[op].text;
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
		str = json_serialize(calc->u.literal, 1);
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
		printf("[");
		if (calc->LEFT) {
			json_calc_dump(calc->LEFT);
			for (p = calc->RIGHT; p; p = p->RIGHT) {
				printf(",");
				json_calc_dump(p->LEFT);
			}
		}
		printf("]");
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
	  case JSONOP_NJOIN:
	  case JSONOP_LJOIN:
	  case JSONOP_RJOIN:
	  case JSONOP_SUBSCRIPT:
	  case JSONOP_DOT:
	  case JSONOP_ELIPSIS:
	  case JSONOP_COALESCE:
	  case JSONOP_QUESTION:
	  case JSONOP_COLON:
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
	  case JSONOP_COMMA:
	  case JSONOP_BETWEEN:
		if (calc->LEFT) {
			printf("(");
			json_calc_dump(calc->LEFT);
			printf("%s", operatorname(calc->op));
			if (calc->RIGHT)
				json_calc_dump(calc->RIGHT);
			printf(")");
		} else {
			printf("%s", operatorname(calc->op));
			if (calc->RIGHT)
				json_calc_dump(calc->RIGHT);
		}
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

	  case JSONOP_ORDERBY:
		printf(" ORDER BY");
		break;

	  case JSONOP_DESCENDING:
		printf(" DESCENDING");
		break;

	  case JSONOP_EXPLAIN:
		printf(" EXPLAIN");
		if (calc->RIGHT)
			json_calc_dump(calc->RIGHT);
		break;

	  case JSONOP_STARTPAREN:
	  case JSONOP_ENDPAREN:
	  case JSONOP_STARTARRAY:
	  case JSONOP_ENDARRAY:
	  case JSONOP_STARTOBJECT:
	  case JSONOP_ENDOBJECT:
	  case JSONOP_INVALID:
		printf(" %s ", json_calc_op_name(calc->op));
		break;

	  case JSONOP_ASSIGN:
	  case JSONOP_CONST:
	  case JSONOP_FUNCTION:
	  case JSONOP_RETURN:
	  case JSONOP_SEMICOLON:
	  case JSONOP_VAR:
		/* Not used yet */
		abort();
	}
}

/* Dump all entries in the parsing stack */
static int dumpstack(stack_t *stack, char *format, char *data)
{
	int     i;
	char	buf[40];

	/* If debugging is off, don't show */
	if (!json_debug_flags.calc)
		return 1;

	/* Print the key */
	sprintf(buf, format, data);
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

/* Given a starting point within a text buffer, parse the next token (storing
 * its details in *token) and return a pointer to the character after the token.
 */
char *lex(char *str, token_t *token, stack_t *stack)
{
	jsonop_t op, best;

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
	while (isspace(*str))
		str++;

	/* If no tokens, return NULL */
	if (!*str) {
		if (json_debug_flags.calc)
			printf("lex(): NULL\n");
		return NULL;
	}

	/* Start with some common defaults */
	token->op = JSONOP_INVALID;
	token->full = str;
	token->len = 1;

	/* Numbers */
	if (isdigit(*str) || (*str == '.' && isdigit(str[1])))
	{
		token->op = JSONOP_NUMBER;
		while (isdigit(token->full[token->len]))
			token->len++;
		if (token->full[token->len] == '.' && token->full[token->len + 1] != '.') {
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
		if (json_debug_flags.calc)
			printf("lex(): number JSONOP_NUMBER \"%.*s\"\n", token->len, token->full);
		return str;
	}

	/* Quoted strings */
	if (strchr("\"'`", *str))
	{
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
	if (isalpha(*str) || *str == '_')
	{
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
		} else if (token->len == 6 && !strncasecmp(token->full, "select", 6)) {
			token->len = 6;
			token->op = JSONOP_SELECT;
		} else if (token->len == 7 && !strncasecmp(token->full, "explain", 7) && token->full[7] == ' ') {
			token->len = 7;
			token->op = JSONOP_EXPLAIN;
		} else if (jcselecting(stack)) {
			/* The following SQL keywords are only recongized as
			 * part of a "select" clause.  This is because some of
			 * them such as "from" and "desc" are often used as
			 * member names, and "distinct" is a jsoncalc function
			 * name.  You could wrap them in backticks to prevent
			 * them from being taked as keywords, but that'd be
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
			} else if (token->len == 5 && !strncasecmp(token->full, "order by", 8)) {
				token->len = 8;
				token->op = JSONOP_ORDERBY;
			} else if (token->len == 10 && !strncasecmp(token->full, "descending", 10)) {
				token->len = 10;
				token->op = JSONOP_DESCENDING;
			} else if (token->len == 4 && !strncasecmp(token->full, "desc", 4)) {
				token->len = 4;
				token->op = JSONOP_DESCENDING;
			}
		}
		str += token->len;
		if (json_debug_flags.calc)
			printf("lex(): keyword JSONOP_%s \"%.*s\"\n", json_calc_op_name(token->op), token->len, token->full);
		return str;
	}

	/* Operators - find the longest matching name */
	best = JSONOP_INVALID;
	for (op = 0; op < JSONOP_INVALID; op++) {
		if (!strncmp(str, operators[op].text, operators[op].len)
		 && (best == JSONOP_INVALID || operators[best].len < operators[op].len))
			best = op;
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
	  case JSONOP_ORDERBY: return &selectorderby;
	  case JSONOP_DESCENDING: return &selectdesc;
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
		jc->u.literal = json_number(token->full, token->len);
	} else if (token->op == JSONOP_BOOLEAN || token->op == JSONOP_NULL) {
		jc->op = JSONOP_LITERAL;
		jc->u.literal = json_symbol(token->full, token->len);
	} else if (token->op == JSONOP_SELECT) {
		jc->u.select = (jsonselect_t *)calloc(1, sizeof(jsonselect_t));
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
	  case JSONOP_ELIPSIS:
	  case JSONOP_ARRAY:
	  case JSONOP_OBJECT:
	  case JSONOP_SUBSCRIPT:
	  case JSONOP_COALESCE:
	  case JSONOP_QUESTION:
	  case JSONOP_COLON:
	  case JSONOP_AS:
	  case JSONOP_EACH:
	  case JSONOP_GROUP:
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
	  case JSONOP_EXPLAIN:
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

	  case JSONOP_ASSIGN:
	  case JSONOP_CONST:
	  case JSONOP_FUNCTION:
	  case JSONOP_RETURN:
	  case JSONOP_SEMICOLON:
	  case JSONOP_VAR:
		/* These aren't used yet. */
		abort();

	  case JSONOP_DISTINCT:
	  case JSONOP_FROM:
	  case JSONOP_WHERE:
	  case JSONOP_GROUPBY:
	  case JSONOP_ORDERBY:
	  case JSONOP_DESCENDING:
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
static jsoncalc_t *fixcolon(stack_t *stack, char *srcend)
{
	jsoncalc_t *jc = stack->stack[stack->sp - 1];
	if (jc->op == JSONOP_AS) {
		/* Convert "expr AS name" to "name:expr" */
		jsoncalc_t *swapper = jc->LEFT;
		jc->LEFT = jc->RIGHT;
		jc->RIGHT = swapper;
		jc->op = JSONOP_COLON;
	} else if (jc->op != JSONOP_COLON) {
		/* Use the source text as the name */
		token_t t;
		t.op = JSONOP_NAME;
		t.full = stack->str[stack->sp - 1];
		if (strchr("\"'`", *t.full))
			t.full++;
		while (srcend - 1 > t.full && strchr("\"'` ", srcend[-1]))
			srcend--;
		t.len = (int)(srcend - t.full);
printf("name=\"%.*s\"\n", t.len, t.full);

		/* Make it a COLON expression */
		jc = jcleftright(JSONOP_COLON, jcalloc(&t), jc);
	} /* Else it is already "name:expr" */

	return jc;
}

/* Convert a select statement to a jsoncalc_t */
static jsoncalc_t *jcselect(jsonselect_t *sel)
{
	jsoncalc_t *jc, *ja;
	token_t t;

	/* If there's a column list in the SELECT clause, convert it to an
	 * object generator.
	 */
	if (sel->select)
		sel->select = fixcomma(sel->select, JSONOP_OBJECT);
printf("jcselect(\n");
printf("   distinct=%s\n", sel->distinct ? "true" : "false");
printf("   select=");json_calc_dump(sel->select);putchar('\n');
printf("   from=");json_calc_dump(sel->from);putchar('\n');
{char *tmp = json_serialize(sel->groupby, 0);printf("   groupby=%s\n", tmp); free(tmp);}
{char *tmp = json_serialize(sel->orderby, 0);printf("   orderby=%s\n", tmp); free(tmp);}
printf("   limit=%d\n", sel->limit);
printf(")\n");

	/* Was there a FROM clause? */
	if (sel->from) {
		/* Yes, use it */
		jc = sel->from;
	} else {
		/* No, arrange for us to choose a default at runtime */
		jc = &selectfrom;
	}

	/* If there's a GROUP BY list, add a groupBy() function call */
	if (sel->groupby) {
		/* The list is already json_t array of strings.  Make it be
		 * a jsoncalc_t literal.
		 */
		t.op = JSONOP_LITERAL;
		ja = jcalloc(&t);
		ja->u.literal = sel->groupby;

		/* Add groupBy() function call */
		jc = jcfunc("groupBy", jc, ja, NULL);
	}

	/* Is there a column list in SELECT, or a WHERE clause? */
	if (sel->select || sel->where || sel->groupby) {
		/* If we have both, then combine them via a ? operator */
		if (sel->select && sel->where)
			ja = jcleftright(JSONOP_QUESTION, sel->where, sel->select);
		else if (sel->select)
			ja = sel->select;
		else if (sel->where)
			ja = sel->where;
		else {
			/* With GROUP BY, we always want an @ even if all we
			 * do with it is "this".
			 */
			t.op = JSONOP_NAME;
			t.full = "this";
			t.len = 4;
			ja = jcalloc(&t);
		}

		/* Add an @ operator to connect select/where with table */
		jc = jcleftright(JSONOP_GROUP, jc, ja);
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
		jc = jcfunc("distinct", jc, NULL, NULL);
	}

	/* If there's a LIMIT number, add a .slice(0, limit) function call */
	if (sel->limit > 0) {
		jsoncalc_t *jzero, *jlimit;
		char	buf[30];
		token_t	t;
		t.op = JSON_NUMBER;
		t.full = "0";
		t.len = 1;
		jzero = jcalloc(&t);
		sprintf(buf, "%d", sel->limit);
		t.op = JSON_NUMBER;
		t.full = buf;
		t.len = strlen(buf);
		jlimit = jcalloc(&t);
		jc = jcfunc("slice", jc, jzero, jlimit);
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
 *   $  JSONOP_SUBSCRIPT (array subscript)
 *   ?  JSONOP_QUESTION
 *   :  JSONOP_COLON
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
 *   O  JSONOP_ORDERBY
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

	  case '$': /* JSONOP_SUBSCRIPT (array subscript) */
		if (jc->op != JSONOP_SUBSCRIPT)
			return FALSE;
		break;

	  case '?': /* JSONOP_QUESTION (array subscript) */
		if (jc->op != JSONOP_QUESTION)
			return FALSE;
		break;

	  case ':': /* JSONOP_COLON */
		if (jc->op != JSONOP_COLON)
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

	  case 'O': /* JSONOP_ORDERBY */
		if (jc->op != JSONOP_ORDERBY)
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
		 && ((jc->op != JSONOP_NEGATE
		   && jc->op != JSONOP_NOT
		   && jc->op != JSONOP_EXPLAIN)
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
			 && jc->op != JSONOP_SUBSCRIPT)
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
#define PREC(o)		(!next || operators[o].prec >= operators[next->op].prec)


/* Reduce the parse state, back to a given precedence level or the most
 * recent incomplete parenthesis/bracket/brace.  Return NULL if successful,
 * or an error message if error.
 */
static char *reduce(stack_t *stack, jsoncalc_t *next, char *srcend)
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

		/* In an array generator or SELECT clause, commas are treated
		 * specially because we want to conver "expr AS name" to
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
		if (PATTERN("SD")) {
			/* Fold the "DISTINCT" into "SELECT */
			top[-2]->u.select->distinct = 1;
			stack->sp--;
			continue;
		} else if (PATTERN("S*")) {
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
		} else if (PATTERN("SOnd") && PREC(top[-3]->op)) {
			/* Add the name to an array of names */
			jc = top[-4];
			if (!jc->u.select->orderby)
				jc->u.select->orderby = json_array();
			json_append(jc->u.select->orderby, json_symbol("true",-1));
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
			stack->sp -= 2;
			continue;
		} else if (PATTERN("SO") && PREC(JSONOP_ORDERBY)) {
			stack->sp--; /* keep "S" */
			continue;
		} else if (PATTERN("S") && PREC(JSONOP_SELECT)) {
if (next) printf("SELECT=%d >= %s=%d\n", operators[JSONOP_SELECT].prec, json_calc_op_name(next->op), operators[next->op].prec); else printf("!next\n");
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
		if ((PATTERN("x()") || PATTERN("x(*)")) && PREC(JSONOP_FNCALL)) {
			/* Function call with no extra parameters.  If x is
			 * a dotted expression then the left-hand-side object
			 * is a parameter, otherwise we use "this".
			 */
			if (top[-2]->op == JSONOP_MULTIPLY)
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
				return "Syntax error";
			jf = json_calc_function_by_name(jn->u.text);
			if (jf == NULL) {
				snprintf(stack->errbuf, sizeof stack->errbuf, "Unknown function %s", jn->u.text);
				return stack->errbuf;
			}
			if (jc == NULL) {
				t.op = JSONOP_BOOLEAN;
				t.full = "true";
				t.len = 4;
				jc = jcalloc(&t);
			}
			stack->stack[startsp]->op = JSONOP_FNCALL;
			stack->stack[startsp]->u.func.jf = jf;
			stack->stack[startsp]->u.func.args = fixcomma(jc, JSONOP_ARRAY);
			stack->stack[startsp]->u.func.agoffset = 0;
			stack->sp = startsp + 1;
			continue;
		} else if (PATTERN("x(x)") && PREC(JSONOP_FNCALL)) {
			/* Function call with extra parameters */

			/* May be name(args) or arg1.name(args) */
			jn = top[-4];
			if (jn->op == JSONOP_DOT)
				jn = jn->RIGHT;
			if (jn->op != JSONOP_NAME)
				return "Syntax error";
			jf = json_calc_function_by_name(jn->u.text);
			if (!jf) {
				snprintf(stack->errbuf, sizeof stack->errbuf, "Unknown function %s", jn->u.text);
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
		if (PATTERN("x[]")) {
			/* Empty subscript */
			t.op = JSONOP_SUBSCRIPT;
			jc = jcalloc(&t);
			jc->LEFT = top[-3];
			top[-3] = jc;
			stack->sp -= 2;
			continue;
		} else if (PATTERN("x[x]")) {
			/* Subscript with a value */
			t.op = JSONOP_SUBSCRIPT;
			jc = jcalloc(&t);
			jc->LEFT = top[-4];
			jc->RIGHT = top[-2];
			top[-4] = jc;
			stack->sp -= 3;
			continue;
		}

		/* "in" list, similar to array generator except it lets you
		 * use parentheses for the list instead of brackets.
		 */
		if (PATTERN("i(") && PREC(JSONOP_IN)) {
			/* Treat "in (" like "in [" */
			if (json_debug_flags.calc)
				printf("Converting ( to [\n");
			top[-1]->op = JSONOP_STARTARRAY;
			continue;
		} else if (PATTERN("xix") && PREC(JSONOP_IN)) {
			/* We have both operands of an "in" operator */
			top[-2]->LEFT = top[-3];
			top[-2]->RIGHT = top[-1];
			top[-3] = top[-2];
			stack->sp -= 2;
			continue;
		}

		/* Array generators (must be checked after subscript pattern) */
		if (PATTERN("^[]") || PATTERN("xi[)")) {
			/* Empty array generator, convert from STARTARRAY and
			 * ENDARRAY to just ARRAY
			 */
			t.op = JSONOP_ARRAY;
			top[-2] = jcalloc(&t);
			stack->sp--;
			continue;
		} else if (PATTERN("[x]") || PATTERN("xi[x)")) {
			/* Non-empty array generator.  All elements are in
			 * a comma expression in top[-2].  Convert comma to
			 * array.
			 */
			top[-3] = fixcomma(top[-2], JSONOP_ARRAY);
			stack->sp -= 2;
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
			 * expression in top[-2].  Convert comma to array.
			 */
			top[-3] = fixcomma(top[-2], JSONOP_OBJECT);
			stack->sp -= 2;
			top -= 2;

			/* Convert any simple names to colon expressions. Also
			 * convert any strings to names, where unambiguous.
			 */
			for (jn = top[-1]; jn; jn = jn->RIGHT) {
				if (jn->LEFT->op == JSONOP_COLON) {
					/* If left of colon is a string instead
					 * of a name, fix it
					 */
					if (JC_IS_STRING(jn->LEFT->LEFT)) {
						t.op = JSONOP_NAME;
						t.full = jn->LEFT->LEFT->u.literal->text;
						t.len = strlen(t.full);
						json_calc_free(jn->LEFT->LEFT);
						jn->LEFT->LEFT = jcalloc(&t);
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
static void shift(stack_t *stack, jsoncalc_t *jc, char *str)
{
	stack->stack[stack->sp] = jc;
	stack->str[stack->sp] = str;
	stack->sp++;
	dumpstack(stack, "Shift %.1s", str);
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
		break;

	  case JSONOP_DOT:
	  case JSONOP_ELIPSIS:
	  case JSONOP_ARRAY:
	  case JSONOP_OBJECT:
	  case JSONOP_SUBSCRIPT:
	  case JSONOP_COALESCE:
	  case JSONOP_QUESTION:
	  case JSONOP_COLON:
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
	  case JSONOP_EXPLAIN:
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

	  case JSONOP_ASSIGN:
	  case JSONOP_CONST:
	  case JSONOP_FUNCTION:
	  case JSONOP_RETURN:
	  case JSONOP_SEMICOLON:
	  case JSONOP_VAR:
		/* These aren't used yet. */
		abort();

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
	  case JSONOP_ORDERBY:
	  case JSONOP_DESCENDING:
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
 */
jsoncalc_t *json_calc_parse(char *str, char **refend, char **referr)
{
	char    *c = str;
	jsoncalc_t *jc;
	char *err;
	token_t token;
	stack_t stack;

	stack.sp = 0;
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
		else if (operators[jc->op].prec >= 0)
			err = reduce(&stack, jc, token.full);
		if (err)
			break;

		/* push it onto the stack */
		shift(&stack, jc, token.full);
	}

	/* One last reduce */
	if (!err)
		err = reduce(&stack, NULL, token.full);

	/* If it compiled cleanly, look for aggregate functions */
	if (stack.sp == 1)
		stack.stack[0] = parseag(stack.stack[0], NULL);

	/* Store the error message (or lack thereof) */
	if (referr)
		*referr = err ? strdup(err) : NULL;

	/* Store the end of the parse.  If there are surplus items on the
	 * stack, then that's where parsing really ended, otherwise use
	 * the end of the last token.
	 */
	if (refend)
	{
		if (stack.sp >= 2)
			*refend = stack.str[1];
		else {
			*refend = token.full + token.len;

			/* If the last token was a quoted string or name,
			 * then the closing quote should *NOT* be included
			 * in the tail.
			 */
			if ((token.op == JSONOP_STRING || token.op == JSONOP_NAME) && strchr("\"'`", **refend))
				(*refend)++;
		}

		/* Skip past any trailing whitespace */
		while (isspace(**refend))
			(*refend)++;
	}

	/* Clean up any extra stack items */
	while (stack.sp >= 2)
		json_calc_free(stack.stack[--stack.sp]);

	return stack.sp == 0 ? NULL : stack.stack[0];
}
