#ifndef FALSE
# define FALSE 0
# define TRUE 1
#endif

/* This is a list of token types.  Nearly all of them are operators. */
typedef enum {
	JSONOP_LITERAL,
        JSONOP_STRING, JSONOP_NUMBER, JSONOP_BOOLEAN, JSONOP_NULL,
        JSONOP_STARTPAREN, JSONOP_ENDPAREN, JSONOP_SUBSCRIPT,
        JSONOP_STARTARRAY, JSONOP_ARRAY, JSONOP_ENDARRAY,
        JSONOP_STARTOBJECT, JSONOP_OBJECT, JSONOP_ENDOBJECT,
        JSONOP_NAME, JSONOP_FUNCTION, JSONOP_AG, JSONOP_COMMA,
        JSONOP_DOT, JSONOP_ELIPSIS, JSONOP_QUESTION, JSONOP_COLON,
        JSONOP_EACH, JSONOP_GROUP, JSONOP_NJOIN, JSONOP_LJOIN, JSONOP_RJOIN,
        JSONOP_COALESCE, JSONOP_NEGATE, JSONOP_ISNULL, JSONOP_ISNOTNULL,
        JSONOP_MULTIPLY, JSONOP_DIVIDE, JSONOP_MODULO,
        JSONOP_ADD, JSONOP_SUBTRACT, 
        JSONOP_BITNOT, JSONOP_BITAND, JSONOP_BITOR, JSONOP_BITXOR,
        JSONOP_NOT, JSONOP_AND, JSONOP_OR,
        JSONOP_LT, JSONOP_LE, JSONOP_EQ, JSONOP_NE, JSONOP_GE, JSONOP_GT,
        JSONOP_ICEQ, JSONOP_ICNE, JSONOP_BETWEEN, JSONOP_LIKE, JSONOP_IN,
        JSONOP_EQSTRICT, JSONOP_NESTRICT,
        JSONOP_SELECT, JSONOP_DISTINCT, JSONOP_AS, JSONOP_FROM, JSONOP_WHERE,
        JSONOP_GROUPBY, JSONOP_ORDERBY, JSONOP_DESCENDING,
        /* This must be the last */
        JSONOP_INVALID,
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
} jsonfunc_t;


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
