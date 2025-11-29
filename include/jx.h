/* jx.h */

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
 * parsing and the internal jx_t tree, and some are internally for jx_t only.
 */
typedef enum {
	JX_BADTOKEN, JX_NEWLINE,
	JX_OBJECT, JX_ENDOBJECT,
	JX_ARRAY, JX_ENDARRAY, JX_DEFER,
	JX_KEY, JX_STRING, JX_NUMBER, JX_NULL, JX_BOOLEAN
} jxtype_t;

/* These represent a parsed token */
typedef struct {
	const char *start;
	size_t len;
	jxtype_t type;
} jx_token_t;


/* This represents a JSON value.  The way it is used depends on the type:
 * JX_OBJECT	first points to first member
 * JX_ARRAY	first points to first element
 * JX_KEY	first points to value, text contains name
 * JX_STRING	text contains value
 * JX_NUMBER	text contains value, as a string
 * JX_BOOLEAN	text contains "true" or "false"
 * JX_NULL	text is "" or an error message
 */
typedef struct jx_s {
	struct jx_s *next;	/* next element of an array or object */
	struct jx_s *first;	/* contents of this object, array, or key */
	jxtype_t type : 4;	/* type of this jx_t node */
	unsigned    memslot:12; /* used for JX_DEBUG_MEMORY */
	char        text[14];	/* value of string, number, boolean; name of key */
} jx_t;

/* For jx_t's that are JX_ARRAY or JX_OBJECT, this is a pointer to the
 * last element/member, while parsing a file only.  This saves us from having
 * to scan potentially long arrays every time we want to append one element.
 *
 * The way this works is: It adds 1 to the container, so it points to the
 * memory immediately after the jx_t.  It then casts that pointer to be
 * a pointer to a (jx_t*), and uses [-1] to find the space for a (jxt_t *)
 * at then end of j's memory.
 */
#define JX_END_POINTER(j)	(((jx_t **)((j) + 1))[-1])

/* In arrays, we use the 4 bytes before JX_END_POINTER to store the length */
#define JX_ARRAY_LENGTH(j)	(((__uint32_t *)&JX_END_POINTER(j))[-1])

/* These are used to stuff a binary double or int into a JX_NUMBER*/
#define JX_DOUBLE(j)	(((double *)((j) + 1))[-1])
#define JX_INT(j)	(((int *)((j) + 1))[-1])
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
	FILE	*fp;	/* where to write to */
} jxformat_t;

/* This is a collection of debugging flags.  These are generally set via
 * the jx_debug() function, but various library functions need access to
 * them.
 */
typedef struct {
        int abort;      /* Controls whether some errors cause an abort */
        int expr;       /* Output information about simple expressions */
        int calc;       /* Output information about complex expressions */
        int trace;      /* Trace commands as they're run */
} jx_debug_t;

extern jx_debug_t jx_debug_flags;
extern jxformat_t jx_format_default;

/* This represents a file that is open for reading JSON data or scripts.
 * The file is mapped into memory starting at "base", and can be accessed
 * like a giant string.
 *
 * For data files, the reference count is 0 unless it contains a deferred
 * array, in which case the reference count is 1.  For script files, it the
 * number of functions or vars/consts defined in the script.
 */
typedef struct jxfile_s {
	struct jxfile_s *other;/* Used to for a linked list */
	int		fd;	/* File descriptor of the open file */
	int		isfile;	/* Boolean: is this a regular file (not pipe, etc) */
	int		refs;	/* Reference count */
	size_t		size;	/* Size of the file, in bytes */
	const char	*base;	/* Contents of the file, as a giant string */
	char		*filename; /* Name of the file */
} jxfile_t;

#define JX_PATH_DELIM		':'

/* This stores info about the implementation of different types of deferred
 * arrays. It's basically a collection of function pointers, plus a size_t
 * indicating how much storage space it needs.
 */
