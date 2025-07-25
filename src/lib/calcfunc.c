#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <assert.h>
#include <time.h>
#define _XOPEN_SOURCE
#define __USE_XOPEN
#include <wchar.h>
#include <jsoncalc.h>

/* This file mostly implements the built-in functions.  It also defines
 * the json_calc_function() function for adding user-defined functions.
 *
 * The json_calc_parse() function converts argument lists into array generators
 * so the built-in functions are always passed a JSON_ARRAY of the arguments.
 * For function calls of the form expr.func(args), expr is moved to become
 * the first argument, so it looks like func(expr, args).
 *
 * The json_calc() function handles automatically frees the argument list.
 * Your function should allocate new a new json_t tree and return that;
 * it should not attempt to reuse parts of the argument list.  As a special
 * case, if your function is going to return "null" then you can have it
 * return C's NULL pointer instead of a json_t.
 *
 * Aggregate functions are divided into two parts: an aggregator function
 * and a final function.  Memory for storing aggregated results (e.g.,
 * counts and totals) is automatically allocated/freed, and is passed
 * to the functions as "agdata".
 */

/* Several aggregate functions use these to store results */
typedef struct { int count; double val; } agdata_t;
typedef struct { json_t *json; char *sval; int count; double dval; } agmaxdata_t;
typedef struct { char *ag; size_t size;} agjoindata_t;

/* Forward declarations of the built-in non-aggregate functions */
static json_t *jfn_toUpperCase(json_t *args, void *agdata);
static json_t *jfn_toLowerCase(json_t *args, void *agdata);
static json_t *jfn_toMixedCase(json_t *args, void *agdata);
static json_t *jfn_substr(json_t *args, void *agdata);
static json_t *jfn_hex(json_t *args, void *agdata);
static json_t *jfn_toString(json_t *args, void *agdata);
static json_t *jfn_isString(json_t *args, void *agdata);
static json_t *jfn_isArray(json_t *args, void *agdata);
static json_t *jfn_isTable(json_t *args, void *agdata);
static json_t *jfn_isObject(json_t *args, void *agdata);
static json_t *jfn_isNumber(json_t *args, void *agdata);
static json_t *jfn_isInteger(json_t *args, void *agdata);
static json_t *jfn_isNaN(json_t *args, void *agdata);
static json_t *jfn_isDate(json_t *args, void *agdata);
static json_t *jfn_isTime(json_t *args, void *agdata);
static json_t *jfn_isDateTime(json_t *args, void *agdata);
static json_t *jfn_isPeriod(json_t *args, void *agdata);
static json_t *jfn_typeOf(json_t *args, void *agdata);
static json_t *jfn_sizeOf(json_t *args, void *agdata);
static json_t *jfn_widthOf(json_t *args, void *agdata);
static json_t *jfn_heightOf(json_t *args, void *agdata);
static json_t *jfn_keys(json_t *args, void *agdata);
static json_t *jfn_trim(json_t *args, void *agdata);
static json_t *jfn_trimStart(json_t *args, void *agdata);
static json_t *jfn_trimEnd(json_t *args, void *agdata);
static json_t *jfn_concat(json_t *args, void *agdata);
static json_t *jfn_orderBy(json_t *args, void *agdata);
static json_t *jfn_groupBy(json_t *args, void *agdata);
static json_t *jfn_flat(json_t *args, void *agdata);
static json_t *jfn_slice(json_t *args, void *agdata);
static json_t *jfn_repeat(json_t *args, void *agdata);
static json_t *jfn_toFixed(json_t *args, void *agdata);
static json_t *jfn_distinct(json_t *args, void *agdata);
static json_t *jfn_unroll(json_t *args, void *agdata);
static json_t *jfn_nameBits(json_t *args, void *agdata);
static json_t *jfn_keysValues(json_t *args, void *agdata);
static json_t *jfn_charAt(json_t *args, void *agdata);
static json_t *jfn_charCodeAt(json_t *args, void *agdata);
static json_t *jfn_fromCharCode(json_t *args, void *agdata);
static json_t *jfn_replace(json_t *args, void *agdata);
static json_t *jfn_replaceAll(json_t *args, void *agdata);
static json_t *jfn_includes(json_t *args, void *agdata);
static json_t *jfn_indexOf(json_t *args, void *agdata);
static json_t *jfn_lastIndexOf(json_t *args, void *agdata);
static json_t *jfn_startsWith(json_t *args, void *agdata);
static json_t *jfn_endsWith(json_t *args, void *agdata);
static json_t *jfn_split(json_t *args, void *agdata);
static json_t *jfn_getenv(json_t *args, void *agdata);
static json_t *jfn_stringify(json_t *args, void *agdata);
static json_t *jfn_parse(json_t *args, void *agdata);
static json_t *jfn_parseInt(json_t *args, void *agdata);
static json_t *jfn_parseFloat(json_t *args, void *agdata);
static json_t *jfn_find(json_t *args, void *agdata);
static json_t *jfn_date(json_t *args, void *agdata);
static json_t *jfn_time(json_t *args, void *agdata);
static json_t *jfn_dateTime(json_t *args, void *agdata);
static json_t *jfn_timeZone(json_t *args, void *agdata);
static json_t *jfn_period(json_t *args, void *agdata);
static json_t *jfn_abs(json_t *args, void *agdata);
static json_t *jfn_random(json_t *args, void *agdata);
static json_t *jfn_sign(json_t *args, void *agdata);
static json_t *jfn_wrap(json_t *args, void *agdata);
static json_t *jfn_sleep(json_t *args, void *agdata);

/* Forward declarations of the built-in aggregate functions */
static json_t *jfn_count(json_t *args, void *agdata);
static void    jag_count(json_t *args, void *agdata);
static json_t *jfn_rowNumber(json_t *args, void *agdata);
static void    jag_rowNumber(json_t *args, void *agdata);
static json_t *jfn_min(json_t *args, void *agdata);
static void    jag_min(json_t *args, void *agdata);
static json_t *jfn_max(json_t *args, void *agdata);
static void    jag_max(json_t *args, void *agdata);
static json_t *jfn_avg(json_t *args, void *agdata);
static void    jag_avg(json_t *args, void *agdata);
static json_t *jfn_sum(json_t *args, void *agdata);
static void    jag_sum(json_t *args, void *agdata);
static json_t *jfn_product(json_t *args, void *agdata);
static void    jag_product(json_t *args, void *agdata);
static json_t *jfn_any(json_t *args, void *agdata);
static void    jag_any(json_t *args, void *agdata);
static json_t *jfn_all(json_t *args, void *agdata);
static void    jag_all(json_t *args, void *agdata);
static json_t *jfn_explain(json_t *args, void *agdata);
static void    jag_explain(json_t *args, void *agdata);
static json_t *jfn_writeArray(json_t *args, void *agdata);
static void    jag_writeArray(json_t *args, void *agdata);
static json_t *jfn_arrayAgg(json_t *args, void *agdata);
static void    jag_arrayAgg(json_t *args, void *agdata);
static json_t *jfn_objectAgg(json_t *args, void *agdata);
static void    jag_objectAgg(json_t *args, void *agdata);
static json_t *jfn_join(json_t *args, void *agdata);
static void    jag_join(json_t *args, void *agdata);

