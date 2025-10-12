/* json.h */

/* Help C and C++ peacefully coexist */
#ifndef EXTERN_C
# ifdef _cplusplus
#  define BEGIN_C extern "C" {
#  define END_C }
#  define VOID_C
# else
#  define BEGIN_C
#  define END_C
#  define VOID_C void
# endif
#endif

/* These are token types.  Some are used only during parsing, some are for both
 * parsing and the internal json_t tree, and some are internally for json_t only.
 */
typedef enum {
	JSON_BADTOKEN, JSON_NEWLINE,
	JSON_OBJECT, JSON_ENDOBJECT,
	JSON_ARRAY, JSON_ENDARRAY, JSON_DEFER,
	JSON_KEY, JSON_STRING, JSON_NUMBER, JSON_NULL, JSON_BOOLEAN
} jsontype_t;

/* These represent a parsed token */
typedef struct {
	const char *start;
	size_t len;
	jsontype_t type;
} json_token_t;


/* This represents a JSON value.  The way it is used depends on the type:
 * JSON_OBJECT	first points to first member
 * JSON_ARRAY	first points to first element
 * JSON_KEY	first points to value, text contains name
 * JSON_STRING	text contains value
 * JSON_NUMBER	text contains value, as a string
 * JSON_BOOLEAN	text contains "true" or "false"
 * JSON_NULL	text is "" or an error message
 */
typedef struct json_s {
	struct json_s *next;	/* next element of an array or object */
	struct json_s *first;	/* contents of this object, array, or key */
	jsontype_t type : 4;	/* type of this json_t node */
	unsigned    memslot:12; /* used for JSON_DEBUG_MEMORY */
	char        text[14];	/* value of string, number, boolean; name of key */
} json_t;

/* For json_t's that are JSON_ARRAY or JSON_OBJECT, this is a pointer to the
 * last element/member, while parsing a file only.  This saves us from having
 * to scan potentially long arrays every time we want to append one element.
 *
 * The way this works is: It adds 1 to the container, so it points to the
 * memory immediately after the json_t.  It then casts that pointer to be
 * a pointer to a (json_t*), and uses [-1] to find the space for a (jsont_t *)
 * at then end of j's memory.
 */
#define JSON_END_POINTER(j)	(((json_t **)((j) + 1))[-1])

/* In arrays, we use the 4 bytes before JSON_END_POINTER to store the length */
#define JSON_ARRAY_LENGTH(j)	(((__uint32_t *)&JSON_END_POINTER(j))[-1])

/* These are used to stuff a binary double or int into a JSON_NUMBER*/
#define JSON_DOUBLE(j)	(((double *)((j) + 1))[-1])
#define JSON_INT(j)	(((int *)((j) + 1))[-1])
/* This stores info about formatting -- mostly output formatting, since for
 * input we take whatever we're given.
 */
typedef struct {
	short	tab;	/* indentation to add for pretty-printing nested data */
	short	oneline;/* Force compact output if shorter than this */
	short	digits;	/* precision for floating point output */
	char	table[20];/* Table output: csv/shell/grid/json */
	char	string;	/* unquoted string output */
	char	pretty;	/* Pretty-print JSON */
	char	elem;	/* one element per line */
	char	sh;	/* Quote output for shell */
	char	errors;	/* Error output.  Writes text in "null" to stderr */
	char	ascii;	/* Convert non-ASCII characters to \u sequences */
	char	color;	/* Allow the use of ANSI escape sequences */
	char	quick;	/* Output tables piecemeal.  Use first row for names */
	char	graphic;/* Use Unicode graphic chars where appropriate */
	char	prefix[20]; /* Prefix to add to keys for shell output */
	char	null[20];/* how to display null in tables */
	char	escprompt[20]; /* coloring for the prompt */
	char	escresult[20]; /* coloring of result */
	char	escgridhead[20]; /* coloring of last grid heading line */
	char	escgridhead2[20]; /* coloring of other grid heading lines */
	char	escgridline[20]; /* coloring of grid lines between columns */
	char	escerror[20]; /* coloring of errors */
	char	escdebug[20]; /* coloring of debugging output */
	FILE	*fp;	/* where to write to */
} jsonformat_t;

