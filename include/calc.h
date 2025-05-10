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
	JSONOP_ENVIRON,
	JSONOP_EQ,
	JSONOP_EQSTRICT,
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
	JSONOP_LIMIT,
	JSONOP_LITERAL,
	JSONOP_LJOIN,
	JSONOP_LT,
	JSONOP_MAYBEASSIGN,
	JSONOP_MAYBEMEMBER,
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
        char	*args;		/* Argument list, as text */
        char	*returntype;	/* Return value type, as text */
        json_t *(*fn)(json_t *args, void *agdata);
        void   (*agfn)(json_t *args, void *agdata);
        size_t  agsize;
        int	jfoptions;
        struct jsoncmd_s *user;
        json_t	*userparams;
} jsonfunc_t;
#define JSONFUNC_JSONFREE 1	/* Call json_free() on the agdata afterward */
#define JSONFUNC_FREE 2		/* Call free() on the agdata afterward */

/* This enum represents details about how a single context layer is used */
typedef enum {
	JSON_CONTEXT_NOFREE = 1,/* Don't free the data when context is freed */
	JSON_CONTEXT_VAR = 2,	/* contains vars -- use with GLOBAL for non-local */
        JSON_CONTEXT_CONST = 4,	/* contains consts -- like variable but can't assign */
        JSON_CONTEXT_GLOBAL = 8,/* Context is accessible everywhere */
	JSON_CONTEXT_THIS = 16, /* Context can be "this" or "that" */
	JSON_CONTEXT_DATA = 32,	/* Context contains "data" variable */
        JSON_CONTEXT_ARGS = 64, /* Function arguments and local vars/consts */
        JSON_CONTEXT_NOCACHE = 128, /* try autoload() before *data */
        JSON_CONTEXT_MODIFIED = 256 /* Data has been modified (set via context->modified() function */
} jsoncontextflags_t;

/* This is used to track context (the stack of variable definitions).  */
typedef struct jsoncontext_s {
    struct jsoncontext_s *older;/* link list of jsoncontext_t contexts */
    json_t *data;     /* a used item */
    json_t *(*autoload)(char *key); /* called from json_context_by_key() */
    void   (*modified)(struct jsoncontext_s *layer, jsoncalc_t *lvalue);
    jsoncontextflags_t flags;
} jsoncontext_t;


/* For non-aggregate functions, this is used to pass other information that
 * they might need.
 */
typedef struct {
	jsoncontext_t *context;
	jsoncalc_t    *regex; /* The regex_t is at regex->u.regex.preg */
} jsonfuncextra_t;


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


/* This tracks source code for commands.  For strings, "buf" points to the
 * string.  For files, additional memory is allocated for "buf" and must also
 * be freed, but "filename" is a copy of a pointer to a filename string which
 * must not be freed before the application terminates.
 */
typedef struct {
	char	*filename;	/* name of source file, if any */
	char	*buf;		/* buffer, contains entire souce file */
	char	*str;		/* current parse position within "base" */
	size_t	size;		/* size of "base" */
} jsonsrc_t;

/* This is used for returning the result of a command.  A NULL pointer means
 * the command completed without incident, and execution should continue to
 * the next command.  Otherwise, the meaning is determined by the "ret" field
 * as follows:
 *   NULL		An error, indicated by code and text
 *   &json_cmd_break	A "break" command
 *   &json_cmd_continue A "continue" command
 *   (anything else)	A "return" command with this value
 */
typedef struct {
	json_t	*ret;		/* if really a "return" then this is value */
	char	*filename;	/* filename where error occurred (if any) */
	int	lineno;		/* line number where error occurred */
	int	code;		/* error code */
	char	text[1];	/* extended as necessary */
} jsoncmdout_t;
extern json_t json_cmd_break, json_cmd_continue;

/* This data type is used for storing command names.  Some command names are
 * built in, but plugins can add new command names too.
 */
typedef struct jsoncmdname_s {
	struct jsoncmdname_s *next;
	char	*name;
	struct jsoncmd_s *(*argparser)(jsonsrc_t *src, jsoncmdout_t **referr);
	jsoncmdout_t *(*run)(struct jsoncmd_s *cmd, jsoncontext_t **refcontext);
	char	*pluginname;
} jsoncmdname_t;

/* This stores a parsed statement. */
typedef struct jsoncmd_s {
	char		   *filename;/* source file, for reporting errors */
	int		   lineno;/* line number, for reporting errors */
	jsoncmdname_t	   *name;/* command name and other details */
	char		   var;
	char		   *key; /* Name of a variable, if the cmd uses one */
	jsoncalc_t 	   *calc;/* calc expression, if the cmd uses one */
	jsoncontextflags_t flags;/* Context flags for "key" */
	struct jsoncmd_s   *sub; /* For "then" in "if-then-else" for example */
	struct jsoncmd_s   *more;/* For "else" in "if-then-else" for example */
	struct jsoncmd_s   *next;/* in a series of statements, "next" is next */
} jsoncmd_t;