/* A linked list of the built-in functions */
static jsonfunc_t toUpperCase_jf = {NULL,            "toUpperCase", "str:string", "string",	jfn_toUpperCase};
static jsonfunc_t toLowerCase_jf = {&toUpperCase_jf, "toLowerCase", "str:string", "string",	jfn_toLowerCase};
static jsonfunc_t toMixedCase_jf = {&toLowerCase_jf, "toMixedCase", "str:string, exceptions?:string[]",	"string",	jfn_toMixedCase};
static jsonfunc_t substr_jf      = {&toMixedCase_jf, "substr",      "str:string, start:number, length?:number",	"string", jfn_substr};
static jsonfunc_t hex_jf         = {&substr_jf,      "hex",         "val:string|number, length?:number", "string",	jfn_hex};
static jsonfunc_t toString_jf    = {&hex_jf,         "toString",    "val:any", "string",		jfn_toString};
static jsonfunc_t String_jf      = {&toString_jf,    "String",      "val:any", "string",		jfn_toString};
static jsonfunc_t isString_jf    = {&String_jf,      "isString",    "val:any", "boolean",		jfn_isString};
static jsonfunc_t isArray_jf     = {&isString_jf,    "isArray",     "val:any", "boolean",		jfn_isArray};
static jsonfunc_t isTable_jf     = {&isArray_jf,     "isTable",     "val:any", "boolean",		jfn_isTable};
static jsonfunc_t isObject_jf    = {&isTable_jf,     "isObject",    "val:any", "boolean",		jfn_isObject};
static jsonfunc_t isNumber_jf    = {&isObject_jf,    "isNumber",    "val:any", "boolean",		jfn_isNumber};
static jsonfunc_t isInteger_jf   = {&isNumber_jf,    "isInteger",   "val:any", "boolean",		jfn_isInteger};
static jsonfunc_t isNaN_jf       = {&isInteger_jf,   "isNaN",       "val:any", "boolean",		jfn_isNaN};
static jsonfunc_t isDate_jf      = {&isNaN_jf,       "isDate",      "val:any", "boolean",		jfn_isDate};
static jsonfunc_t isTime_jf      = {&isDate_jf,      "isTime",      "val:any", "boolean",		jfn_isTime};
static jsonfunc_t isDateTime_jf  = {&isTime_jf,      "isDateTime",  "val:any", "boolean",		jfn_isDateTime};
static jsonfunc_t isPeriod_jf    = {&isDateTime_jf,  "isPeriod",    "val:any", "boolean",		jfn_isPeriod};
static jsonfunc_t typeOf_jf      = {&isPeriod_jf,    "typeOf",      "val:any, prevtype:string|true", "string",	jfn_typeOf};
static jsonfunc_t sizeOf_jf      = {&typeOf_jf,      "sizeOf",      "val:any", "number",		jfn_sizeOf};
static jsonfunc_t widthOf_jf     = {&sizeOf_jf,      "widthOf",     "str:string", "number",		jfn_widthOf};
static jsonfunc_t heightOf_jf    = {&widthOf_jf,     "heightOf",    "str:string", "number",		jfn_heightOf};
static jsonfunc_t keys_jf        = {&heightOf_jf,    "keys",        "obj:object", "string[]",		jfn_keys};
static jsonfunc_t trim_jf        = {&keys_jf,        "trim",        "str:string", "string",		jfn_trim};
static jsonfunc_t trimStart_jf   = {&trim_jf,        "trimStart",   "str:string", "string",		jfn_trimStart};
static jsonfunc_t trimEnd_jf     = {&trimStart_jf,   "trimEnd",     "str:string", "string",		jfn_trimEnd};
static jsonfunc_t concat_jf      = {&trimEnd_jf,     "concat",      "item:array|string, ...more", "array|string",	jfn_concat};
static jsonfunc_t orderBy_jf     = {&concat_jf,      "orderBy",     "tbl:table, columns:string|string[]", "table",	jfn_orderBy};
static jsonfunc_t groupBy_jf     = {&orderBy_jf,     "groupBy",     "tbl:table, columns:string|string[]", "array",	jfn_groupBy};
static jsonfunc_t flat_jf        = {&groupBy_jf,     "flat",        "arr:array, depth?:number",	"array",	jfn_flat};
static jsonfunc_t slice_jf       = {&flat_jf,        "slice",       "val:array|string, start:number, end:number", "array|string", jfn_slice};
static jsonfunc_t repeat_jf      = {&slice_jf,       "repeat",      "str:string, count:number", "string",	jfn_repeat};
static jsonfunc_t toFixed_jf     = {&repeat_jf,      "toFixed",     "num:number, precision:number", "string",	jfn_toFixed};
static jsonfunc_t distinct_jf    = {&toFixed_jf,     "distinct",    "arr:array, rule:true|string[]", "array",	jfn_distinct};
static jsonfunc_t unroll_jf      = {&distinct_jf,    "unroll",      "tbl:table, nestlist:string|string[]", "table",	jfn_unroll};
static jsonfunc_t nameBits_jf    = {&unroll_jf,      "nameBits",    "num:number, names:array, delim?:string", "object|string", jfn_nameBits};
static jsonfunc_t keysValues_jf  = {&nameBits_jf,    "keysValues",  "val:object|table", "table",		jfn_keysValues};
static jsonfunc_t charAt_jf      = {&keysValues_jf,  "charAt",      "str:string, pos?:number", "string",	jfn_charAt};
static jsonfunc_t charCodeAt_jf  = {&charAt_jf,      "charCodeAt",  "str:string, pos?:number|number[]", "number|number[]",	jfn_charCodeAt};
static jsonfunc_t fromCharCode_jf= {&charCodeAt_jf,  "fromCharCode","what:number|string|array, ...more", "string",	jfn_fromCharCode};
static jsonfunc_t replace_jf     = {&fromCharCode_jf,"replace",     "str:string, find:string|regex, replace:string", "string",	jfn_replace};
static jsonfunc_t replaceAll_jf  = {&replace_jf,     "replaceAll",  "str:string, find:string|regex, replace:string", "string",	jfn_replaceAll};
static jsonfunc_t includes_jf    = {&replaceAll_jf,  "includes",    "subj:string|array, find:string|regex, ignorecase?:true", "boolean",	jfn_includes};
static jsonfunc_t indexOf_jf     = {&includes_jf,    "indexOf",     "subj:string|array, find:string|regex, ignorecase?:true", "number",	jfn_indexOf};
static jsonfunc_t lastIndexOf_jf = {&indexOf_jf,     "lastIndexOf", "subj:string|array, find:string|regex, ignorecase?:true", "number",	jfn_lastIndexOf};
static jsonfunc_t startsWith_jf  = {&lastIndexOf_jf, "startsWith",  "subj:string, srch:string, ignorecase?:true", "boolean",	jfn_startsWith};
static jsonfunc_t endsWith_jf    = {&startsWith_jf,  "endsWith",    "subj:string, srch:string, ignorecase?:true", "boolean",	jfn_endsWith};
static jsonfunc_t split_jf       = {&endsWith_jf,    "split",       "str:string, delim:string|regex, limit?:number", "string[]",	jfn_split};
static jsonfunc_t getenv_jf      = {&split_jf,       "getenv",      "str:string", "string:null",		jfn_getenv};
static jsonfunc_t stringify_jf   = {&getenv_jf,      "stringify",   "data:any", "string",		jfn_stringify};
static jsonfunc_t parse_jf       = {&stringify_jf,   "parse",       "str:string", "any",		jfn_parse};
static jsonfunc_t parseInt_jf    = {&parse_jf,       "parseInt",    "str:string", "number",		jfn_parseInt};
static jsonfunc_t parseFloat_jf  = {&parseInt_jf,    "parseFloat",  "str:string", "number",		jfn_parseFloat};
static jsonfunc_t find_jf	 = {&parseFloat_jf,  "find", 	    "haystack?:array|object, needle:string|regex|number, key?:string, ignorecase?:true", "table",	jfn_find};
static jsonfunc_t date_jf        = {&find_jf,	     "date",        "when:string|object|number, ...actions", "string|object|number",	jfn_date};
static jsonfunc_t time_jf        = {&date_jf,        "time",        "when:string|object|number, ...actions", "string|object|number",	jfn_time};
static jsonfunc_t dateTime_jf    = {&time_jf,        "dateTime",    "when:string|object|number, ...actions", "string|object|number",	jfn_dateTime};
static jsonfunc_t timeZone_jf    = {&dateTime_jf,    "timeZone",    "when:string|object|number, ...actions", "null",	jfn_timeZone};
static jsonfunc_t period_jf      = {&timeZone_jf,    "period",      "when:string|object|number, ...actions", "string|object|number",	jfn_period};
static jsonfunc_t abs_jf         = {&period_jf,      "abs",         "val:number", "number", jfn_abs};
static jsonfunc_t random_jf      = {&abs_jf,         "random",      "val:number", "number", jfn_random};
static jsonfunc_t sign_jf        = {&random_jf,      "sign",        "val:number", "number", jfn_sign};
static jsonfunc_t wrap_jf        = {&sign_jf,        "wrap",        "text:string, width?:number", "number", jfn_wrap};
static jsonfunc_t sleep_jf        = {&wrap_jf,       "sleep",       "seconds:number|period", "number", jfn_sleep};

static jsonfunc_t count_jf       = {&sleep_jf,       "count",       "val:any|*", "number",	jfn_count, jag_count, sizeof(long)};
static jsonfunc_t rowNumber_jf   = {&count_jf,       "rowNumber",   "format:string", "number|string",		jfn_rowNumber, jag_rowNumber, sizeof(int)};
static jsonfunc_t min_jf         = {&rowNumber_jf,   "min",         "val:number|string, marker?:any", "number|string|any",	jfn_min,   jag_min, sizeof(agmaxdata_t), JSONFUNC_JSONFREE | JSONFUNC_FREE};
static jsonfunc_t max_jf         = {&min_jf,         "max",         "val:number|string, marker?:mixed", "number|string|any",	jfn_max,   jag_max, sizeof(agmaxdata_t), JSONFUNC_JSONFREE | JSONFUNC_FREE};
static jsonfunc_t avg_jf         = {&max_jf,         "avg",         "num:number", "number",		jfn_avg,   jag_avg, sizeof(agdata_t)};
static jsonfunc_t sum_jf         = {&avg_jf,         "sum",         "num:number", "number",		jfn_sum,   jag_sum, sizeof(agdata_t)};
static jsonfunc_t product_jf     = {&sum_jf,         "product",     "num:number", "number",		jfn_product,jag_product, sizeof(agdata_t)};
static jsonfunc_t any_jf         = {&product_jf,     "any",         "bool:boolean", "boolean",		jfn_any,   jag_any, sizeof(int)};
static jsonfunc_t all_jf         = {&any_jf,         "all",         "bool:boolean", "boolean",		jfn_all,   jag_all, sizeof(int)};
static jsonfunc_t explain_jf     = {&all_jf,         "explain",     "tbl:table, depth:?number", "table",		jfn_explain,jag_explain, sizeof(json_t *), JSONFUNC_JSONFREE};
static jsonfunc_t writeArray_jf  = {&explain_jf,     "writeArray",  "data:any, filename:?string", "null",	jfn_writeArray,jag_writeArray, sizeof(FILE *)};
static jsonfunc_t arrayAgg_jf    = {&writeArray_jf,  "arrayAgg",    "data:any", "array",		jfn_arrayAgg,jag_arrayAgg, sizeof(json_t *), JSONFUNC_JSONFREE};
static jsonfunc_t objectAgg_jf   = {&arrayAgg_jf,    "objectAgg",   "key:string, value:any", "object",	jfn_objectAgg,jag_objectAgg, sizeof(json_t *), JSONFUNC_JSONFREE};
static jsonfunc_t join_jf        = {&objectAgg_jf,   "join",        "str:string, delim?:string", "string",	jfn_join,  jag_join, sizeof(agjoindata_t),	JSONFUNC_FREE};
static jsonfunc_t *funclist      = &join_jf;


/* Register a C function that can be called via json_calc().  The function
 * should look like...
 *
 *    json_t *myFunction(json_t *args, void *agdata).
 *
 * ... where "args" is a JSON_ARRAY of the actual parameter values; if invoked
 * as a member function then "this" is the first parameter.  Your function
 * should return newly allocated json_t data, or NULL to represent the JSON
 * "null" symbol.  In particular, it should *NOT* attempt to reuse the
 * argument data in the response.  The json_copy() function is your friend!
 * Upon return, json_calc() will free the parameters immediately and the
 * returned value eventually.
 *
 * The agfn and agsize parameters are only for aggregate functions.  They
 * should be NULL and 0 for non-aggregate functions.  For aggregate functions,
 * agfn is a function that will be called for each item (row or array element)
 * and agsize is the amount of storage space it needs to accumulate results
 * typically sizeof(int) or something like that.  The agdata starts out all
 * zeroes.  The idea is that agfn() will accumulate data, and fn() will return
 * the final result.
 */
void json_calc_aggregate_hook(
	char    *name,
	char	*args,
	char	*type,
	json_t *(*fn)(json_t *args, void *agdata),
	void   (*agfn)(json_t *args, void *agdata),
	size_t  agsize,
	int	jfoptions)
{
	jsonfunc_t *f;

	/* Round agsize up to a multiple of 8 bytes */
	if (agsize > 0)
		agsize = ((agsize - 1) | 0x7) + 1;

	/* If it's already in the table then update it */
	for (f = funclist; f; f = f->next) {
		if (!strcmp(f->name, name)) {
			f->args = args;
			f->fn = fn;
			f->agfn = agfn;
			f->agsize = agsize;
			return;
		}
	}

	/* Add it */
	f = (jsonfunc_t *)malloc(sizeof(jsonfunc_t));
	memset(f, 0, sizeof *f);
	f->name = name;
	f->args = args;
	f->returntype = type;
	f->fn = fn;
	f->agfn = agfn;
	f->agsize = agsize;
	f->jfoptions = jfoptions;
	f->next = funclist;
	funclist = f;
}

/* Register a non-aggregate function.  "name" is the name of the function,
 * and "fn" is a pointer to the actual C function that implements it.
 * The "args" and "type" strings are the argument names and types, and the
 * return type; these are basically just comments.
 */
void json_calc_function_hook(
	char    *name,
	char	*args,
	char	*type,
	json_t *(*fn)(json_t *args, void *agdata))
{
	/* This is just a simplified interface to the aggregate adder */
	json_calc_aggregate_hook(name, args, type, fn, NULL, 0, 0);
}

/* This function is called automatically when the program terminates.  It
 * frees user-defined functions, so that their resources won't be listed as
 * a memory leak.
 */
static void free_user_functions()
{
	jsonfunc_t	*scan, *lag, *next;

	/* For each function in the list... */
	for (scan = funclist, lag = NULL; scan; scan = next) {
		/* Skip if not user-defined */
		next = scan->next;
		if (!scan->user) {
			lag = scan;
			continue;
		}

		/* Remove it from the list */
		if (lag)
			lag->next = next;
		else
			funclist = next;

		/* Free its resources */
		free(scan->name);
		json_cmd_free(scan->user);
		json_free(scan->userparams);
		if (scan->args)
			free(scan->args);
		if (scan->returntype)
			free(scan->returntype);
		free(scan);
	}
}

/* Define or redefine a user function -- one that's defined in JsonCalc's
 * command syntax instead of C code.  Returns 0 normally, or 1 if the function
 * name matches a built-in function (and hence can't be refined).
 */
