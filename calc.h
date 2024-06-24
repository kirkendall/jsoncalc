#ifndef FALSE
# define FALSE 0
# define TRUE 1
#endif

/* This is a list of token types.  Nearly all of them are operators.
 * IF YOU MAKE ANY CHANGES HERE, THEN YOU MUST ALSO UPDATE THE operators[]
 * ARRAY IN calcparse.c
 */
typedef enum {
        JSONOP_ADD,
	JSONOP_AG,
	JSONOP_AND,
	JSONOP_APPEND,
	JSONOP_ARRAY,
	JSONOP_AS,
	JSONOP_ASSIGN,
	JSONOP_BETWEEN,
	JSONOP_BITAND,
	JSONOP_BITNOT,
	JSONOP_BITOR,
	JSONOP_BITXOR,
	JSONOP_BOOLEAN,
	JSONOP_COALESCE,
	JSONOP_COLON,
	JSONOP_COMMA,
	JSONOP_DESCENDING,
	JSONOP_DISTINCT,
	JSONOP_DIVIDE,
	JSONOP_DOT,
	JSONOP_EACH,
	JSONOP_ELIPSIS,
	JSONOP_ENDARRAY,
	JSONOP_ENDOBJECT,
	JSONOP_ENDPAREN,
	JSONOP_EQ,
	JSONOP_EQSTRICT,
	JSONOP_EXPLAIN,
	JSONOP_FNCALL,
	JSONOP_FROM,
	JSONOP_GE,
	JSONOP_GROUP,
	JSONOP_GROUPBY,
	JSONOP_GT,
	JSONOP_ICEQ,
	JSONOP_ICNE,
	JSONOP_IN,
	JSONOP_ISNOTNULL,
	JSONOP_ISNULL,
	JSONOP_LE,
	JSONOP_LIKE,
	JSONOP_LITERAL,
	JSONOP_LJOIN,
	JSONOP_LT,
	JSONOP_MODULO,
	JSONOP_MULTIPLY,
	JSONOP_NAME,
	JSONOP_NE,
	JSONOP_NEGATE,
	JSONOP_NESTRICT,
	JSONOP_NJOIN,
	JSONOP_NOT,
	JSONOP_NOTLIKE,
	JSONOP_NULL,
	JSONOP_NUMBER,
	JSONOP_OBJECT,
	JSONOP_OR,
	JSONOP_ORDERBY,
	JSONOP_QUESTION,
	JSONOP_REGEX,
	JSONOP_RJOIN,
	JSONOP_SELECT,
	JSONOP_STARTARRAY,
	JSONOP_STARTOBJECT,
	JSONOP_STARTPAREN,
	JSONOP_STRING,
	JSONOP_SUBSCRIPT,
	JSONOP_SUBTRACT,
	JSONOP_WHERE,
	JSONOP_INVALID /* <-- This must be the last */
} jsonop_t;

/* This is used to represent an expression, or part of an expression */
typedef struct jsoncalc_s{
        jsonop_t op;
        union {
                struct {
                        struct jsoncalc_s *left;        /* left operand */
                        struct jsoncalc_s *right;       /* right operand */
                } param;
                struct {
                        struct jsonfunc_s *jf;          /* function info */
                        struct jsoncalc_s *args;        /* args as array generator */
                        size_t agoffset;                /* If aggregate, this is the offset of its agdata */
                } func;
                struct {
			void	*preg;
			int	global;
                } regex;
                struct jsonag_s *ag;
                struct jsonselect_s *select;
                json_t *literal;
                char text[1]; /* extra chars get allocated later */
        } u;
} jsoncalc_t;


/* Functions are stored in a linked list of these.  If a function is *not* an
 * aggregate function then agfn is NULL and storeagesize is 0.
 */
typedef struct jsonfunc_s {
        struct jsonfunc_s *next;
        char    *name;
        json_t *(*fn)(json_t *args, void *agdata);
        void   (*agfn)(json_t *args, void *agdata);
        size_t  agsize;
        int	jfoptions;
} jsonfunc_t;
#define JSONFUNC_JSONFREE 1	/* Call json_free() on the agdata afterward */
#define JSONFUNC_FREE 2		/* Call free() on the agdata afterward */

/* This enum represents details about how a single context layer is used */
typedef enum {
	JSON_CONTEXT_VAR = 1,	/* variable -- use with GLOBAL for non-local */
        JSON_CONTEXT_CONST = 2,	/* const -- like variable but can't assign */
        JSON_CONTEXT_GLOBAL = 4,/* Context can't be changed by script */
	JSON_CONTEXT_THIS = 8,  /* Context can be "this" or "that" */
        JSON_CONTEXT_ARGS = 16, /* function arguments, and can be "arguments" */
        JSON_CONTEXT_AUTOLOAD_FIRST = 32 /* try autoload() before *data */
} jsoncontextflags_t;
#define JSON_CONTEXT_LOCAL_VAR		(JSON_CONTEXT_VAR)
#define JSON_CONTEXT_LOCAL_CONST	(JSON_CONTEXT_CONST)
#define JSON_CONTEXT_GLOBAL_VAR		(JSON_CONTEXT_VAR|JSON_CONTEXT_GLOBAL)
#define JSON_CONTEXT_GLOBAL_CONST	(JSON_CONTEXT_CONST|JSON_CONTEXT_GLOBAL)

/* This is used to track context (the stack of variable definitions).  */
typedef struct jsoncontext_s {
    struct jsoncontext_s *older;/* link list of jsoncontext_t contexts */
    json_t *data;     /* a used item */
    json_t *(*autoload)(char *key); /* called from json_used_by_key() */
    void   (*modified)(struct jsoncontext_s *layer, jsoncalc_t *lvalue);
    jsoncontextflags_t flags;
} jsoncontext_t;