/* These are magic values for json_context_file() "current" argument.  They
 * aren't enums because we could also pass an int index to select a file.
 */
#define JSON_CONTEXT_FILE_NEXT		(-1)
#define JSON_CONTEXT_FILE_SAME		(-2)
#define JSON_CONTEXT_FILE_PREVIOUS	(-3)

/* This flag indicates whether computations have been interupted */
extern int json_interupt;

/* Function declarations */
void json_calc_aggregate_hook(
        char    *name,
        char	*args,
        char	*type,
        json_t *(*fn)(json_t *args, void *agdata),
        void   (*agfn)(json_t *args, void *agdata),
        size_t  agsize,
        int	jfoptions);
void json_calc_function_hook(
	char	*name,
	char	*args,
	char	*type,
        json_t *(*fn)(json_t *args, void *agdata));
int json_calc_function_user(
	char *name,
	json_t *params,
	char *paramstr,
	char *returntype,
	jsoncmd_t *cmd);
jsonfunc_t *json_calc_function_by_name(char *name);
char *json_calc_op_name(jsonop_t op);
void json_calc_dump(jsoncalc_t *calc);
jsoncalc_t *json_calc_parse(char *str, char **refend, char **referr, int canassign);
jsoncalc_t *json_calc_list(jsoncalc_t *list, jsoncalc_t *item);
void json_calc_free(jsoncalc_t *calc);
void *json_calc_ag(jsoncalc_t *calc, void *agdata);
json_t *json_calc(jsoncalc_t *calc, jsoncontext_t *context, void *agdata);

json_t *json_context_math;
void json_context_hook(jsoncontext_t *(*addcontext)(jsoncontext_t *context));
jsoncontext_t *json_context_free(jsoncontext_t *context);
jsoncontext_t *json_context(jsoncontext_t *context, json_t *data, jsoncontextflags_t flags);
jsoncontext_t *json_context_insert(jsoncontext_t **refcontext, jsoncontextflags_t flags);
jsoncontext_t *json_context_std(json_t *data);
json_t *json_context_file(jsoncontext_t *context, char *filename, int writable, int *refcurrent);
jsoncontext_t *json_context_func(jsoncontext_t *context, jsonfunc_t *fn, json_t *args);
json_t *json_context_by_key(jsoncontext_t *context, char *key, jsoncontext_t **reflayer);
json_t *json_context_assign(jsoncalc_t *lvalue, json_t *rvalue, jsoncontext_t *context);
json_t *json_context_append(jsoncalc_t *lvalue, json_t *rvalue, jsoncontext_t *context);
int json_context_declare(jsoncontext_t **refcontext, char *key, json_t *value, jsoncontextflags_t flags);
json_t *json_context_default_table(jsoncontext_t *context, char **refexpr);

/****************************************************************************/

/* This value is returned by json_cmd_parse() and json_cmd_parse_string()
 * if an error is detected.  Note that NULL does *NOT* indicate an error.
 */
extern jsoncmd_t JSON_CMD_ERROR[];

void json_cmd_hook(char *pluginname, char *cmdname, jsoncmd_t *(*argparser)(jsonsrc_t *src, jsoncmdout_t **referr), jsoncmdout_t *(*run)(jsoncmd_t *cmd, jsoncontext_t **refcontext));
jsoncmdout_t *json_cmd_error(char *filename, int lineno, int code, char *fmt, ...);
void json_cmd_parse_whitespace(jsonsrc_t *src);
char *json_cmd_parse_key(jsonsrc_t *src, int quotable);
char *json_cmd_parse_paren(jsonsrc_t *src);
jsoncmd_t *json_cmd(jsonsrc_t *src, jsoncmdname_t *name);
void json_cmd_free(jsoncmd_t *cmd);
jsoncmd_t *json_cmd_parse_single(jsonsrc_t *src, jsoncmdout_t **referr);
jsoncmd_t *json_cmd_parse_curly(jsonsrc_t *src, jsoncmdout_t **referr);
jsoncmd_t *json_cmd_parse_string(char *str);
jsoncmd_t *json_cmd_parse_file(char *filename);
jsoncmdout_t *json_cmd_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
json_t *json_cmd_fncall(json_t *args, jsonfunc_t *fn, jsoncontext_t *context);
jsoncmd_t *json_cmd_append(jsoncmd_t *existing, jsoncmd_t *added, jsoncontext_t *context);