int json_calc_function_user(char *name, json_t *params, char *paramstr, char *returntype, jsoncmd_t *body)
{
	jsonfunc_t *fn;
	static int first = 1;

	/* If first, then arrange for all user-defined functions to be freed
	 * when the program terminates.
	 */
	if (first) {
		first = 0;
		atexit(free_user_functions);
	}

	/* Look for an existing function to redefine */
	fn = json_calc_function_by_name(name);
	if (!fn) {
		/* Allocate a new jsonfunc_t and link it into the table */
		fn = (jsonfunc_t *)malloc(sizeof(jsonfunc_t));
		memset(fn, 0, sizeof(jsonfunc_t));
		fn->next = funclist;
		funclist = fn;
	} else if (fn->fn) {
		/* Can't redefine built-ins */
		return 1;
	} else {
		/* Redefining, so discard the old details */
		free(fn->name);
		json_free(fn->userparams);
		if (fn->args) {
			free(fn->args);
			fn->args = NULL;
		}
		if (fn->returntype) {
			free(fn->returntype);
			fn->returntype = NULL;
		}
		json_cmd_free(fn->user);
	}

	/* Store the new info in the jsonfunc_t */
	fn->name = name;
	fn->userparams = params;
	fn->args = paramstr;
	fn->returntype = returntype;
	fn->user = body;

	/* Success! */
	return 0;
}

/* Look up a function by name, and return its info */
jsonfunc_t *json_calc_function_by_name(char *name)
{
	jsonfunc_t *scan;

	/* Try case-sensitive */
	for (scan = funclist; scan; scan = scan->next) {
		if (!strcmp(name, scan->name))
			return scan;
	}

	/* Try case-insensitive */
	for (scan = funclist; scan; scan = scan->next) {
		if (!json_mbs_casecmp(name, scan->name))
			return scan;
	}

	/* Try abbreviation, if the name is more than 1 letter long */
	if (json_mbs_len(name) > 1) {
		for (scan = funclist; scan; scan = scan->next) {
			if (!json_mbs_abbrcmp(name, scan->name))
				return scan;
		}
	}

	return NULL;
}

/***************************************************************************
 * Everything below this is C functions that implement JsonCalc functions. *
 * We'll start with the non-aggregate functions.  These are jfn_xxxx() C   *
 * functions.  They're passed an agdata parameter but they ignore it.      *
 * The aggregate functions are defined later in this file.                 *
 ***************************************************************************/

/* toUpperCase(str) returns an uppercase version of str */
static json_t *jfn_toUpperCase(json_t *args, void *agdata)
{
	json_t  *tmp;

	/* If string, make a copy.  If not a string then use toString on it. */
	if (args->first->type == JSON_STRING)
		tmp = json_string(args->first->text, -1);
	else
		tmp = jfn_toString(args, agdata);

	/* Convert to uppercase */
	json_mbs_toupper(tmp->text);

	/* Return it */
	return tmp;
}

/* toLowerCase(str) returns a lowercase version of str */
static json_t *jfn_toLowerCase(json_t *args, void *agdata)
{
	json_t  *tmp;

	/* If string, make a copy.  If not a string then use toString on it. */
	if (args->first->type == JSON_STRING)
		tmp = json_string(args->first->text, -1);
	else
		tmp = jfn_toString(args, agdata);

	/* Convert to uppercase */
	json_mbs_tolower(tmp->text);

	/* Return it */
	return tmp;
}

/* to MixedCase(str, exceptions */
static json_t *jfn_toMixedCase(json_t *args, void *agdata)
{
	json_t	*tmp;

	/* If string, make a copy.  If not a string then use toString on it. */
	if (args->first->type == JSON_STRING)
		tmp = json_string(args->first->text, -1);
	else
		tmp = jfn_toString(args, agdata);

	/* Convert to mixedcase */
	json_mbs_tomixed(tmp->text, args->first->next);

	/* Return it */
	return tmp;

}


/* substr(str, start, len) returns a substring */
static json_t *jfn_substr(json_t *args, void *agdata)
{
	const char    *str;
	size_t  len, start, limit;

	/* If not a string or no other parameters, just return null */
	if (args->first->type != JSON_STRING || !args->first->next)
		return json_error_null(1, "substr() requires a string");
	str = args->first->text;

	/* Get the length of the string.  We'll need that to adjust bounds */
	len = json_mbs_len(str);

	/* Get the starting position */
	if (args->first->next->type != JSON_NUMBER)
		return json_error_null(1, "substr() position must be a number");
	start = json_int(args->first->next);
	if (start < 0 && start + len >= 0)
		start = len + start;
	else if (start < 0 || start > len)
		start = len;

	/* Get the length limit */
	if (!args->first->next->next)
		limit = len - start; /* all the way to the end */
	else if (args->first->next->next->type != JSON_NUMBER)
		return json_error_null(1, "substr() length must be a number");
	else {
		limit = json_int(args->first->next->next);
		if (start + limit > len)
			limit = len - start;
	}

	/* Find the substring.  This isn't trivial with multibyte chars */
	str = json_mbs_substr(str, start, &limit);

	/* Copy the substring into a new json_t */
	return json_string(str, limit);
}

/* hex(arg) converts strings into a series of hex digits, or numbers into hex
 * optionally padded with leading 0's.
 */
static json_t *jfn_hex(json_t *args, void *agdata)
{
	json_t  *result;
	char    *str;
	int     len;
	size_t	size;
	long    n;

	if (args->first->type == JSON_STRING) {
		/* Allocate a big enough string */
		str = args->first->text;
		result = json_string("", strlen(str) * 2);

		/* Convert each byte of the string to hex */
		for (len = 0; str[len]; len++)
			snprintf(&result->text[2 * len], 3, "%02x", str[len] & 0xff);

		/* Return that */
		return result;
	} else if (args->first->type == JSON_NUMBER) {
		/* Get the args, including length */
		n = json_int(args->first);
		len = 0;
		if (args->first->next && args->first->next->type == JSON_NUMBER)
			len = json_int(args->first->next);
		if (len < 0)
			len = 0;

		/* Allocate the return buffer -- probably bigger than we need,
		 * but not by much.*/
		size = len ? len : n < 0xffffffffff ? 12 : 2 + sizeof(n) * 2;
		result = json_string("", size);

		/* Fill it */
		if (len == 0)
			snprintf(result->text, size + 1, "0x%lx", n);
		else
			snprintf(result->text, size + 1, "%0*lx", len, n);

		return result;
	}
	return json_error_null(1, "hex() only works on numbers or strings");
}

/* toString(arg) converts arg to a string */
static json_t *jfn_toString(json_t *args, void *agdata)
{
	char    *tmpstr;
	json_t  *tmp;

	/* If already a string, return a copy of it as-is */
	if (args->first->type == JSON_STRING)
		return json_copy(args->first);

	/* If boolean or non-binary number, convert its text to a string. */
	if (args->first->type == JSON_BOOL
	 || (args->first->type == JSON_NUMBER && args->first->text[0] != '\0'))
		return json_string(args->first->text, -1);

	/* If null, return "null" */
	if (args->first->type == JSON_NULL)
		return json_string("null", 4);

	/* For anything else, use json_serialize() */
	tmpstr = json_serialize(args->first, 0);
	tmp = json_string(tmpstr, -1);
	free(tmpstr);
	return tmp;
}

static json_t *jfn_isString(json_t *args, void *agdata)
{
	return json_bool(args->first->type == JSON_STRING);
}

static json_t *jfn_isObject(json_t *args, void *agdata)
{
	return json_bool(args->first->type == JSON_OBJECT);
}

static json_t *jfn_isArray(json_t *args, void *agdata)
{
	return json_bool(args->first->type == JSON_ARRAY);
}

static json_t *jfn_isTable(json_t *args, void *agdata)
{
	return json_bool(json_is_table(args->first));
}

static json_t *jfn_isNumber(json_t *args, void *agdata)
{
	return json_bool(args->first->type == JSON_NUMBER);
}

static json_t *jfn_isInteger(json_t *args, void *agdata)
{
	double d;

	if (args->first->type != JSON_NUMBER)
		return json_bool(0);
	if (args->first->text[0] == '\0' && args->first->text[1] == 'i')
		return json_bool(1);
	d = json_double(args->first);
	return json_bool(d == (int)d);
}

static json_t *jfn_isNaN(json_t *args, void *agdata)
{
	return json_bool(args->first->type != JSON_NUMBER);
}

static json_t *jfn_isDate(json_t *args, void *agdata)
{
	return json_bool(json_is_date(args->first));
}

static json_t *jfn_isTime(json_t *args, void *agdata)
{
	return json_bool(json_is_time(args->first));
}

static json_t *jfn_isDateTime(json_t *args, void *agdata)
{
	return json_bool(json_is_datetime(args->first));
}

static json_t *jfn_isPeriod(json_t *args, void *agdata)
{
	return json_bool(json_is_period(args->first));
}

/* typeOf(data) returns a string identifying the data's type */
static json_t *jfn_typeOf(json_t *args, void *agdata)
{
	char	*type, *mixed;
	if (!args->first->next || (args->first->next->type == JSON_BOOL && *args->first->next->text == 'f'))
		type = json_typeof(args->first, 0);
	else {
		type = json_typeof(args->first, 1);
		if (args->first->next->type == JSON_STRING) {
			mixed = json_mix_types(args->first->next->text, type);
			if (mixed)
				type = mixed;
		}
	}
	return json_string(type, -1);
}

/* Estimate the memory usage of a json_t datum */
static json_t *jfn_sizeOf(json_t *args, void *agdata)
{
	return json_from_int(json_sizeof(args->first));
}

/* Estimate the width of a string.  Some characters may be wider than others,
 * even in a fixed-pitch font.
 */
static json_t *jfn_widthOf(json_t *args, void *agdata)
{
	char *numstr;
	int  width;

	switch (args->first->type) {
	case JSON_NUMBER:
		/* Is the number in binary format? */
		if (args->text[0] == 0) {
			/* Convert from binary to string, and check that */
			numstr = json_serialize(args, NULL);
			width = strlen(numstr); /* number width is easy */
			free(numstr);
			return json_from_int(width);
		}
		/* else number is in text form so fall through... */
	case JSON_STRING:
	case JSON_BOOL:
		return json_from_int(json_mbs_width(json_text(args->first)));
	default:
		return NULL;
	}
}

/* Return the height of a string.  This is 1 plus the number of newlines,
 * except that if the string ends with a newline then that one doesn't count.
 */
static json_t *jfn_heightOf(json_t *args, void *agdata)
{
	switch (args->first->type) {
	case JSON_NULL:
	case JSON_BOOL:
	case JSON_NUMBER:
	case JSON_OBJECT:
	case JSON_ARRAY:
		return json_from_int(1);

	case JSON_STRING:
		return json_from_int(json_mbs_height(args->first->text));

	case JSON_BADTOKEN:
	case JSON_NEWLINE:
	case JSON_ENDOBJECT:
	case JSON_ENDARRAY:
	case JSON_KEY:
		/* can't happen */
		abort();
	}
	return json_null(); /* to keep the compiler happy */
}

/* keys(obj) returns an array of key names, as strings */
static json_t *jfn_keys(json_t *args, void *agdata)
{
	json_t *result = json_array();
	json_t *scan;

	/* This only really works on objects */
	if (args->first->type == JSON_OBJECT) {
		/* For each member... */
		for (scan = args->first->first; scan; scan = scan->next) {
			assert(scan->type == JSON_KEY);
			/* Append its name to the result as a string */
			json_append(result, json_string(scan->text, -1));
		}
	}
	return result;
}

static json_t *help_trim(json_t *args, int start, int end, char *name)
{
	char	*substr;
	size_t	len;

	/* This only works on non-strings */
	if (args->first->type != JSON_STRING)
		return json_error_null(1, "The %s function only works on strings", name);

	/* Get the string to trim */
	substr = args->first->text;

	/* Trim leading spaces */
	if (start)
		while (*substr == ' ')
			substr++;

	/* Trim trailing spaces */
	len = strlen(substr);
	if (end)
		while (len > 0 && substr[len - 1] == ' ')
			len--;

	/* Return the trimmed substring */
	return json_string(substr, len);
}