/* This stores a list of aggregate functions used in a given context.  Each
 * jsoncalc_t node with ->op==JSONOP_AG contains an "ag" pointer that points
 * to one of these, so  ->ag[i]->u.func->jf->agfn(args, context, storage)
 * is the way to call the aggregating function. Yikes.
 *
 * Note that the combined size of the agdata is stored here, but the memory
 * for it is allocated in json_calc() as needed.  This is done for thread
 * safety, in case two threads call json_calc() on the same jsoncalc_t at
 * the same time.
 */
typedef struct jsonag_s {
        jsoncalc_t *expr;   /* equation containing aggregate functions */
        int        nags;    /* number of aggregates */
        size_t     agsize;  /* combined storage requirements */
        jsoncalc_t *ag[1];  /* function calls with params, expanded as needed */
} jsonag_t;


/* This is used to collect details about a "select" statement */
typedef struct jsonselect_s {
	jsoncalc_t *select;	/* Selected columns as an object generator, or NULL */
	int	distinct;
	jsoncalc_t *from;	/* expression that returns a table, or NULL for first array in context */
	jsoncalc_t *where;	/* expression that selects rows, or NULL for all */
	json_t *groupby;	/* list of field names, or NULL */
	json_t *orderby;	/* list of field names, or NULL */
	int	limit;
} jsonselect_t;
        

/* Function declarations */
void json_calc_function(
        char    *name,
        json_t *(*fn)(json_t *args, void *agdata),
        void   (*agfn)(json_t *args, void *agdata),
        size_t  agsize);
jsonfunc_t *json_calc_function_by_name(char *name);
char *json_calc_op_name(jsonop_t op);
void json_calc_dump(jsoncalc_t *calc);
jsoncalc_t *json_calc_parse(char *str, char **refend, char **referr);
void json_calc_free(jsoncalc_t *calc);
void *json_calc_ag(jsoncalc_t *calc, void *agdata);
json_t *json_calc(jsoncalc_t *calc, jsoncontext_t *context, void *agdata);

jsoncontext_t *json_context_free(jsoncontext_t *context, int freedata);
jsoncontext_t *json_context(jsoncontext_t *context, json_t *data, jsoncontextflags_t flags);
jsoncontext_t *json_context_insert(jsoncontext_t **refcontext, jsoncontextflags_t flags);
json_t *json_context_by_key(jsoncontext_t *context, char *key, jsoncontext_t **reflayer);
json_t *json_context_assign(jsoncalc_t *lvalue, json_t *rvalue, jsoncontext_t *context);
json_t *json_context_append(jsoncalc_t *lvalue, json_t *rvalue, jsoncontext_t *context);
int json_context_declare(jsoncontext_t **refcontext, char *key, json_t *value, jsoncontextflags_t flags);
json_t *json_context_default_table(jsoncontext_t *context);

/****************************************************************************/

/* This describes an error.  "where" is a pointer into the source script where
 * the error was detected, and "text" is the locale-specific text describing
 * the error.
 *
 * It is also used for successfully returning values.  This is denoted by
 * a non-NULL "ret" field.  So the try-catch mechanism allows errors to
 * "bubble up" until they're caught, and user-defined functions allow return
 * values to "bubble up" to the function invocation.
 */
typedef struct {
	int	code;
	char	*where;
	json_t	*ret;
	char	text[1];	/* extended as necessary */
} jsonerror_t;

/* This data type is used for storing command names.  Some command names are
 * built in, but plugins can add new command names too.
 */
typedef struct jsoncmdname_s {
	struct jsoncmdname_s *next;
	char	*name;
	struct jsoncmd_s *(*argparser)(char **refstr, jsonerror_t **referr);
	jsonerror_t *(*run)(struct jsoncmd_s *cmd, jsoncontext_t **refcontext);
	char	*pluginname;
} jsoncmdname_t;

/* This stores a parsed statement. */
typedef struct jsoncmd_s {
	char		  *where;/* source location, for reporting errors */
	jsoncmdname_t 	  *name;/* command name and other details */
	char		  *key;	/* Name of a variable, if the cmd uses one */
	jsoncalc_t 	  *calc;/* calc expression, fit he cmd uses one */
	jsoncontextflags_t flags;/* Context flags for "key" */
	struct jsoncmd_s *sub;	/* For "then" in "if-then-else" for example */
	struct jsoncmd_s *more;/* For "else" in "if-then-else" for example */
	struct jsoncmd_s *next;/* in a series of statements, "next" is next */
} jsoncmd_t;


void json_cmd_hook(char *pluginname, char *cmdname, jsoncmd_t *(*argparser)(char **refstr, jsonerror_t **referr), jsonerror_t *(*run)(jsoncmd_t *cmd, jsoncontext_t **refcontext));
jsonerror_t *json_cmd_error(char *where, int code, char *fmt, ...);
void json_cmd_parse_whitespace(char **refstr);
char *json_cmd_parse_key(char **refstr, int quotable);
char *json_cmd_parse_paren(char **refstr);
jsoncmd_t *json_cmd(char *where, jsoncmdname_t *name);
void json_cmd_free(jsoncmd_t *cmd);
jsoncmd_t *json_cmd_parse_single(char **refstr, jsonerror_t **referr);
jsoncmd_t *json_cmd_parse_curly(char **refstr, jsonerror_t **referr);
jsoncmd_t *json_cmd_parse_string(char *str);
jsoncmd_t *json_cmd_parse_file(char *filename);
jsonerror_t *json_cmd_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