typedef struct {
	size_t	size;	/* Size of jxdef_t plus any other needed storage */
	char	*desc;	/* basically the "class" of deferred items */
	jx_t	*(*first)(jx_t *array);	/* REQUIRED */
	jx_t	*(*next)(jx_t *elem);		/* REQUIRED */
	int	(*islast)(const jx_t *elem);	/* REQUIRED */
	void	(*free)(jx_t *array_or_elem);	/* Only if special needs */
	jx_t	*(*byindex)(jx_t *array, int index);
	jx_t	*(*bykeyvalue)(jx_t *array, const char *key, jx_t *value);
} jxdeffns_t;

/* This is the generic part of a JX_DEFER node.  It starts with plain jx_t,
 * and adds some extra fields.  An actual JX_DEFER note will generally have
 * other information, as its own data type.  The functions that jxdeffns_t
 * points to know the actual data type.
 */
typedef struct {
	jx_t	json; /* Standard stuff, with ->type=JX_DEFER */
	jxdeffns_t *fns; /* pointer to a group of function pointers */
	jxfile_t *file; /* if non-NULL, it indicates which file to read */
} jxdef_t;

/* This is a list of token types.  Nearly all of them are operators.
 * IF YOU MAKE ANY CHANGES HERE, THEN YOU MUST ALSO UPDATE THE operators[]
 * ARRAY IN calcparse.c
 */
typedef enum {
        JXOP_ADD,
	JXOP_AG,
	JXOP_AND,
	JXOP_APPEND,
	JXOP_ARRAY,
	JXOP_AS,
	JXOP_ASSIGN,
	JXOP_BETWEEN,
	JXOP_BITAND,
	JXOP_BITNOT,
	JXOP_BITOR,
	JXOP_BITXOR,
	JXOP_BOOLEAN,
	JXOP_COALESCE,
	JXOP_COLON,
	JXOP_COMMA,
	JXOP_DESCENDING,
	JXOP_DISTINCT,
	JXOP_DIVIDE,
	JXOP_DOT,
	JXOP_DOTDOT,
	JXOP_EACH,
	JXOP_ELLIPSIS,
	JXOP_ENDARRAY,
	JXOP_ENDOBJECT,
	JXOP_ENDPAREN,
	JXOP_ENVIRON,
	JXOP_EQ,
	JXOP_EQSTRICT,
	JXOP_FIND,
	JXOP_FNCALL,
	JXOP_FROM,
	JXOP_GE,
	JXOP_GROUP,
	JXOP_GROUPBY,
	JXOP_GT,
	JXOP_HAVING,
	JXOP_ICEQ,
	JXOP_ICNE,
	JXOP_IN,
	JXOP_ISNOTNULL,
	JXOP_ISNULL,
	JXOP_LE,
	JXOP_LIKE,
	JXOP_LIMIT,
	JXOP_LITERAL,
	JXOP_LJOIN,
	JXOP_LT,
	JXOP_MAYBEASSIGN,
	JXOP_MAYBEMEMBER,
	JXOP_MODULO,
	JXOP_MULTIPLY,
	JXOP_NAME,
	JXOP_NE,
	JXOP_NEGATE,
	JXOP_NESTRICT,
	JXOP_NJOIN,
	JXOP_NOT,
	JXOP_NOTIN,
	JXOP_NOTLIKE,
	JXOP_NULL,
	JXOP_NUMBER,
	JXOP_OBJECT,
	JXOP_OR,
	JXOP_ORDERBY,
	JXOP_QUESTION,
	JXOP_REGEX,
	JXOP_RJOIN,
	JXOP_SELECT,
	JXOP_STARTARRAY,
	JXOP_STARTOBJECT,
	JXOP_STARTPAREN,
	JXOP_STRING,
	JXOP_SUBSCRIPT,
	JXOP_SUBTRACT,
	JXOP_VALUES,
	JXOP_WHERE,
	JXOP_INVALID /* <-- This must be the last */
} jxop_t;