/* trim(obj) returns a string with leading and trailing spaces removed */
static json_t *jfn_trim(json_t *args, void *agdata)
{
	return help_trim(args, 1, 1, "trim");
}

/* trimStart(obj) returns a string with leading spaces removed */
static json_t *jfn_trimStart(json_t *args, void *agdata)
{
	return help_trim(args, 1, 0, "trimStart");
}

/* trimEnd(obj) returns a string with trailing spaces removed */
static json_t *jfn_trimEnd(json_t *args, void *agdata)
{
	return help_trim(args, 0, 1, "trimEnd");
}

/* Combine multiple arrays to form one long array, or multiple strings to
 * form one long string.
 */
static json_t *jfn_concat(json_t *args, void *agdata)
{
	json_t  *scan, *elem;
	json_t  *result;
	size_t	len;
	char	*build;

	/* Are we doing arrays or strings? */
	if (args->first->type == JSON_ARRAY) {
		/* Arrays -- make sure everything is an array */
		for (scan = args->first->next; scan; scan = scan->next) {
			if (scan->type != JSON_ARRAY && scan->type != JSON_NULL)
				goto BadMix;
		}

		/* Start with an empty array */
		result = json_array();

		/* Append the elements of all arrays */
		for (scan = args->first; scan; scan = scan->next) {
			if (scan->type == JSON_NULL)
				continue;
			for (elem = scan->first; elem; elem = elem->next)
				json_append(result, json_copy(elem));
		}
	} else {
		/* Strings -- Can't handle objects/arrays but other types okay.
		 * Also, count the length of the combined string, in bytes.
		 */
		for (len = 0, scan = args->first; scan; scan = scan->next) {
			if (scan->type == JSON_ARRAY || scan->type == JSON_OBJECT)
				goto BadMix;
			if (scan->type == JSON_STRING || scan->type == JSON_BOOL || (scan->type == JSON_NUMBER && *scan->text))
				len += strlen(scan->text);
			else if (scan->type == JSON_NUMBER)
				len += 20; /* just a guess */
		}

		/* Allocate a result string with enough space */
		result = json_string("", len);

		/* Append all of the strings together */
		for (build = result->text, scan = args->first; scan; scan = scan->next) {
			if (scan->type == JSON_NULL)
				continue;
			else if (scan->type == JSON_NUMBER && !*scan->text) {
				if (scan->text[1] == 'i')
					snprintf(build, len, "%i", JSON_INT(scan));
				else
					snprintf(build, len, "%.*g", json_format_default.digits, JSON_DOUBLE(scan));
			} else
				strcpy(build, scan->text);
			build += strlen(build);
		}
	}

	/* Return the result */
	return result;

BadMix:
	return json_error_null(1, "concat() works on arrays or strings, not a mixture");
}

/* orderBy(arr, sortlist) - Sort an array of objects */
json_t *jfn_orderBy(json_t *args, void *agdata)
{
	json_t *result, *order;
	json_t arraybuf;

	/* Extract "order" from args */
	order = args->first->next;
	if (order && order->type == JSON_STRING) {
		arraybuf.type = JSON_ARRAY;
		arraybuf.first = order;
		order = &arraybuf;
	}

	/* First arg must be a table (array of objects).  Second arg must be
	 * an array of fields and "true" for descending
	 */
	if (!json_is_table(args->first) || !order || order->type != JSON_ARRAY || !order->first)
		return json_error_null(1, "orderBy() requires a table and an array of keys");

	/* Sort a copy of the table */
	result = json_copy(args->first);
	json_sort(result, order);
	return result;
}

/* groupBy(arr, sortlist) - group table elements via row members */
json_t *jfn_groupBy(json_t *args, void *agdata)
{
	json_t *result;

	/* Must be at least 2 args.  First must be a table (array of objects).
	 * Second must be array, hopefully of strings
	 */
	if (!json_is_table(args->first)
	 || !args->first->next
	 || (args->first->next->type != JSON_ARRAY && args->first->next->type != JSON_STRING))
		return NULL;
	result = json_array_group_by(args->first, args->first->next);

	/* If a third arg is given and true, then append an empty object to
	 * trigger a totals line when the @ ot @@ operator is used.
	 */
	if (result
	 && result->type == JSON_ARRAY
	 && args->first->next->next) {
		json_t *totals = json_object();
		if (args->first->next->next->type == JSON_STRING) {
			/* find the first field name */
			json_t *name = args->first->next;
			char *text, *dot;
			if (name->type == JSON_ARRAY)
				name = name->first;
			while (name && name->type != JSON_STRING)
				name = name->next;
			dot = strrchr(name->text, '.');
			text = dot ? dot + 1 : name->text;
			if (name)
				json_append(totals, json_key(text, json_copy(args->first->next->next)));
			json_append(result, totals);
		} else if (json_is_true(args->first->next->next))
			json_append(result, totals);
		else
			json_free(totals);
	}

	/* Return the result */
	return result;
}

/* flat(arr, depth) - ungroup array elements */
json_t *jfn_flat(json_t *args, void *agdata)
{
	int	depth;
	if (args->first->next && args->first->next->type == JSON_NUMBER)
		depth = json_int(args->first->next);
	else
		depth = -1;
	return json_array_flat(args->first, depth);
}

/* slice(arr/str, start, end) - return part of an array or string */
json_t *jfn_slice(json_t *args, void *agdata)
{
	int	start, end;
	size_t	srclength;
	json_t	*result, *scan;
	const char	*str, *strend;

	/* If first param isn't an array or string, then return null */
	if (args->first->type == JSON_ARRAY)
		srclength = json_length(args->first);
	else if (args->first->type == JSON_STRING)
		srclength = json_mbs_len(args->first->text);
	else
		return NULL;

	/* Get the start and end parameters */
	if (!args->first->next || args->first->next->type != JSON_NUMBER) {
		/* No endpoints, why bother? */
		start = 0, end = srclength; /* the whole array/string */
	} else {
		/* We at least have a start */
		start = json_int(args->first->next);
		if (start < 0)
			start += srclength;
		if (start < 0)
			start = 0;

		/* Do we also have an end? */
		if (!args->first->next->next || args->first->next->next->type != JSON_NUMBER) {
			end = srclength;
		} else {
			end = json_int(args->first->next->next);
			if (end < 0)
				end += srclength;
			if (end < start)
				end = start;
		}
	}

	/* Now that we have the endpoints, do it! */
	if (args->first->type == JSON_ARRAY) {
		/* Copy the slice to a new array */
		result = json_array();
		for (scan = json_by_index(args->first, start); scan && start < end; start++, scan = scan->next) {
			json_append(result, json_copy(scan));
		}
	} else { /* JSON_STRING */
		str = json_mbs_substr(args->first->text, start, NULL);
		strend = json_mbs_substr(args->first->text, end, NULL);
		result = json_string(str, strend - str);
	}

	return result;
}

/* slice(str, qty) Concatenate qty copies of str */
static json_t *jfn_repeat(json_t *args, void *agdata)
{
	int	len;
	int	count;
	json_t	*result;
	char	*end;

	/* Requires a string and a number */
	if (args->first->type != JSON_STRING || !args->first->next || args->first->next->type != JSON_NUMBER)
		return NULL;

	/* Get the quantity */
	count = json_int(args->first->next);
	if (count < 0)
		return NULL;

	/* Allocate the result, with room for the repeated text */
	len = (int)strlen(args->first->text);
	result = json_string("", count * len);

	/* Copy qty copies of the string into it */
	for (end = result->text; count > 0; end += len, count--) {
		strncpy(end, args->first->text, len);
	}
	*end = '\0';

	/* Done! */
	return result;
}

/* toFixed(num, digits) Format a number with the given digits after the decimal */
static json_t *jfn_toFixed(json_t *args, void *agdata)
{
	double	num;
	int	digits;
	char	buf[100];

	/* Requires two numbers */
	if (args->first->type != JSON_NUMBER || !args->first->next || args->first->next->type != JSON_NUMBER)
		return NULL;

	num = json_double(args->first);
	digits = json_int(args->first->next);
	snprintf(buf, sizeof buf, "%.*f", digits, num);
	return json_string(buf, -1);
}

/* Eliminate duplicates from an array */
static json_t *jfn_distinct(json_t *args, void *agdata)
{
	int	bestrict = 0;
	json_t	*fieldlist = NULL;
	json_t	pretendArray;
	json_t	*result, *scan, *prev;

	/* If not an array, or empty, return it unchanged */
	if (args->first->type != JSON_ARRAY || !args->first->first)
		return json_copy(args->first);

	/* Check for a "strict" flag or field list as the second parameter */
	fieldlist = args->first->next;
	if (fieldlist) {
		if (fieldlist->type == JSON_BOOL && json_is_true(fieldlist)) {
			bestrict = 1;
			fieldlist = fieldlist->next;
		}
		if (fieldlist && fieldlist->type == JSON_STRING) {
			/* Fieldlist is supposed to be an array of strings.
			 * If we're given a single string instead of an array,
			 * then treat it like an array.
			 */
			pretendArray.type = JSON_ARRAY;
			pretendArray.first = fieldlist;
			fieldlist = &pretendArray;
		}
	}

	/* Start building a new array with unique items.  The first item is
	 * always included.
	 */
	result = json_array();
	prev = args->first->first;
	json_append(result, json_copy(prev));

	/* Separate methods for strict vs. non-strict */
	if (bestrict) {
		/* Strict!  We want to compare each prospective element against
		 * all elements currently in the result, and add only if new.
		 */

		/* For each element after the first... */
		for (scan = prev->next; scan; prev = scan, scan = scan->next) {
			/* Check for a match anywhere in the result so far */
			for (prev = result->first; prev; prev = prev->next) {
				if (fieldlist && prev->type == JSON_OBJECT && scan->type == JSON_OBJECT) {
					if (json_compare(prev, scan, fieldlist) == 0)
						break;
				} else {
					if (json_equal(prev, scan))
						break;
				}
			}

			/* If nothing already in the list matched, add it */
			if (!prev)
				json_append(result, json_copy(scan));
		}
	} else {
		/* Non-strict!  We just compare each prospective element
		 * against the one that preceded it.
		 */
		/* for each element after the first... */
		for (scan = prev->next; scan; prev = scan, scan = scan->next) {
			/* If it matches the previous item, skip */
			if (fieldlist && prev->type == JSON_OBJECT && scan->type == JSON_OBJECT) {
				if (json_compare(prev, scan, fieldlist) == 0)
					continue;
			} else {
				if (json_equal(prev, scan))
					continue;
			}

			/* New, so add it */
			json_append(result, json_copy(scan));
		}
	}

	/* Return the resulting array */
	return result;
}

/* Unroll nested tables */
json_t *jfn_unroll(json_t *args, void *agdata)
{
	return json_unroll(args->first, args->first->next);
}