/* This is a collection of debugging flags.  These are generally set via
 * the json_debug() function, but various library functions need access to
 * them.
 */
typedef struct {
        int abort;      /* Controls whether some errors cause an abort */
        int expr;       /* Output information about simple expressions */
        int calc;       /* Output information about complex expressions */
        int trace;      /* Trace commands as they're run */
} json_debug_t;

extern json_debug_t json_debug_flags;
extern jsonformat_t json_format_default;
extern char json_format_color_end[20];

/* This represents a file that is open for reading JSON data or scripts.
 * The file is mapped into memory starting at "base", and can be accessed
 * like a giant string.
 *
 * For data files, the reference count is 0 unless it contains a deferred
 * array, in which case the reference count is 1.  For script files, it the
 * number of functions or vars/consts defined in the script.
 */
typedef struct jsonfile_s {
	struct jsonfile_s *other;/* Used to for a linked list */
	int		fd;	/* File descriptor of the open file */
	int		isfile;	/* Boolean: is this a regular file (not pipe, etc) */
	int		refs;	/* Reference count */
	size_t		size;	/* Size of the file, in bytes */
	const char	*base;	/* Contents of the file, as a giant string */
	char		*filename; /* Name of the file */
} jsonfile_t;

#define JSON_PATH_DELIM		':'

/* This stores info about the implementation of different types of deferred
 * arrays. It's basically a collection of function pointers, plus a size_t
 * indicating how much storage space it needs.
 */
typedef struct {
	size_t	size;	/* Size of jsondef_t plus any other needed storage */
	char	*desc;	/* basically the "class" of deferred items */
	json_t	*(*first)(json_t *array);	/* REQUIRED */
	json_t	*(*next)(json_t *elem);		/* REQUIRED */
	int	(*islast)(const json_t *elem);	/* REQUIRED */
	void	(*free)(json_t *array_or_elem);	/* Only if special needs */
	json_t	*(*byindex)(json_t *array, int index);
	json_t	*(*bykeyvalue)(json_t *array, const char *key, json_t *value);
} jsondeffns_t;

/* This is the generic part of a JSON_DEFER node.  It starts with plain json_t,
 * and adds some extra fields.  An actual JSON_DEFER note will generally have
 * other information, as its own data type.  The functions that jsondeffns_t
 * points to know the actual data type.
 */
typedef struct {
	json_t	json; /* Standard stuff, with ->type=JSON_DEFER */
	jsondeffns_t *fns; /* pointer to a group of function pointers */
	jsonfile_t *file; /* if non-NULL, it indicates which file to read */
} jsondef_t;

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
	JSONOP_DOTDOT,
	JSONOP_EACH,
	JSONOP_ELLIPSIS,
	JSONOP_ENDARRAY,
	JSONOP_ENDOBJECT,
	JSONOP_ENDPAREN,
	JSONOP_ENVIRON,
	JSONOP_EQ,
	JSONOP_EQSTRICT,
	JSONOP_FIND,
	JSONOP_FNCALL,
	JSONOP_FROM,
	JSONOP_GE,
	JSONOP_GROUP,
	JSONOP_GROUPBY,
	JSONOP_GT,
	JSONOP_HAVING,
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
	JSONOP_NOTIN,
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
	JSONOP_VALUES,
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

BEGIN_C

/* Files */
char json_file_new_type;
void json_file_defer(jsonfile_t *jf, json_t *array);
void json_file_defer_free(json_t *array);
jsonfile_t *json_file_load(const char *filename);
void json_file_unload(jsonfile_t *jf);
jsonfile_t *json_file_containing(const char *where, int *refline);
FILE *json_file_update(const char *filename);
char *json_file_path(const char *prefix, const char *name, const char *suffix);

/* Error handling */
extern char *json_debug(char *flags);