/* This is used to represent an expression, or part of an expression */
typedef struct jxcalc_s{
        jxop_t op;
        union {
                struct {
                        struct jxcalc_s *left;        /* left operand */
                        struct jxcalc_s *right;       /* right operand */
                } param;
                struct {
                        struct jxfunc_s *jf;          /* function info */
                        struct jxcalc_s *args;        /* args as array generator */
                        size_t agoffset;                /* If aggregate, this is the offset of its agdata */
                } func;
                struct {
			void	*preg;
			int	global;
                } regex;
                struct jxag_s *ag;
                struct jxselect_s *select;
                jx_t *literal;
                char text[1]; /* extra chars get allocated later */
        } u;
} jxcalc_t;
/* This enum represents details about how a single context layer is used */
typedef enum {
	JX_CONTEXT_NOFREE = 1,/* Don't free the data when context is freed */
	JX_CONTEXT_VAR = 2,	/* contains vars -- use with GLOBAL for non-local */
        JX_CONTEXT_CONST = 4,	/* contains consts -- like variable but can't assign */
        JX_CONTEXT_GLOBAL = 8,/* Context is accessible everywhere */
	JX_CONTEXT_THIS = 16, /* Context can be "this" or "that" */
	JX_CONTEXT_DATA = 32,	/* Context contains "data" variable */
        JX_CONTEXT_ARGS = 64, /* Function arguments and local vars/consts */
        JX_CONTEXT_NOCACHE = 128, /* try autoload() before *data */
        JX_CONTEXT_MODIFIED = 256 /* Data has been modified (set via context->modified() function */
} jxcontextflags_t;

/* This is used to track context (the stack of variable definitions).  */
typedef struct jxcontext_s {
    struct jxcontext_s *older;/* link list of jxcontext_t contexts */
    jx_t *data;     /* a used item */
    jx_t *(*autoload)(char *key); /* called from jx_context_by_key() */
    void   (*modified)(struct jxcontext_s *layer, jxcalc_t *lvalue);
    jxcontextflags_t flags;
} jxcontext_t;

BEGIN_C

/* Files */
char jx_file_new_type;
void jx_file_defer(jxfile_t *jf, jx_t *array);
void jx_file_defer_free(jx_t *array);
jxfile_t *jx_file_load(const char *filename);
void jx_file_unload(jxfile_t *jf);
jxfile_t *jx_file_containing(const char *where, int *refline);
FILE *jx_file_update(const char *filename);
char *jx_file_path(const char *prefix, const char *name, const char *suffix);

/* Error handling */
extern char *jx_debug(char *flags);

/* This flag indicates whether computations have been interrupted */
extern int jx_interrupt;

/* Manipulation */
extern void jx_free(jx_t *json);
extern jx_t *jx_simple(const char *str, size_t len, jxtype_t type);
extern jx_t *jx_simple_from_token(jx_token_t *token);
extern jx_t *jx_string(const char *str, size_t len);
extern jx_t *jx_number(const char *str, size_t len);
extern jx_t *jx_boolean(int boolean);
extern jx_t *jx_null(void);
extern jx_t *jx_error_null(const char *where, const char *fmt, ...);
extern jx_t *jx_from_int(int i);
extern jx_t *jx_from_double(double f);
extern jx_t *jx_key(const char *key, jx_t *value);
extern jx_t *jx_object();
extern jx_t *jx_array();
extern jx_t *jx_defer(jxdeffns_t *fns);
extern jx_t *jx_defer_ellipsis(int from, int to);
extern char *jx_append(jx_t *container, jx_t *more);
extern size_t jx_sizeof(jx_t *json);
extern char *jx_typeof(jx_t *json, int extended);
extern char *jx_mix_types(char *oldtype, char *newtype);
extern void jx_sort(jx_t *array, jx_t *orderby, int grouping);
extern jx_t *jx_copy_filter(jx_t *json, int (*filter)(jx_t *));
extern jx_t *jx_copy(jx_t *json);
extern jx_t *jx_array_flat(jx_t *array, int depth);
extern jx_t *jx_unroll(jx_t *table, jx_t *nestlist);
extern jx_t *jx_array_group_by(jx_t *array, jx_t *orderby);
extern int jx_walk(jx_t *json, int (*callback)(jx_t *, void *), void *data);