json_t *jfn_nameBits(json_t *args, void *agdata)
{
	int	inbits;
	int	pos, nbits;
	json_t	*result;
	json_t	*names;
	char	*delim;

	/* Check the args */
	if (args->first->type != JSON_NUMBER
	 || !args->first->next
	 || args->first->next->type != JSON_ARRAY)
		return NULL;

	/* Extract arguments */
	inbits = json_int(args->first);
	names = args->first->next;
	delim = NULL;
	if (args->first->next->next
	 && args->first->next->next->type == JSON_STRING)
		delim = args->first->next->next->text;

	/* If the bits value is negative, use all-1's */
	if (inbits < 0)
		inbits = ~0;

	/* Start with an empty object */
	result = json_object();

	/* Scan the bits... */
	for (pos = 0, names = names->first; names; pos += nbits, names = names->next) {
		/* Single bit? */
		if (names->type == JSON_STRING) {
			nbits = 1;
			if (inbits & (1 << pos))
				json_append(result, json_key(names->text, json_from_int(1 << pos)));
		} else if (names->type == JSON_ARRAY && names->first) {
			/* Figure out which bits we need for this array */
			int length = json_length(names);
			int mask, i;
			json_t *elem;
			for (nbits = 1; length > (1 << nbits); nbits++) {
			}
			mask = (1 << nbits) - 1;

			/* Choose an index.  We want to skip 0x00 by default
			 * so the first name in the names subarray is 0x01.
			 */
			i = ((inbits >> pos) - 1) & mask;

			/* Look it up.  If missing or not a string, then it
			 * is just a placeholder.  But for strings, add a
			 * member to the result.
			 */
			elem = json_by_index(names, i);
			if (elem && elem->type == JSON_STRING)
				json_append(result, json_key(elem->text, json_from_int(inbits & (mask << pos))));
		} else /* basically a placeholder for a "do not care" bit */ {
			nbits = 1;
		}
	}

	/* If we were given a delimiter, we should extract the keys from the
	 * member and join them to form a single string.
	 */
	if (delim) {
		/* Count the lengths of the names and delimiters */
		size_t	len = 0;
		size_t	delimlen = strlen(delim);
		json_t	*member, *str;
		for (member = result->first; member; member = member->next) {
			len += strlen(member->text);
			if (member->next)
				len += delimlen;
		}

		/* Collect the names in a string */
		str = json_string("", len);
		for (member = result->first; member; member = member->next) {
			strcat(str->text, member->text);
			if (member->next)
				strcat(str->text, delim);
		}

		/* Use the string as the result, instead of the object */
		json_free(result);
		result = str;
	}

	return result;
}

/* Return an array of {key,value} objects, from a given object */
static json_t *keysValuesHelper(json_t *obj)
{
	json_t	*array, *scan, *pair;

	/* Start with an empty array */
	array = json_array();

	/* Add a {name,value} pair for each member of obj */
	for (scan = obj->first; scan; scan = scan->next) {
		pair = json_object();
		json_append(pair, json_key("key", json_string(scan->text, -1)));
		json_append(pair, json_key("value", json_copy(scan->first)));
		json_append(array, pair);
	}

	/* Return the array */
	return array;
}

/* Convert an object into an array of {name,value} objects.  Or if passed a
 * table instead of an object, then output a grouped array of {name,value}
 * objects.
 */
static json_t *jfn_keysValues(json_t *args, void *agdata)
{
	json_t	*scan, *result;

	if (args->first->type == JSON_OBJECT) {
		/* Single object is easy */
		return keysValuesHelper(args->first);
	} else if (json_is_table(args->first)) {
		/* For a table, we want to return a grouped array -- that is,
		 * an array of arrays, where each embedded array represents a
		 * single row from the argument table.
		 */
		result = json_array();
		for (scan = args->first->first; scan; scan = scan->next)
			json_append(result, keysValuesHelper(scan));
		return result;
	}
	return NULL;
}

/* Return a single-character substring */
static json_t *jfn_charAt(json_t *args, void *agdata)
{
	const char	*pos;
	size_t	size;

	/* The first argument must be a non-empty string */
	if (args->first->type != JSON_STRING || !args->first->text)
		return NULL;

	/* Single number?  No subscript given? */
	if (!args->first->next || args->first->next->type == JSON_NUMBER) {
		/* Get the position of the character */
		size = 1;
		if (args->first->next)
			pos = json_mbs_substr(args->first->text, json_int(args->first->next), &size);
		else /* no character offset given, so use first character */
			pos = json_mbs_substr(args->first->text, 0, &size);

		/* If end of string, return null */
		if (!*pos)
			return NULL;

		/* Return it as a string */
		return json_string(pos, size);
	}

	return NULL;
}

/* Return the character at a given index, as a number. */
static json_t *jfn_charCodeAt(json_t *args, void *agdata)
{
	const char	*pos;
	wchar_t	wc;
	json_t	*scan, *result;

	/* The first argument must be a string */
	if (args->first->type != JSON_STRING)
		return NULL;

	/* Single number?  No subscript given? */
	if (!args->first->next || args->first->next->type == JSON_NUMBER) {
		/* Get the position of the character */
		if (args->first->next)
			pos = json_mbs_substr(args->first->text, json_int(args->first->next), NULL);
		else /* no character offset given, so use first character */
			pos = args->first->text;

		/* Convert the character from UTF-8 to wchar_t */
		(void)mbtowc(&wc, pos, MB_CUR_MAX);

		/* Return it as an integer */
		return json_from_int((int)wc);
	}

	/* Array? */
	if (args->first->next->type == JSON_ARRAY) {
		/* Start with an empty array */
		result = json_array();

		/* Scan the arg2 array for numbers. */
		for (scan = args->first->next->first; scan; scan = scan->next) {
			if (scan->type == JSON_NUMBER) {
				/* Find the position of the character */
				pos = json_mbs_substr(args->first->text, json_int(scan), NULL);

				/* Convert the character from UTF-8 to wchar_t */
				(void)mbtowc(&wc, pos, MB_CUR_MAX);

				/* Add it to the array */
				json_append(result, json_from_int((int)wc));
			}
		}

		/* Return the result */
		return result;
	}

	/* Nope.  Fail due to bad arguments */
	return NULL;
}

/* Return a string generated from character codepoints. */
static json_t *jfn_fromCharCode(json_t *args, void *agdata)
{
	size_t len;
	json_t	*scan, *elem;
	char	dummy[MB_CUR_MAX];
	int	in;
	json_t	*result;
	char	*s;

	/* Count the length.  Note that some codepoints require multiple bytes */
	for (len = 0, scan = args->first; scan; scan = scan->next) {
		if (scan->type == JSON_NUMBER) {
			in = wctomb(dummy, json_int(scan));
			if (in > 0)
				len += in;
		} else if (scan->type == JSON_ARRAY) {
			for (elem = scan->first; elem; elem = elem->next) {
				in = wctomb(dummy, json_int(elem));
				if (in > 0)
					len += in;
			}
		} else if (scan->type == JSON_STRING) {
			len += strlen(scan->text);
		}
	}

	/* Allocate a big enough JSON_STRING.  Note that "len" does not need to allow
	 * for the '\0' that json_string() adds after the string.
	 */
	result = json_string("", len);

	/* Loop through the args again, building the result */
	for (s = result->text, scan = args->first; scan; scan = scan->next) {
		if (scan->type == JSON_NUMBER) {
			in = wctomb(s, json_int(scan));
			if (in > 0)
				s += in;
		} else if (scan->type == JSON_ARRAY) {
			for (elem = scan->first; elem; elem = elem->next) {
				in = wctomb(s, json_int(elem));
				if (in > 0)
					s += in;
			}
		} else if (scan->type == JSON_STRING) {
			strcpy(s, scan->text);
			s += strlen(scan->text);
		}
	}

	/* Done! */
	return result;
}

/* Append str to buf, extending buf if necessary.  Return buf */
static char *addstr(char *buf, size_t *refsize, size_t used, const char *str, size_t len)
{
	/* If size is -1 then use strlen() to find it */
	if (len == (size_t)-1)
		len = strlen(str);

	/* If buf is too small, extend it */
	if (used + len + 1 > *refsize) {
		*refsize = ((*refsize + len) | 0xff) + 1;
		buf = (char *)realloc(buf, *refsize);
	}

	/* Append the new string, and a trailing '\0' */
	memcpy(buf + used, str, len);
	buf[used + len] = '\0';

	return buf;
}

static json_t *help_replace(json_t *args, regex_t *preg, int globally)
{
	const char	*subject, *search, *replace;
	size_t		searchlen;
	int		ignorecase;
	const char	*found;
	char		*buf;
	size_t		bufsize, used;
	regmatch_t	matches[10];
	int		m, scan, chunk, in;
	json_t		*result;

	/* Check parameters */
	if (args->first->type != JSON_STRING
	 || args->first->next == NULL
	 || (!preg && args->first->next->type != JSON_STRING)
	 || args->first->next->next == NULL
	 || args->first->next->next->type != JSON_STRING)
		return NULL;

	/* Copy parameter strings into variables */
	subject = args->first->text;
	search = (preg ? NULL : args->first->next->text);
	replace = args->first->next->next->text;
	ignorecase = json_is_true(args->first->next->next->next);

	/* Start building a replacement string */
	bufsize = 128;
	buf = (char *)malloc(bufsize);
	used = 0;
	buf[0] = '\0';

	/* Find the first/next match */
	if (preg) {
		/* REGULAR EXPRESSION VERSION */

		/* For each match... */
		while (0 == regexec(preg, subject, 10, matches, 0)) {
			/* Include any text from before the match */
			buf = addstr(buf, &bufsize, used, subject, matches[0].rm_so);
			used += matches[0].rm_so;

			/* Copy replacement text, handling $n notations */
			for (scan = chunk = 0; replace[scan]; scan++) {
				if (replace[scan] == '$') {
					/* Copy plain text before $ */
					if (scan > chunk) {
						buf = addstr(buf, &bufsize, used, replace + chunk, scan - chunk);
						used += scan - chunk;
					}

					/* Substitute for $n */
					scan++;
					chunk = scan;
					if (replace[scan] == '&')
						m = 0;
					else if (replace[scan] >= '0' && replace[scan] <= '9')
						m = replace[scan] - '0';
					else
						continue;
					chunk++;
					if (matches[m].rm_so >= 0 && matches[m].rm_so < matches[m].rm_eo) {
						buf = addstr(buf, &bufsize, used, subject + matches[m].rm_so, matches[m].rm_eo - matches[m].rm_so);
						used += matches[m].rm_eo - matches[m].rm_so;
					}
				}
			}

			/* Final segment of replacement, after the last $n */
			if (replace[chunk] != '\0') {
				buf = addstr(buf, &bufsize, used, &replace[chunk], -1);
				used += strlen(&replace[chunk]);
			}

			/* Move past this match */
			subject += matches[0].rm_eo;

			/* If that last match was empty, then skip 1 character */
			if (matches[0].rm_eo == matches[0].rm_so && *subject) {
				/* Find the byte-length of the next character */
				wchar_t wc;
				int in = mbtowc(&wc, subject, MB_CUR_MAX);

				/* Move it to the result string buffer */
				buf = addstr(buf, &bufsize, used, subject, (size_t)in);
				used += in;
				subject += in;
			}

			/* If only supposed to do once, break out of the loop */
			if (!globally)
				break;
		}

		/* Append the tail of the subject to the buf */
		buf = addstr(buf, &bufsize, used, subject, -1);

	} else {
		/* STRING VERSION */

		/* For each match...  */
		while ((found = json_mbs_str(subject, search, NULL, &searchlen, 0, ignorecase)) != NULL) {
			/* Add any text from the subject string before the match */
			if (found != subject) {
				buf = addstr(buf, &bufsize, used, subject, (size_t)(found - subject));
				used += (size_t)(found - subject);
			}

			/* Add the replacement text */
			buf = addstr(buf, &bufsize, used, replace, -1);
			used += strlen(replace);

			/* Move past the match. */
			subject = found + searchlen;
			if (!globally)
				break;

			/* If the match was zero-length, then skip a character */
			if (*subject && searchlen == 0) {
				wchar_t wc;
				in = mbtowc(&wc, subject, MB_CUR_MAX);
				if (in > 0) {
					buf = addstr(buf, &bufsize, used, subject, in);
					subject += in;
				}
			}
		}

		/* Append the tail of the subject to the buf */
		buf = addstr(buf, &bufsize, used, subject, -1);
	}

	/* Copy the string into a json_t, and return it */
	result = json_string(buf, -1);

	/* Clean up */
	free(buf);

	return result;
}