/* This flag indicates whether computations have been interrupted */
extern int json_interrupt;

/* Manipulation */
extern void json_free(json_t *json);
extern json_t *json_simple(const char *str, size_t len, jsontype_t type);
extern json_t *json_simple_from_token(json_token_t *token);
extern json_t *json_string(const char *str, size_t len);
extern json_t *json_number(const char *str, size_t len);
extern json_t *json_boolean(int boolean);
extern json_t *json_null(void);
extern json_t *json_error_null(const char *where, const char *fmt, ...);
extern json_t *json_from_int(int i);
extern json_t *json_from_double(double f);
extern json_t *json_key(const char *key, json_t *value);
extern json_t *json_object();
extern json_t *json_array();
extern json_t *json_defer(jsondeffns_t *fns);
extern json_t *json_defer_ellipsis(int from, int to);
extern char *json_append(json_t *container, json_t *more);
extern size_t json_sizeof(json_t *json);
extern char *json_typeof(json_t *json, int extended);
extern char *json_mix_types(char *oldtype, char *newtype);
extern void json_sort(json_t *array, json_t *orderby, int grouping);
extern json_t *json_copy_filter(json_t *json, int (*filter)(json_t *));
extern json_t *json_copy(json_t *json);
extern json_t *json_array_flat(json_t *array, int depth);
extern json_t *json_unroll(json_t *table, json_t *nestlist);
extern json_t *json_array_group_by(json_t *array, json_t *orderby);
extern int json_walk(json_t *json, int (*callback)(json_t *, void *), void *data);

/* Parsing */
extern void json_parse_hook(const char *plugin, const char *name, const char *suffix, const char *mimetype, int (*tester)(const char *str, size_t len), json_t *(*parser)(const char *str, size_t len, const char **refend, const char **referr));
extern json_t *json_parse_string(const char *str);
extern json_t *json_parse_file(const char *filename);

/* Serialization / Output */
extern json_t *json_explain(json_t *stats, json_t *row, int depth);
extern char *json_serialize(json_t *json, jsonformat_t *format);
extern void json_print_table_hook(char *name, void (*fn)(json_t *json, jsonformat_t *format));
extern int json_print_incomplete_line;
extern void json_print(json_t *json, jsonformat_t *format);
extern void json_grid(json_t *json, jsonformat_t *format);
extern void json_format_set(jsonformat_t *format, json_t *config);
extern void json_format_esc(char *esc, const char *name, int nounderlined);
extern void json_undefer(json_t *arr);

/* Accessing */
extern json_t *json_by_key(const json_t *container, const char *key);
extern json_t *json_by_deep_key(json_t *container, char *key);
extern json_t *json_by_index(json_t *container, int idx);
extern json_t *json_by_expr(json_t *container, const char *expr, const char **after);
extern json_t *json_find(json_t *haystack, json_t *needle, int ignorecase, char *needkey);
#ifdef REG_ICASE /* skip this if <regex.h> not included */
extern json_t *json_find_regex(json_t *haystack, regex_t *regex, char *needkey);
#endif
extern json_t *json_find_calc(json_t *haystack, jsoncalc_t *calc, jsoncontext_t *context);
extern char *json_default_text(char *newdefault);
extern char *json_text(json_t *json);
extern double json_double(json_t *json);
extern int json_int(json_t *json);
extern json_t *json_first(json_t *arr);
extern json_t *json_next(json_t *elem);
extern void json_break(json_t *elem);
extern int json_is_last(const json_t *elem);