/* Binary files */
typedef enum {
	JX_BLOB_ANY = -1,    /* Automatically choose best interpretation */
	JX_BLOB_STRING = -2, /* Automatically choose best text interpretation */
	JX_BLOB_UTF8 = -3,   /* Treat like UTF-8 text.  Fail if malformed */
	JX_BLOB_LATIN1 = -4, /* Treat like Latin1 text, convert to UTF-8 */
	JX_BLOB_BYTES = -5   /* Treat like a deferred array of bytes */
} jxblobconv_t;
extern jxblobconv_t jx_blob_best(const char *data, size_t len, size_t *reflatin1len);
extern jx_t *jx_blob_convert(const char *data, size_t len, jxblobconv_t conversion);
extern size_t jx_blob_unconvert(jx_t *json, char *data, jxblobconv_t conversion);
extern jx_t *jx_blob(jx_t *json, jxblobconv_t convin, jxblobconv_t convout);
extern const char *jx_blob_data(jx_t *json, size_t *reflen);
extern int jx_blob_test(const char *data, size_t len);
extern jx_t *jx_blob_parse(const char *data, size_t len, const char **refend, const char **referr);

/* Parsing */
extern void jx_parse_hook(
	const char *plugin,
	const char *name,
	const char *suffix,
	const char *mimetype,
	int (*tester)(const char *str, size_t len),
	jx_t *(*parser)(const char *str, size_t len, const char **refend, const char **referr),
	int (*updater)(jx_t *data, const char *filename));
extern jx_t *jx_parse_string(const char *str);
extern jx_t *jx_parse_file(const char *filename);

/* Serialization / Output */
extern jx_t *jx_explain(jx_t *stats, jx_t *row, int depth);
extern char *jx_serialize(jx_t *json, jxformat_t *format);
extern void jx_print_table_hook(char *name, void (*fn)(jx_t *json, jxformat_t *format));
extern int jx_print_incomplete_line;
extern void jx_print(jx_t *json, jxformat_t *format);
extern void jx_grid(jx_t *json, jxformat_t *format);
extern void jx_format_set(jxformat_t *format, jx_t *config);
extern void jx_undefer(jx_t *arr);

/* Accessing */
extern jx_t *jx_by_key(const jx_t *object, const char *key);
extern jx_t *jx_by_deep_key(jx_t *container, char *key);
extern jx_t *jx_by_index(jx_t *array, int idx);
extern jx_t *jx_by_key_value(jx_t *array, const char *key, jx_t *value);
extern jx_t *jx_by_expr(jx_t *container, const char *expr, const char **after);
extern jx_t *jx_find(jx_t *haystack, jx_t *needle, int ignorecase, char *needkey);
#ifdef REG_ICASE /* skip this if <regex.h> not included */
extern jx_t *jx_find_regex(jx_t *haystack, regex_t *regex, char *needkey);
#endif
extern jx_t *jx_find_calc(jx_t *haystack, jxcalc_t *calc, jxcontext_t *context);
extern char *jx_default_text(char *newdefault);
extern char *jx_text(jx_t *json);
extern double jx_double(jx_t *json);
extern int jx_int(jx_t *json);
extern jx_t *jx_first(jx_t *arr);
extern jx_t *jx_next(jx_t *elem);
extern void jx_break(jx_t *elem);
extern int jx_is_last(const jx_t *elem);

/* Testing */
extern int jx_length(jx_t *container);
extern int jx_is_true(jx_t *json);
extern int jx_is_null(jx_t *json);
extern int jx_is_error(jx_t *json);
extern int jx_is_table(jx_t *json);
extern int jx_is_short(jx_t *json, size_t oneline);
extern int jx_is_date(jx_t *json);
extern int jx_is_time(jx_t *json);
extern int jx_is_datetime(jx_t *json);
extern int jx_is_period(jx_t *json);
extern int jx_is_deferred_array(const jx_t *arr);
extern int jx_is_deferred_element(const jx_t *elem);
extern int jx_equal(jx_t *j1, jx_t *j2);
extern int jx_compare(jx_t *obj1, jx_t *obj2, jx_t *compare);
#define jx_text_by_key(container, key) jx_text(jx_by_key((container), (key)))
#define jx_text_by_deep_key(container, key) jx_text(jx_by_deep_key((container), (key)))
#define jx_text_by_index(container, index) jx_text(jx_by_index((container), (index)))
/* The next parameter may be NULL.  See jx_by_expr() for more details. */
#define jx_text_by_expr(container, expr, after) jx_text(jx_by_expr((container), (expr), (after)))