/* Replace the first instance of a substring or regular expression */
static json_t *jfn_replace(json_t *args, void *agdata)
{
	jsonfuncextra_t *recon = (jsonfuncextra_t *)agdata;
	jsoncalc_t *regex = recon->regex;
	if (regex)
		return help_replace(args, regex->u.regex.preg, regex->u.regex.global);
	else
		return help_replace(args, NULL, 0);
}

/* Replace all instances of a substring or regular expression */
static json_t *jfn_replaceAll(json_t *args, void *agdata)
{
	jsonfuncextra_t *recon = (jsonfuncextra_t *)agdata;
	jsoncalc_t *regex = recon->regex;
	if (regex)
		return help_replace(args, regex->u.regex.preg, 1);
	else
		return help_replace(args, NULL, 10);
}

/* This is a helper function for indexOf(), lastIndexOf(), and includes().
 * These three are similar enough to benefit from common code.  Returns -2 or
 * -3 on bad parameters, -1 if not found, or an index number otherwise.
 */
int help_indexOf(json_t *args, int last)
{
	int	ignorecase;

	/* We need at least 2 arguments.  Third may be ignorecase flag */
	if (!args->first->next)
		return -2;
	ignorecase = json_is_true(args->first->next->next);
	if (ignorecase && args->first->next->type != JSON_STRING)
		return -3;

	/* Array version?  String version? */
	if (args->first->type == JSON_ARRAY) {
		/* Array version! */
		json_t	*haystack = args->first;
		json_t	*needle = args->first->next;;
		json_t	*scan;
		int	i;

		/* If looking for last, start at end */
		if (last) {
			/* Scan the array backwards.  This is awkward and
			 * inefficient since the array is stored as a linked
			 * list.
			 */
			for (i = json_length(haystack) - 1; i >= 0; i--) {
				scan = json_by_index(haystack, i);
				if (ignorecase) {
					/* Case-insensitive search for a string */
					if (scan->type == JSON_STRING && json_mbs_casecmp(scan->text, needle->text) == 0)
						return i;
				} else {
					/* Search for anything, case-sensitive */
					if (json_equal(scan, needle))
						return i;
				}
			}
		} else {
			/* Scan the array forward.  Much better! */
			for (i = 0, scan = haystack->first; scan; i++, scan = scan->next) {

				if (ignorecase) {
					/* Case-insensitive search for a string */
					if (scan->type == JSON_STRING && json_mbs_casecmp(scan->text, needle->text) == 0)
						return i;
				} else {
					/* Search for anything, case-sensitive */
					if (json_equal(scan, needle))
						return i;
				}
			}
		}
	} else if (args->first->type == JSON_STRING) {
		/* String version! */

		char *haystack = args->first->text;
		char *needle = args->first->next->text;
		size_t	position;

		if (json_mbs_str(haystack, needle, &position, NULL, last, ignorecase))
			return (int)position;

	} else {
		/* Trying to search in something other than an array or string */
		return -2;
	}

	/* If we get here, we didn't find it */
	return -1;
}

/* Return a boolean indicator of whether array or string contains target */
static json_t *jfn_includes(json_t *args, void *agdata)
{
	int	i = help_indexOf(args, 0);
	if (i == -2)
		return json_error_null(1, "The %s function requires an array or string, and something to search for", "includes");
	if (i == -3)
		return json_error_null(1, "The %s function's ignorecase flag only works when searching for a string", "includes");
	return json_bool(i >= 0);
}

/* Return a number indicating the position of the first match within an array
 * or string, or -1 if no match is found.
 */
static json_t *jfn_indexOf(json_t *args, void *agdata)
{
	int	i = help_indexOf(args, 0);
	if (i == -2)
		return json_error_null(1, "The %s function requires an array or string, and something to search for", "indexOf");
	if (i == -3)
		return json_error_null(1, "The %s function's ignorecase flag only works when searching for a string", "indexOf");
	return json_from_int(i);
}

/* Return a number indicating the position of the last match within an array
 * or string, or -1 if no match is found.
 */
static json_t *jfn_lastIndexOf(json_t *args, void *agdata)
{
	int	i = help_indexOf(args, 1);
	if (i == -2)
		return json_error_null(1, "The %s function requires an array or string, and something to search for", "lastIndexOf");
	if (i == -3)
		return json_error_null(1, "The %s function's ignorecase flag only works when searching for a string", "lastIndexOf");
	return json_from_int(i);
}

/* Return a boolean indicator of whether a string begins with a target */
static json_t *jfn_startsWith(json_t *args, void *agdata)
{
	size_t	len;
	char	*haystack, *needle;

	/* Requires two strings */
	if (args->first->type != JSON_STRING
	 || !args->first->next
	 || args->first->next->type != JSON_STRING) {
		return json_error_null(1, "The %s function requires two strings", "startsWith");
	}
	haystack = args->first->text;
	needle = args->first->next->text;

	/* Compare the leading part of the first string to the second */
	if (json_is_true(args->first->next->next)) {
		/* Case-insensitive version */
		len = json_mbs_len(needle);
		return json_bool(json_mbs_ncasecmp(haystack, needle, len) == 0);
	} else {
		/* Case-sensitive version */
		len = strlen(needle);
		return json_bool(strncmp(haystack, needle, len) == 0);
	}
}

/* Return a boolean indicator of whether a string ends with a target */
static json_t *jfn_endsWith(json_t *args, void *agdata)
{
	size_t	haylen, len;
	const char	*haystack, *needle;

	/* Requires two strings */
	if (args->first->type != JSON_STRING
	 || !args->first->next
	 || args->first->next->type != JSON_STRING) {
		return json_error_null(1, "The %s function requires two strings", "endsWith");
	}
	haystack = args->first->text;
	needle = args->first->next->text;

	/* Compare the leading part of the first string to the second */
	if (json_is_true(args->first->next->next)) {
		/* Case-insensitive version */
		haylen = json_mbs_len(haystack);
		len = json_mbs_len(needle);
		if (len > haylen)
			return json_bool(0);
		haystack = json_mbs_substr(haystack, haylen - len, NULL);
		return json_bool(json_mbs_casecmp(haystack, needle) == 0);
	} else {
		/* Case-sensitive version */
		haylen = strlen(haystack);
		len = strlen(needle);
		if (len > haylen)
			return json_bool(0);
		haystack += haylen - len;
		return json_bool(strcmp(haystack, needle) == 0);
	}
}


/* Split a string into an array of substrings */
static json_t *jfn_split(json_t *args, void *agdata)
{
	jsonfuncextra_t *recon = (jsonfuncextra_t *)agdata;
	char	*str, *next;
	json_t	*djson;
	char	*delim;
	size_t	delimlen;
	regex_t *regex;
	regmatch_t matches[10];
	int	nelems, limit, all, regexmatch;
	json_t	*result;
	wchar_t	wc;	/* found multibyte char */
	int	len;	/* length in bytes of a multibyte char */
	int	i;

	/* Check parameters */
	if (args->first->type != JSON_STRING)
		return json_error_null(0, "%s() requires a string as its first parameter", "split");
	str = args->first->text;
	djson = args->first->next;
	if (!djson || (json_is_null(djson) && (!recon->regex || !(recon->regex->u.regex.preg)))) {
		/* If no delimiter (not even a regex) then return the string
		 * as the only member of an array.
		 */
		result = json_array();
		json_append(result, json_string(args->first->text, -1));
		return result;
	}
	regex = NULL;
	delim = NULL;
	if (json_is_null(djson) && agdata && ((jsoncalc_t *)agdata)->u.regex.preg)
		regex = recon->regex->u.regex.preg;
	else {
		if (djson->type != JSON_STRING)
			return json_error_null(0, "%s() delimiter must be a string or regex", "split");
		delim = djson->text;
		delimlen = strlen(delim); /* yes, byte length not char count */
	}
	if (!djson->next)
		limit = 0;
	else if (djson->next->type != JSON_NUMBER)
		return json_error_null(0, "%s() third parameter should be a number", "split");
	else
		limit = json_int(djson->next);
	all = 0;
	if (limit < 0) {
		all = 1;
		limit = -limit;
	}
	/* AT THIS POINT...
	 * str is the string to be split
	 * delim is NULL or the delimiter string, maybe "" for character split
	 * delimlen is the byte length of delim, if delim is not NULL.
	 * regex is NULL or the delimiter regex
	 * limit is max array size, or 0 for unlimited
	 * all is 1 if last element should contain all remaining text, 0 to clip
	 */

	/* Start the result array */
	result = json_array();

	/* Append substrings to the array until we hit limit.  If the last
	 * response element is intended to include all remaining text, then
	 * we want to end 1 step before the limit, so we can do that last
	 * one specially.
	 */
	len = 1;
	for (nelems = 0; (*str || len > 0) && (limit == 0 || nelems < limit - all); str = next) {
		/* Find the next delimiter as char, string, or regex */
		if (delim && !*delim) {
			/* Find the byte length of the next char */
			len = mbtowc(&wc, str, MB_CUR_MAX);

			/* Next substring will start at next char */
			next = str + len;
		} else if (delim) {
			/* Scan for the next match */
			for (len = 0; str[len] && strncmp(&str[len], delim, delimlen); len++)
			{
			}

			/* If we hit the end of the string without finding
			 * a match, then "next" should be the end of str.
			 * If there was a match, then next is after delim.
			 */
			if (!str[len])
				next = &str[len];
			else
				next = &str[len] + delimlen;
		} else /* regex */ {
			/* Search for the next match */
			regexmatch = (0 == regexec(regex, str, 10, matches, 0));

			if (regexmatch) {
				/* Found! matches[0] contains overall match */
				len = matches[0].rm_so;
				next = &str[matches[0].rm_eo];

				/* If the regex matched an empty string, then
				 * advance by one extra character.  Otherwise
				 * we'd get stuck in an infinite loop on the
				 * same match.
				 */
				if (matches[0].rm_so == matches[0].rm_eo && *next)
					next++;
			} else {
				len = strlen(str);
				next = str + len;
			}
		}

		/* AT THIS POINT...
		 * str is the start of the next segment
		 * len is the length in bytes of the next segment
		 * next is the start of the next segment
		 */

		/* Add the next segment to the array */
		json_append(result, json_string(str, len));
		nelems++;

		/* If we're using regexp, there may be subexpressions too */
		if (regex && regexmatch) {
			char *tail = next;
			for (i = 1; (limit == 0 || nelems < limit - all) && i <= 9; i++) {
				if (matches[i].rm_so >= 0) {
					json_append(result, json_string(str + matches[i].rm_so, matches[i].rm_eo - matches[i].rm_so));
					nelems++;
					tail = &str[matches[i].rm_eo];
				}
			}

			/* If we hit the limit, then set next to tail. That way,
			 * if "all" is in effect, everything after the last
			 * subexpression will be included in the last element.
			 */
			if (limit > 0 && nelems >= limit - all) {
				next = tail;
				len = 0;
			}
		}

		/* If we hit the end, set len to 0 */
		if (!*next)
			len = 0;
	}

	/* If we hit the limit and are doing "all", then copy everything left
	 * into one final element.
	 */
	if (all && nelems >= limit - all)
		json_append(result, json_string(str, -1));

	/* Return it! */
	return result;
}



