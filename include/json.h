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
	JSON_ARRAY, JSON_ENDARRAY,
	JSON_KEY, JSON_STRING, JSON_NUMBER, JSON_NULL, JSON_BOOL
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
 * JSON_BOOL	text contains "true" or "false"
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

/* This represents a file that is open for reading JSON data.  The file is
 * mapped into memory starting at "base", and can be accessed like a giant
 * string.
 */
typedef struct {
	int		fd;	/* File descriptor of the open file */
	int		isfile;	/* Boolean: is this a regular file (not pipe, etc) */
	size_t		size;	/* Size of the file, in bytes */
	const char	*base;	/* Contents of the file, as a giant string */
} jsonfile_t;

#define JSON_PATH_DELIM		':'

BEGIN_C

/* Files */
char json_file_new_type;
jsonfile_t *json_file_load(const char *filename);
void json_file_unload(jsonfile_t *jf);
FILE *json_file_update(const char *filename);
char *json_file_path(const char *prefix, const char *name, const char *suffix, int major, int minor);

/* Error handling */
extern char *json_debug(char *flags);

/* This flag indicates whether computations have been interupted */
extern int json_interupt;

/* Manipulation */
extern void json_free(json_t *json);
extern json_t *json_simple(const char *str, size_t len, jsontype_t type);
extern json_t *json_simple_from_token(json_token_t *token);
extern json_t *json_string(const char *str, size_t len);
extern json_t *json_number(const char *str, size_t len);
extern json_t *json_bool(int boolean);
extern json_t *json_null(void);
extern json_t *json_error_null(int code, char *fmt, ...);
extern json_t *json_from_int(int i);
extern json_t *json_from_double(double f);
extern json_t *json_key(const char *key, json_t *value);
extern json_t *json_object();
extern json_t *json_array();
extern char *json_append(json_t *container, json_t *more);
extern size_t json_sizeof(json_t *json);
extern char *json_typeof(json_t *json, int extended);
extern char *json_mix_types(char *oldtype, char *newtype);
extern void json_sort(json_t *array, json_t *orderby);
extern json_t *json_copy_filter(json_t *json, int (*filter)(json_t *));
extern json_t *json_copy(json_t *json);
extern json_t *json_array_flat(json_t *array, int depth);
extern json_t *json_unroll(json_t *table, json_t *nestlist);
extern json_t *json_array_group_by(json_t *array, json_t *orderby);
extern int json_walk(json_t *json, int (*callback)(json_t *, void *), void *data);

/* Parsing */
extern void json_parse_hook(const char *name, int (*tester)(const char *str, size_t len), json_t *(*parser)(const char *str, size_t len, const char **refend, const char **referr));
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

/* Accessing */
extern json_t *json_by_key(const json_t *container, const char *key);
extern json_t *json_by_deep_key(json_t *container, char *key);
extern json_t *json_by_index(json_t *container, int idx);
extern json_t *json_by_expr(json_t *container, const char *expr, const char **next);
extern json_t *json_find(json_t *haystack, json_t *needle, int ignorecase, char *needkey);
#ifdef REG_ICASE /* skip this if <regex.h> not included */
extern json_t *json_find_regex(json_t *haystack, regex_t *regex, char *needkey);
#endif
extern char *json_default_text(char *newdefault);
extern char *json_text(json_t *json);
extern double json_double(json_t *json);
extern int json_int(json_t *json);
extern int json_length(json_t *container);
extern int json_is_true(json_t *json);
extern int json_is_null(json_t *json);
extern int json_is_table(json_t *json);
extern int json_is_short(json_t *json, size_t oneline);
extern int json_is_date(json_t *json);
extern int json_is_time(json_t *json);
extern int json_is_datetime(json_t *json);
extern int json_is_period(json_t *json);
extern int json_equal(json_t *j1, json_t *j2);
extern int json_compare(json_t *obj1, json_t *obj2, json_t *compare);
#define json_text_by_key(container, key) json_text(json_by_key((container), (key)))
#define json_text_by_deep_key(container, key) json_text(json_by_deep_key((container), (key)))
#define json_text_by_index(container, index) json_text(json_by_index((container), (index)))
/* The next parameter may be NULL.  See json_by_expr() for more details. */
#define json_text_by_expr(container, expr, next) json_text(json_by_expr((container), (expr), (next)))

/* The following are for debugging memory leaks.  They're only used if your
 * program defined JSON_DEBUG_MEMORY.
 */
extern int json_debug_count;
extern void json_debug_free(const char *file, int line, json_t *json);
extern json_t *json_debug_simple(const char *file, int line, const char *str, size_t len, jsontype_t type);
extern json_t *json_debug_string(const char *file, int line, const char *str, size_t len);
extern json_t *json_debug_number(const char *file, int line, const char *str, size_t len);
extern json_t *json_debug_bool(const char *file, int line, int boolean);
extern json_t *json_debug_null(const char *file, int line);
extern json_t *json_debug_error_null(const char *file, int line, char *fmt, ...);
extern json_t *json_debug_from_int(const char *file, int line, int i);
extern json_t *json_debug_from_double(const char *file, int line, double f);
extern json_t *json_debug_key(const char *file, int line, const char *key, json_t *value);
extern json_t *json_debug_object(const char *file, int line);
extern json_t *json_debug_array(const char *file, int line);
extern json_t *json_debug_parse_string(const char *file, int line, const char *str);
extern json_t *json_debug_copy(const char *file, int line, json_t *json);
extern json_t *json_debug_copy_filter(const char *file, int line, json_t *json, int (*filter)(json_t *item));
#ifdef JSON_DEBUG_MEMORY
#define json_free(json)			json_debug_free(__FILE__, __LINE__, json)
#define json_simple(str, len, type)	json_debug_simple(__FILE__, __LINE__, str, len, type)
#define json_string(str, len)		json_debug_string(__FILE__, __LINE__, str, len)
#define json_number(str, len)		json_debug_number(__FILE__, __LINE__, str, len)
#define json_bool(boolean)			json_debug_bool(__FILE__, __LINE__, boolean)
#define json_null()			json_debug_null(__FILE__, __LINE__)
#define json_error(...)			json_debug_error(__FILE__, __LINE__, __VA_ARGS__)
#define json_from_int(i)		json_debug_from_int(__FILE__, __LINE__, i)
#define json_from_double(f)		json_debug_from_double(__FILE__, __LINE__, f)
#define json_key(key, value)		json_debug_key(__FILE__, __LINE__, key, value)
#define json_object()			json_debug_object(__FILE__, __LINE__)
#define json_array()			json_debug_array(__FILE__, __LINE__)
#define json_parse_string(str)          json_debug_parse_string(__FILE__, __LINE__, str)
#define json_copy(json)			json_debug_copy(__FILE__, __LINE__, json)
#define json_copy_filter(json, filter)	json_debug_copy_filter(__FILE__, __LINE__, json, filter)
#endif

/* Multibyte character strings */
size_t json_mbs_len(const char *s);
int json_mbs_width(const char *s);
int json_mbs_height(const char *s);
size_t json_mbs_line(const char *s, int line, char *buf, char **refstart, int *refwidth);
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
json_t *json_plugin_load(const char *name, int major, int minor);


END_C