/* Multibyte character strings */
size_t jx_mbs_len(const char *s);
int jx_mbs_width(const char *s);
int jx_mbs_height(const char *s);
size_t jx_mbs_line(const char *s, int line, char *buf, char **refstart, int *refwidth);
size_t jx_mbs_wrap_char(char *buf, const char *s, int width);
size_t jx_mbs_wrap_word(char *buf, const char *s, int width);
size_t jx_mbs_simple_key(char *dest, const char *src);
int jx_mbs_cmp(const char *s1, const char *s2);
int jx_mbs_ncmp(const char *s1, const char *s2, size_t len);
const char *jx_mbs_substr(const char *s, size_t start, size_t *reflimit);
const char *jx_mbs_str(const char *haystack, const char *needle, size_t *refccount, size_t *reflen, int last, int ignorecase);
void jx_mbs_tolower(char *s);
void jx_mbs_toupper(char *s);
void jx_mbs_tomixed(char *s, jx_t *exceptions);
int jx_mbs_casecmp(const char *s1, const char *s2);
int jx_mbs_ncasecmp(const char *s1, const char *s2, size_t len);
int jx_mbs_abbrcmp(const char *abbr, const char *full);
const char *jx_mbs_ascii(const char *str, char *buf);
size_t jx_mbs_escape(char *dst, const char *src, size_t len, int quote, jxformat_t *format);
size_t jx_mbs_unescape(char *dst, const char *src, size_t len);
int jx_mbs_like(const char *text, const char *pattern);

/* Dates and times */
int jx_str_date(const char *str);
int jx_str_time(const char *str);
int jx_str_datetime(const char *str);
int jx_str_period(const char *str);
int jx_date(char *result, const char *str);
int jx_time(char *result, const char *str, const char *tz);
int jx_datetime(char *result, const char *str, const char *tz);
int jx_datetime_add(char *result, const char *str, const char *period);
int jx_datetime_subtract(char *result, const char *str, const char *period);
int jx_datetime_diff(char *result, const char *str1, const char *str2);
jx_t *jx_datetime_fn(jx_t *args, char *type);

/* Configuration data */
jx_t *jx_config, *jx_system;
void jx_config_load(const char *name);
void jx_config_save(const char *name);
jx_t *jx_config_style(const char *name, jx_t **refstyles);
jx_t *jx_config_get(const char *section, const char *key);
void jx_config_set(const char *section, const char *key, jx_t *value);
jx_t *jx_config_parse(jx_t *config, const char *settings, const char **refend);
#define jx_config_get_int(section, key) jx_int(jx_config_get(section, key))
#define jx_config_get_double(section, key) jx_double(jx_config_get(section, key))
/* jx_config_get_text() is not threadsafe because it returns a pointer into
 * the jx_config tree.  If the option is changed while an expression is
 * being evaluated, the returned value could become a dangling pointer.
 */
#define jx_config_get_text(section, key) jx_text(jx_config_get(section, key))
#define jx_config_get_boolean(section, key) jx_is_true(jx_config_get(section, key))

/* Plugins */
jx_t *jx_plugins;
jx_t *jx_plugin_load(const char *name);


#ifndef FALSE
# define FALSE 0
# define TRUE 1
#endif



/* Functions are stored in a linked list of these.  If a function is *not* an
 * aggregate function then agfn is NULL and storeagesize is 0.  Also, the
 * name, args, and returntype will generally be (const char *) literals for
 * compiled C functions, but dynamically-allocated strings for user-defined
 * (jxcalc script) functions; the latter prevents us from declaring those
 * fields as "const" here.
 */
