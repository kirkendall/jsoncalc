#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include <regex.h>
#include <assert.h>
#include <jx.h>


/* This is by far the largest single source file in the whole library.
 * It defines the jx_calc_parse() function, which is responsible for
 * parsing "calc" expressions for later use via the jx_calc() function
 * defined in calc.c
 *
 * The parser is a simple shift-reduce parser.  This type of parser works
 * great for operators, so I made nearly all syntax tokens be operators.
 * Things that AREN'T operators: Literals, object generators, array generators,
 * subscripts, and a few oddball things like elements of a SELECT statement. 
 */

/* These macros make jxcalc_t trees easier to navigate */
#define LEFT u.param.left
#define RIGHT u.param.right
#define JC_IS_STRING(jc)  ((jc)->op == JXOP_LITERAL && (jc)->u.literal->type == JX_STRING)

/* This represents a token from the "calc" expression */
typedef struct {
	jxop_t op;
	int len;
	const char *full;
} token_t;

/* This is used as the expression parsing stack. */
typedef struct {
	jxcalc_t *stack[100];
	const char	*str[100];
	int	sp;
	int	canassign;
	char	errbuf[100];
} stack_t;

/* These are broad classifications of jxop_t tokens */
typedef enum {
	JCOP_OTHER,	/* not an operator -- some other type of token */
	JCOP_INFIX,	/* left-associative infix binary operator */
	JCOP_RIGHTINFIX,/* right-associative infix binary operator */
	JCOP_PREFIX,	/* prefix unary operator */
	JCOP_POSTFIX	/* postfix unary operator */
} jcoptype_t;

/* This is used to collect details about a "select" statement */
typedef struct jxselect_s {
	jxcalc_t *select;	/* Selected columns as an object generator, or NULL */
	int	distinct;
	jxcalc_t *from;	/* expression that returns a table, or NULL for first array in context */
	jx_t *unroll;		/* list of field names to unroll, or NULL */
	jxcalc_t *where;	/* expression that selects rows, or NULL for all */
	jx_t *groupby;	/* list of field names, or NULL */
	jxcalc_t *having;	/* expression that selects groups */
	jx_t *orderby;	/* list of field names, or NULL */
	jxcalc_t *limit;	/* expression that limits the returned values */
} jxselect_t;


/* This table defines the relationship between text and the jxop_t symbols.
 * It also includes precedence and quirks to help the parser.  The items are
 * indexed by jxop_t, so you can use operators[jc->op] to find information
 * about jc.
 */
static struct {
	char symbol[11];/* Derived form the JXOP_xxxx enumerated value */
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
	{"NOTIN",	"IN",	175,	0,	JCOP_INFIX},
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
	{"SUBTRACT",	"-",	210,	0,	JCOP_INFIX}, /* or JXOP_NEGATE */
	{"VALUES",	"VAL",	125,	0,	JCOP_INFIX},
	{"WHERE",	"WHE",	2,	1,	JCOP_OTHER},
	{"INVALID",	"XXX",	666,	1,	JCOP_OTHER}
};

static int pattern(stack_t *stack, char *want);

/* We can use static copies of some jxcalc_t's */
static jxcalc_t startparen = {JXOP_STARTPAREN};
static jxcalc_t endparen = {JXOP_ENDPAREN};
static jxcalc_t startarray = {JXOP_STARTARRAY};
static jxcalc_t endarray = {JXOP_ENDARRAY};
static jxcalc_t startobject = {JXOP_STARTOBJECT};
static jxcalc_t endobject = {JXOP_ENDOBJECT};
static jxcalc_t selectdistinct = {JXOP_DISTINCT};
static jxcalc_t selectfrom = {JXOP_FROM};
static jxcalc_t selectwhere = {JXOP_WHERE};
static jxcalc_t selectgroupby = {JXOP_GROUPBY};
static jxcalc_t selecthaving = {JXOP_HAVING};
static jxcalc_t selectorderby = {JXOP_ORDERBY};
static jxcalc_t selectdesc = {JXOP_DESCENDING};
static jxcalc_t selectlimit = {JXOP_LIMIT};


/* Return the name of an operation, mostly for debugging. */
char *jx_calc_op_name(jxop_t jxop)
{
	return operators[jxop].symbol;
}

/* Dump an expression.  This is recursive and doesn't add a newline.  The
 * result isn't pretty, and it couldn't be reparsed to generate the same
 * tree.  It is merely for debugging.
 */
void jx_calc_dump(jxcalc_t *calc)
{
	jxcalc_t *p;
	char	*str;

	/* Defend against NULL */
	if (!calc)
		return;

	switch (calc->op) {
	  case JXOP_LITERAL:
		str = jx_serialize(calc->u.literal, NULL);
		printf("`%s'", str);
		free(str);
		break;

	  case JXOP_STRING:
		printf("\"%s\"", calc->u.text);
		break;

	  case JXOP_NUMBER:
	  case JXOP_BOOLEAN:
	  case JXOP_NULL:
	  case JXOP_NAME:
		printf("%s", calc->u.text);
		break;

	  case JXOP_ARRAY:
#if 0
		printf("[");
		if (calc->LEFT) {
			jx_calc_dump(calc->LEFT);
			for (p = calc->RIGHT; p; p = p->RIGHT) {
				printf(",");
				jx_calc_dump(p->LEFT);
			}
		}
		printf("]");
#else
		printf("[array]");
#endif
		break;

	  case JXOP_OBJECT:
		printf("{");
		if (calc->LEFT) {
			jx_calc_dump(calc->LEFT);
			for (p = calc->RIGHT; p; p = p->RIGHT) {
				printf(",");
				jx_calc_dump(p->LEFT);
			}
		}
		printf("}");
		break;

	  case JXOP_EACH:
	  case JXOP_GROUP:
	  case JXOP_FIND:
	  case JXOP_NJOIN:
	  case JXOP_LJOIN:
	  case JXOP_RJOIN:
	  case JXOP_SUBSCRIPT:
	  case JXOP_DOT:
	  case JXOP_DOTDOT:
	  case JXOP_ELLIPSIS:
	  case JXOP_COALESCE:
	  case JXOP_QUESTION:
	  case JXOP_COLON:
	  case JXOP_MAYBEMEMBER:
	  case JXOP_AS:
	  case JXOP_NEGATE:
	  case JXOP_ISNULL:
	  case JXOP_ISNOTNULL:
	  case JXOP_MULTIPLY:
	  case JXOP_DIVIDE:
	  case JXOP_MODULO:
	  case JXOP_ADD:
	  case JXOP_SUBTRACT:
	  case JXOP_BITNOT:
	  case JXOP_BITAND:
	  case JXOP_BITOR:
	  case JXOP_BITXOR:
	  case JXOP_NOT:
	  case JXOP_AND:
	  case JXOP_OR:
	  case JXOP_LT:
	  case JXOP_LE:
	  case JXOP_EQ:
	  case JXOP_NE:
	  case JXOP_GE:
	  case JXOP_GT:
	  case JXOP_ICEQ:
	  case JXOP_ICNE:
	  case JXOP_LIKE:
	  case JXOP_NOTIN:
	  case JXOP_NOTLIKE:
	  case JXOP_IN:
	  case JXOP_EQSTRICT:
	  case JXOP_NESTRICT:
	  case JXOP_BETWEEN:
	  case JXOP_ASSIGN:
	  case JXOP_APPEND:
	  case JXOP_MAYBEASSIGN:
	  case JXOP_VALUES:
		if (calc->LEFT) {
			printf("(");
			jx_calc_dump(calc->LEFT);
			printf("%s", operators[calc->op].text);
			if (calc->RIGHT)
				jx_calc_dump(calc->RIGHT);
			printf(")");
		} else {
			printf("%s", operators[calc->op].text);
			if (calc->RIGHT)
				jx_calc_dump(calc->RIGHT);
		}
		break;

	  case JXOP_COMMA:
		/* Comma expressions can get huge.  Best to hide LEFT */
		printf("...,");
		if (calc->RIGHT)
			jx_calc_dump(calc->RIGHT);
		break;

	  case JXOP_FNCALL:
		printf("%s( ", calc->u.func.jf ? calc->u.func.jf->name : "?");
		jx_calc_dump(calc->u.func.args);
		printf(" ) ");
		break;

	  case JXOP_AG:
		printf(" <<");
		jx_calc_dump(calc->u.ag->expr);
		printf(">> ");
		break;

	  case JXOP_SELECT:
		printf(" SELECT");
		break;

	  case JXOP_DISTINCT:
		printf(" DISTINCT");
		break;

	  case JXOP_FROM:
		printf(" FROM");
		break;

	  case JXOP_WHERE:
		printf(" WHERE");
		break;

	  case JXOP_GROUPBY:
		printf(" GROUP BY");
		break;

	  case JXOP_HAVING:
		printf(" HAVING");
		break;

	  case JXOP_ORDERBY:
		printf(" ORDER BY");
		break;

	  case JXOP_DESCENDING:
		printf(" DESCENDING");
		break;

	  case JXOP_LIMIT:
		printf(" LIMIT");
		break;

	  case JXOP_STARTPAREN:
	  case JXOP_ENDPAREN:
	  case JXOP_STARTARRAY:
	  case JXOP_ENDARRAY:
	  case JXOP_STARTOBJECT:
	  case JXOP_ENDOBJECT:
	  case JXOP_INVALID:
	  case JXOP_REGEX:
	  case JXOP_ENVIRON:
		printf(" %s ", jx_calc_op_name(calc->op));
		break;
	}
}