/* Testing */
extern int json_length(json_t *container);
extern int json_is_true(json_t *json);
extern int json_is_null(json_t *json);
extern int json_is_error(json_t *json);
extern int json_is_table(json_t *json);
extern int json_is_short(json_t *json, size_t oneline);
extern int json_is_date(json_t *json);
extern int json_is_time(json_t *json);
extern int json_is_datetime(json_t *json);
extern int json_is_period(json_t *json);
extern int json_is_deferred_array(const json_t *arr);
extern int json_is_deferred_element(const json_t *elem);
extern int json_equal(json_t *j1, json_t *j2);
extern int json_compare(json_t *obj1, json_t *obj2, json_t *compare);
#define json_text_by_key(container, key) json_text(json_by_key((container), (key)))
#define json_text_by_deep_key(container, key) json_text(json_by_deep_key((container), (key)))
#define json_text_by_index(container, index) json_text(json_by_index((container), (index)))
/* The next parameter may be NULL.  See json_by_expr() for more details. */
#define json_text_by_expr(container, expr, after) json_text(json_by_expr((container), (expr), (after)))


/* Multibyte character strings */
size_t json_mbs_len(const char *s);
int json_mbs_width(const char *s);
int json_mbs_height(const char *s);
size_t json_mbs_line(const char *s, int line, char *buf, char **refstart, int *refwidth);
size_t json_mbs_wrap_char(char *buf, const char *s, int width);
size_t json_mbs_wrap_word(char *buf, const char *s, int width);
size_t json_mbs_canonize(char *dest, const char *src);
int json_mbs_cmp(const char *s1, const char *s2);
int json_mbs_ncmp(const char *s1, const char *s2, size_t len);
const char *json_mbs_substr(const char *s, size_t start, size_t *reflimit);
const char *json_mbs_str(const char *haystack, const char *needle, size_t *refccount, size_t *reflen, int last, int ignorecase);
void json_mbs_tolower(char *s);
void json_mbs_toupper(char *s);
void json_mbs_tomixed(char *s, json_t *exceptions);
int json_mbs_casecmp(const char *s1, const char *s2);
int json_mbs_ncasecmp(const char *s1, const char *s2, size_t len);
int json_mbs_abbrcmp(const char *abbr, const char *full);
const char *json_mbs_ascii(const char *str, char *buf);
size_t json_mbs_escape(char *dst, const char *src, size_t len, int quote, jsonformat_t *format);
size_t json_mbs_unescape(char *dst, const char *src, size_t len);
int json_mbs_like(const char *text, const char *pattern);

/* Dates and times */
int json_str_date(const char *str);
int json_str_time(const char *str);
int json_str_datetime(const char *str);
int json_str_period(const char *str);
int json_date(char *result, const char *str);
int json_time(char *result, const char *str, const char *tz);
int json_datetime(char *result, const char *str, const char *tz);
int json_datetime_add(char *result, const char *str, const char *period);
int json_datetime_subtract(char *result, const char *str, const char *period);
int json_datetime_diff(char *result, const char *str1, const char *str2);
json_t *json_datetime_fn(json_t *args, char *type);

/* Configuration data */
json_t *json_config, *json_system;
void json_config_load(const char *name);
void json_config_save(const char *name);
json_t *json_config_get(const char *section, const char *key);
void json_config_set(const char *section, const char *key, json_t *value);
json_t *json_config_parse(json_t *config, const char *settings, const char **refend);
#define json_config_get_int(section, key) json_int(json_config_get(section, key))
#define json_config_get_double(section, key) json_double(json_config_get(section, key))
#define json_config_get_text(section, key) json_text(json_config_get(section, key))

/* Plugins */
json_t *json_plugins;
json_t *json_plugin_load(const char *name);


#ifndef FALSE
# define FALSE 0
# define TRUE 1
#endif



/* Functions are stored in a linked list of these.  If a function is *not* an
 * aggregate function then agfn is NULL and storeagesize is 0.  Also, the
 * name, args, and returntype will generally be (const char *) literals for
 * compiled C functions, but dynamically-allocated strings for user-defined
 * (jsoncalc script) functions; the latter prevents us from declaring those
 * fields as "const" here.
 */
typedef struct jsonfunc_s {
        struct jsonfunc_s *other;
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
	const char	*buf;	/* buffer, contains entire source text */
	const char	*str;	/* current parse position within "base" */
	size_t	size;		/* size of "buf" */
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
	const char *where;	/* where error detected */
	char	text[1];	/* extended as necessary */
} jsoncmdout_t;
extern json_t json_cmd_break, json_cmd_continue;