typedef struct jxfunc_s {
        struct jxfunc_s *other;
        char    *name;
        char	*args;		/* Argument list, as text */
        char	*returntype;	/* Return value type, as text */
        jx_t *(*fn)(jx_t *args, void *agdata);
        void   (*agfn)(jx_t *args, void *agdata);
        size_t  agsize;
        int	jfoptions;
        struct jxcmd_s *user;
        jx_t	*userparams;
} jxfunc_t;
#define JXFUNC_JXFREE 1		/* Call jx_free() on the agdata afterward */
#define JXFUNC_FREE 2		/* Call free() on the agdata afterward */

/* For non-aggregate functions, this is used to pass other information that
 * they might need.
 */
typedef struct {
	jxcontext_t *context;
	jxcalc_t    *regex; /* The regex_t is at regex->u.regex.preg */
} jxfuncextra_t;


/* This stores a list of aggregate functions used in a given context.  Each
 * jxcalc_t node with ->op==JXOP_AG contains an "ag" pointer that points
 * to one of these, so  ->ag[i]->u.func->jf->agfn(args, context, storage)
 * is the way to call the aggregating function. Yikes.
 *
 * Note that the combined size of the agdata is stored here, but the memory
 * for it is allocated in jx_calc() as needed.  This is done for thread
 * safety, in case two threads call jx_calc() on the same jxcalc_t at
 * the same time.
 */
typedef struct jxag_s {
        jxcalc_t *expr;   /* equation containing aggregate functions */
        int        nags;    /* number of aggregates */
        size_t     agsize;  /* combined storage requirements */
        jxcalc_t *ag[1];  /* function calls with params, expanded as needed */
} jxag_t;


/* This tracks source code for commands.  For strings, "buf" points to the
 * string.  For files, additional memory is allocated for "buf" and must also
 * be freed, but "filename" is a copy of a pointer to a filename string which
 * must not be freed before the application terminates.
 */
typedef struct {
	const char	*buf;	/* buffer, contains entire source text */
	const char	*str;	/* current parse position within "base" */
	size_t	size;		/* size of "buf" */
} jxsrc_t;

/* This is used for returning the result of a command.  A NULL pointer means
 * the command completed without incident, and execution should continue to
 * the next command.  Otherwise, the meaning is determined by the "ret" field
 * as follows:
 *   NULL		An error, indicated by code and text
 *   &jx_cmd_break	A "break" command
 *   &jx_cmd_continue A "continue" command
 *   (anything else)	A "return" command with this value
 */
typedef struct {
	jx_t	*ret;		/* if really a "return" then this is value */
	const char *where;	/* where error detected */
	char	text[1];	/* extended as necessary */
} jxcmdout_t;
extern jx_t jx_cmd_break, jx_cmd_continue;

/* This data type is used for storing command names.  Some command names are
 * built in, but plugins can add new command names too.
 */
typedef struct jxcmdname_s {
	struct jxcmdname_s *other;
	char	*name;
	struct jxcmd_s *(*argparser)(jxsrc_t *src, jxcmdout_t **referr);
	jxcmdout_t *(*run)(struct jxcmd_s *cmd, jxcontext_t **refcontext);
	char	*pluginname;
} jxcmdname_t;

/* This stores a parsed statement. */
typedef struct jxcmd_s {
	const char	   *where;/* pointer into source text, for reporting errors */
	jxcmdname_t	   *name;/* command name and other details */
	char		   var;
	char		   *key; /* Name of a variable, if the cmd uses one */
	jxcalc_t 	   *calc;/* calc expression, if the cmd uses one */
	jxcontextflags_t flags;/* Context flags for "key" */
	struct jxcmd_s   *sub; /* For "then" in "if-then-else" for example */
	struct jxcmd_s   *more;/* For "else" in "if-then-else" for example */
	struct jxcmd_s   *nextcmd;/* in a series of statements, "nextcmd" is next */
} jxcmd_t;

/* These are magic values for jx_context_file() "current" argument.  They
 * aren't enums because we could also pass an int index to select a file.
 */