/* Dump all entries in the parsing stack */
static int dumpstack(stack_t *stack, const char *format, const char *data)
{
	int     i;
	char	buf[40];

	/* If debugging is off, don't show */
	if (!jx_debug_flags.calc)
		return 1;

	/* Print the key */
	snprintf(buf, sizeof buf, format, data);
	strcat(buf, ":");
	printf("%-14s", buf);

	/* Dump the stack */
	if (stack->sp > 0) {
		for (i = 0; i < stack->sp; i++) {
			putchar(' ');
			jx_calc_dump(stack->stack[i]);
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
		if (stack->stack[i]->op == JXOP_SELECT)
			return 1;
	return 0;
}

/* Test whether the parsing stack is in a context where "/" is the start of a
 * regular expression, not a division operator.
 */
static int jcregex(stack_t *stack)
{
	if (stack->sp == 0
	 || stack->stack[stack->sp - 1]->op == JXOP_LIKE
	 || stack->stack[stack->sp - 1]->op == JXOP_NOTLIKE
	 || stack->stack[stack->sp - 1]->op == JXOP_STARTPAREN
	 || stack->stack[stack->sp - 1]->op == JXOP_COMMA)
		return 1;
	return 0;
}

/* Test whether the parseing stack is in a context where "=" is an assignment
 * operator, not a comparison operator.  Return JXOP_ICEQ if it is a
 * comparison, or one of JX_ASSIGN or JX_APPEND for assignment.
 *
 * THIS FUNCTION CAN MODIFY THE PARSE STACK!  For JX_APPEND (denoted by []=)
 * it will remove the [].
 */
static jxop_t jcisassign(stack_t *stack)
{
	int	sp = stack->sp;
	jxcalc_t	*jc;
	jxop_t	op = JXOP_ASSIGN;

	/* If assignment isn't allowed in this expression, the anwer is "no" */
	if (!stack->canassign)
		return JXOP_ICEQ;

	/* Any basic l-value can be followed by "[]" to denote appending
	 * to an array.
	 */
	if (sp >= 3 && stack->stack[sp - 2]->op == JXOP_STARTARRAY && stack->stack[sp - 1]->op == JXOP_ENDARRAY) {
		op = JXOP_APPEND;
		sp -= 2;
	}

	/* A name can be an lvalue, except for the names "this" and "that" */
	if (sp == 1 && stack->stack[0]->op == JXOP_NAME) {
		if (!strcasecmp(stack->stack[0]->u.text, "this")
		 || !strcasecmp(stack->stack[0]->u.text, "that"))
			return JXOP_ICEQ;
		goto IsAssign;
	}

	/* For more complex assignments, the lvalue can be a name followed by
	 * a series of dot-names and subscripts.  This is complicated slightly
	 * by the fact that the last dot-name or subscript might not be
	 * reduced yet since this function is called from lex().
	 */
	if (sp == 1 ||
	    (sp == 3 && stack->stack[1]->op == JXOP_DOT && stack->stack[2]->op == JXOP_NAME) ||
	    (sp == 4 && stack->stack[1]->op == JXOP_STARTARRAY && stack->stack[3]->op == JXOP_ENDARRAY)) {
		for (jc = stack->stack[0];
		     jc && ((jc->op == JXOP_DOT && jc->RIGHT->op == JXOP_NAME)
			|| jc->op == JXOP_SUBSCRIPT);
		     jc = jc->LEFT) {
		}
		if (!jc || jc->op != JXOP_NAME)
			return JXOP_ICEQ;
		goto IsAssign;
	}
	return JXOP_ICEQ;

IsAssign:
	/* For JXOP_APPEND, remove the empty brackets from the stack */
	if (op == JXOP_APPEND) {
		jx_calc_free(stack->stack[sp]); /* JXOP_SUBSCRIPT */
		jx_calc_free(stack->stack[sp + 1]); /* JXOP_ENDARRAY */
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
	jxop_t op, best;
	char *end;

	/* Fix the operators[] array, if we haven't already done so.
	 * This just means counting the lengths of the operators
	 */
	if (!operators[0].len) {
		/* Verify that the JXOP_INVALID symbol is in the right
		 * place.  That strongly suggests that the rest of them are
		 * all correct too.
		 */
		assert(operators[JXOP_INVALID].prec == 666);

		/* Compute lengths of names */
		for (op = 0; op <= JXOP_INVALID; op++)
			operators[op].len = strlen(operators[op].text);
	}

	/* Skip whitespace */
	str = skipwhitespace(str);

	/* Start with some common defaults */
	token->op = JXOP_INVALID;
	token->full = str;
	token->len = 1;

	/* If no tokens, return NULL */
	if (!*str) {
		if (jx_debug_flags.calc)
			jx_user_printf(NULL, "debug", "lex(): NULL\n");
		return NULL;
	}

	/* Numbers */
	if (isdigit(*str) || (*str == '.' && isdigit(str[1]))) {
		token->op = JXOP_NUMBER;
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
		if (jx_debug_flags.calc)
			jx_user_printf(NULL, "debug", "lex(): number JXOP_NUMBER \"%.*s\"\n", token->len, token->full);
		return str;
	}

	/* Quoted strings */
	if (strchr("\"'`", *str)) {
		token->op = JXOP_STRING;
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
			token->op = JXOP_NAME;
			token->full++;
			token->len -= 2;
		}
		if (jx_debug_flags.calc)
			jx_user_printf(NULL, "debug", "lex(): string JXOP_%s \"%.*s\"\n", jx_calc_op_name(token->op), token->len, token->full);
		return str;
	}

	/* Names or alphanumeric keywords */
	if (isalpha(*str) || *str == '_') {
		token->op = JXOP_NAME;
		token->full = str;
		token->len = 1;
		while (isalnum(token->full[token->len]) || token->full[token->len] == '_')
			token->len++;

		/* Distinguish keywords from names */
		if ((token->len == 4 && !strncmp(token->full, "true", 4))
		 || (token->len == 5 && !strncmp(token->full, "false", 5)))
			token->op = JXOP_BOOLEAN;
		else if (token->len == 4 && !strncmp(token->full, "null", 4))
			token->op = JXOP_NULL;
		else if (token->len == 4 && !strncasecmp(token->full, "like", 4))
			token->op = JXOP_LIKE;
		else if (token->len == 3 && !strncasecmp(token->full, "not like", 8)) {
			token->len = 8;
			token->op = JXOP_NOTLIKE;
		} else if (token->len == 2 && !strncasecmp(token->full, "in", 2))
			token->op = JXOP_IN;
		else if (token->len == 3 && !strncasecmp(token->full, "not in", 6)) {
			token->len = 6;
			token->op = JXOP_NOTIN;
		} else if (token->len == 3 && !strncasecmp(token->full, "and", 3))
			token->op = JXOP_AND;
		else if (token->len == 2 && !strncasecmp(token->full, "or", 2))
			token->op = JXOP_OR;
		else if (token->len == 3 && !strncasecmp(token->full, "not", 3))
			token->op = JXOP_NOT;
		else if (token->len == 7 && !strncasecmp(token->full, "between", 7))
			token->op = JXOP_BETWEEN;
		else if (token->len == 2 && !strncasecmp(token->full, "is null", 7)) {
			token->len = 7;
			token->op = JXOP_ISNULL;
		} else if (token->len == 2 && !strncasecmp(token->full, "is not null", 7)) {
			token->len = 11;
			token->op = JXOP_ISNOTNULL;
		} else if (token->len == 2 && !strncasecmp(token->full, "as", 2)) {
			token->op = JXOP_AS;
		} else if (token->len == 6 && !strncasecmp(token->full, "values", 6)) {
			token->op = JXOP_VALUES;
		} else if (token->len == 6 && !strncasecmp(token->full, "select", 6)) {
			token->len = 6;
			token->op = JXOP_SELECT;
		} else if (jcselecting(stack)) {
			/* The following SQL keywords are only recognized as
			 * part of a "select" clause.  This is because some of
			 * them such as "from" and "desc" are often used as
			 * member names, and "distinct" is a jxcalc function
			 * name.  You could wrap them in backticks to prevent
			 * them from being taken as keywords, but that'd be
			 * inconvenient.
			 */
			if (token->len == 8 && !strncasecmp(token->full, "distinct", 8)) {
				token->op = JXOP_DISTINCT;
			} else if (token->len == 4 && !strncasecmp(token->full, "from", 4)) {
				token->op = JXOP_FROM;
			} else if (token->len == 5 && !strncasecmp(token->full, "where", 5)) {
				token->op = JXOP_WHERE;
			} else if (token->len == 5 && !strncasecmp(token->full, "group by", 8)) {
				token->len = 8;
				token->op = JXOP_GROUPBY;
			} else if (token->len == 6 && !strncasecmp(token->full, "having", 6)) {
				token->op = JXOP_HAVING;
			} else if (token->len == 5 && !strncasecmp(token->full, "order by", 8)) {
				token->len = 8;
				token->op = JXOP_ORDERBY;
			} else if (token->len == 10 && !strncasecmp(token->full, "descending", 10)) {
				token->len = 10;
				token->op = JXOP_DESCENDING;
			} else if (token->len == 4 && !strncasecmp(token->full, "desc", 4)) {
				token->len = 4;
				token->op = JXOP_DESCENDING;
			} else if (token->len == 5 && !strncasecmp(token->full, "limit", 5)) {
				token->len = 5;
				token->op = JXOP_LIMIT;
			}
		}
		str += token->len;
		if (jx_debug_flags.calc)
			jx_user_printf(NULL, "debug", "lex(): keyword JXOP_%s \"%.*s\"\n", jx_calc_op_name(token->op), token->len, token->full);
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
		token->op = JXOP_REGEX;
		token->len = (int)(str - token->full);
		return str;
	}

	/* Operators - find the longest matching name */
	best = JXOP_INVALID;
	for (op = 0; op < JXOP_INVALID; op++) {
		if (!strncmp(str, operators[op].text, operators[op].len)
		 && (best == JXOP_INVALID || operators[best].len < operators[op].len))
			best = op;
	}
	if (best == JXOP_ENDOBJECT) {
		/* An unmatched } can end an expression, for example if a
		 * function definition ends with "return expr}".  To the
		 * expression parser, this use of "}" should be treated
		 * much like a ";" -- it should be JXOP_INVALID.
		 */
		int i;
		for (i = stack->sp - 1; i >= 0; i--)
			if (stack->stack[i]->op == JXOP_STARTOBJECT)
				break;
		if (i < 0)
			best = JXOP_INVALID;
	}
	if (best != JXOP_INVALID) {
		/* Use this operator for this token */
		token->op = best;
		token->full = str;
		token->len = operators[best].len;
		str += token->len;

		/* SUBTRACT could be NEGATE -- depends on context */
		if (token->op == JXOP_SUBTRACT && (pattern(stack, "^") || pattern(stack, "+")))
			token->op = JXOP_NEGATE;

		/* ICEQ could be ASSIGN -- depends on context */
		if (token->op == JXOP_ICEQ)
			token->op = jcisassign(stack);

		if (jx_debug_flags.calc)
			jx_user_printf(NULL, "debug", "lex(): operator JXOP_%s \"%.*s\"\n", jx_calc_op_name(token->op), token->len, token->full);
		return str;
	}

	/* Invalid */
	if (jx_debug_flags.calc)
		jx_user_printf(NULL, "debug", "lex(): invalid JXOP_%s \"%.*s\"\n", jx_calc_op_name(token->op), token->len, token->full);
	str++;
	return str;
} 


/* Allocate a jxcalc_t structure. Type and (if appropriate) text come from
 * a token_t struct.
 */
static jxcalc_t *jcalloc(token_t *token)
{
	jxcalc_t *jc;
	size_t len, size;
	char	*end;

	/* Some tokens don't need to be allocated dynamically */
	switch (token->op) {
	  case JXOP_STARTPAREN: return &startparen;
	  case JXOP_ENDPAREN: return &endparen;
	  case JXOP_STARTARRAY: return &startarray;
	  case JXOP_ENDARRAY: return &endarray;
	  case JXOP_STARTOBJECT: return &startobject;
	  case JXOP_ENDOBJECT: return &endobject;
	  case JXOP_DISTINCT:	return &selectdistinct;
	  case JXOP_FROM: return &selectfrom;
	  case JXOP_WHERE: return &selectwhere;
	  case JXOP_GROUPBY: return &selectgroupby;
	  case JXOP_HAVING: return &selecthaving;
	  case JXOP_ORDERBY: return &selectorderby;
	  case JXOP_DESCENDING: return &selectdesc;
	  case JXOP_LIMIT: return &selectlimit;
	  default:;
	}

	/* Allocate it.  If long "name" then add extra space. */
	len = 0;
	if (token->op == JXOP_NAME)
		len = token->len;
	if (len + 1 < sizeof(jc->u))
		size = sizeof(*jc);
	else
		size = sizeof(*jc) - sizeof(jc->u) + len + 1;
	jc = (jxcalc_t *)malloc(size);

	/* Initialize it to all zeroes */
	memset((void *)jc, 0, size);
	jc->op = token->op;

	/* Copy names u.text, other literals into a jx_t */
	if (token->op == JXOP_NAME)
		strncpy(jc->u.text, token->full, token->len);
	else if (token->op == JXOP_STRING) {
		jc->op = JXOP_LITERAL;
		len = jx_mbs_unescape(NULL, token->full + 1, token->len - 2);
		jc->u.literal = jx_string("", len);
		if (len > 0)
			jx_mbs_unescape(jc->u.literal->text, token->full + 1, token->len - 2);
	} else if (token->op == JXOP_NUMBER) {
		jc->op = JXOP_LITERAL;
		if (*token->full == '0' && token->len > 1 && strchr("0123456789XxOoBb", token->full[1])) {
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
			jc->u.literal = jx_from_int((int)value);
		} else if (((end = strchr(token->full, '.')) != NULL
			 || (end = strchr(token->full, 'e')) != NULL
			 || (end = strchr(token->full, 'E')) != NULL)
			&& end < token->full + token->len) {
			jc->u.literal = jx_from_double(atof(token->full));
		} else {
			jc->u.literal = jx_from_int(atoi(token->full));
		}
	} else if (token->op == JXOP_BOOLEAN) {
		jc->op = JXOP_LITERAL;
		jc->u.literal = jx_boolean(*token->full == 't');
	} else if (token->op == JXOP_NULL) {
		jc->op = JXOP_LITERAL;
		jc->u.literal = jx_null();
	}

	/* SELECT needs some extra space allocated during parsing. */
	if (token->op == JXOP_SELECT) {
		jc->u.select = (jxselect_t *)calloc(1, sizeof(jxselect_t));
	}

	/* REGEX needs a buffer allocated, and then the text and flags need to
	 * be parsed.  Big stuff.
	 */
	if (token->op == JXOP_REGEX) {
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
			jc->op = JXOP_LITERAL;
			jc->u.literal = jx_error_null(NULL, "regex:%s", buf);
		}
	}

	/* return it */
	return jc;
}

/* Allocate a jxcalc_t structure that uses u.param.left and u.param.right */
static jxcalc_t *jcleftright(jxop_t op, jxcalc_t *left, jxcalc_t *right)
{
	token_t	t;
	jxcalc_t *jc;

	t.op = op;
	jc = jcalloc(&t);
	jc->LEFT = left;
	jc->RIGHT = right;
	return jc;
}

/* Allocate a jxcalc_t structure for a function call, and up to 3 parameters.
 * The first parameter, p1, is required; the others may be NULL to skip them.
 */
static jxcalc_t *jcfunc(char *name, jxcalc_t *p1, jxcalc_t *p2, jxcalc_t *p3)
{
	token_t	t;
	jxcalc_t *jc;

	/* Allocate the jxcalc_t for the function call */
	t.op = JXOP_FNCALL;
	jc = jcalloc(&t);

	/* Link it to the named function */
	jc->u.func.jf = jx_calc_function_by_name(name);

	/* The args are an array generator */
	t.op = JXOP_ARRAY;
	jc->u.func.args = jcalloc(&t);
	jc->u.func.args->LEFT = p1;
	if (p2) {
		jc->u.func.args->RIGHT = jcleftright(JXOP_ARRAY, p2, NULL);
		if (p3)
			jc->u.func.args->RIGHT->RIGHT = jcleftright(JXOP_ARRAY, p3, NULL);
	}

	return jc;
}

/* Build an array generator, one item at a time.  For the first call, "list"
 * should be NULL; in subsequent calls, it should be the value returned by
 * the previous call.  This function is used in some command parsers in cmd.c
 */
jxcalc_t *jx_calc_list(jxcalc_t *list, jxcalc_t *item)
{
	jxcalc_t *tail;

	/* Allocate a JXOP_ARRAY with "item" on the left */
	item = jcleftright(JXOP_ARRAY, item, NULL);

	/* If no list, then just return the JXOP_ARRAY containing item */
	if (!list)
		return item;

	/* Find the tail of the list */
	for (tail = list; tail->RIGHT; tail = tail->RIGHT) {
	}

	/* Append the new item to the tail, and return the whole list */
	tail->RIGHT = item;
	return list;
}

/* Free a jxcalc_t tree that was allocated via jx_calc_parse()  ... or
 * ultimately the internal jcalloc() function.
 */
void jx_calc_free(jxcalc_t *jc)
{
	/* defend against NULL */
	if (!jc)
		return;

	/* Some jxcalc_t values aren't dynamically allocated, so not freed */
	if (jc == &startparen || jc == &endparen
	 || jc == &startarray || jc == &endarray
	 || jc == &startobject || jc == &endobject)
		return;

	/* Recursively work down through the expression tree */
	switch (jc->op) {
	  case JXOP_STRING:
	  case JXOP_NUMBER:
	  case JXOP_BOOLEAN:
	  case JXOP_NULL:
	  case JXOP_NAME:
		break;

	  case JXOP_LITERAL:
		jx_free(jc->u.literal);
		break;

	  case JXOP_DOT:
	  case JXOP_DOTDOT:
	  case JXOP_ELLIPSIS:
	  case JXOP_ARRAY:
	  case JXOP_OBJECT:
	  case JXOP_SUBSCRIPT:
	  case JXOP_COALESCE:
	  case JXOP_QUESTION:
	  case JXOP_COLON:
	  case JXOP_MAYBEMEMBER:
	  case JXOP_AS:
	  case JXOP_EACH:
	  case JXOP_GROUP:
	  case JXOP_FIND:
	  case JXOP_NJOIN:
	  case JXOP_LJOIN:
	  case JXOP_RJOIN:
	  case JXOP_NEGATE:
	  case JXOP_ISNULL:
	  case JXOP_ISNOTNULL:
	  case JXOP_MULTIPLY:
	  case JXOP_DIVIDE:
	  case JXOP_MODULO:
	  case JXOP_ADD:
	  case JXOP_SUBTRACT:
	  case JXOP_BITNOT:
	  case JXOP_BITAND:
	  case JXOP_BITOR:
	  case JXOP_BITXOR:
	  case JXOP_NOT:
	  case JXOP_AND:
	  case JXOP_OR:
	  case JXOP_LT:
	  case JXOP_LE:
	  case JXOP_EQ:
	  case JXOP_NE:
	  case JXOP_GE:
	  case JXOP_GT:
	  case JXOP_ICEQ:
	  case JXOP_ICNE:
	  case JXOP_LIKE:
	  case JXOP_NOTIN:
	  case JXOP_NOTLIKE:
	  case JXOP_IN:
	  case JXOP_EQSTRICT:
	  case JXOP_NESTRICT:
	  case JXOP_COMMA:
	  case JXOP_BETWEEN:
	  case JXOP_ENVIRON:
	  case JXOP_ASSIGN:
	  case JXOP_APPEND:
	  case JXOP_MAYBEASSIGN:
	  case JXOP_VALUES:
		jx_calc_free(jc->LEFT);
		jx_calc_free(jc->RIGHT);
		break;

	  case JXOP_SELECT:
		jx_calc_free(jc->u.select->select);
		jx_calc_free(jc->u.select->from);
		jx_calc_free(jc->u.select->where);
		jx_free(jc->u.select->groupby);
		jx_free(jc->u.select->orderby);
		free(jc->u.select);
		break;

	  case JXOP_FNCALL:
		jx_calc_free(jc->u.func.args);
		break;

	  case JXOP_AG:
		jx_calc_free(jc->u.ag->expr);
		free(jc->u.ag);
		break;

	  case JXOP_REGEX:
		/* jc->u.regex.preg is a pointer to a regex_t buffer.  We need
		 * to call regfree() on that buffer to release the data
		 * associated with the buffer, and also free() to release
		 * the memory for the buffer itself.
		 */
		regfree((regex_t *)jc->u.regex.preg);
		free(jc->u.regex.preg);
		break;

	  case JXOP_DISTINCT:
	  case JXOP_FROM:
	  case JXOP_WHERE:
	  case JXOP_GROUPBY:
	  case JXOP_HAVING:
	  case JXOP_ORDERBY:
	  case JXOP_DESCENDING:
	  case JXOP_LIMIT:
		/* These SQL keywords are harmless, and never need to be freed*/
		return;

	  case JXOP_STARTPAREN:
	  case JXOP_ENDPAREN:
	  case JXOP_STARTARRAY:
	  case JXOP_ENDARRAY:
	  case JXOP_STARTOBJECT:
	  case JXOP_ENDOBJECT:
	  case JXOP_INVALID:
		/* None of these should appear in a parsed expression */
		abort();
	}

	/* Finally, free this jxcalt_t */
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
static jxcalc_t *fixcomma(jxcalc_t *jc, jxop_t op)
{
	jxcalc_t *top, *parent;

	/* We could get a single item, which is not a not a comma operator
	 * but should be a single item in the returned list.  We'll need to
	 * allocate an array struct to make it into a list of 1.
	 */
	if (jc->op != JXOP_COMMA)
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
	if (parent->op == JXOP_COMMA)
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
static jxcalc_t *fixcolon(stack_t *stack, const char *srcend)
{
	jxcalc_t *jc = stack->stack[stack->sp - 1];
	if (jc->op == JXOP_AS) {
		/* Convert "expr AS name" to "name:expr" */
		jxcalc_t *swapper = jc->LEFT;
		jc->LEFT = jc->RIGHT;
		jc->RIGHT = swapper;
		jc->op = JXOP_COLON;
	} else if (jc->op != JXOP_COLON && jc->op != JXOP_MAYBEMEMBER) {
		/* Use the source text as the name */
		token_t t;
		t.op = JXOP_NAME;
		t.full = stack->str[stack->sp - 1];
		if (strchr("\"'`", *t.full))
			t.full++;
		while (srcend - 1 > t.full && strchr("\"'` ", srcend[-1]))
			srcend--;
		t.len = (int)(srcend - t.full);

		/* Make it a COLON expression */
		jc = jcleftright(JXOP_COLON, jcalloc(&t), jc);
	} /* Else it is already "name:expr" or "name:??expr" */

	return jc;
}

/* Test whether jc uses an aggregate function */
static int jcisag(jxcalc_t *jc)
{
	/* Defend against NULL */
	if (!jc)
		return 0;

	/* Recursively check subexpressions */
	switch (jc->op) {
	  case JXOP_LITERAL:
	  case JXOP_STRING:
	  case JXOP_NUMBER:
	  case JXOP_BOOLEAN:
	  case JXOP_NULL:
	  case JXOP_NAME:
	  case JXOP_FROM:
	  case JXOP_REGEX:
		return 0;

	  case JXOP_DOT:
	  case JXOP_DOTDOT:
	  case JXOP_ELLIPSIS:
	  case JXOP_ARRAY:
	  case JXOP_OBJECT:
	  case JXOP_SUBSCRIPT:
	  case JXOP_COALESCE:
	  case JXOP_QUESTION:
	  case JXOP_COLON:
	  case JXOP_MAYBEMEMBER:
	  case JXOP_NJOIN:
	  case JXOP_LJOIN:
	  case JXOP_RJOIN:
	  case JXOP_NEGATE:
	  case JXOP_ISNULL:
	  case JXOP_ISNOTNULL:
	  case JXOP_MULTIPLY:
	  case JXOP_DIVIDE:
	  case JXOP_MODULO:
	  case JXOP_ADD:
	  case JXOP_SUBTRACT:
	  case JXOP_BITNOT:
	  case JXOP_BITAND:
	  case JXOP_BITOR:
	  case JXOP_BITXOR:
	  case JXOP_NOT:
	  case JXOP_AND:
	  case JXOP_OR:
	  case JXOP_LT:
	  case JXOP_LE:
	  case JXOP_EQ:
	  case JXOP_NE:
	  case JXOP_GE:
	  case JXOP_GT:
	  case JXOP_ICEQ:
	  case JXOP_ICNE:
	  case JXOP_LIKE:
	  case JXOP_NOTIN:
	  case JXOP_NOTLIKE:
	  case JXOP_IN:
	  case JXOP_EQSTRICT:
	  case JXOP_NESTRICT:
	  case JXOP_COMMA:
	  case JXOP_BETWEEN:
	  case JXOP_ASSIGN:
	  case JXOP_APPEND:
	  case JXOP_MAYBEASSIGN:
	  case JXOP_EACH:
	  case JXOP_GROUP:
	  case JXOP_FIND:
	  case JXOP_VALUES:
		return jcisag(jc->LEFT) || jcisag(jc->RIGHT);

	  case JXOP_ENVIRON:
		return jcisag(jc->RIGHT);

	  case JXOP_FNCALL:
		/* If this is an aggregate function, that's our answer */
		if (jc->u.func.jf->agfn)
			return 1;

		/* Check arguments */
		return jcisag(jc->u.func.args);

	  case JXOP_AG:
	  case JXOP_STARTPAREN:
	  case JXOP_ENDPAREN:
	  case JXOP_STARTARRAY:
	  case JXOP_ENDARRAY:
	  case JXOP_STARTOBJECT:
	  case JXOP_ENDOBJECT:
	  case JXOP_SELECT:
	  case JXOP_DISTINCT:
	  case JXOP_AS:
	  case JXOP_WHERE:
	  case JXOP_GROUPBY:
	  case JXOP_HAVING:
	  case JXOP_ORDERBY:
	  case JXOP_DESCENDING:
	  case JXOP_LIMIT:
	  case JXOP_INVALID:
		/* None of these should appear in a parsed expression */
		abort();
	}

	/*NOTREACHED*/
	return 0;
}


/* Convert a select statement to "native" jxcalc_t */
static jxcalc_t *jcselect(jxselect_t *sel)
{
	jxcalc_t *jc, *ja;
	token_t t;
	int	anyselectag;

	/* If there's a column list in the SELECT clause, convert it to an
	 * object generator.
	 */
	anyselectag = 0;
	if (sel->select) {
		sel->select = fixcomma(sel->select, JXOP_OBJECT);

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
			sel->where = jcleftright(JXOP_AND, sel->where, sel->having);
		else
			sel->where = sel->having;
		sel->having = NULL;
	}

	/* Maybe dump some debugging info */
	if (jx_debug_flags.calc) {
		char *tmp; 
		jx_user_printf(NULL, "debug", "jcselect(\n");
		jx_user_printf(NULL, "debug", "   distinct=%s\n", sel->distinct ? "true" : "false");
		jx_user_printf(NULL, "debug", "   select=");jx_calc_dump(sel->select);jx_user_ch('\n');
		jx_user_printf(NULL, "debug", "   from=");jx_calc_dump(sel->from);jx_user_ch('\n');
		tmp = jx_serialize(sel->groupby, 0);
		jx_user_printf(NULL, "debug", "   groupby=%s\n", tmp);
		free(tmp);
		tmp = jx_serialize(sel->orderby, 0);
		jx_user_printf(NULL, "debug", "   orderby=%s\n", tmp);
		free(tmp);
		jx_user_printf(NULL, "debug", "   limit=");jx_calc_dump(sel->limit);jx_user_ch('\n');
		jx_user_ch(')');
		jx_user_ch('\n');
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
			/* Make the unroll list be a jxcalc_t literal */
			t.op = JXOP_LITERAL;
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
			jc = jcleftright(JXOP_EACH, jc, sel->where);

		/* The list is already jx_t array of strings.  Make it be
		 * a jxcalc_t literal.
		 */
		t.op = JXOP_LITERAL;
		ja = jcalloc(&t);
		ja->u.literal = sel->groupby;

		/* Add groupBy() function call */
		jc = jcfunc("groupBy", jc, ja, NULL);

		/* If there's a having clause and/or a select list, that's
		 * next.  Even without it, we still need an # operator even
		 * if the right operand is just "this".
		 */
		if (sel->having && sel->select) {
			ja = jcleftright(JXOP_QUESTION, sel->having, sel->select);
		} else if (sel->having) {
			ja = sel->having;
		} else if (sel->select) {
			ja = sel->select;
		} else {
			/* use "this" as the right operator */
			t.op = JXOP_NAME;
			t.full = "this";
			t.len = 4;
			ja = jcalloc(&t);
		}
		jc = jcleftright(JXOP_GROUP, jc, ja);
	} else if (sel->where || sel->select) {
		/* We want to add a ##where?select or just part of that */
		if (sel->where && sel->select)
			ja = jcleftright(JXOP_QUESTION, sel->where, sel->select);
		else if (sel->where)
			ja = sel->where;
		else
			ja = sel->select;
		jc = jcleftright(JXOP_EACH, jc, ja);
	}

	/* If there's an ORDER BY clause, add an orderBy() function call */
	if (sel->orderby) {
		/* The list is already jx_t array of strings.  Make it be
		 * a jxcalc_t literal.
		 */
		t.op = JXOP_LITERAL;
		ja = jcalloc(&t);
		ja->u.literal = sel->orderby;

		/* Add an orderBy() function call */
		jc = jcfunc("orderBy", jc, ja, NULL);
	}

	/* If there's a DISTINCT keyword, add a distinct() function call */
	if (sel->distinct) {
		t.op = JXOP_LITERAL;
		ja = jcalloc(&t);
		ja->u.literal = jx_boolean(1);
		jc = jcfunc("distinct", jc, ja, NULL);
	}

	/* If there's a LIMIT number, add a .slice(0,limit) function call */
	if (sel->limit) {
		jxcalc_t *jzero;
		token_t	t;
		t.op = JXOP_NUMBER;
		t.full = "0";
		t.len = 1;
		jzero = jcalloc(&t);
		jc = jcfunc("slice", jc, jzero, sel->limit);
	}

	return jc;
}

/* Check a single character against a token.  The characters used are:
 *   ^  Start of expression, or parenthesis/bracket/brace
 *   (  JXOP_STARTPAREN (parenthesis)
 *   )  JXOP_ENDPAREN
 *   [  JXOP_STARTARRAY (array generator)
 *   ]  JXOP_ENDARRAY
 *   {  JXOP_STARTOBJECT (object generator)
 *   }  JXOP_ENDOBJECT
 *   @  JXOP_SUBSCRIPT (array subscript)
 *   $  JXOP_ENVIRON
 *   ?  JXOP_QUESTION
 *   :  JXOP_COLON or JXOP_MAYBEMEMBER
 *   &  JXOP_AND
 *   b  JXOP_BETWEEN
 *   i  JXOP_IN or JXOP_NOTIN
 *   N  JXOP_ISNULL or JXOP_ISNOTNULL
 *   l  Literal
 *   n  Name or string literal
 *   m  Object member -- a complete name:value clause or just a name.
 *   M  Object member -- a complete name:value clause.
 *   S  JXOP_SELECT
 *   D  JXOP_DISTINCT
 *   A  JXOP_AS
 *   F  JXOP_FROM
 *   W  JXOP_WHERE
 *   G  JXOP_GROUPBY
 *   H  JXOP_HAVING
 *   O  JXOP_ORDERBY
 *   L  JXOP_LIMIT
 *   d  JXOP_DESCENDING
 *   .  JXOP_DOT
 *   ,  JXOP_COMMA
 *   *  JXOP_MULTIPLY
 *   +  Binary operator (needs right param)
 *   -  Unary operator (needs right param)
 *   x  Literal or completed operator
 */
static int pattern_single(jxcalc_t *jc, char pchar)
{
	/* Check the jxcalc node against a single character */
	switch (pchar) {
	  case '^': /* handled in pattern() because it needs offsetsp */

	  case '(': /* JXOP_STARTPAREN (parenthesis) */
		if (jc->op != JXOP_STARTPAREN)
			return FALSE;
		break;

	  case ')': /* JXOP_ENDPAREN (parenthesis) */
		if (jc->op != JXOP_ENDPAREN)
			return FALSE;
		break;

	  case '[': /* JXOP_STARTARRAY (array generator) */
		if (jc->op != JXOP_STARTARRAY)
			return FALSE;
		break;

	  case ']': /* JXOP_ENDARRAY */
		if (jc->op != JXOP_ENDARRAY)
			return FALSE;
		break;

	  case '{': /* JXOP_STARTOBJECT (object generator) */
		if (jc->op != JXOP_STARTOBJECT)
			return FALSE;
		break;

	  case '}': /* JXOP_ENDOBJECT */
		if (jc->op != JXOP_ENDOBJECT)
			return FALSE;
		break;

	  case '@': /* JXOP_SUBSCRIPT (array subscript) */
		if (jc->op != JXOP_SUBSCRIPT)
			return FALSE;
		break;

	  case '$': /* JXOP_ENVIRON ($ for environ variable, without name) */
		if (jc->op != JXOP_ENVIRON || jc->LEFT)
			return FALSE;
		break;

	  case '?': /* JXOP_QUESTION (array subscript) */
		if (jc->op != JXOP_QUESTION)
			return FALSE;
		break;

	  case ':': /* JXOP_COLON or JXOP_MAYBEMEMBER */
		if (jc->op != JXOP_COLON && jc->op != JXOP_MAYBEMEMBER)
			return FALSE;
		break;

	  case 'b': /* JXOP_BETWEEN */
		if (jc->op != JXOP_BETWEEN)
			return FALSE;
		break;

	  case 'i': /* JXOP_IN or JXOP_NOTIN */
		if (jc->op != JXOP_IN && jc->op != JXOP_NOTIN)
			return FALSE;
		break;

	  case 'N': /* JXOP_ISNULL or JXOP_ISNOTNULL */
		if (jc->op != JXOP_ISNULL && jc->op != JXOP_ISNOTNULL)
			return FALSE;
		break;

	  case '&': /* JXOP_AND */
		if (jc->op != JXOP_AND)
			return FALSE;
		break;

	  case 'l': /* Literal */
		if (jc->op != JXOP_LITERAL)
			return FALSE;
		break;

	  case 'n': /* Name */
		if (jc->op != JXOP_NAME
		 && !JC_IS_STRING(jc)
		 && jc->op != JXOP_DOT)
			return FALSE;
		break;

	  case 'm': /* Member or list of members (name or name:expr)*/
		if (jc->op != JXOP_NAME
		 && (jc->op != JXOP_COLON
		  || !jc->LEFT
		  || (jc->LEFT->op != JXOP_NAME && !JC_IS_STRING(jc->LEFT))
		  || !jc->RIGHT)) {
			/* If we get here then it isn't the first
			 * member in a list, but maybe it's the comma
			 * for a later member in the list?
			 */
			if (jc->op != JXOP_COMMA
			 || !jc->LEFT
			 || (jc->LEFT->op != JXOP_COLON
			  && (jc->LEFT->op != JXOP_NAME && jc->LEFT->op != JXOP_COMMA))
			 || !jc->RIGHT)
				return FALSE;
		}
		break;

	  case 'M': /* Stricter member (name:expr) */
		if (jc->op != JXOP_COLON
		 || !jc->LEFT
		 || (jc->LEFT->op != JXOP_NAME && !JC_IS_STRING(jc->LEFT))
		 || !jc->RIGHT) {
			return FALSE;
		}
		break;

	  case 'S': /* JXOP_SELECT */
		if (jc->op != JXOP_SELECT)
			return FALSE;
		break;

	  case 'D': /* JXOP_DISTINCT */
		if (jc->op != JXOP_DISTINCT)
			return FALSE;
		break;

	  case 'A': /* JXOP_AS */
		if (jc->op != JXOP_AS)
			return FALSE;
		break;

	  case 'F': /* JXOP_FROM */
		if (jc->op != JXOP_FROM)
			return FALSE;
		break;

	  case 'W': /* JXOP_WHERE */
		if (jc->op != JXOP_WHERE)
			return FALSE;
		break;

	  case 'G': /* JXOP_GROUPBY */
		if (jc->op != JXOP_GROUPBY)
			return FALSE;
		break;

	  case 'H': /* JXOP_HAVING */
		if (jc->op != JXOP_HAVING)
			return FALSE;
		break;

	  case 'O': /* JXOP_ORDERBY */
		if (jc->op != JXOP_ORDERBY)
			return FALSE;
		break;

	  case 'L': /* JXOP_LIMIT */
		if (jc->op != JXOP_LIMIT)
			return FALSE;
		break;

	  case 'd': /* JXOP_DESCENDING */
		if (jc->op != JXOP_DESCENDING)
			return FALSE;
		break;

	  case '.': /* JXOP_DOT */
		if (jc->op != JXOP_DOT)
			return FALSE;
		break;

	  case ',': /* JXOP_COMMA */
		if (jc->op != JXOP_COMMA)
			return FALSE;
		break;

	  case '*': /* JXOP_MULTIPLY */
		if (jc->op != JXOP_MULTIPLY)
			return FALSE;
		break;

	  case '+': /* Incomplete binary operator */
		if ((operators[jc->op].optype != JCOP_INFIX && operators[jc->op].optype != JCOP_RIGHTINFIX)
		 || jc->RIGHT != NULL
		 || jc->op == JXOP_COMMA)
			return FALSE;
		break;

	  case '-': /* Incomplete prefix unary operator */
		if (operators[jc->op].optype != JCOP_PREFIX
		 || jc->RIGHT != NULL)
			return FALSE;
		break;

	  case 'x': /* Literal or completed operator */
		if (jc->op != JXOP_LITERAL
		 && jc->op != JXOP_NAME
		 && jc->op != JXOP_ARRAY
		 && jc->op != JXOP_OBJECT
		 && jc->op != JXOP_SUBSCRIPT
		 && jc->op != JXOP_FNCALL
		 && jc->op != JXOP_REGEX
		 && (jc->op != JXOP_ENVIRON || jc->LEFT == NULL)
		 && ((jc->op != JXOP_NEGATE
		   && jc->op != JXOP_NOT)
		  || jc->RIGHT == NULL)
		 && (operators[jc->op].prec < 0
		  || jc->LEFT == NULL
		  || jc->RIGHT == NULL))
			return FALSE;
		if (jc->op == JXOP_FNCALL && !jc->u.func.args)
			return FALSE;
		break;

	  default:
		abort();
	}

	return TRUE;
}

/* Recognize a pattern at the top of the stack. The pattern is given as a
 * string in which each character represents a type of jxop_t, except that
 * a space splits it into a single-token-per-character segment and an
 * any-of-these-at-the-end segment.  The characters recognized are:
 */
static int pattern(stack_t *stack, char *want)
{
	char *pat;
	int  offsetsp;
	jxcalc_t *jc;

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
			 && jc->op != JXOP_STARTPAREN
			 && jc->op != JXOP_STARTARRAY
			 && jc->op != JXOP_STARTOBJECT
			 && jc->op != JXOP_SUBSCRIPT
			 && jc->op != JXOP_VALUES
			 && jc->op != JXOP_COLON
			 && jc->op != JXOP_MAYBEMEMBER
			 && jc->op != JXOP_ASSIGN
			 && jc->op != JXOP_APPEND
			 && jc->op != JXOP_COMMA
			 && jc->op != JXOP_FROM
			 && jc->op != JXOP_WHERE
			 && jc->op != JXOP_GROUPBY
			 && jc->op != JXOP_HAVING
			 && jc->op != JXOP_ORDERBY
			 && jc->op != JXOP_LIMIT)
				return FALSE;
			continue;
		} else if (!jc || !pattern_single(jc, *pat))
			return FALSE;
	}

	/* If we get here, it matched */
	return TRUE;
}