/* Fetch an environment variable */
static json_t *jfn_getenv(json_t *args, void *agdata)
{
	char	*value;

	/* If not given a string parameter, then fail */
	if (!args->first || args->first->type != JSON_STRING || args->first->next)
		return json_error_null(1, "getenv() expects a string parameter");

	/* Fetch the value of the environment variable.  If no such variable
	 * exists, then get NULL.
	 */
	value = getenv(args->first->text);
	if (!value)
		return json_null();
	return json_string(value, -1);
}

/* Convert data to a JSON string */
static json_t *jfn_stringify(json_t *args, void *agdata)
{
	char	*str;
	json_t	*result;

	/* If there are two args and the first is an empty object, then skip it
	 * on the assumption that it is the JSON object. Convert the second
	 * argument instead.
	 */
	json_t *data = args->first;
	if (data->next && data->type == JSON_OBJECT)
		data = data->next;

	/* Convert to string */
	str = json_serialize(data, NULL);

	/* Stuff the string into a json_t and return it */
	result = json_string(str, -1);
	free(str);
	return result;
}

/* Convert JSON string to data */
static json_t *jfn_parse(json_t *args, void *agdata)
{
	/* If there are two args and the first is an empty object, then skip it
	 * on the assumption that it is the JSON object. Convert the second
	 * argument instead.
	 */
	json_t *data = args->first;
	if (data->next && data->type == JSON_OBJECT)
		data = data->next;

	/* We can only parse strings */
	if (data->type != JSON_STRING)
		return json_error_null(0, "parse() only works on strings");

	/* Parse it */
	return json_parse_string(data->text);
}

/* Convert a string to an integer */
static json_t *jfn_parseInt(json_t *args, void *agdata)
{
	int	value;
	if (args->first->type == JSON_STRING
	 || (args->first->type == JSON_NUMBER && args->first->text[0])) {
		char	*digits = args->first->text;
		if (*digits == '0') {
			int	radix;
			switch (digits[1]) {
			case 'x': case 'X': radix = 16;	digits += 2; break;
			case 'o': case 'O': radix = 8;	digits += 2; break;
			case 'b': case 'B': radix = 2;	digits += 2; break;
			default:	    radix = 8;
			}
			value = (int)strtol(digits, NULL, radix);
		} else
			value = atoi(digits);
	} else if (args->first->type == JSON_NUMBER && args->first->text[1] == 'i')
		value = JSON_INT(args->first);
	else if (args->first->type == JSON_NUMBER /* text[1] == 'f' */)
		value = (int)JSON_DOUBLE(args->first);
	else
		return json_error_null(1, "%s() expects a string", "parseInt");

	return json_from_int(value);
}

/* Convert a string to a floating point number */
static json_t *jfn_parseFloat(json_t *args, void *agdata)
{
	double	value;
	if (args->first->type == JSON_STRING
	 || (args->first->type == JSON_NUMBER && args->first->text[0]))
		value = atof(args->first->text);
	else if (args->first->type == JSON_NUMBER && args->first->text[1] == 'i')
		value = (double)JSON_INT(args->first);
	else if (args->first->type == JSON_NUMBER /* text[1] == 'f' */)
		value = JSON_DOUBLE(args->first);
	else
		return json_error_null(1, "%s() expects a string", "parseFloat");
	return json_from_double(value);
}


/* Do a deep search for a given value */
static json_t *jfn_find(json_t *args, void *agdata)
{
	jsonfuncextra_t *recon = (jsonfuncextra_t *)agdata;
	regex_t *regex = recon->regex ? recon->regex->u.regex.preg : NULL;
	json_t	*haystack, *needle, *result, *other;
	char	*defaulttable, *needkey;
	int	ignorecase;

	/* If first parameter is an object or array, that's the haystack;
	 * otherwise it's the default table.
	 */
	if (args->first->type == JSON_OBJECT || args->first->type == JSON_ARRAY) {
		haystack = args->first;
		needle = args->first->next;
		defaulttable = NULL;
	} else {
		haystack = json_context_default_table(recon->context, &defaulttable);
		if (!haystack)
			return json_error_null(0, "No default table");
		needle = args->first;
	}
	if (!needle)
		return json_error_null(0, "find() needs to know what to search for");

	/* Check for optional args after "needle" */
	ignorecase = 0;
	needkey = NULL;
	for (other = needle->next; other; other = other->next) {
		if (other->type == JSON_BOOL)
			ignorecase = json_is_true(other);
		else if (other->type == JSON_STRING && !needkey)
			needkey = other->text;
		else {
			return json_error_null(0, "find() was passed an unexpexted extra parameter");
		}
	}

	/* Search! */
	if (regex)
		result = json_find_regex(haystack, regex, needkey);
	else
		result = json_find(haystack, needle, ignorecase, needkey);

	/* If we were searching through the default table, then prepend the
	 * its expression to the "expr" members of the results.  Note that
	 * since the default table is always an array, "expr" currently
	 * begins with a subscript, not a member name, so we don't need to
	 * add a "." between them.
	 */
	if (result->type == JSON_ARRAY && defaulttable) {
		char *buf = NULL;
		char *expr;
		size_t	bufsize = 0;
		size_t	dtlen = strlen(defaulttable);
		size_t	totlen;
		for (other = result->first; other; other = other->next) {
			expr = json_text_by_key(other, "expr");
			assert(expr && *expr == '[');
			totlen = dtlen + strlen(expr);
			if (totlen + 1 > bufsize) {
				if (buf)
					free(buf);
				bufsize = (totlen | 0x1f) + 1;
				buf = (char *)malloc(bufsize);
			}
			strcpy(buf, defaulttable);
			strcat(buf, expr);
			json_append(other, json_key("expr", json_string(buf, -1)));
		}
		if (buf)
			free(buf);

	}

	/* Return the result */
	return result;
}

/******************************************************************************/
/* Date/time functions. The real guts are in datetime.c */


/* Return an ISO date string */
static json_t *jfn_date(json_t *args, void *agdata)
{
	return json_datetime_fn(args, "date");
}

/* Return an ISO time string, possibly tweaking the time zone */
static json_t *jfn_time(json_t *args, void *agdata)
{
	return json_datetime_fn(args, "time");
}

/* Return an ISO dateTime string, possibly tweaking the time zone */
static json_t *jfn_dateTime(json_t *args, void *agdata)
{
	return json_datetime_fn(args, "datetime");
}

/* Extract the time zone from an ISO time or dateTime */
static json_t *jfn_timeZone(json_t *args, void *agdata)
{
	return NULL;
}

/* Convert ISO period between string and number. */
static json_t *jfn_period(json_t *args, void *agdata)
{
	return json_datetime_fn(args, "period");
}

/******************************************************************************/
/* Math functions.  For the sake of JavaScript compatibility, the first
 * argument to these may optionally be the "Math" object, which is ignored.
 */

static json_t *jfn_abs(json_t *args, void *agdata)
{
	double d;

	/* Get the number, skipping an optional "Math" argument */
	json_t	*num = args->first;
	if (num->type == JSON_OBJECT)
		num = num->next;

	/* Fail if not a number */
	if (num->type != JSON_NUMBER)
		return json_error_null(0, "The %s() function expects a number", "abs");

	/* Apply the function */
	d = json_double(num);
	if (d < 0)
		d = -d;

	/* Return the result */
	return json_from_double(d);
}

/* Random number */
static json_t *jfn_random(json_t *args, void *agdata)
{
	/* Look for an optional limit, after an optional "Math" argument */
	int	limit;
	json_t	*num = args->first;
	if (num->type == JSON_OBJECT)
		num = num->next;
	if (num && num->type == JSON_NUMBER && (limit = json_int(num)) >= 2) {
		/* Return an int in the range [0,limit-1] */
		return json_from_int((int)lrand48() % limit);
	} else {
		/* Return a double in the range [0.0,1.0) */
		return json_from_double(drand48());
	}
}

/* Sign */
static json_t *jfn_sign(json_t *args, void *agdata)
{
	double d;
	int	sign;

	/* Get the number, skipping an optional "Math" argument */
	json_t	*num = args->first;
	if (num->type == JSON_OBJECT)
		num = num->next;

	/* Fail if not a number */
	if (num->type != JSON_NUMBER)
		return json_error_null(0, "The %s() function expects a number", "abs");

	/* Apply the function */
	d = json_double(num);
	if (d < 0)
		sign = -1;
	else if (d > 0)
		sign = 1;
	else	
		sign = 0;

	/* Return the result */
	return json_from_int(sign);
}


static json_t *jfn_wrap(json_t *args, void *agdata)
{
	char	*str;
	int	width;
	size_t	len;
	json_t	*result;

	/* Check args */
	if (args->first->type != JSON_STRING)
		return json_error_null(0, "The %s() function's first argument should be a string to wrap", "wrap");
	str = args->first->text;
	if (args->first->next && args->first->next->type != JSON_NUMBER)
		return json_error_null(0, "The %s() function's second argument should be wrap width", "wrap");
	if (args->first->next)
		width = json_int(args->first->next);
	else
		width = 0;

	/* Passing width=0 uses the default width */
	if (width == 0)
		width = 80; /* !!! should probably be a setting */

	/* Positive means word wrap, negative means character wrap */
	if (width < 0)
		len = json_mbs_wrap_char(NULL, str, -width);
	else
		len = json_mbs_wrap_word(NULL, str, width);
printf("width=%d, len=%d, str=\"%s\"\n", width, (int)len, str);
	result = json_string("", len);
	if (width < 0)
		len = json_mbs_wrap_char(result->text, str, -width);
	else
		len = json_mbs_wrap_word(result->text, str, width);
	return result;
}

static json_t *jfn_sleep(json_t *args, void *agdata)
{
	struct timespec ts;
	double	seconds;

	/* Get the sleep duration */
	if (args->first->type == JSON_NUMBER) {
		seconds = json_double(args->first);
	} else if (json_is_period(args->first)) {
		json_t *jseconds;
		json_t p, *oldnext;

		/* Build argument array containing args->first and "s". */
		memset(&p, 0, sizeof p);
		p.type = JSON_STRING;
		p.text[0] = 's';
		oldnext = args->first->next;
		args->first->next = &p;

		/* Pass that into json_datetime_fn() to get seconds. */
		jseconds = json_datetime_fn(args, "period");
		if (json_is_error(jseconds)) {
			args->first->next = oldnext;
			return jseconds;
		}
		seconds = json_double(jseconds);

		/* Clean up */
		args->first->next = oldnext;
		json_free(jseconds);
	} else {
		return json_error_null(0, "The %s() function should be passed a number of seconds on an ISO-8601 period string");
	}

	/* Sanity check */
	if (seconds < 0.0)
		return json_error_null(0, "The %s() function's sleep time must be positive", "sleep");

	/* Sleep */
	ts.tv_sec = (time_t)seconds;
	ts.tv_nsec = 1000000000 * (seconds - ts.tv_sec);
	nanosleep(&ts, NULL);

	/* Return null */
	return json_null();
}


/**************************************************************************
 * The following are aggregate functions.  These are implemented as pairs *
 * of C functions -- jag_xxxx() to accumulate data from each row, and     *
 * jfn_xxx() to return the final result.                                  *
 **************************************************************************/

/* count(arg) count non-null and non-false values */
static json_t *jfn_count(json_t *args, void *agdata)
{
	return json_from_int(*(int *)agdata);
}
static void jag_count(json_t *args, void *agdata)
{
	if (json_is_null(args->first))
		return;
	if (args->first->type == JSON_BOOL && !json_is_true(args->first))
		return;
	(*(int *)agdata)++;
}