#define JX_CONTEXT_FILE_NEXT		(-1)
#define JX_CONTEXT_FILE_SAME		(-2)
#define JX_CONTEXT_FILE_PREVIOUS	(-3)

/* Function declarations */
jxfunc_t *jx_calc_function_first(void);
void jx_calc_aggregate_hook(
        const char    *name,
        const char	*args,
        const char	*type,
        jx_t *(*fn)(jx_t *args, void *agdata),
        void   (*agfn)(jx_t *args, void *agdata),
        size_t  agsize,
        int	jfoptions);
void jx_calc_function_hook(
	const char	*name,
	const char	*args,
	const char	*type,
        jx_t *(*fn)(jx_t *args, void *agdata));
int jx_calc_function_user(
	char *name,
	jx_t *params,
	char *paramstr,
	char *returntype,
	jxcmd_t *cmd);
jxfunc_t *jx_calc_function_by_name(const char *name);
char *jx_calc_op_name(jxop_t op);
void jx_calc_dump(jxcalc_t *calc);
jxcalc_t *jx_calc_parse(const char *str, const char **refend, const char **referr, int canassign);
jxcalc_t *jx_calc_list(jxcalc_t *list, jxcalc_t *item);
void jx_calc_free(jxcalc_t *calc);
void *jx_calc_ag(jxcalc_t *calc, void *agdata);
jx_t *jx_calc(jxcalc_t *calc, jxcontext_t *context, void *agdata);

void jx_context_hook(jxcontext_t *(*addcontext)(jxcontext_t *context));
jxcontext_t *jx_context_free(jxcontext_t *context);
jxcontext_t *jx_context(jxcontext_t *context, jx_t *data, jxcontextflags_t flags);
jxcontext_t *jx_context_insert(jxcontext_t **refcontext, jxcontextflags_t flags);
jxcontext_t *jx_context_std(jx_t *data);
jx_t *jx_context_file(jxcontext_t *context, const char *filename, int writable, int *refcurrent);
jxcontext_t *jx_context_func(jxcontext_t *context, jxfunc_t *fn, jx_t *args);
jx_t *jx_context_by_key(jxcontext_t *context, char *key, jxcontext_t **reflayer);
jx_t *jx_context_assign(jxcalc_t *lvalue, jx_t *rvalue, jxcontext_t *context);
jx_t *jx_context_append(jxcalc_t *lvalue, jx_t *rvalue, jxcontext_t *context);
int jx_context_declare(jxcontext_t **refcontext, char *key, jx_t *value, jxcontextflags_t flags);
jx_t *jx_context_default_table(jxcontext_t *context, char **refexpr);

void jx_user_printf(jxformat_t *format, const char *face, const char *fmt, ...);
void jx_user_ch(int ch);
int jx_user_result(jx_t *result);
void jx_user_hook(int (*handler)(jx_t *jface, int newface, const char *text, size_t len));
void jx_user_result_hook(int (*handler)(jx_t *result));

/****************************************************************************/

/* This value is returned by jx_cmd_parse() and jx_cmd_parse_string()
 * if an error is detected.  Note that NULL does *NOT* indicate an error.
 */
extern jxcmd_t JX_CMD_ERROR[];

jxcmdname_t *jx_cmd_hook(char *pluginname, char *cmdname, jxcmd_t *(*argparser)(jxsrc_t *src, jxcmdout_t **referr), jxcmdout_t *(*run)(jxcmd_t *cmd, jxcontext_t **refcontext));
int jx_cmd_lineno(jxsrc_t *src);
jxcmdout_t *jx_cmd_error(const char *where, const char *fmt, ...);
jxcmdout_t *jx_cmd_src_error(jxsrc_t *src, int code, char *fmt, ...);
void jx_cmd_parse_whitespace(jxsrc_t *src);
char *jx_cmd_parse_key(jxsrc_t *src, int quotable);
char *jx_cmd_parse_paren(jxsrc_t *src);
jxcmd_t *jx_cmd(jxsrc_t *src, jxcmdname_t *name);
void jx_cmd_free(jxcmd_t *cmd);
jxcmd_t *jx_cmd_parse_single(jxsrc_t *src, jxcmdout_t **referr);
jxcmd_t *jx_cmd_parse_curly(jxsrc_t *src, jxcmdout_t **referr);
jxcmd_t *jx_cmd_parse_string(char *str);
jxcmd_t *jx_cmd_parse_file(const char *filename);
jxcmdout_t *jx_cmd_run(jxcmd_t *cmd, jxcontext_t **refcontext);
jx_t *jx_cmd_fncall(jx_t *args, jxfunc_t *fn, jxcontext_t *context);
jxcmd_t *jx_cmd_append(jxcmd_t *existing, jxcmd_t *added, jxcontext_t *context);