static int pattern_verbose(stack_t *stack, char *want, jxcalc_t *next)
{
	int result = pattern(stack, want);
	dumpstack(stack, "pattern %s", want);
	if (jx_debug_flags.calc && result) {
		if (next)
			jx_user_printf(NULL, "debug", " -> TRUE, if prec>=%d (%s next.prec)\n", operators[next->op].prec, jx_calc_op_name(next->op));
		else
			jx_user_printf(NULL, "debug", " -> TRUE, unconditionally\n");
	}
	return result;
}

#define PATTERN(x)	(pattern_verbose(stack, match = (x), next))
#define PREC(o)		(!next || operators[o].prec >= operators[next->op].prec + (operators[next->op].optype == JCOP_RIGHTINFIX ? 1 : 0))


/* Reduce the parse state, back to a given precedence level or the most
 * recent incomplete parenthesis/bracket/brace.  Return NULL if successful,
 * or an error message if error.
 */
static char *reduce(stack_t *stack, jxcalc_t *next, const char *srcend)
{
	token_t t;
	jxcalc_t *jc, *jn;
	jxfunc_t *jf;
	int     startsp = stack->sp;
	jxcalc_t **top;
	char *match;

	/* Keep reducing until you can't */
	for (;; (void)(jx_debug_flags.calc && dumpstack(stack, "Reduce %s", match))) {
		/* For convenience, set "top" to the top of the stack.
		 * We can then use top[-1] for the last item on the stack,
		 * top[-2] for the item before that, and so on.
		 */
		top = &stack->stack[stack->sp];

		/* The "between" operator has an odd precedence */
		if (PATTERN("xbx&x") && PREC(JXOP_BETWEEN)) {
			top[-4]->LEFT = top[-5];
			top[-4]->RIGHT = top[-2];
			top[-2]->LEFT = top[-3];
			top[-2]->RIGHT = top[-1];
			top[-5] = top[-4];
			stack->sp -= 4;
			continue;
		}

		/* The ?: operator is weird */
		if (PATTERN("x?x:x") && PREC(JXOP_QUESTION)) {
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
		if (PATTERN("$n") && (!next || next->op != JXOP_STARTARRAY)) {
			/* Make the name be LHS of $ */
			top[-2]->LEFT = top[-1];
			stack->sp--;
			continue;
		} else if (PATTERN("$@")) {
			/* Convert the subscript to a $env[n] */
			jx_calc_free(top[-2]);
			top[-2] = top[-1];
			top[-2]->op = JXOP_ENVIRON;
			stack->sp--;
			continue;
		}

		/* In an array generator or SELECT clause, commas are treated
		 * specially because we want to convert "expr AS name" to
		 * "name:expr", and for any other expr we want to use the
		 * expression's source code as its name.  (So"SELECT count(*)"
		 * uses "count(*)" as the column label.)
		 */
		if ((PATTERN("Sx") && PREC(JXOP_COMMA))
		 || (PATTERN("{x") && PREC(JXOP_COMMA))) {
			/* First element.  Convert it */
			top[-1] = fixcolon(stack, srcend);
		}
		else if ((PATTERN("Sx,x") || PATTERN("{x,x")) && PREC(JXOP_COMMA)) {
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
		} else if (PATTERN("Sx") && PREC(JXOP_FROM)) {
			/* Save the whole expression (a comma list) as the
			 * column list.
			 */
			top[-2]->u.select->select = top[-1];
			stack->sp--;
			continue;
		} else if (PATTERN("SFx,n") && PREC(JXOP_COMMA)) {
			/* Comma in a FROM clause adds to an unroll list */
			jc = top[-5];
			if (!jc->u.select->unroll)
				jc->u.select->unroll = jx_array();
			jx_append(jc->u.select->unroll, jx_string(top[-1]->u.text, -1));
			/* Remove the name and comma, keep "SFx" */
			jx_calc_free(top[-1]);
			jx_calc_free(top[-2]);
			stack->sp -= 2;
			continue;
		} else if (PATTERN("SFx") && PREC(JXOP_FROM)) {
			/* Save the table in select.from */
			top[-3]->u.select->from = top[-1];
			stack->sp -= 2;
			continue;
		} else if (PATTERN("SWx") && PREC(JXOP_WHERE)) {
			/* Save the condition in select.where */
			top[-3]->u.select->where = top[-1];
			stack->sp -= 2;
			continue;
		} else if (PATTERN("SGn,n") && PREC(JXOP_COMMA)) {
			/* Add the first name to an array of names */
			jc = top[-5];
			if (!jc->u.select->groupby)
				jc->u.select->groupby = jx_array();
			jx_append(jc->u.select->groupby, jx_string(top[-3]->u.text, -1));

			/* Remove the first name and comma, keep "SG" and "n" */
			jx_calc_free(top[-3]);
			jx_calc_free(top[-2]);
			top[-3] = top[-1];
			stack->sp -= 2; /* keep "SGn" */
			continue;
		} else if (PATTERN("SGn") && PREC(JXOP_GROUPBY)) {
			/* Add the name to an array of names */
			jc = top[-3];
			if (!jc->u.select->groupby)
				jc->u.select->groupby = jx_array();
			jx_append(jc->u.select->groupby, jx_string(top[-1]->u.text, -1));
			jx_calc_free(top[-1]);
			stack->sp--; /* keep "SG" */
			continue;
		} else if (PATTERN("SG") && PREC(JXOP_GROUPBY)) {
			stack->sp--; /* keep "S" */
			continue;
		} else if (PATTERN("SHx") && PREC(JXOP_HAVING)) {
			/* Save the condition in select.having */
			top[-3]->u.select->having = top[-1];
			stack->sp -= 2;
			continue;
		} else if (PATTERN("SOnd,n") && PREC(JXOP_COMMA)) {
			/* Add the name to an array of names */
			jc = top[-6];
			if (!jc->u.select->orderby)
				jc->u.select->orderby = jx_array();
			jx_append(jc->u.select->orderby, jx_boolean(1));
			jx_append(jc->u.select->orderby, jx_string(top[-4]->u.text, -1));

			/* Discard first name and comma, but keep "SO" and "n"*/
			jx_calc_free(top[-4]);
			jx_calc_free(top[-2]);
			top[-4] = top[-1];
			stack->sp -= 3; /* keep "SOn" */
			continue;
		} else if (PATTERN("SOn,n") && PREC(JXOP_COMMA)) {
			/* Add the name to an array of names */
			jc = top[-5];
			if (!jc->u.select->orderby)
				jc->u.select->orderby = jx_array();
			jx_append(jc->u.select->orderby, jx_string(top[-3]->u.text, -1));

			/* Discard first name and comma, but keep "SO" and "n"*/
			jx_calc_free(top[-3]);
			jx_calc_free(top[-2]);
			top[-3] = top[-1];
			stack->sp -= 2;
			continue;
		} else if (PATTERN("SOnd") && PREC(JXOP_ORDERBY)) {
			/* Add the name to an array of names */
			jc = top[-4];
			if (!jc->u.select->orderby)
				jc->u.select->orderby = jx_array();
			jx_append(jc->u.select->orderby, jx_boolean(1));
			jx_append(jc->u.select->orderby, jx_string(top[-2]->u.text, -1));
			jx_calc_free(top[-2]);
			stack->sp -= 2; /* keep "SO" */
			continue;
		} else if (PATTERN("SOn") && PREC(JXOP_ORDERBY)) {
			/* Add the name to an array of names */
			jc = top[-3];
			if (!jc->u.select->orderby)
				jc->u.select->orderby = jx_array();
			jx_append(jc->u.select->orderby, jx_string(top[-1]->u.text, -1));
			jx_calc_free(top[-1]);
			stack->sp--;
			continue;
		} else if (PATTERN("SO") && PREC(JXOP_ORDERBY)) {
			stack->sp--; /* keep "S" */
			continue;
		} else if (PATTERN("SLl") && PREC(JXOP_LIMIT)) {
			top[-3]->u.select->limit = top[-1];

			/* Remove "LIMIT" and the number, but keep "SELECT" */
			stack->sp -= 2;
			continue;
		} else if (PATTERN("S") && PREC(JXOP_SELECT)) {
			/* All parts of the SELECT have now been parsed.  All
			 * we need to do now is convert it to a "normal"
			 * jxcalc expression.
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
			if (top[-2]->op == JXOP_DOT) {
				if (JC_IS_STRING(top[-1])) {
					/* Convert the string to a name */
					t.op = JXOP_NAME;
					t.full = top[-1]->u.literal->text;
					t.len = strlen(t.full);
					jn = jcalloc(&t);
					jx_calc_free(top[-1]);
					top[-1] = jn;
				} else if (top[-1]->op != JXOP_NAME) {
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
		 && PREC(JXOP_COMMA)) {
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
			if (top[-2]->op == JXOP_MULTIPLY) /* asterisk */
				startsp = stack->sp - 4;
			else
				startsp = stack->sp - 3;
			if (stack->stack[startsp]->op == JXOP_DOT) {
				jc = stack->stack[startsp]->LEFT;
				jn = stack->stack[startsp]->RIGHT;
			} else {
				jc = NULL; /* we'll make it "this" later */
				jn = stack->stack[startsp];
			}
			if (jn->op != JXOP_NAME)
				return "Syntax error - Name expected";
			jf = jx_calc_function_by_name(jn->u.text);
			if (jf == NULL) {
				snprintf(stack->errbuf, sizeof stack->errbuf, "Unknown function %s", jn->u.text);
				return stack->errbuf;
			}
			if (jc == NULL) {
				if (top[-2]->op == JXOP_MULTIPLY) {
					/* For function(*) use true as the argument */
					t.op = JXOP_BOOLEAN;
					t.full = "true";
					t.len = 4;
					jc = jcalloc(&t);
				} else {
					/* For function() use this as the argument */
					t.op = JXOP_NAME;
					t.full = "this";
					t.len = 4;
					jc = jcalloc(&t);
				}
			}
			stack->stack[startsp]->op = JXOP_FNCALL;
			stack->stack[startsp]->u.func.jf = jf;
			stack->stack[startsp]->u.func.args = fixcomma(jc, JXOP_ARRAY);
			stack->stack[startsp]->u.func.agoffset = 0;
			stack->sp = startsp + 1;
			continue;
		} else if (PATTERN("x(x)")) {
			/* Function call with extra parameters */

			/* May be name(args) or arg1.name(args) */
			jn = top[-4];
			if (jn->op == JXOP_DOT)
				jn = jn->RIGHT;
			if (jn->op != JXOP_NAME)
				return "Syntax error - Function name expected";
			jf = jx_calc_function_by_name(jn->u.text);
			if (!jf) {
				snprintf(stack->errbuf, sizeof stack->errbuf, "Unknown function \"%s\"", jn->u.text);
				return stack->errbuf;
			}

			/* Convert parameters to an array generator.  If the
			 * arg1.name(args...) notation was used, convert to
			 * name(arg1, args...).
			 */
			jc = fixcomma(top[-2], JXOP_ARRAY);
			if (top[-4]->op == JXOP_DOT) {
				jxcalc_t *tmp = top[-4];
				tmp->op = JXOP_ARRAY;
				tmp->RIGHT = jc;
				jc = tmp;
			}

			/* Store it - convert name to function call */
			jn->op = JXOP_FNCALL;
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
			t.op = JXOP_SUBSCRIPT;
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
			t.op = JXOP_ARRAY;
			top[-2] = jcalloc(&t);
			stack->sp--;
			continue;
		} else if (PATTERN("[x]") || PATTERN("xi[x)")) { /*!!!*/
			/* Non-empty array generator.  All elements are in
			 * a comma expression in top[-2].  Convert comma to
			 * array.
			 */
			top[-3] = fixcomma(top[-2], JXOP_ARRAY);
			stack->sp -= 2;
			continue;
		} else if (PATTERN("[x,]")) {
			/* Superfluous trailing comma in array generator */
			top[-4] = fixcomma(top[-3], JXOP_ARRAY);
			stack->sp -= 3;
			continue;
		}

		/* Treat "x is null" and "x is not null" like "x === null"
		 * and "x !== null"
		 */
		if (PATTERN("xN")) {
			top[-1]->LEFT = top[-2];
			t.op = JXOP_NULL;
			t.full = "null";
			t.len = 4;
			top[-1]->RIGHT = jcalloc(&t);
			if (top[-1]->op == JXOP_ISNULL)
				top[-1]->op = JXOP_EQSTRICT;
			else
				top[-1]->op = JXOP_NESTRICT;
			top[-2] = top[-1];
			stack->sp--;
			continue;
		}

		/* Object generators */
		if (PATTERN("n:x") && PREC(JXOP_COLON)) {
			/* colon expression */
			top[-3]->op = JXOP_NAME; /* string to name */
			top[-2]->LEFT = top[-3];
			top[-2]->RIGHT = top[-1];
			top[-3] = top[-1];
			stack->sp -= 2;
			continue;
		} else if (PATTERN("{}")) {
			/* Empty object generator, convert from STARTOBJECT
			 * ENDOBJECT to just OBJECT
			 */
			t.op = JXOP_OBJECT;
			top[-2] = jcalloc(&t);
			stack->sp--;
			continue;
		} else if (PATTERN("{m}") || PATTERN("{x}")) {
			/* Non-empty object.  All elements are in a comma
			 * expression in top[-2].  Convert comma to object.
			 */
			top[-3] = fixcomma(top[-2], JXOP_OBJECT);
			stack->sp -= 2;
			top -= 2;

			/* Convert any simple names to colon expressions. Also
			 * convert any strings to names, where unambiguous.
			 */
			for (jn = top[-1]; jn; jn = jn->RIGHT) {
				if (jn->LEFT->op == JXOP_COLON || jn->LEFT->op == JXOP_MAYBEMEMBER) {
					/* If left of colon is a string instead
					 * of a name, fix it
					 */
					if (JC_IS_STRING(jn->LEFT->LEFT)) {
						jxcalc_t *name;
						t.op = JXOP_NAME;
						t.full = jn->LEFT->LEFT->u.literal->text;
						t.len = strlen(t.full);
						name = jcalloc(&t);
						jx_calc_free(jn->LEFT->LEFT);
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
static void shift(stack_t *stack, jxcalc_t *jc, const char *str)
{
	stack->stack[stack->sp] = jc;
	stack->str[stack->sp] = str;
	stack->sp++;
	dumpstack(stack, "Shift %.1s", str);
}

/* Check whether JXOP_COLON is misused anywhere */
static int parsecolon(jxcalc_t *jc)
{
	/* Defend against NULL */
	if (!jc)
		return 0;

	switch (jc->op) {
	  case JXOP_COLON:
		return 1;

	  case JXOP_SUBSCRIPT:
		if (parsecolon(jc->LEFT))
			return 1;
		if (jc->RIGHT->op == JXOP_COLON)
			return parsecolon(jc->RIGHT->RIGHT);
		else
			return parsecolon(jc->RIGHT);
		break;

	  case JXOP_QUESTION:
		if (parsecolon(jc->LEFT))
			return 1;
		if (jc->RIGHT->op == JXOP_COLON)
			return parsecolon(jc->RIGHT->LEFT) || parsecolon(jc->RIGHT->RIGHT);
		else
			return parsecolon(jc->RIGHT);

	  case JXOP_OBJECT:
		return 0;

	  case JXOP_LITERAL:
	  case JXOP_STRING:
	  case JXOP_NUMBER:
	  case JXOP_BOOLEAN:
	  case JXOP_NULL:
	  case JXOP_NAME:
	  case JXOP_FROM:
	  case JXOP_REGEX:
		break;

	  case JXOP_DOT:
	  case JXOP_DOTDOT:
	  case JXOP_ELLIPSIS:
	  case JXOP_ARRAY:
	  case JXOP_COALESCE:
	  case JXOP_MAYBEMEMBER:
	  case JXOP_NJOIN:
	  case JXOP_LJOIN:
	  case JXOP_RJOIN:
	  case JXOP_NEGATE:
	  case JXOP_ISNULL:
	  case JXOP_ISNOTNULL:
	  case JXOP_MULTIPLY:
	  case JXOP_DIVIDE:
	  case JXOP_MODULO:
	  case JXOP_ADD:
	  case JXOP_SUBTRACT:
	  case JXOP_BITNOT:
	  case JXOP_BITAND:
	  case JXOP_BITOR:
	  case JXOP_BITXOR:
	  case JXOP_NOT:
	  case JXOP_AND:
	  case JXOP_OR:
	  case JXOP_LT:
	  case JXOP_LE:
	  case JXOP_EQ:
	  case JXOP_NE:
	  case JXOP_GE:
	  case JXOP_GT:
	  case JXOP_ICEQ:
	  case JXOP_ICNE:
	  case JXOP_LIKE:
	  case JXOP_NOTIN:
	  case JXOP_NOTLIKE:
	  case JXOP_IN:
	  case JXOP_EQSTRICT:
	  case JXOP_NESTRICT:
	  case JXOP_COMMA:
	  case JXOP_BETWEEN:
	  case JXOP_ENVIRON:
	  case JXOP_ASSIGN:
	  case JXOP_APPEND:
	  case JXOP_MAYBEASSIGN:
	  case JXOP_VALUES:
	  case JXOP_EACH:
	  case JXOP_GROUP:
	  case JXOP_FIND:
		return parsecolon(jc->LEFT) || parsecolon(jc->RIGHT);

	  case JXOP_FNCALL:
		return parsecolon(jc->u.func.args);

	  case JXOP_AG:
	  case JXOP_STARTPAREN:
	  case JXOP_ENDPAREN:
	  case JXOP_STARTARRAY:
	  case JXOP_ENDARRAY:
	  case JXOP_STARTOBJECT:
	  case JXOP_ENDOBJECT:
	  case JXOP_SELECT:
	  case JXOP_DISTINCT:
	  case JXOP_AS:
	  case JXOP_WHERE:
	  case JXOP_GROUPBY:
	  case JXOP_HAVING:
	  case JXOP_ORDERBY:
	  case JXOP_DESCENDING:
	  case JXOP_LIMIT:
	  case JXOP_INVALID:
		/* None of these should appear in a parsed expression */
		abort();
	}

	return 0;
}


/* Search for aggregate functions, and add JXOP_AG to the expression where
 * appropriate.  Return the altered expression.
 */
static jxcalc_t *parseag(jxcalc_t *jc, jxag_t *ag)
{
	int     addhere;
	token_t t;

	/* Defend against NULL */
	if (!jc)
		return NULL;

	/* If no aggregate list passed in, make one now.  Make it big */
	addhere = 0;
	if (!ag) {
		ag = (jxag_t *)calloc(1, sizeof(*ag) + 100 * sizeof(jxcalc_t *));
		addhere = 1;
	}

	/* Recursively check subexpressions */
	switch (jc->op) {
	  case JXOP_LITERAL:
	  case JXOP_STRING:
	  case JXOP_NUMBER:
	  case JXOP_BOOLEAN:
	  case JXOP_NULL:
	  case JXOP_NAME:
	  case JXOP_FROM:
	  case JXOP_REGEX:
		break;

	  case JXOP_DOT:
	  case JXOP_DOTDOT:
	  case JXOP_ELLIPSIS:
	  case JXOP_ARRAY:
	  case JXOP_OBJECT:
	  case JXOP_SUBSCRIPT:
	  case JXOP_COALESCE:
	  case JXOP_QUESTION:
	  case JXOP_COLON:
	  case JXOP_MAYBEMEMBER:
	  case JXOP_NJOIN:
	  case JXOP_LJOIN:
	  case JXOP_RJOIN:
	  case JXOP_NEGATE:
	  case JXOP_ISNULL:
	  case JXOP_ISNOTNULL:
	  case JXOP_MULTIPLY:
	  case JXOP_DIVIDE:
	  case JXOP_MODULO:
	  case JXOP_ADD:
	  case JXOP_SUBTRACT:
	  case JXOP_BITNOT:
	  case JXOP_BITAND:
	  case JXOP_BITOR:
	  case JXOP_BITXOR:
	  case JXOP_NOT:
	  case JXOP_AND:
	  case JXOP_OR:
	  case JXOP_LT:
	  case JXOP_LE:
	  case JXOP_EQ:
	  case JXOP_NE:
	  case JXOP_GE:
	  case JXOP_GT:
	  case JXOP_ICEQ:
	  case JXOP_ICNE:
	  case JXOP_LIKE:
	  case JXOP_NOTIN:
	  case JXOP_NOTLIKE:
	  case JXOP_IN:
	  case JXOP_EQSTRICT:
	  case JXOP_NESTRICT:
	  case JXOP_COMMA:
	  case JXOP_BETWEEN:
	  case JXOP_ENVIRON:
	  case JXOP_ASSIGN:
	  case JXOP_APPEND:
	  case JXOP_MAYBEASSIGN:
	  case JXOP_VALUES:
	  case JXOP_FIND:
		jc->LEFT = parseag(jc->LEFT, ag);
		jc->RIGHT = parseag(jc->RIGHT, ag);
		break;

	  case JXOP_EACH:
	  case JXOP_GROUP:
		jc->LEFT = parseag(jc->LEFT, ag);
		jc->RIGHT = parseag(jc->RIGHT, NULL); /* gets its own list */
		break;

	  case JXOP_FNCALL:
		/* If this is an aggregate function, add it */
		if (jc->u.func.jf->agfn) {
			ag->ag[ag->nags++] = jc;
			jc->u.func.agoffset = ag->agsize;
			ag->agsize += jc->u.func.jf->agsize;
		}

		/* Check arguments */
		jc->u.func.args = parseag(jc->u.func.args, ag);
		break;

	  case JXOP_AG:
	  case JXOP_STARTPAREN:
	  case JXOP_ENDPAREN:
	  case JXOP_STARTARRAY:
	  case JXOP_ENDARRAY:
	  case JXOP_STARTOBJECT:
	  case JXOP_ENDOBJECT:
	  case JXOP_SELECT:
	  case JXOP_DISTINCT:
	  case JXOP_AS:
	  case JXOP_WHERE:
	  case JXOP_GROUPBY:
	  case JXOP_HAVING:
	  case JXOP_ORDERBY:
	  case JXOP_DESCENDING:
	  case JXOP_LIMIT:
	  case JXOP_INVALID:
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
	ag = (jxag_t *)realloc(ag, sizeof(*ag) + ag->nags * sizeof(jxcalc_t *));
	ag->expr = jc;
	t.op = JXOP_AG;
	jc = jcalloc(&t);
	jc->u.ag = ag;
	return jc;
}


/* Parse a calc expression, and return it as a jxop_t tree.  "str" is the
 * string to parse, and "end" is the end of the string or NULL to end at '\0'.
 * *refend will be set to the point where parsing stopped, which may be before
 * the end, e.g. if the string ends with a semicolon or other non-operator.
 * *referr will be set to an error message in a dynamically-allocated string.
 * canassign enables parsing "=" as an assignment operator.
 */
jxcalc_t *jx_calc_parse(const char *str, const char **refend, const char **referr, int canassign)
{
	const char    *c = str;
	jxcalc_t *jc;
	char *err;
	token_t token;
	stack_t stack;

	/* Initialize the stack */
	stack.sp = 0;
	stack.canassign = canassign;
	err = NULL;

	/* Scan tokens through the end of the expression */
	while ((c = lex(c, &token, &stack)) != NULL && token.op != JXOP_INVALID) {
		/* Convert token to a jxcalc_t */
		jc = jcalloc(&token);

		/* Try to reduce */
		if (stack.sp == 0)
			err = NULL; /* can't reduce an empty stack */
		else if (stack.stack[stack.sp - 1]->op == JXOP_NAME && jc->op == JXOP_STARTPAREN)
			err = reduce(&stack, jc, token.full);
		else if (jc->op == JXOP_STARTARRAY && !pattern(&stack, "^") && !pattern(&stack, "+"))
			err = reduce(&stack, jc, token.full);
		else if (jc == &selectdistinct) {
			if (stack.stack[stack.sp - 1]->op == JXOP_SELECT) {
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
		if (jx_debug_flags.calc)
			jx_user_printf(NULL, "debug", "Doing the final reduce...\n");
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
		else if (token.op == JXOP_INVALID)
			*refend = token.full;
		else {
			*refend = token.full + token.len;

			/* If the last token was a quoted string or name,
			 * then the closing quote should *NOT* be included
			 * in the tail.
			 */
			if ((token.op == JXOP_STRING || token.op == JXOP_NAME) && **refend && strchr("\"'`", **refend))
				(*refend)++;
		}

		/* Skip past any trailing whitespace */
		*refend = skipwhitespace(*refend);
	}

	/* Clean up any extra stack items */
	while (stack.sp >= 2)
		jx_calc_free(stack.stack[--stack.sp]);
	if (stack.sp == 1 && err)
		jx_calc_free(stack.stack[--stack.sp]);

	return stack.sp == 0 ? NULL : stack.stack[0];
}