/* This data type is used for storing command names.  Some command names are
 * built in, but plugins can add new command names too.
 */
typedef struct jsoncmdname_s {
	struct jsoncmdname_s *other;
	char	*name;
	struct jsoncmd_s *(*argparser)(jsonsrc_t *src, jsoncmdout_t **referr);
	jsoncmdout_t *(*run)(struct jsoncmd_s *cmd, jsoncontext_t **refcontext);
	char	*pluginname;
} jsoncmdname_t;

/* This stores a parsed statement. */
typedef struct jsoncmd_s {
	const char	   *where;/* pointer into source text, for reporting errors */
	jsoncmdname_t	   *name;/* command name and other details */
	char		   var;
	char		   *key; /* Name of a variable, if the cmd uses one */
	jsoncalc_t 	   *calc;/* calc expression, if the cmd uses one */
	jsoncontextflags_t flags;/* Context flags for "key" */
	struct jsoncmd_s   *sub; /* For "then" in "if-then-else" for example */
	struct jsoncmd_s   *more;/* For "else" in "if-then-else" for example */
	struct jsoncmd_s   *nextcmd;/* in a series of statements, "nextcmd" is next */
} jsoncmd_t;

/* These are magic values for json_context_file() "current" argument.  They
 * aren't enums because we could also pass an int index to select a file.
 */
#define JSON_CONTEXT_FILE_NEXT		(-1)
#define JSON_CONTEXT_FILE_SAME		(-2)
#define JSON_CONTEXT_FILE_PREVIOUS	(-3)

/* Function declarations */
jsonfunc_t *json_calc_function_first(void);
void json_calc_aggregate_hook(
        const char    *name,
        const char	*args,
        const char	*type,
        json_t *(*fn)(json_t *args, void *agdata),
        void   (*agfn)(json_t *args, void *agdata),
        size_t  agsize,
        int	jfoptions);
void json_calc_function_hook(
	const char	*name,
	const char	*args,
	const char	*type,
        json_t *(*fn)(json_t *args, void *agdata));
int json_calc_function_user(
	char *name,
	json_t *params,
	char *paramstr,
	char *returntype,
	jsoncmd_t *cmd);
jsonfunc_t *json_calc_function_by_name(const char *name);
char *json_calc_op_name(jsonop_t op);
void json_calc_dump(jsoncalc_t *calc);
jsoncalc_t *json_calc_parse(const char *str, const char **refend, const char **referr, int canassign);
jsoncalc_t *json_calc_list(jsoncalc_t *list, jsoncalc_t *item);
void json_calc_free(jsoncalc_t *calc);
void *json_calc_ag(jsoncalc_t *calc, void *agdata);
json_t *json_calc(jsoncalc_t *calc, jsoncontext_t *context, void *agdata);

void json_context_hook(jsoncontext_t *(*addcontext)(jsoncontext_t *context));
jsoncontext_t *json_context_free(jsoncontext_t *context);
jsoncontext_t *json_context(jsoncontext_t *context, json_t *data, jsoncontextflags_t flags);
jsoncontext_t *json_context_insert(jsoncontext_t **refcontext, jsoncontextflags_t flags);
jsoncontext_t *json_context_std(json_t *data);
json_t *json_context_file(jsoncontext_t *context, const char *filename, int writable, int *refcurrent);
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
int json_cmd_lineno(jsonsrc_t *src);
jsoncmdout_t *json_cmd_error(const char *where, const char *fmt, ...);
jsoncmdout_t *json_cmd_src_error(jsonsrc_t *src, int code, char *fmt, ...);
void json_cmd_parse_whitespace(jsonsrc_t *src);
char *json_cmd_parse_key(jsonsrc_t *src, int quotable);
char *json_cmd_parse_paren(jsonsrc_t *src);
jsoncmd_t *json_cmd(jsonsrc_t *src, jsoncmdname_t *name);
void json_cmd_free(jsoncmd_t *cmd);
jsoncmd_t *json_cmd_parse_single(jsonsrc_t *src, jsoncmdout_t **referr);
jsoncmd_t *json_cmd_parse_curly(jsonsrc_t *src, jsoncmdout_t **referr);
jsoncmd_t *json_cmd_parse_string(char *str);
jsoncmd_t *json_cmd_parse_file(const char *filename);
jsoncmdout_t *json_cmd_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
json_t *json_cmd_fncall(json_t *args, jsonfunc_t *fn, jsoncontext_t *context);
jsoncmd_t *json_cmd_append(jsoncmd_t *existing, jsoncmd_t *added, jsoncontext_t *context);