/* The following are for debugging memory leaks.  They're only used if your
 * program defined JX_DEBUG_MEMORY.
 */
extern int jx_debug_count;
extern void jx_debug_free(const char *file, int line, jx_t *json);
extern jx_t *jx_debug_simple(const char *file, int line, const char *str, size_t len, jxtype_t type);
extern jx_t *jx_debug_string(const char *file, int line, const char *str, size_t len);
extern jx_t *jx_debug_number(const char *file, int line, const char *str, size_t len);
extern jx_t *jx_debug_boolean(const char *file, int line, int boolean);
extern jx_t *jx_debug_null(const char *file, int line);
extern jx_t *jx_debug_error_null(const char *file, int line, char *fmt, ...);
extern jx_t *jx_debug_from_int(const char *file, int line, int i);
extern jx_t *jx_debug_from_double(const char *file, int line, double f);
extern jx_t *jx_debug_key(const char *file, int line, const char *key, jx_t *value);
extern jx_t *jx_debug_object(const char *file, int line);
extern jx_t *jx_debug_array(const char *file, int line);
extern jx_t *jx_debug_defer(const char *file, int line, jxdeffns_t *fns);
extern jx_t *jx_debug_first(const char *file, int line, jx_t *array);
extern jx_t *jx_debug_parse_string(const char *file, int line, const char *str);
extern jx_t *jx_debug_copy(const char *file, int line, jx_t *json);
extern jx_t *jx_debug_copy_filter(const char *file, int line, jx_t *json, int (*filter)(jx_t *item));
extern jx_t *jx_debug_calc(const char *file, int line, jxcalc_t *calc, jxcontext_t *context, void *agdata);
#ifdef JX_DEBUG_MEMORY
#define jx_free(json)			jx_debug_free(__FILE__, __LINE__, json)
#define jx_simple(str, len, type)	jx_debug_simple(__FILE__, __LINE__, str, len, type)
#define jx_string(str, len)		jx_debug_string(__FILE__, __LINE__, str, len)
#define jx_number(str, len)		jx_debug_number(__FILE__, __LINE__, str, len)
#define jx_boolean(boolean)		jx_debug_boolean(__FILE__, __LINE__, boolean)
#define jx_null()			jx_debug_null(__FILE__, __LINE__)
#define jx_error(...)			jx_debug_error(__FILE__, __LINE__, __VA_ARGS__)
#define jx_from_int(i)		jx_debug_from_int(__FILE__, __LINE__, i)
#define jx_from_double(f)		jx_debug_from_double(__FILE__, __LINE__, f)
#define jx_key(key, value)		jx_debug_key(__FILE__, __LINE__, key, value)
#define jx_object()			jx_debug_object(__FILE__, __LINE__)
#define jx_array()			jx_debug_array(__FILE__, __LINE__)
#define jx_defer(fns)			jx_debug_defer(__FILE__, __LINE__, (fns))
#define jx_first(array)		jx_debug_first(__FILE__, __LINE__, (array))
#define jx_parse_string(str)          jx_debug_parse_string(__FILE__, __LINE__, str)
#define jx_copy(json)			jx_debug_copy(__FILE__, __LINE__, json)
#define jx_copy_filter(json, filter)	jx_debug_copy_filter(__FILE__, __LINE__, json, filter)
#define jx_calc(calc,context,agdata)	jx_debug_calc(__FILE__, __LINE__, calc, context, agdata)
#endif
END_C
