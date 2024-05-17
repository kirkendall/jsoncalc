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

/* These are token types */
typedef enum {
	JSON_BADTOKEN, JSON_NEWLINE,
	JSON_OBJECT, JSON_ENDOBJECT,
	JSON_ARRAY, JSON_ENDARRAY,
	JSON_KEY, JSON_STRING, JSON_NUMBER, JSON_SYMBOL
} json_type_t;

/* These represent a parsed token */
typedef struct {
	const char *start;
	size_t len;
	json_type_t type;
} json_token_t;


/* This represents a JSON value.  The way it is used depends on the type:
 * JSON_OBJECT	first points to first member
 * JSON_ARRAY	first points to first element
 * JSON_KEY	first points to value, string contains name
 * JSON_STRING	string contains value
 * JSON_NUMBER	string contains value, as a string
 * JSON_SYMBOL	string contains value, as a string
 */
typedef struct json_s {
	struct json_s *next;	/* next element of an array or object */
	struct json_s *first;	/* contents of this object, array, or key */
	json_type_t type : 4;	/* type of this json_t node */
	unsigned    memslot:12; /* Used for JSON_DEBUG_MEMORY */
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

/* These are used to stuff a binary double or int into a JSON_NUMBER*/
#define JSON_DOUBLE(j)	(((double *)((j) + 1))[-1])
#define JSON_INT(j)	(((int *)((j) + 1))[-1])


/* This is used for parsing. */
typedef struct {
	/* The following implement a parsing stack */
	json_t **stack;	/* stack of parsing items */
	int	nest;	/* current item in the stack */
	int	maxnest;/* size of the stack */

	/* This flag is used to stop parsing, via a call to json_parse_stop()
	 * or by a diverter returning JSON_STOP
	 */
	int	stop;	/* stop parsing */

	/* This is a linked list of diversions */
	struct json_divert_s *divert;

	/* The following are just for debugging purposes */
	const char	*file;	/* name of file being parsed, if any */
	long	lineno;	/* line number */
	long	offset;	/* offset of buf relative to start of JSON data */
	const char	*buf;	/* start of buffer */
	const char	*token;	/* pointer within buf of last token */
	size_t	len;	/* length of token */
} json_parse_t;

BEGIN_C
typedef struct json_divert_s
{
	struct json_divert_s *next;
	json_t *(*handler)(json_parse_t *parse, json_t *element);
	json_t	*found;	/* the found container (NULL if not found yet) */
	void	*data; /* available to diversion handler for other data */
	long	diverted; /* counts items parsed in array */
	long	deleted;  /* counts items where handler returned NULL */
	long	changed;  /* counts items where handler returned other record */
	char	expr[1];/* expression for finding the container */
} json_divert_t;

/* This is used to track "used" items, for the benefit of json_free_unused().
 * The "used" array is maintained by the json_used() and json_unused()
 * functions.  "hash" and "hashsize" are built by json_is_used(), but
 * retained between invocations so it doesn't need to rebuild the hash table
 * every time.
 */
typedef struct jsoncontext_s {
    struct jsoncontext_s *older;/* link list of jsoncontext_t contexts */
    json_t *data;     /* a used item */
    json_t *(*autoload)(char *key); /* called from json_used_by_key() */
} jsoncontext_t;

/* This stores info about formatting -- mostly output formatting, since for
 * input we take whatever we're given.
 */
typedef struct {
	short	tab;	/* indentation to add for pretty-printing nested data */
	short	oneline;/* Force compact output if shorter than this */
	short	digits;	/* precision for floating point output */
	char	table;	/* Table output: csv/shell/grid/json */
	char	string;	/* unquoted string output */
	char	pretty;	/* Pretty-print JSON */
	char	elem;	/* one element per line */
	char	sh;	/* Quote output for shell */
	char	color;	/* Allow the use of ANSI escape sequences */
	char	ascii;	/* Convert non-ASCII characters to \u sequences */
	char	quick;	/* Output tables piecemeal.  Use first row for names */
	char	prefix[20]; /* Prefix to add to keys for shell output */
	char	null[20];/* how to display null in tables */
} jsonformat_t;

/* This is a collection of debugging flags.  These are generally set via
 * the json_debug() function, but various library functions need access to
 * them.
 */
typedef struct {
        int token;      /* Output JSON tokens as they are read */
        int file;       /* Output information about files being scanned */
        int buffer;     /* Output information about the input buffer */
        int abort;      /* Controls whether some errors cause an abort */
        int expr;       /* Output information about simple expressions */
        int calc;       /* Output information about complex expressions */
} json_debug_t;

END_C

extern json_debug_t json_debug_flags;
extern jsonformat_t json_format_default;
extern char json_format_color_result[20];
extern char json_format_color_head[20];
extern char json_format_color_delim[20];
extern char json_format_color_error[20];
extern char json_format_color_debug[20];
extern char json_format_color_end[20];

/* Magic value that diverters can return to stop processing */
#define JSON_STOP ((json_t *)1)

BEGIN_C
/* Error handling */
typedef void (*json_catcher_t)(json_parse_t *parse, char *fmt, ...);
json_catcher_t json_throw;
json_catcher_t json_catch(json_catcher_t f);
extern char *json_debug(char *flags);

/* Manipulation */
extern void json_free(json_t *json);
extern json_t *json_simple(const char *str, size_t len, json_type_t type);
extern json_t *json_simple_from_token(json_token_t *token);
extern json_t *json_string(const char *str, size_t len);
extern json_t *json_number(const char *str, size_t len);
extern json_t *json_symbol(const char *str, size_t len);
extern json_t *json_from_int(int i);
extern json_t *json_from_double(double f);
extern json_t *json_key(const char *key, json_t *value);
extern json_t *json_object();
extern json_t *json_array();
extern size_t json_sizeof(json_t *json);
extern char *json_typeof(json_t *json);
extern void json_sort(json_t *array, json_t *orderby);
extern json_t *json_copy(json_t *json);
extern json_t *json_array_flat(json_t *array, int depth);
extern json_t *json_unroll(json_t *table, json_t *nestlist);
extern json_t *json_array_group_by(json_t *array, json_t *orderby);
extern int json_walk(json_t *json, int (*callback)(json_t *, void *), void *data);
#define json_append(container, more) json_parse_append(NULL, container, more)

/* Parsing */
extern const char *json_token(const char *str, json_token_t *token);
extern void json_parse_begin(json_parse_t *state, int maxnest);
extern json_divert_t *json_parse_divert(json_parse_t *state, char *expr, json_t *(*f)(json_parse_t *parse, json_t *element));
extern void json_parse_newbuf(json_parse_t *state, const char *buf, long offset);
extern void json_parse_token(json_parse_t *state, json_token_t *token);
extern int json_parse_append(json_parse_t *state, json_t *container, json_t *more);
extern int json_parse_complete(json_parse_t *state);
extern json_t *json_parse_end(json_parse_t *state);
extern void json_parse_stop(json_parse_t *state);
extern json_t *json_parse_string(const char *str);
extern json_t *json_parse_file(json_parse_t *state, char *filename);

/* Serialization / Output */
extern json_t *json_explain(json_t *stats, json_t *row, int depth);
extern char *json_serialize(json_t *json, jsonformat_t *format);
extern int json_print(json_t *json, FILE *fp, jsonformat_t *format);
extern int json_grid(json_t *json, FILE *fp, jsonformat_t *format);
extern char *json_format(jsonformat_t *format, char *str);
extern char *json_format_str(jsonformat_t *format);
extern char *json_format_color(char *str);
extern char *json_format_color_str(void);

/* Accessing */
extern json_t *json_by_key(json_t *container, char *key);
extern json_t *json_by_deep_key(json_t *container, char *key);
extern json_t *json_by_index(json_t *container, int idx);
extern json_t *json_by_expr(json_t *container, char *expr, char **next);
extern char *json_default_text(char *newdefault);
extern char *json_text(json_t *json);
extern double json_double(json_t *json);
extern int json_int(json_t *json);
extern int json_length(json_t *container);
extern int json_is_true(json_t *json);
extern int json_is_null(json_t *json);
extern int json_is_table(json_t *json);
extern int json_is_short(json_t *json, size_t oneline);
extern int json_equal(json_t *j1, json_t *j2);
extern int json_compare(json_t *obj1, json_t *obj2, json_t *compare);
#define json_text_by_key(container, key) json_text(json_by_key((container), (key)))
#define json_text_by_deep_key(container, key) json_text(json_by_deep_key((container), (key)))
#define json_text_by_index(container, index) json_text(json_by_index((container), (index)))
/* The next parameter may be NULL.  See json_by_expr() for more details. */
#define json_text_by_expr(container, expr, next) json_text(json_by_expr((container), (expr), (next)))

jsoncontext_t *json_context_free(jsoncontext_t *context, int freedata);
jsoncontext_t *json_context(jsoncontext_t *context, json_t *data, json_t *(*autoload)(char *name));
json_t *json_context_by_key(jsoncontext_t *context, char *key);
json_t *json_context_default_table(jsoncontext_t *context);

/* The following are for debugging memory leaks.  They're only used if your
 * program defined JSON_DEBUG_MEMORY.
 */
extern int json_debug_count;
extern void json_debug_free(const char *file, int line, json_t *json);
extern json_t *json_debug_simple(const char *file, int line, const char *str, size_t len, json_type_t type);
extern json_t *json_debug_string(const char *file, int line, const char *str, size_t len);
extern json_t *json_debug_number(const char *file, int line, const char *str, size_t len);
extern json_t *json_debug_symbol(const char *file, int line, const char *str, size_t len);
extern json_t *json_debug_from_int(const char *file, int line, int i);
extern json_t *json_debug_from_double(const char *file, int line, double f);
extern json_t *json_debug_key(const char *file, int line, const char *key, json_t *value);
extern json_t *json_debug_object(const char *file, int line);
extern json_t *json_debug_array(const char *file, int line);
extern json_t *json_debug_parse_string(const char *file, int line, const char *str);
extern json_t *json_debug_copy(const char *file, int line, json_t *json);
#ifdef JSON_DEBUG_MEMORY
#define json_free(json)			json_debug_free(__FILE__, __LINE__, json)
#define json_simple(str, len, type)	json_debug_simple(__FILE__, __LINE__, str, len, type)
#define json_string(str, len)		json_debug_string(__FILE__, __LINE__, str, len)
#define json_number(str, len)		json_debug_number(__FILE__, __LINE__, str, len)
#define json_symbol(str, len)		json_debug_symbol(__FILE__, __LINE__, str, len)
#define json_from_int(i)		json_debug_from_int(__FILE__, __LINE__, i)
#define json_from_double(f)		json_debug_from_double(__FILE__, __LINE__, f)
#define json_key(key, value)		json_debug_key(__FILE__, __LINE__, key, value)
#define json_object()			json_debug_object(__FILE__, __LINE__)
#define json_array()			json_debug_array(__FILE__, __LINE__)
#define json_parse_string(str)          json_debug_parse_string(__FILE__, __LINE__, str)
#define json_copy(json)			json_debug_copy(__FILE__, __LINE__, json)
#endif

size_t json_mbs_len(const char *s);
int json_mbs_width(const char *s);
int json_mbs_cmp(const char *s1, const char *s2);
int json_mbs_ncmp(const char *s1, const char *s2, size_t len);
const char *json_mbs_substr(const char *s, size_t start, size_t *reflimit);
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
END_C