/* The following are for debugging memory leaks.  They're only used if your
 * program defined JSON_DEBUG_MEMORY.
 */
extern int json_debug_count;
extern void json_debug_free(const char *file, int line, json_t *json);
extern json_t *json_debug_simple(const char *file, int line, const char *str, size_t len, jsontype_t type);
extern json_t *json_debug_string(const char *file, int line, const char *str, size_t len);
extern json_t *json_debug_number(const char *file, int line, const char *str, size_t len);
extern json_t *json_debug_boolean(const char *file, int line, int boolean);
extern json_t *json_debug_null(const char *file, int line);
extern json_t *json_debug_error_null(const char *file, int line, char *fmt, ...);
extern json_t *json_debug_from_int(const char *file, int line, int i);
extern json_t *json_debug_from_double(const char *file, int line, double f);
extern json_t *json_debug_key(const char *file, int line, const char *key, json_t *value);
extern json_t *json_debug_object(const char *file, int line);
extern json_t *json_debug_array(const char *file, int line);
extern json_t *json_debug_defer(const char *file, int line, jsondeffns_t *fns);
extern json_t *json_debug_first(const char *file, int line, json_t *array);
extern json_t *json_debug_parse_string(const char *file, int line, const char *str);
extern json_t *json_debug_copy(const char *file, int line, json_t *json);
extern json_t *json_debug_copy_filter(const char *file, int line, json_t *json, int (*filter)(json_t *item));
extern json_t *json_debug_calc(const char *file, int line, jsoncalc_t *calc, jsoncontext_t *context, void *agdata);
#ifdef JSON_DEBUG_MEMORY
#define json_free(json)			json_debug_free(__FILE__, __LINE__, json)
#define json_simple(str, len, type)	json_debug_simple(__FILE__, __LINE__, str, len, type)
#define json_string(str, len)		json_debug_string(__FILE__, __LINE__, str, len)
#define json_number(str, len)		json_debug_number(__FILE__, __LINE__, str, len)
#define json_boolean(boolean)		json_debug_boolean(__FILE__, __LINE__, boolean)
#define json_null()			json_debug_null(__FILE__, __LINE__)
#define json_error(...)			json_debug_error(__FILE__, __LINE__, __VA_ARGS__)
#define json_from_int(i)		json_debug_from_int(__FILE__, __LINE__, i)
#define json_from_double(f)		json_debug_from_double(__FILE__, __LINE__, f)
#define json_key(key, value)		json_debug_key(__FILE__, __LINE__, key, value)
#define json_object()			json_debug_object(__FILE__, __LINE__)
#define json_array()			json_debug_array(__FILE__, __LINE__)
#define json_defer(fns)			json_debug_defer(__FILE__, __LINE__, (fns))
#define json_first(array)		json_debug_first(__FILE__, __LINE__, (array))
#define json_parse_string(str)          json_debug_parse_string(__FILE__, __LINE__, str)
#define json_copy(json)			json_debug_copy(__FILE__, __LINE__, json)
#define json_copy_filter(json, filter)	json_debug_copy_filter(__FILE__, __LINE__, json, filter)
#define json_calc(calc,context,agdata)	json_debug_calc(__FILE__, __LINE__, calc, context, agdata)
#endif
END_C