/* rowNumber(arg) returns a different value for each element in the group */
static json_t *jfn_rowNumber(json_t *args, void *agdata)
{
	int *counter = (int *)agdata;
	int tmp;
	char	buf[20], *p, base;

	/* First arg defines the counting style.  If it is null or false then
	 * no item is returned and the count isn't incremented.
	 */
	if (args->first->type == JSON_NULL
	 || (args->first->type == JSON_BOOL && !json_is_true(args->first)))
		return NULL;

	/* If it is a number, then add that to the counter */
	if (args->first->type == JSON_NUMBER)
		return json_from_int(json_int(args->first) + (*counter)++);

	/* If it is 'a' or "A" then use upper or lowercase ASCII letters */
	if (args->first->type == JSON_STRING) {
		base = args->first->text[0];
		switch (base) {
		case 'a':
		case 'A':
			p = &buf[sizeof buf];
			*--p = '\0';
			tmp = (*counter)++;
			do {
				*--p = base + tmp % 26;
				tmp /= 26;
			} while (tmp-- > 0);
			return json_string(p, -1);

		/* Maybe put roman numerals here some day? case 'i'/'I'
		 * ivxlcdm: i,ii,iii,iv,v,vi,vii,viii,ix
		 */
		}
	}

	/* As a last resort, just return it as a 1-based number. */
	return json_from_int(1 + (*counter)++);
}
static void jag_rowNumber(json_t *args, void *agdata)
{
}

/* min(arg) returns the minimum value */
static json_t *jfn_min(json_t *args, void *agdata)
{
	agmaxdata_t *data = (agmaxdata_t *)agdata;

	/* NOTE: data->json and data->sval, if used, will be automatically freed */
	if (data->count == 0)
		return json_null();
	if (data->json)
		return json_copy(data->json);
	if (data->sval)
		return json_string(data->sval, -1);
	return json_from_double(data->dval);

}
static void jag_min(json_t *args, void *agdata)
{
	agmaxdata_t *data = (agmaxdata_t *)agdata;

	/* If this is a number and we're comparing numbers ... */
	if (args->first->type == JSON_NUMBER && !data->sval) {
		/* If this is first, or less than previous, use it */
		double d = json_double(args->first);
		if (data->count == 0 || d < data->dval) {
			data->dval = d;
			if (data->json) {
				json_free(data->json);
				data->json = NULL;
			}
			if (args->first->next)
				data->json = json_copy(args->first->next);
		}
		data->count++;
	} else if (args->first->type == JSON_STRING) {
		if (!data->sval || json_mbs_casecmp(args->first->text, data->sval) < 0) {
			if (data->sval)
				free(data->sval);
			data->sval = strdup(args->first->text);
			if (data->json) {
				json_free(data->json);
				data->json = NULL;
			}
			if (args->first->next)
				data->json = json_copy(args->first->next);
		}
		data->count++;
	}
}

/* max(arg) returns the maximum value */
static json_t *jfn_max(json_t *args, void *agdata)
{
	agmaxdata_t *data = (agmaxdata_t *)agdata;

	/* NOTE: data->json and data->sval, if used, will be automatically freed */
	if (data->count == 0)
		return json_null();
	if (data->json)
		return json_copy(data->json);
	if (data->sval)
		return json_string(data->sval, -1);
	return json_from_double(data->dval);

}
static void jag_max(json_t *args, void *agdata)
{
	agmaxdata_t *data = (agmaxdata_t *)agdata;

	/* If this is a number, and we're comparing numbers... */
	if (args->first->type == JSON_NUMBER && !data->sval) {
		double d = json_double(args->first);

		/* If this is first, or more than previous max, use it*/
		if (data->count == 0 || d > data->dval) {
			data->dval = d;
			if (data->json) {
				json_free(data->json);
				data->json = NULL;
			}
			if (args->first->next)
				data->json = json_copy(args->first->next);
		}
		data->count++;
	} else if (args->first->type == JSON_STRING) {
		if (!data->sval || json_mbs_casecmp(args->first->text, data->sval) > 0) {
			if (data->sval)
				free(data->sval);
			data->sval = strdup(args->first->text);
			if (data->json) {
				json_free(data->json);
				data->json = NULL;
			}
			if (args->first->next)
				data->json = json_copy(args->first->next);
		}
		data->count++;
	}
}

/* avg(arg) returns the average value of arg */
static json_t *jfn_avg(json_t *args, void *agdata)
{
	agdata_t *data = (agdata_t *)agdata;

	if (data->count == 0)
		return json_null();
	return json_from_double(data->val / (double)data->count);
}
static void jag_avg(json_t *args, void *agdata)
{
	agdata_t *data = (agdata_t *)agdata;

	if (args->first->type == JSON_NUMBER) {
		double d = json_double(args->first);
		if (data->count == 0)
			data->val = d;
		else
			data->val += d;
		data->count++;
	}
}

/* sum(arg) returns the sum of arg */
static json_t *jfn_sum(json_t *args, void *agdata)
{
	agdata_t *data = (agdata_t *)agdata;

	if (data->count == 0)
		return json_from_int(0);
	return json_from_double(data->val);
}
static void jag_sum(json_t *args, void *agdata)
{
	agdata_t *data = (agdata_t *)agdata;

	if (args->first->type == JSON_NUMBER) {
		double d = json_double(args->first);
		if (data->count == 0)
			data->val = d;
		else
			data->val += d;
		data->count++;
	}
}

/* product(arg) returns the product of arg */
static json_t *jfn_product(json_t *args, void *agdata)
{
	agdata_t *data = (agdata_t *)agdata;

	if (data->count == 0)
		return json_from_int(1);
	return json_from_double(data->val);
}
static void jag_product(json_t *args, void *agdata)
{
	agdata_t *data = (agdata_t *)agdata;

	if (args->first->type == JSON_NUMBER) {
		double d = json_double(args->first);
		if (data->count == 0)
			data->val = d;
		else
			data->val *= d;
		data->count++;
	}
}

/* any(arg) returns true if any row's arg is true */
static json_t *jfn_any(json_t *args, void *agdata)
{
	int i = *(int *)agdata;
	return json_bool(i);
}
static void jag_any(json_t *args, void *agdata)
{
	int *refi = (int *)agdata;
	*refi |= json_is_true(args->first);
}

/* all(arg) returns true if all of row's arg is true */
static json_t *jfn_all(json_t *args, void *agdata)
{
	int i = *(int *)agdata;
	return json_bool(!i);
}
static void jag_all(json_t *args, void *agdata)
{
	int *refi = (int *)agdata;

	*refi |= !json_is_true(args->first);
}


/* Return column statistics about a table (array of objects) */
static json_t *jfn_explain(json_t *args, void *agdata)
{
	json_t *stats = *(json_t **)agdata;

	/* Don't free the memory -- we're returning it */
	*(json_t **)agdata = NULL;

	if (!stats)
		stats = json_null();
	return stats;
}

static void jag_explain(json_t *args, void *agdata)
{
	json_t *stats = *(json_t **)agdata;
	int depth = 0;

	/* If second parameter is given and is true, then recursively explain
	 * any embedded objects or arrays of objects.
	 */
	if (args->first->next && json_is_true(args->first->next))
		depth = -1;
	stats = json_explain(stats, args->first, depth);

	*(json_t **)agdata = stats;
}



/* Write an array out to a file */
static json_t *jfn_writeArray(json_t *args, void *agdata)
{
	FILE *fp = *(FILE **)agdata;
	if (fp) {
		fputs("\n]\n", fp);
		if (fp != stdout)
			fclose(fp);
		*(FILE **)agdata = NULL;
	}
	return json_null();
}

static void jag_writeArray(json_t *args, void *agdata)
{
	FILE *fp = *(FILE **)agdata;
	json_t	*item;
	char    *ser;

	/* For "null" or "false", do nothing.  For "true" we'd *like* to
	 * substitute "this" but unfortunately we don't have access to the
	 * context.
	 */
	item = args->first;
	if (item->type == JSON_BOOL) {
		if (!json_is_true(item))
			return;
	}
	if (!fp) {
		if (args->first->next && args->first->next->type == JSON_STRING)
			fp = fopen(args->first->next->text, "w");
		else
			fp = stdout;
		*(FILE **)agdata = fp;
		fputs("[\n  ", fp);
	} else {
		fputs(",\n  ", fp);
	}

	/* Write this item */
	ser = json_serialize(item, NULL);
	fwrite(ser, strlen(ser), 1, fp);
	free(ser);
}

/* Collect non-null items in an array */
static json_t *jfn_arrayAgg(json_t *args, void *agdata)
{
	json_t *result = *(json_t **)agdata;
	if (!result)
		return json_array();
	return json_copy(result);
}
static void  jag_arrayAgg(json_t *args, void *agdata)
{
	json_t *result = *(json_t **)agdata;
	if (!result)
		result = json_array();
	if (!json_is_null(args->first))
		json_append(result, json_copy(args->first));
	*(json_t **)agdata = result;
}


/* objectAgg(key,value) Collect key/value pairs into an object. */
static json_t *jfn_objectAgg(json_t *args, void *agdata)
{
	json_t *result = *(json_t **)agdata;
	if (!result)
		return json_object();
	return json_copy(result);
}
static void  jag_objectAgg(json_t *args, void *agdata)
{
	json_t *result = *(json_t **)agdata;
	if (!result)
		result = json_object();
	if (args->first->type == JSON_STRING && *args->first->text && args->first->next)
		json_append(result, json_key(args->first->text, json_copy(args->first->next)));
	*(json_t **)agdata = result;
}

/* join(str, delim) Concatenate a series of strings into a single big string.
 * The delim is optional and defaults to ",".
 */
static json_t *jfn_join(json_t *args, void *agdata)
{
	agjoindata_t *data = (agjoindata_t *)agdata;

	/* Return the accumulated string.  If no string, return "" */
	if (data->ag)
		return json_string(data->ag, -1);
	else
		return json_string("", 0);
}
static void  jag_join(json_t *args, void *agdata)
{
	char	*text, *mustfree, *delim;
	char	buf[40];
	agjoindata_t *data = (agjoindata_t *)agdata;
	size_t	newlen;

	/* Get the text.  Skip null, but get text for anything else */
	mustfree = NULL;
	switch (args->first->type) {
	case JSON_NULL:
		return;
	case JSON_STRING:
	case JSON_BOOL:
		text = args->first->text;
		break;
	case JSON_NUMBER:
		if (*args->first->text)
			text = args->first->text; /* number in text format */
		else {
			if (args->first->text[1] == 'i')
				snprintf(buf, sizeof buf, "%i", JSON_INT(args->first));
			else
				snprintf(buf, sizeof buf, "%g", JSON_DOUBLE(args->first));
			text = buf;
		}
		break;
	default:
		text = mustfree = json_serialize(args->first, NULL);
	}

	/* First string? */
	if (data->ag == NULL) {
		/* The first call always stores a string unchanged */
		data->size = (strlen(text) | 0xff) + 1;
		data->ag = (char *)malloc(data->size);
		strcpy(data->ag, text);
	} else {
		/* Get the delimiter, default to "," */
		if (args->first->next && args->first->next->type == JSON_STRING)
			delim = args->first->next->text;
		else
			delim = ",";

		/* Maybe need to reallocate */
		newlen = strlen(data->ag) + strlen(delim) + strlen(text);
		if (newlen + 1 > data->size) {
			data->size = (newlen | 0x1ff) + 1;
			data->ag = (char *)realloc(data->ag, data->size);
		}

		/* Append the delimiter and text */
		strcat(data->ag, delim);
		strcat(data->ag, text);
	}

	/* Clean up */
	if (mustfree)
		free(mustfree);
}
