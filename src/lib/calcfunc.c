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
#include <jx.h>

/* This file mostly implements the built-in functions.  It also defines
 * the jx_calc_function() function for adding user-defined functions.
 *
 * The jx_calc_parse() function converts argument lists into array generators
 * so the built-in functions are always passed a JX_ARRAY of the arguments.
 * For function calls of the form expr.func(args), expr is moved to become
 * the first argument, so it looks like func(expr, args).
 *
 * The jx_calc() function handles automatically frees the argument list.
 * Your function should allocate new a new jx_t tree and return that;
 * it should not attempt to reuse parts of the argument list.  As a special
 * case, if your function is going to return "null" then you can have it
 * return C's NULL pointer instead of a jx_t.
 *
 * Aggregate functions are divided into two parts: an aggregator function
 * and a final function.  Memory for storing aggregated results (e.g.,
 * counts and totals) is automatically allocated/freed, and is passed
 * to the functions as "agdata".
 */

/* Several aggregate functions use these to store results */
typedef struct { int count; double val; } agdata_t;
typedef struct { jx_t *json; char *sval; int count; double dval; } agmaxdata_t;
typedef struct { char *ag; size_t size;} agjoindata_t;

/* Forward declarations of the built-in non-aggregate functions */
static jx_t *jfn_toUpperCase(jx_t *args, void *agdata);
static jx_t *jfn_toLowerCase(jx_t *args, void *agdata);
static jx_t *jfn_toMixedCase(jx_t *args, void *agdata);
static jx_t *jfn_simpleKey(jx_t *args, void *agdata);
static jx_t *jfn_substr(jx_t *args, void *agdata);
static jx_t *jfn_hex(jx_t *args, void *agdata);
static jx_t *jfn_toString(jx_t *args, void *agdata);
static jx_t *jfn_isString(jx_t *args, void *agdata);
static jx_t *jfn_isArray(jx_t *args, void *agdata);
static jx_t *jfn_isTable(jx_t *args, void *agdata);
static jx_t *jfn_isObject(jx_t *args, void *agdata);
static jx_t *jfn_isNumber(jx_t *args, void *agdata);
static jx_t *jfn_isInteger(jx_t *args, void *agdata);
static jx_t *jfn_isNaN(jx_t *args, void *agdata);
static jx_t *jfn_isDate(jx_t *args, void *agdata);
static jx_t *jfn_isTime(jx_t *args, void *agdata);
static jx_t *jfn_isDateTime(jx_t *args, void *agdata);
static jx_t *jfn_isPeriod(jx_t *args, void *agdata);
static jx_t *jfn_typeOf(jx_t *args, void *agdata);
static jx_t *jfn_deferTypeOf(jx_t *args, void *agdata);
static jx_t *jfn_blob(jx_t *args, void *agdata);
static jx_t *jfn_sizeOf(jx_t *args, void *agdata);
static jx_t *jfn_widthOf(jx_t *args, void *agdata);
static jx_t *jfn_heightOf(jx_t *args, void *agdata);
static jx_t *jfn_keys(jx_t *args, void *agdata);
static jx_t *jfn_trim(jx_t *args, void *agdata);
static jx_t *jfn_trimStart(jx_t *args, void *agdata);
static jx_t *jfn_trimEnd(jx_t *args, void *agdata);
static jx_t *jfn_concat(jx_t *args, void *agdata);
static jx_t *jfn_orderBy(jx_t *args, void *agdata);
static jx_t *jfn_groupBy(jx_t *args, void *agdata);
static jx_t *jfn_flat(jx_t *args, void *agdata);
static jx_t *jfn_slice(jx_t *args, void *agdata);
static jx_t *jfn_repeat(jx_t *args, void *agdata);
static jx_t *jfn_toFixed(jx_t *args, void *agdata);
static jx_t *jfn_distinct(jx_t *args, void *agdata);
static jx_t *jfn_unroll(jx_t *args, void *agdata);
static jx_t *jfn_nameBits(jx_t *args, void *agdata);
static jx_t *jfn_keysValues(jx_t *args, void *agdata);
static jx_t *jfn_charAt(jx_t *args, void *agdata);
static jx_t *jfn_charCodeAt(jx_t *args, void *agdata);
static jx_t *jfn_fromCharCode(jx_t *args, void *agdata);
static jx_t *jfn_replace(jx_t *args, void *agdata);
static jx_t *jfn_replaceAll(jx_t *args, void *agdata);
static jx_t *jfn_includes(jx_t *args, void *agdata);
static jx_t *jfn_indexOf(jx_t *args, void *agdata);
static jx_t *jfn_lastIndexOf(jx_t *args, void *agdata);
static jx_t *jfn_startsWith(jx_t *args, void *agdata);
static jx_t *jfn_endsWith(jx_t *args, void *agdata);
static jx_t *jfn_split(jx_t *args, void *agdata);
static jx_t *jfn_getenv(jx_t *args, void *agdata);
static jx_t *jfn_stringify(jx_t *args, void *agdata);
static jx_t *jfn_parse(jx_t *args, void *agdata);
static jx_t *jfn_parseInt(jx_t *args, void *agdata);
static jx_t *jfn_parseFloat(jx_t *args, void *agdata);
static jx_t *jfn_find(jx_t *args, void *agdata);
static jx_t *jfn_hash(jx_t *args, void *agdata);
static jx_t *jfn_diff(jx_t *args, void *agdata);
static jx_t *jfn_date(jx_t *args, void *agdata);
static jx_t *jfn_time(jx_t *args, void *agdata);
static jx_t *jfn_dateTime(jx_t *args, void *agdata);
static jx_t *jfn_timeZone(jx_t *args, void *agdata);
static jx_t *jfn_period(jx_t *args, void *agdata);
static jx_t *jfn_abs(jx_t *args, void *agdata);
static jx_t *jfn_random(jx_t *args, void *agdata);
static jx_t *jfn_sign(jx_t *args, void *agdata);
static jx_t *jfn_wrap(jx_t *args, void *agdata);
static jx_t *jfn_sleep(jx_t *args, void *agdata);
static jx_t *jfn_writeJSON(jx_t *args, void *agdata);

/* Forward declarations of the built-in aggregate functions */
static jx_t *jfn_count(jx_t *args, void *agdata);
static void    jag_count(jx_t *args, void *agdata);
static jx_t *jfn_rowNumber(jx_t *args, void *agdata);
static void    jag_rowNumber(jx_t *args, void *agdata);
static jx_t *jfn_min(jx_t *args, void *agdata);
static void    jag_min(jx_t *args, void *agdata);
static jx_t *jfn_max(jx_t *args, void *agdata);
static void    jag_max(jx_t *args, void *agdata);
static jx_t *jfn_avg(jx_t *args, void *agdata);
static void    jag_avg(jx_t *args, void *agdata);
static jx_t *jfn_sum(jx_t *args, void *agdata);
static void    jag_sum(jx_t *args, void *agdata);
static jx_t *jfn_product(jx_t *args, void *agdata);
static void    jag_product(jx_t *args, void *agdata);
static jx_t *jfn_any(jx_t *args, void *agdata);
static void    jag_any(jx_t *args, void *agdata);
static jx_t *jfn_all(jx_t *args, void *agdata);
static void    jag_all(jx_t *args, void *agdata);
static jx_t *jfn_explain(jx_t *args, void *agdata);
static void    jag_explain(jx_t *args, void *agdata);
static jx_t *jfn_writeArray(jx_t *args, void *agdata);
static void    jag_writeArray(jx_t *args, void *agdata);
static jx_t *jfn_arrayAgg(jx_t *args, void *agdata);
static void    jag_arrayAgg(jx_t *args, void *agdata);
static jx_t *jfn_objectAgg(jx_t *args, void *agdata);
static void    jag_objectAgg(jx_t *args, void *agdata);
static jx_t *jfn_join(jx_t *args, void *agdata);
static void    jag_join(jx_t *args, void *agdata);

/* A linked list of the built-in functions */
static jxfunc_t toUpperCase_jf = {NULL,            "toUpperCase", "str:string", "string",	jfn_toUpperCase};
static jxfunc_t toLowerCase_jf = {&toUpperCase_jf, "toLowerCase", "str:string", "string",	jfn_toLowerCase};
static jxfunc_t toMixedCase_jf = {&toLowerCase_jf, "toMixedCase", "str:string, exceptions?:string[]",	"string",	jfn_toMixedCase};
static jxfunc_t simpleKey_jf = {&toMixedCase_jf,    "simpleKey",    "str:string",	"string",	jfn_simpleKey};
static jxfunc_t substr_jf      = {&simpleKey_jf,    "substr",      "str:string, start:number, length?:number",	"string", jfn_substr};
static jxfunc_t hex_jf         = {&substr_jf,      "hex",         "val:string|number, length?:number", "string",	jfn_hex};
static jxfunc_t toString_jf    = {&hex_jf,         "toString",    "val:any", "string",		jfn_toString};
static jxfunc_t String_jf      = {&toString_jf,    "String",      "val:any", "string",		jfn_toString};
static jxfunc_t isString_jf    = {&String_jf,      "isString",    "val:any", "boolean",		jfn_isString};
static jxfunc_t isArray_jf     = {&isString_jf,    "isArray",     "val:any", "boolean",		jfn_isArray};
static jxfunc_t isTable_jf     = {&isArray_jf,     "isTable",     "val:any", "boolean",		jfn_isTable};
static jxfunc_t isObject_jf    = {&isTable_jf,     "isObject",    "val:any", "boolean",		jfn_isObject};
static jxfunc_t isNumber_jf    = {&isObject_jf,    "isNumber",    "val:any", "boolean",		jfn_isNumber};
static jxfunc_t isInteger_jf   = {&isNumber_jf,    "isInteger",   "val:any", "boolean",		jfn_isInteger};
static jxfunc_t isNaN_jf       = {&isInteger_jf,   "isNaN",       "val:any", "boolean",		jfn_isNaN};
static jxfunc_t isDate_jf      = {&isNaN_jf,       "isDate",      "val:any", "boolean",		jfn_isDate};
static jxfunc_t isTime_jf      = {&isDate_jf,      "isTime",      "val:any", "boolean",		jfn_isTime};
static jxfunc_t isDateTime_jf  = {&isTime_jf,      "isDateTime",  "val:any", "boolean",		jfn_isDateTime};
static jxfunc_t isPeriod_jf    = {&isDateTime_jf,  "isPeriod",    "val:any", "boolean",		jfn_isPeriod};
static jxfunc_t typeOf_jf      = {&isPeriod_jf,    "typeOf",      "val:any, prevtype:string|true", "string",	jfn_typeOf};
static jxfunc_t deferTypeOf_jf = {&typeOf_jf,      "deferTypeOf", "val:any", "string",	jfn_deferTypeOf};
static jxfunc_t blob_jf 	 = {&deferTypeOf_jf, "blob", 	    "data:string|array, convout?:number, convin?:number", "string|array",	jfn_blob};
static jxfunc_t sizeOf_jf      = {&blob_jf,	     "sizeOf",      "val:any", "number",		jfn_sizeOf};
static jxfunc_t widthOf_jf     = {&sizeOf_jf,      "widthOf",     "str:string", "number",		jfn_widthOf};
static jxfunc_t heightOf_jf    = {&widthOf_jf,     "heightOf",    "str:string", "number",		jfn_heightOf};
static jxfunc_t keys_jf        = {&heightOf_jf,    "keys",        "obj:object", "string[]",		jfn_keys};
static jxfunc_t trim_jf        = {&keys_jf,        "trim",        "str:string", "string",		jfn_trim};
static jxfunc_t trimStart_jf   = {&trim_jf,        "trimStart",   "str:string", "string",		jfn_trimStart};
static jxfunc_t trimEnd_jf     = {&trimStart_jf,   "trimEnd",     "str:string", "string",		jfn_trimEnd};
static jxfunc_t concat_jf      = {&trimEnd_jf,     "concat",      "item:array|string, ...more", "array|string",	jfn_concat};
static jxfunc_t orderBy_jf     = {&concat_jf,      "orderBy",     "tbl:table, columns:string|string[]", "table",	jfn_orderBy};
static jxfunc_t groupBy_jf     = {&orderBy_jf,     "groupBy",     "tbl:table, columns:string|string[]", "array",	jfn_groupBy};
static jxfunc_t flat_jf        = {&groupBy_jf,     "flat",        "arr:array, depth?:number",	"array",	jfn_flat};
static jxfunc_t slice_jf       = {&flat_jf,        "slice",       "val:array|string, start:number, end?:number", "array|string", jfn_slice};
static jxfunc_t repeat_jf      = {&slice_jf,       "repeat",      "str:string, count:number", "string",	jfn_repeat};
static jxfunc_t toFixed_jf     = {&repeat_jf,      "toFixed",     "num:number, precision:number", "string",	jfn_toFixed};
static jxfunc_t distinct_jf    = {&toFixed_jf,     "distinct",    "arr:array, strict?:true, columns?:string[]", "array",	jfn_distinct};
static jxfunc_t unroll_jf      = {&distinct_jf,    "unroll",      "tbl:table, nestlist:string|string[]", "table",	jfn_unroll};
static jxfunc_t nameBits_jf    = {&unroll_jf,      "nameBits",    "num:number, names:array, delim?:string", "object|string", jfn_nameBits};
static jxfunc_t keysValues_jf  = {&nameBits_jf,    "keysValues",  "val:object|table", "table",		jfn_keysValues};
static jxfunc_t charAt_jf      = {&keysValues_jf,  "charAt",      "str:string, pos?:number", "string",	jfn_charAt};
static jxfunc_t charCodeAt_jf  = {&charAt_jf,      "charCodeAt",  "str:string, pos?:number|number[]", "number|number[]",	jfn_charCodeAt};
static jxfunc_t fromCharCode_jf= {&charCodeAt_jf,  "fromCharCode","what:number|string|array, ...", "string",	jfn_fromCharCode};
static jxfunc_t replace_jf     = {&fromCharCode_jf,"replace",     "str:string, find:string|regex, replace:string", "string",	jfn_replace};
static jxfunc_t replaceAll_jf  = {&replace_jf,     "replaceAll",  "str:string, find:string|regex, replace:string", "string",	jfn_replaceAll};
static jxfunc_t includes_jf    = {&replaceAll_jf,  "includes",    "subj:string|array, find:string|regex, ignorecase?:true", "boolean",	jfn_includes};
static jxfunc_t indexOf_jf     = {&includes_jf,    "indexOf",     "subj:string|array, find:string|regex, ignorecase?:true", "number",	jfn_indexOf};
static jxfunc_t lastIndexOf_jf = {&indexOf_jf,     "lastIndexOf", "subj:string|array, find:string|regex, ignorecase?:true", "number",	jfn_lastIndexOf};
static jxfunc_t startsWith_jf  = {&lastIndexOf_jf, "startsWith",  "subj:string, srch:string, ignorecase?:true", "boolean",	jfn_startsWith};
static jxfunc_t endsWith_jf    = {&startsWith_jf,  "endsWith",    "subj:string, srch:string, ignorecase?:true", "boolean",	jfn_endsWith};
static jxfunc_t split_jf       = {&endsWith_jf,    "split",       "str:string, delim?:string|regex, limit?:number", "string[]",	jfn_split};
static jxfunc_t getenv_jf      = {&split_jf,       "getenv",      "str:string", "string:null",		jfn_getenv};
static jxfunc_t stringify_jf   = {&getenv_jf,      "stringify",   "data:any", "string",		jfn_stringify};
static jxfunc_t parse_jf       = {&stringify_jf,   "parse",       "str:string", "any",		jfn_parse};
static jxfunc_t parseInt_jf    = {&parse_jf,       "parseInt",    "str:string", "number",		jfn_parseInt};
static jxfunc_t parseFloat_jf  = {&parseInt_jf,    "parseFloat",  "str:string", "number",		jfn_parseFloat};
static jxfunc_t find_jf	       = {&parseFloat_jf,  "find", 	  "haystack?:array|object, needle:string|regex|number, key?:string, ignorecase?:true", "table",	jfn_find};
static jxfunc_t hash_jf        = {&find_jf,        "hash", 	  "data:any, seed?:number", "number",	jfn_hash};
static jxfunc_t diff_jf        = {&hash_jf,        "diff", 	  "old:array|object, new:array|object", "table", jfn_diff};
static jxfunc_t date_jf        = {&diff_jf,	   "date",        "when:string|object|number, action?:string|number|true, ...", "string|object|number",	jfn_date};
static jxfunc_t time_jf        = {&date_jf,        "time",        "when:string|object|number, action?:string|number|true, ...", "string|object|number",	jfn_time};
static jxfunc_t dateTime_jf    = {&time_jf,        "dateTime",    "when:string|object|number, action?:string|number|true, ...", "string|object|number",	jfn_dateTime};
static jxfunc_t timeZone_jf    = {&dateTime_jf,    "timeZone",    "when:string|object|number, action?:string|number|true, ...", "null",	jfn_timeZone};
static jxfunc_t period_jf      = {&timeZone_jf,    "period",      "when:string|object|number, action?:string|number|true, ...", "string|object|number",	jfn_period};
static jxfunc_t abs_jf         = {&period_jf,      "abs",         "val:number", "number", jfn_abs};
static jxfunc_t random_jf      = {&abs_jf,         "random",      "intbound?:number", "number", jfn_random};
static jxfunc_t sign_jf        = {&random_jf,      "sign",        "val:number", "number", jfn_sign};
static jxfunc_t wrap_jf        = {&sign_jf,        "wrap",        "text:string, width?:number", "number", jfn_wrap};
static jxfunc_t sleep_jf       = {&wrap_jf,        "sleep",       "seconds:number|period", "number", jfn_sleep};
static jxfunc_t writeJX_jf   = {&sleep_jf,       "writeJSON",   "data:any, filename:string", "null", jfn_writeJSON};

static jxfunc_t count_jf       = {&writeJX_jf,   "count",       "val:any|*", "number",	jfn_count, jag_count, sizeof(long)};
static jxfunc_t rowNumber_jf   = {&count_jf,       "rowNumber",   "format:string", "number|string",		jfn_rowNumber, jag_rowNumber, sizeof(int)};
static jxfunc_t min_jf         = {&rowNumber_jf,   "min",         "val:number|string, marker?:any", "number|string|any",	jfn_min,   jag_min, sizeof(agmaxdata_t), JXFUNC_JXFREE | JXFUNC_FREE};
static jxfunc_t max_jf         = {&min_jf,         "max",         "val:number|string, marker?:any", "number|string|any",	jfn_max,   jag_max, sizeof(agmaxdata_t), JXFUNC_JXFREE | JXFUNC_FREE};
static jxfunc_t avg_jf         = {&max_jf,         "avg",         "num:number", "number",		jfn_avg,   jag_avg, sizeof(agdata_t)};
static jxfunc_t sum_jf         = {&avg_jf,         "sum",         "num:number", "number",		jfn_sum,   jag_sum, sizeof(agdata_t)};
static jxfunc_t product_jf     = {&sum_jf,         "product",     "num:number", "number",		jfn_product,jag_product, sizeof(agdata_t)};
static jxfunc_t any_jf         = {&product_jf,     "any",         "bool:boolean", "boolean",		jfn_any,   jag_any, sizeof(int)};
static jxfunc_t all_jf         = {&any_jf,         "all",         "bool:boolean", "boolean",		jfn_all,   jag_all, sizeof(int)};
static jxfunc_t explain_jf     = {&all_jf,         "explain",     "tbl:table, depth:?number", "table",		jfn_explain,jag_explain, sizeof(jx_t *), JXFUNC_JXFREE};
static jxfunc_t writeArray_jf  = {&explain_jf,     "writeArray",  "data:any, filename:?string", "null",	jfn_writeArray,jag_writeArray, sizeof(FILE *)};
static jxfunc_t arrayAgg_jf    = {&writeArray_jf,  "arrayAgg",    "data:any", "array",		jfn_arrayAgg,jag_arrayAgg, sizeof(jx_t *), JXFUNC_JXFREE};
static jxfunc_t objectAgg_jf   = {&arrayAgg_jf,    "objectAgg",   "key:string, value:any", "object",	jfn_objectAgg,jag_objectAgg, sizeof(jx_t *), JXFUNC_JXFREE};
static jxfunc_t join_jf        = {&objectAgg_jf,   "join",        "str:string, delim?:string", "string",	jfn_join,  jag_join, sizeof(agjoindata_t),	JXFUNC_FREE};
static jxfunc_t *funclist      = &join_jf;


/* Return the start of the function list.  You can find successive functions
 * by following each function's ->other pointer.
 */
jxfunc_t *jx_calc_function_first(void)
{
	return funclist;
}

/* Register a C function that can be called via jx_calc().  The function
 * should look like...
 *
 *    jx_t *myFunction(jx_t *args, void *agdata).
 *
 * ... where "args" is a JX_ARRAY of the actual parameter values; if invoked
 * as a member function then "this" is the first parameter.  Your function
 * should return newly allocated jx_t data, or NULL to represent the JSON
 * "null" symbol.  In particular, it should *NOT* attempt to reuse the
 * argument data in the response.  The jx_copy() function is your friend!
 * Upon return, jx_calc() will free the parameters immediately and the
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
void jx_calc_aggregate_hook(
	const char    *name,
	const char	*args,
	const char	*type,
	jx_t *(*fn)(jx_t *args, void *agdata),
	void   (*agfn)(jx_t *args, void *agdata),
	size_t  agsize,
	int	jfoptions)
{
	jxfunc_t *f;

	/* Round agsize up to a multiple of 8 bytes */
	if (agsize > 0)
		agsize = ((agsize - 1) | 0x7) + 1;

	/* If it's already in the table then update it */
	for (f = funclist; f; f = f->other) {
		if (!strcmp(f->name, name)) {
			f->args = (char *)args;
			f->fn = fn;
			f->agfn = agfn;
			f->agsize = agsize;
			return;
		}
	}

	/* Add it */
	f = (jxfunc_t *)malloc(sizeof(jxfunc_t));
	memset(f, 0, sizeof *f);
	f->name = (char *)name;
	f->args = (char *)args;
	f->returntype = (char *)type;
	f->fn = fn;
	f->agfn = agfn;
	f->agsize = agsize;
	f->jfoptions = jfoptions;
	f->other = funclist;
	funclist = f;
}

/* Register a non-aggregate function.  "name" is the name of the function,
 * and "fn" is a pointer to the actual C function that implements it.
 * The "args" and "type" strings are the argument names and types, and the
 * return type; these are basically just comments.
 */
void jx_calc_function_hook(
	const char    *name,
	const char	*args,
	const char	*type,
	jx_t *(*fn)(jx_t *args, void *agdata))
{
	/* This is just a simplified interface to the aggregate adder */
	jx_calc_aggregate_hook(name, args, type, fn, NULL, 0, 0);
}

/* This function is called automatically when the program terminates.  It
 * frees user-defined functions, so that their resources won't be listed as
 * a memory leak.
 */
static void free_user_functions()
{
	jxfunc_t	*scan, *lag, *other;

	/* For each function in the list... */
	for (scan = funclist, lag = NULL; scan; scan = other) {
		/* Skip if not user-defined */
		other = scan->other;
		if (!scan->user) {
			lag = scan;
			continue;
		}

		/* Remove it from the list */
		if (lag)
			lag->other = other;
		else
			funclist = other;

		/* Free its resources */
		free(scan->name);
		jx_cmd_free(scan->user);
		jx_free(scan->userparams);
		if (scan->args)
			free(scan->args);
		if (scan->returntype)
			free(scan->returntype);
		free(scan);
	}
}

/* Define or redefine a user function -- one that's defined in jx's
 * command syntax instead of C code.  Returns 0 normally, or 1 if the
 * function name matches a built-in function (and hence can't be refined).
 *
 * The name, paramstr, and returntype arguments are expected to be
 * dynamically-allocated strings.  Use strdup() if necessary.
 */
int jx_calc_function_user(
	char *name,
	jx_t *params,
	char *paramstr,
	char *returntype,
	jxcmd_t *body)
{
	jxfunc_t *fn;
	static int first = 1;

	/* If first, then arrange for all user-defined functions to be freed
	 * when the program terminates.
	 */
	if (first) {
		first = 0;
		atexit(free_user_functions);
	}

	/* Look for an existing function to redefine */
	fn = jx_calc_function_by_name(name);
	if (!fn) {
		/* Allocate a new jxfunc_t and link it into the table */
		fn = (jxfunc_t *)malloc(sizeof(jxfunc_t));
		memset(fn, 0, sizeof(jxfunc_t));
		fn->other = funclist;
		funclist = fn;
	} else if (fn->fn) {
		/* Can't redefine built-ins */
		return 1;
	} else {
		/* Redefining, so discard the old details */
		free(fn->name);
		jx_free(fn->userparams);
		if (fn->args) {
			free(fn->args);
			fn->args = NULL;
		}
		if (fn->returntype) {
			free(fn->returntype);
			fn->returntype = NULL;
		}
		jx_cmd_free(fn->user);
	}

	/* Store the new info in the jxfunc_t */
	fn->name = name;
	fn->userparams = params;
	fn->args = paramstr;
	fn->returntype = returntype;
	fn->user = body;

	/* Success! */
	return 0;
}

/* Look up a function by name, and return its info */
jxfunc_t *jx_calc_function_by_name(const char *name)
{
	jxfunc_t *scan;

	/* Try case-sensitive */
	for (scan = funclist; scan; scan = scan->other) {
		if (!strcmp(name, scan->name))
			return scan;
	}

	/* Try case-insensitive */
	for (scan = funclist; scan; scan = scan->other) {
		if (!jx_mbs_casecmp(name, scan->name))
			return scan;
	}

	/* Try abbreviation, if the name is more than 1 letter long */
	if (jx_mbs_len(name) > 1) {
		for (scan = funclist; scan; scan = scan->other) {
			if (!jx_mbs_abbrcmp(name, scan->name))
				return scan;
		}
	}

	return NULL;
}

/***************************************************************************
 * Everything below this is C functions that implement jx functions.       *
 * We'll start with the non-aggregate functions.  These are jfn_xxxx() C   *
 * functions.  They're passed an agdata parameter but they ignore it.      *
 * The aggregate functions are defined later in this file.                 *
 ***************************************************************************/

/* toUpperCase(str) returns an uppercase version of str */
static jx_t *jfn_toUpperCase(jx_t *args, void *agdata)
{
	jx_t  *tmp;

	/* If string, make a copy.  If not a string then use toString on it. */
	if (args->first->type == JX_STRING)
		tmp = jx_string(args->first->text, -1);
	else
		tmp = jfn_toString(args, agdata);

	/* Convert to uppercase */
	jx_mbs_toupper(tmp->text);

	/* Return it */
	return tmp;
}

/* toLowerCase(str) returns a lowercase version of str */
static jx_t *jfn_toLowerCase(jx_t *args, void *agdata)
{
	jx_t  *tmp;

	/* If string, make a copy.  If not a string then use toString on it. */
	if (args->first->type == JX_STRING)
		tmp = jx_string(args->first->text, -1);
	else
		tmp = jfn_toString(args, agdata);

	/* Convert to uppercase */
	jx_mbs_tolower(tmp->text);

	/* Return it */
	return tmp;
}

/* to MixedCase(str, exceptions */
static jx_t *jfn_toMixedCase(jx_t *args, void *agdata)
{
	jx_t	*tmp;

	/* If string, make a copy.  If not a string then use toString on it. */
	if (args->first->type == JX_STRING)
		tmp = jx_string(args->first->text, -1);
	else
		tmp = jfn_toString(args, agdata);

	/* Convert to mixedcase */
	jx_mbs_tomixed(tmp->text, args->first->next); /* undeferred */

	/* Return it */
	return tmp;

}

/* simpleKey(str) returns a simplified version of str.  This gives scripts
 * a way to access the function that jx uses for doing case-insensitive
 * member lookups.
 */
static jx_t *jfn_simpleKey(jx_t *args, void *agdata)
{
	jx_t  *tmp;

	/* If string, make a copy.  If not a string then use toString on it. */
	if (args->first->type == JX_STRING)
		tmp = jx_string(args->first->text, -1);
	else
		tmp = jfn_toString(args, agdata);

	/* Simplify it.  Note that we do this in-place. */
	jx_mbs_simple_key(tmp->text, tmp->text);

	/* Return it */
	return tmp;
}


/* substr(str, start, len) returns a substring */
static jx_t *jfn_substr(jx_t *args, void *agdata)
{
	const char    *str;
	int	istart;
	size_t  len, start, limit;

	/* If not a string or no other parameters, just return null */
	if (args->first->type != JX_STRING || !args->first->next) /* undeferred */
		return jx_error_null(NULL, "substrStr:substr() requires a string");
	str = args->first->text;

	/* Get the length of the string.  We'll need that to adjust bounds */
	len = jx_mbs_len(str);

	/* Get the starting position */
	if (args->first->next->type != JX_NUMBER) /* undeferred */
		return jx_error_null(NULL, "substrPos:substr() position must be a number");
	istart = jx_int(args->first->next); /* undeferred */
	if (istart < 0 && istart + len >= 0)
		start = len + istart;
	else if (istart < 0 || istart > len)
		start = len;
	else
		start = istart;

	/* Get the length limit */
	if (!args->first->next->next) /* undeferred */
		limit = len - start; /* all the way to the end */
	else if (args->first->next->next->type != JX_NUMBER) /* undeferred */
		return jx_error_null(NULL, "substrLen:substr() length must be a number");
	else {
		limit = jx_int(args->first->next->next); /* undeferred */
		if (start + limit > len)
			limit = len - start;
	}

	/* Find the substring.  This isn't trivial with multibyte chars */
	str = jx_mbs_substr(str, start, &limit);

	/* Copy the substring into a new jx_t */
	return jx_string(str, limit);
}

/* hex(arg) converts strings into a series of hex digits, or numbers into hex
 * optionally padded with leading 0's.
 */
static jx_t *jfn_hex(jx_t *args, void *agdata)
{
	jx_t  *result;
	char    *str;
	int     len;
	size_t	size;
	long    n;

	if (args->first->type == JX_STRING) {
		/* Allocate a big enough string */
		str = args->first->text;
		result = jx_string("", strlen(str) * 2);

		/* Convert each byte of the string to hex */
		for (len = 0; str[len]; len++)
			snprintf(&result->text[2 * len], 3, "%02x", str[len] & 0xff);

		/* Return that */
		return result;
	} else if (args->first->type == JX_NUMBER) {
		/* Get the args, including length */
		n = jx_int(args->first);
		len = 0;
		if (args->first->next && args->first->next->type == JX_NUMBER) /* undeferred */
			len = jx_int(args->first->next); /* undeferred */
		if (len < 0)
			len = 0;

		/* Allocate the return buffer -- probably bigger than we need,
		 * but not by much.*/
		size = len ? len : n < 0xffffffffff ? 12 : 2 + sizeof(n) * 2;
		result = jx_string("", size);

		/* Fill it */
		if (len == 0)
			snprintf(result->text, size + 1, "0x%lx", n);
		else
			snprintf(result->text, size + 1, "%0*lx", len, n);

		return result;
	}
	return jx_error_null(NULL, "hex:%s() only works on numbers or strings", "hex");
}

/* toString(arg) converts arg to a string */
static jx_t *jfn_toString(jx_t *args, void *agdata)
{
	char    *tmpstr;
	jx_t  *tmp;

	/* If already a string, return a copy of it as-is */
	if (args->first->type == JX_STRING)
		return jx_copy(args->first);

	/* If boolean or non-binary number, convert its text to a string. */
	if (args->first->type == JX_BOOLEAN
	 || (args->first->type == JX_NUMBER && args->first->text[0] != '\0'))
		return jx_string(args->first->text, -1);

	/* If null, return "null" */
	if (args->first->type == JX_NULL)
		return jx_string("null", 4);

	/* For anything else, use jx_serialize() */
	tmpstr = jx_serialize(args->first, 0);
	tmp = jx_string(tmpstr, -1);
	free(tmpstr);
	return tmp;
}

static jx_t *jfn_isString(jx_t *args, void *agdata)
{
	return jx_boolean(args->first->type == JX_STRING);
}

static jx_t *jfn_isObject(jx_t *args, void *agdata)
{
	return jx_boolean(args->first->type == JX_OBJECT);
}

static jx_t *jfn_isArray(jx_t *args, void *agdata)
{
	return jx_boolean(args->first->type == JX_ARRAY);
}

static jx_t *jfn_isTable(jx_t *args, void *agdata)
{
	return jx_boolean(jx_is_table(args->first));
}

static jx_t *jfn_isNumber(jx_t *args, void *agdata)
{
	return jx_boolean(args->first->type == JX_NUMBER);
}

static jx_t *jfn_isInteger(jx_t *args, void *agdata)
{
	double d;

	if (args->first->type != JX_NUMBER)
		return jx_boolean(0);
	if (args->first->text[0] == '\0' && args->first->text[1] == 'i')
		return jx_boolean(1);
	d = jx_double(args->first);
	return jx_boolean(d == (int)d);
}

static jx_t *jfn_isNaN(jx_t *args, void *agdata)
{
	return jx_boolean(args->first->type != JX_NUMBER);
}

static jx_t *jfn_isDate(jx_t *args, void *agdata)
{
	return jx_boolean(jx_is_date(args->first));
}

static jx_t *jfn_isTime(jx_t *args, void *agdata)
{
	return jx_boolean(jx_is_time(args->first));
}

static jx_t *jfn_isDateTime(jx_t *args, void *agdata)
{
	return jx_boolean(jx_is_datetime(args->first));
}

static jx_t *jfn_isPeriod(jx_t *args, void *agdata)
{
	return jx_boolean(jx_is_period(args->first));
}

/* typeOf(data) returns a string identifying the data's type */
static jx_t *jfn_typeOf(jx_t *args, void *agdata)
{
	char	*type, *mixed;
	if (!args->first->next || (args->first->next->type == JX_BOOLEAN && *args->first->next->text == 'f')) /* undeferred */
		type = jx_typeof(args->first, 0);
	else {
		type = jx_typeof(args->first, 1);
		if (args->first->next->type == JX_STRING) { /* undeferred */
			mixed = jx_mix_types(args->first->next->text, type); /* undeferred */
			if (mixed)
				type = mixed;
		}
	}
	return jx_string(type, -1);
}

/* deferTypeOf(array) returns a string identifying the type of deferring that
 * an array is using.  This usually indicates the source of the array (file,
 * blob, elipsis, etc.)  Returns NULL if not a deferred array.
 */
static jx_t *jfn_deferTypeOf(jx_t *args, void *agdata)
{
	jxdef_t *def;

	/* If not a deferred array, return null */
	if (!jx_is_deferred_array(args->first))
		return jx_null();

	/* The type is stored in the JX_DEFER node */
	def = (jxdef_t *)args->first->first;
	return jx_string(def->fns->desc, -1);
}


/* Estimate the memory usage of a jx_t datum */
static jx_t *jfn_sizeOf(jx_t *args, void *agdata)
{
	return jx_from_int(jx_sizeof(args->first));
}

/* Estimate the width of a string.  Some characters may be wider than others,
 * even in a fixed-pitch font.
 */
static jx_t *jfn_widthOf(jx_t *args, void *agdata)
{
	char *numstr;
	int  width;

	switch (args->first->type) {
	case JX_NUMBER:
		/* Is the number in binary format? */
		if (args->text[0] == 0) {
			/* Convert from binary to string, and check that */
			numstr = jx_serialize(args, NULL);
			width = strlen(numstr); /* number width is easy */
			free(numstr);
			return jx_from_int(width);
		}
		/* else number is in text form so fall through... */
	case JX_STRING:
	case JX_BOOLEAN:
		return jx_from_int(jx_mbs_width(jx_text(args->first)));
	default:
		return NULL;
	}
}

/* Return the height of a string.  This is 1 plus the number of newlines,
 * except that if the string ends with a newline then that one doesn't count.
 */
static jx_t *jfn_heightOf(jx_t *args, void *agdata)
{
	switch (args->first->type) {
	case JX_NULL:
	case JX_BOOLEAN:
	case JX_NUMBER:
	case JX_OBJECT:
	case JX_ARRAY:
		return jx_from_int(1);

	case JX_STRING:
		return jx_from_int(jx_mbs_height(args->first->text));

	case JX_BADTOKEN:
	case JX_NEWLINE:
	case JX_ENDOBJECT:
	case JX_ENDARRAY:
	case JX_DEFER:
	case JX_KEY:
		/* can't happen */
		abort();
	}
	return jx_null(); /* to keep the compiler happy */
}

/* keys(obj) returns an array of key names, as strings */
static jx_t *jfn_keys(jx_t *args, void *agdata)
{
	jx_t *result = jx_array();
	jx_t *scan;

	/* This only really works on objects */
	if (args->first->type == JX_OBJECT) {
		/* For each member... */
		for (scan = args->first->first; scan; scan = scan->next) { /* undeferred */
			assert(scan->type == JX_KEY);
			/* Append its name to the result as a string */
			jx_append(result, jx_string(scan->text, -1));
		}
	}
	return result;
}

static jx_t *help_trim(jx_t *args, int start, int end, char *name)
{
	char	*substr;
	size_t	len;

	/* This only works on non-strings */
	if (args->first->type != JX_STRING)
		return jx_error_null(NULL, "trim:The %s function only works on strings", name);

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
	return jx_string(substr, len);
}

/* trim(obj) returns a string with leading and trailing spaces removed */
static jx_t *jfn_trim(jx_t *args, void *agdata)
{
	return help_trim(args, 1, 1, "trim");
}

/* trimStart(obj) returns a string with leading spaces removed */
static jx_t *jfn_trimStart(jx_t *args, void *agdata)
{
	return help_trim(args, 1, 0, "trimStart");
}

/* trimEnd(obj) returns a string with trailing spaces removed */
static jx_t *jfn_trimEnd(jx_t *args, void *agdata)
{
	return help_trim(args, 0, 1, "trimEnd");
}

/* Combine multiple arrays to form one long array, or multiple strings to
 * form one long string.
 */
static jx_t *jfn_concat(jx_t *args, void *agdata)
{
	jx_t  *scan, *elem;
	jx_t  *result;
	size_t	len;
	char	*build;

	/* Are we doing arrays or strings? */
	if (args->first->type == JX_ARRAY) {
		/* Arrays -- make sure everything is an array */
		for (scan = args->first->next; scan; scan = scan->next) { /* undeferred */
			if (scan->type != JX_ARRAY && scan->type != JX_NULL)
				goto BadMix;
		}

		/* Start with an empty array */
		result = jx_array();

		/* Append the elements of all arrays */
		for (scan = args->first; scan; scan = scan->next) { /* undeferred */
			if (scan->type == JX_NULL)
				continue;
			for (elem = jx_first(scan); elem; elem = jx_next(elem))
				jx_append(result, jx_copy(elem));
		}
	} else {
		/* Strings -- Can't handle objects/arrays but other types okay.
		 * Also, count the length of the combined string, in bytes.
		 */
		for (len = 0, scan = args->first; scan; scan = scan->next) { /* undeferred */
			if (scan->type == JX_ARRAY || scan->type == JX_OBJECT)
				goto BadMix;
			if (scan->type == JX_STRING || scan->type == JX_BOOLEAN || (scan->type == JX_NUMBER && *scan->text))
				len += strlen(scan->text);
			else if (scan->type == JX_NUMBER)
				len += 20; /* just a guess */
		}

		/* Allocate a result string with enough space */
		result = jx_string("", len);

		/* Append all of the strings together */
		for (build = result->text, scan = args->first; scan; scan = scan->next) { /* undeferred */
			if (scan->type == JX_NULL)
				continue;
			else if (scan->type == JX_NUMBER && !*scan->text) {
				if (scan->text[1] == 'i')
					snprintf(build, len, "%i", JX_INT(scan));
				else
					snprintf(build, len, "%.*g", jx_format_default.digits, JX_DOUBLE(scan));
			} else
				strcpy(build, scan->text);
			build += strlen(build);
		}
	}

	/* Return the result */
	return result;

BadMix:
	return jx_error_null(NULL, "concat:%s() works on arrays or strings, not a mixture", "concat");
}

/* orderBy(arr, sortlist) - Sort an array of objects */
jx_t *jfn_orderBy(jx_t *args, void *agdata)
{
	jx_t *result, *order;
	jx_t arraybuf;

	/* Extract "order" from args */
	order = args->first->next; /* undeferred */
	if (order && order->type == JX_STRING) {
		arraybuf.type = JX_ARRAY;
		arraybuf.first = order;
		order = &arraybuf;
	}

	/* First arg must be a table (array of objects).  Second arg must be
	 * an array of fields and "true" for descending
	 */
	if (!jx_is_table(args->first) || !order || order->type != JX_ARRAY || !order->first)
		return jx_error_null(NULL, "orderBy:%s() requires a table and an array of keys", "orderBy");

	/* Sort a copy of the table */
	result = jx_copy(args->first);
	jx_sort(result, order, 0);
	return result;
}

/* groupBy(arr, sortlist) - group table elements via row members */
jx_t *jfn_groupBy(jx_t *args, void *agdata)
{
	jx_t *result;

	/* Must be at least 2 args.  First must be a table (array of objects).
	 * Second must be array, hopefully of strings
	 */
	if (!jx_is_table(args->first)
	 || !args->first->next /* undeferred */
	 || (args->first->next->type != JX_ARRAY && args->first->next->type != JX_STRING)) /* undeferred */
		return NULL;
	result = jx_copy(args->first);
	jx_sort(result, args->first->next, 1); /* undeferred */

	/* If a third arg is given, then append an empty object to trigger a
	 * totals line when the @ ot @@ operator is used.
	 */
	if (result
	 && result->type == JX_ARRAY
	 && args->first->next->next) { /* undeferred */
		jx_t *totals = jx_object();
		if (args->first->next->next->type == JX_STRING) { /* undeferred */
			/* find the first field name */
			jx_t *name = args->first->next; /* undeferred */
			char *text, *dot;
			if (name->type == JX_ARRAY)
				name = name->first;
			while (name && name->type != JX_STRING)
				name = name->next; /* undeferred */
			dot = strrchr(name->text, '.');
			text = dot ? dot + 1 : name->text;
			if (name)
				jx_append(totals, jx_key(text, jx_copy(args->first->next->next))); /* undeferred */
			jx_append(result, totals);
		} else if (jx_is_true(args->first->next->next)) /* undeferred */
			jx_append(result, totals);
		else
			jx_free(totals);
	}

	/* Return the result */
	return result;
}

/* flat(arr, depth) - ungroup array elements */
jx_t *jfn_flat(jx_t *args, void *agdata)
{
	int	depth;
	if (args->first->next && args->first->next->type == JX_NUMBER) /* undeferred */
		depth = jx_int(args->first->next); /* undeferred */
	else
		depth = -1;
	return jx_array_flat(args->first, depth);
}

/* slice(arr/str, start, end) - return part of an array or string */
jx_t *jfn_slice(jx_t *args, void *agdata)
{
	int	start, end;
	size_t	srclength;
	jx_t	*result, *scan;
	const char	*str, *strend;

	/* If first param isn't an array or string, then return null */
	if (args->first->type == JX_ARRAY)
		srclength = jx_length(args->first);
	else if (args->first->type == JX_STRING)
		srclength = jx_mbs_len(args->first->text);
	else
		return NULL;

	/* Get the start and end parameters */
	if (!args->first->next || args->first->next->type != JX_NUMBER) { /* undeferred */
		/* No endpoints, why bother? */
		start = 0, end = srclength; /* the whole array/string */
	} else {
		/* We at least have a start */
		start = jx_int(args->first->next); /* undeferred */
		if (start < 0)
			start += srclength;
		if (start < 0)
			start = 0;

		/* Do we also have an end? */
		if (!args->first->next->next || args->first->next->next->type != JX_NUMBER) { /* undeferred */
			end = srclength;
		} else {
			end = jx_int(args->first->next->next); /* undeferred */
			if (end < 0)
				end += srclength;
			if (end < start)
				end = start;
		}
	}

	/* Now that we have the endpoints, do it! */
	if (args->first->type == JX_ARRAY) {
		/* Copy the slice to a new array */
		result = jx_array();
		for (scan = jx_by_index(args->first, start); scan && start < end; start++, scan = jx_next(scan)) { /* undeferred */
			jx_append(result, jx_copy(scan));
		}
		jx_break(scan);
	} else { /* JX_STRING */
		str = jx_mbs_substr(args->first->text, start, NULL);
		strend = jx_mbs_substr(args->first->text, end, NULL);
		result = jx_string(str, strend - str);
	}

	return result;
}

/* repeat(str, qty) Concatenate qty copies of str */
static jx_t *jfn_repeat(jx_t *args, void *agdata)
{
	int	len;
	int	count;
	jx_t	*result;
	char	*end;

	/* Requires a string and a number */
	if (args->first->type != JX_STRING || !args->first->next || args->first->next->type != JX_NUMBER) /* undeferred */
		return NULL;

	/* Get the quantity */
	count = jx_int(args->first->next); /* undeferred */
	if (count < 0)
		return NULL;

	/* Allocate the result, with room for the repeated text */
	len = (int)strlen(args->first->text);
	result = jx_string("", count * len);

	/* Copy qty copies of the string into it */
	for (end = result->text; count > 0; end += len, count--) {
		strncpy(end, args->first->text, len);
	}
	*end = '\0';

	/* Done! */
	return result;
}

/* toFixed(num, digits) Format a number with the given digits after the decimal */
static jx_t *jfn_toFixed(jx_t *args, void *agdata)
{
	double	num;
	int	digits;
	char	buf[100];

	/* Requires two numbers */
	if (args->first->type != JX_NUMBER || !args->first->next || args->first->next->type != JX_NUMBER) /* undeferred */
		return NULL;

	num = jx_double(args->first);
	digits = jx_int(args->first->next); /* undeferred */
	snprintf(buf, sizeof buf, "%.*f", digits, num);
	return jx_string(buf, -1);
}

/* Eliminate duplicates from an array */
static jx_t *jfn_distinct(jx_t *args, void *agdata)
{
	int	bestrict = 0;
	jx_t	*fieldlist = NULL;
	jx_t	pretendArray;
	jx_t	*result, *scan, *prev;

	/* If not an array, or empty, return it unchanged */
	if (args->first->type != JX_ARRAY || !args->first->first)
		return jx_copy(args->first);

	/* Check for a "strict" flag or field list as the second parameter */
	fieldlist = args->first->next; /* undeferred */
	if (fieldlist) {
		if (fieldlist->type == JX_BOOLEAN && jx_is_true(fieldlist)) {
			bestrict = 1;
			fieldlist = fieldlist->next; /* undeferred */
		}
		if (fieldlist && fieldlist->type == JX_STRING) {
			/* Fieldlist is supposed to be an array of strings.
			 * If we're given a single string instead of an array,
			 * then treat it like an array.
			 */
			pretendArray.type = JX_ARRAY;
			pretendArray.first = fieldlist;
			fieldlist = &pretendArray;
		}
	}

	/* Start building a new array with unique items. */
	result = jx_array();

	/* Separate methods for strict vs. non-strict */
	if (bestrict) {
		/* Strict!  We want to compare each prospective element against
		 * all elements currently in the result, and add only if new.
		 */

		/* First element is always added */
		scan = jx_first(args->first);
		prev = jx_copy(scan);
		jx_append(result, prev);

		/* For each element after the first... */
		for (scan = jx_next(scan); scan; scan = jx_next(scan)) {
			/* Check for a match anywhere in the result so far */
			for (prev = jx_first(result); prev; prev = jx_next(prev)) {
				if (fieldlist && prev->type == JX_OBJECT && scan->type == JX_OBJECT) {
					if (jx_compare(prev, scan, fieldlist) == 0)
						break;
				} else {
					if (jx_equal(prev, scan))
						break;
				}
			}
			jx_break(prev);

			/* If nothing already in the list matched, add it */
			if (!prev)
				jx_append(result, jx_copy(scan));
		}
	} else {
		/* Non-strict!  We just compare each prospective element
		 * against the one that preceded it.
		 */

		/* First element is always added */
		scan = jx_first(args->first);
		prev = jx_copy(scan);
		jx_append(result, prev);

		/* for each element after the first... */
		for (scan = jx_next(scan); scan; scan = jx_next(scan)) {
			/* If it matches the previous item, skip */
			if (fieldlist && prev->type == JX_OBJECT && scan->type == JX_OBJECT) {
				if (jx_compare(prev, scan, fieldlist) == 0)
					continue;
			} else {
				if (jx_equal(prev, scan))
					continue;
			}

			/* New, so add it */
			prev = jx_copy(scan);
			jx_append(result, prev);
		}
	}

	/* Return the resulting array */
	return result;
}

/* Unroll nested tables */
jx_t *jfn_unroll(jx_t *args, void *agdata)
{
	return jx_unroll(args->first, args->first->next); /* undeferred */
}

jx_t *jfn_nameBits(jx_t *args, void *agdata)
{
	int	inbits;
	int	pos, nbits;
	jx_t	*result;
	jx_t	*names;
	char	*delim;

	/* Check the args */
	if (args->first->type != JX_NUMBER
	 || !args->first->next /* undeferred */
	 || args->first->next->type != JX_ARRAY) /* undeferred */
		return NULL;

	/* Extract arguments */
	inbits = jx_int(args->first);
	names = args->first->next; /* undeferred */
	delim = NULL;
	if (args->first->next->next /* undeferred */
	 && args->first->next->next->type == JX_STRING) /* undeferred */
		delim = args->first->next->next->text; /* undeferred */

	/* If the bits value is negative, use all-1's */
	if (inbits < 0)
		inbits = ~0;

	/* Start with an empty object */
	result = jx_object();

	/* Scan the bits... */
	for (pos = 0, names = jx_first(names); names; pos += nbits, names = jx_next(names)) {
		/* Single bit? */
		if (names->type == JX_STRING) {
			nbits = 1;
			if (inbits & (1 << pos))
				jx_append(result, jx_key(names->text, jx_from_int(1 << pos)));
		} else if (names->type == JX_ARRAY && names->first) {
			/* Figure out which bits we need for this array */
			int length = jx_length(names);
			int mask, i;
			jx_t *elem;
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
			elem = jx_by_index(names, i);
			if (elem && elem->type == JX_STRING)
				jx_append(result, jx_key(elem->text, jx_from_int(inbits & (mask << pos))));
			jx_break(elem);
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
		jx_t	*member, *str;
		for (member = result->first; member; member = member->next) { /* object */
			len += strlen(member->text);
			if (member->next) /* object */
				len += delimlen;
		}

		/* Collect the names in a string */
		str = jx_string("", len);
		for (member = result->first; member; member = member->next) { /* object */
			strcat(str->text, member->text);
			if (member->next) /* object */
				strcat(str->text, delim);
		}

		/* Use the string as the result, instead of the object */
		jx_free(result);
		result = str;
	}

	return result;
}

/* Return an array of {key,value} objects, from a given object */
static jx_t *keysValuesHelper(jx_t *obj)
{
	jx_t	*array, *scan, *pair;

	/* Start with an empty array */
	array = jx_array();

	/* Add a {name,value} pair for each member of obj */
	for (scan = obj->first; scan; scan = scan->next) { /* object */
		pair = jx_object();
		jx_append(pair, jx_key("key", jx_string(scan->text, -1)));
		jx_append(pair, jx_key("value", jx_copy(scan->first)));
		jx_append(array, pair);
	}

	/* Return the array */
	return array;
}

/* Convert an object into an array of {name,value} objects.  Or if passed a
 * table instead of an object, then output a grouped array of {name,value}
 * objects.
 */
static jx_t *jfn_keysValues(jx_t *args, void *agdata)
{
	jx_t	*scan, *result;

	if (args->first->type == JX_OBJECT) {
		/* Single object is easy */
		return keysValuesHelper(args->first);
	} else if (jx_is_table(args->first)) {
		/* For a table, we want to return a grouped array -- that is,
		 * an array of arrays, where each embedded array represents a
		 * single row from the argument table.
		 */
		result = jx_array();
		for (scan = args->first->first; scan; scan = scan->next) /* undeferred */
			jx_append(result, keysValuesHelper(scan));
		return result;
	}
	return NULL;
}

/* Return a single-character substring */
static jx_t *jfn_charAt(jx_t *args, void *agdata)
{
	const char	*pos;
	size_t	size;

	/* The first argument must be a non-empty string */
	if (args->first->type != JX_STRING || !args->first->text)
		return NULL;

	/* Single number?  No subscript given? */
	if (!args->first->next || args->first->next->type == JX_NUMBER) { /* undeferred */
		/* Get the position of the character */
		size = 1;
		if (args->first->next) /* undeferred */
			pos = jx_mbs_substr(args->first->text, jx_int(args->first->next), &size); /* undeferred */
		else /* no character offset given, so use first character */
			pos = jx_mbs_substr(args->first->text, 0, &size);

		/* If end of string, return null */
		if (!*pos)
			return NULL;

		/* Return it as a string */
		return jx_string(pos, size);
	}

	return NULL;
}

/* Return the character at a given index, as a number. */
static jx_t *jfn_charCodeAt(jx_t *args, void *agdata)
{
	const char	*pos;
	wchar_t	wc;
	jx_t	*scan, *result;
	int	in;

	/* The first argument must be a string */
	if (args->first->type != JX_STRING)
		return NULL;

	/* Single number?  No subscript given? */
	if (!args->first->next || args->first->next->type == JX_NUMBER) { /* undeferred */
		/* Get the position of the character */
		if (args->first->next) /* undeferred */
			pos = jx_mbs_substr(args->first->text, jx_int(args->first->next), NULL); /* undeferred */
		else /* no character offset given, so use first character */
			pos = args->first->text;

		/* Convert the character from UTF-8 to wchar_t */
		(void)mbtowc(&wc, pos, MB_CUR_MAX);
		if (wc == 0xffff)
			wc = 0;

		/* Return it as an integer */
		return jx_from_int((int)wc);
	}

	/* Array? */
	if (args->first->next->type == JX_ARRAY) {
		/* Start with an empty array */
		result = jx_array();

		/* Scan the arg2 array for numbers. */
		for (scan = jx_first(args->first->next); scan; scan = jx_next(scan)) { /* undeferred */
			if (scan->type == JX_NUMBER) {
				/* Find the position of the character */
				pos = jx_mbs_substr(args->first->text, jx_int(scan), NULL);

				/* Convert the character from UTF-8 to wchar_t */
				(void)mbtowc(&wc, pos, MB_CUR_MAX);
				if (wc == 0xffff)
					wc = 0;

				/* Add it to the array */
				jx_append(result, jx_from_int((int)wc));
			}
		}

		/* Return the result */
		return result;
	}

	/* Boolean "true"? */
	if (args->first->next->type == JX_BOOLEAN && jx_is_true(args->first->next)) {
		/* Start with an empty array */
		result = jx_array();

		/* For each character... */
		for (pos = args->first->text; *pos; pos += in) {
			/* Convert the character from UTF-8 to wchar_t */
			in = mbtowc(&wc, pos, MB_CUR_MAX);
			if (in <= 0)
				break;
			if (wc == 0xffff)
				wc = 0;

			/* Add it to the array */
			jx_append(result, jx_from_int((int)wc));
		}

		/* Return the result */
		return result;
	}

	/* Nope.  Fail due to bad arguments */
	return NULL;
}

/* This helper function returns a number like jx_int() except that if the
 * number is 0 then it returns 0xffff.  This is handy because jxcalc uses
 * U+ffff to represent the 0 byte.
 */
static int fromCharCodeGetWC(jx_t *scan)
{
	int c = jx_int(scan);
	if (c == 0)
		return 0xffff;
	return c;
}

/* Return a string generated from character codepoints. */
static jx_t *jfn_fromCharCode(jx_t *args, void *agdata)
{
	size_t len;
	jx_t	*scan, *elem;
	char	dummy[MB_CUR_MAX];
	int	in;
	jx_t	*result;
	char	*s;

	/* Count the length.  Note that some codepoints require multiple bytes */
	for (len = 0, scan = args->first; scan; scan = scan->next) { /* undeferred */
		if (scan->type == JX_NUMBER) {
			in = wctomb(dummy, fromCharCodeGetWC(scan));
			if (in > 0)
				len += in;
		} else if (scan->type == JX_ARRAY) {
			for (elem = jx_first(scan); elem; elem = jx_next(elem)) {
				in = wctomb(dummy, fromCharCodeGetWC(elem));
				if (in > 0)
					len += in;
			}
		} else if (scan->type == JX_STRING) {
			len += strlen(scan->text);
		}
	}

	/* Allocate a big enough JX_STRING.  Note that "len" does not need
	 * to allow for the '\0' that jx_string() adds after the string.
	 */
	result = jx_string("", len);

	/* Loop through the args again, building the result */
	for (s = result->text, scan = args->first; scan; scan = scan->next) { /* undeferred */
		if (scan->type == JX_NUMBER) {
			in = wctomb(s, fromCharCodeGetWC(scan));
			if (in > 0)
				s += in;
		} else if (scan->type == JX_ARRAY) {
			for (elem = jx_first(scan); elem; elem = jx_next(elem)) {
				in = wctomb(s, fromCharCodeGetWC(elem));
				if (in > 0)
					s += in;
			}
		} else if (scan->type == JX_STRING) {
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

static jx_t *help_replace(jx_t *args, regex_t *preg, int globally)
{
	const char	*subject, *search, *replace;
	size_t		searchlen;
	int		ignorecase;
	const char	*found;
	char		*buf;
	size_t		bufsize, used;
	regmatch_t	matches[10];
	int		m, scan, chunk, in;
	jx_t		*result;

	/* Check parameters */
	if (args->first->type != JX_STRING
	 || args->first->next == NULL /* undeferred */
	 || (!preg && args->first->next->type != JX_STRING) /* undeferred */
	 || args->first->next->next == NULL /* undeferred */
	 || args->first->next->next->type != JX_STRING) /* undeferred */
		return NULL;

	/* Copy parameter strings into variables */
	subject = args->first->text;
	search = (preg ? NULL : args->first->next->text); /* undeferred */
	replace = args->first->next->next->text; /* undeferred */
	ignorecase = jx_is_true(args->first->next->next->next); /* undeferred */

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
		while ((found = jx_mbs_str(subject, search, NULL, &searchlen, 0, ignorecase)) != NULL) {
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

	/* Copy the string into a jx_t, and return it */
	result = jx_string(buf, -1);

	/* Clean up */
	free(buf);

	return result;
}

/* Replace the first instance of a substring or regular expression */
static jx_t *jfn_replace(jx_t *args, void *agdata)
{
	jxfuncextra_t *recon = (jxfuncextra_t *)agdata;
	jxcalc_t *regex = recon->regex;
	if (regex)
		return help_replace(args, regex->u.regex.preg, regex->u.regex.global);
	else
		return help_replace(args, NULL, 0);
}

/* Replace all instances of a substring or regular expression */
static jx_t *jfn_replaceAll(jx_t *args, void *agdata)
{
	jxfuncextra_t *recon = (jxfuncextra_t *)agdata;
	jxcalc_t *regex = recon->regex;
	if (regex)
		return help_replace(args, regex->u.regex.preg, 1);
	else
		return help_replace(args, NULL, 10);
}

/* This is a helper function for indexOf(), lastIndexOf(), and includes().
 * These three are similar enough to benefit from common code.  Returns -2 or
 * -3 on bad parameters, -1 if not found, or an index number otherwise.
 */
int help_indexOf(jx_t *args, int last)
{
	int	ignorecase;

	/* We need at least 2 arguments.  Third may be ignorecase flag */
	if (!args->first->next) /* undeferred */
		return -2;
	ignorecase = jx_is_true(args->first->next->next); /* undeferred */
	if (ignorecase && args->first->next->type != JX_STRING) /* undeferred */
		return -3;

	/* Array version?  String version? */
	if (args->first->type == JX_ARRAY) {
		/* Array version! */
		jx_t	*haystack = args->first;
		jx_t	*needle = args->first->next;; /* undeferred */
		jx_t	*scan;
		int	i, found;

		if (last) {
			/* Scan the entire array.  Remember the index of the
			 * most recent match.
			 */
			found = -1;
			for (i = 0, scan = jx_first(haystack); scan; i++, scan = jx_next(scan)) {
				if (ignorecase) {
					/* Case-insensitive search for a string */
					if (scan->type == JX_STRING && jx_mbs_casecmp(scan->text, needle->text) == 0)
						found = i;
				} else {
					/* Search for anything, case-sensitive */
					if (jx_equal(scan, needle))
						found = i;
				}
			}

			/* Return the last match */
			if (found != -1)
				return found;
		} else {
			/* Scan the array forward.  Much better! */
			for (i = 0, scan = jx_first(haystack); scan; i++, scan = jx_next(scan)) {

				if (ignorecase) {
					/* Case-insensitive search for a string */
					if (scan->type == JX_STRING && jx_mbs_casecmp(scan->text, needle->text) == 0) {
						jx_break(scan);
						return i;
					}
				} else {
					/* Search for anything, case-sensitive */
					if (jx_equal(scan, needle)) {
						jx_break(scan);
						return i;
					}
				}
			}
		}
	} else if (args->first->type == JX_STRING) {
		/* String version! */

		char *haystack = args->first->text;
		char *needle = args->first->next->text; /* undeferred */
		size_t	position;

		if (jx_mbs_str(haystack, needle, &position, NULL, last, ignorecase))
			return (int)position;

	} else {
		/* Trying to search in something other than an array or string */
		return -2;
	}

	/* If we get here, we didn't find it */
	return -1;
}

/* Return a boolean indicator of whether array or string contains target */
static jx_t *jfn_includes(jx_t *args, void *agdata)
{
	int	i = help_indexOf(args, 0);
	if (i == -2)
		return jx_error_null(NULL, "srch:The %s function requires an array or string, and something to search for", "includes");
	if (i == -3)
		return jx_error_null(NULL, "srchIC:The %s function's ignorecase flag only works when searching for a string", "includes");
	return jx_boolean(i >= 0);
}

/* Return a number indicating the position of the first match within an array
 * or string, or -1 if no match is found.
 */
static jx_t *jfn_indexOf(jx_t *args, void *agdata)
{
	int	i = help_indexOf(args, 0);
	if (i == -2)
		return jx_error_null(NULL, "srch:The %s function requires an array or string, and something to search for", "indexOf");
	if (i == -3)
		return jx_error_null(NULL, "srchIC:The %s function's ignorecase flag only works when searching for a string", "indexOf");
	return jx_from_int(i);
}

/* Return a number indicating the position of the last match within an array
 * or string, or -1 if no match is found.
 */
static jx_t *jfn_lastIndexOf(jx_t *args, void *agdata)
{
	int	i = help_indexOf(args, 1);
	if (i == -2)
		return jx_error_null(NULL, "srch:The %s function requires an array or string, and something to search for", "lastIndexOf");
	if (i == -3)
		return jx_error_null(NULL, "srchIC:The %s function's ignorecase flag only works when searching for a string", "lastIndexOf");
	return jx_from_int(i);
}

/* Return a boolean indicator of whether a string begins with a target */
static jx_t *jfn_startsWith(jx_t *args, void *agdata)
{
	size_t	len;
	char	*haystack, *needle;

	/* Requires two strings */
	if (args->first->type != JX_STRING
	 || !args->first->next /* undeferred */
	 || args->first->next->type != JX_STRING) { /* undeferred */
		return jx_error_null(NULL, "startsEndsWith:The %s function requires two strings", "startsWith");
	}
	haystack = args->first->text;
	needle = args->first->next->text; /* undeferred */

	/* Compare the leading part of the first string to the second */
	if (jx_is_true(args->first->next->next)) { /* undeferred */
		/* Case-insensitive version */
		len = jx_mbs_len(needle);
		return jx_boolean(jx_mbs_ncasecmp(haystack, needle, len) == 0);
	} else {
		/* Case-sensitive version */
		len = strlen(needle);
		return jx_boolean(strncmp(haystack, needle, len) == 0);
	}
}

/* Return a boolean indicator of whether a string ends with a target */
static jx_t *jfn_endsWith(jx_t *args, void *agdata)
{
	size_t	haylen, len;
	const char	*haystack, *needle;

	/* Requires two strings */
	if (args->first->type != JX_STRING
	 || !args->first->next /* undeferred */
	 || args->first->next->type != JX_STRING) { /* undeferred */
		return jx_error_null(NULL, "startsEndsWith:The %s function requires two strings", "endsWith");
	}
	haystack = args->first->text;
	needle = args->first->next->text; /* undeferred */

	/* Compare the leading part of the first string to the second */
	if (jx_is_true(args->first->next->next)) { /* undeferred */
		/* Case-insensitive version */
		haylen = jx_mbs_len(haystack);
		len = jx_mbs_len(needle);
		if (len > haylen)
			return jx_boolean(0);
		haystack = jx_mbs_substr(haystack, haylen - len, NULL);
		return jx_boolean(jx_mbs_casecmp(haystack, needle) == 0);
	} else {
		/* Case-sensitive version */
		haylen = strlen(haystack);
		len = strlen(needle);
		if (len > haylen)
			return jx_boolean(0);
		haystack += haylen - len;
		return jx_boolean(strcmp(haystack, needle) == 0);
	}
}


/* Split a string into an array of substrings */
static jx_t *jfn_split(jx_t *args, void *agdata)
{
	jxfuncextra_t *recon = (jxfuncextra_t *)agdata;
	char	*str, *next;
	jx_t	*djson;		/* delimiter, as a jx_t */
	char	*delim;		/* delimiter if djson is a JX_STRING */
	size_t	delimlen;
	regex_t *regex;
	regmatch_t matches[10];
	int	nelems, limit, all, regexmatch;
	jx_t	*result;
	wchar_t	wc;	/* found multibyte char */
	int	len;	/* length in bytes of a multibyte char */
	int	i;

	/* Check parameters */
	if (args->first->type != JX_STRING)
		return jx_error_null(NULL, "splitStr:%s() requires a string as its first parameter", "split");
	str = args->first->text;
	djson = args->first->next; /* undeferred */
	if (!djson || (jx_is_null(djson) && (!recon->regex || !(recon->regex->u.regex.preg)))) {
		/* If no delimiter (not even a regex) then return the string
		 * as the only member of an array.
		 */
		result = jx_array();
		jx_append(result, jx_string(args->first->text, -1));
		return result;
	}
	regex = NULL;
	delim = NULL;
	if (jx_is_null(djson) && agdata && ((jxcalc_t *)agdata)->u.regex.preg)
		regex = recon->regex->u.regex.preg;
	else {
		if (djson->type != JX_STRING)
			return jx_error_null(NULL, "splitDelim:%s() delimiter must be a string or regex", "split");
		delim = djson->text;
		delimlen = strlen(delim); /* yes, byte length not char count */
	}
	if (!djson->next) /* undeferred */
		limit = 0;
	else if (djson->next->type != JX_NUMBER) /* undeferred */
		return jx_error_null(NULL, "splitLimit:%s() third parameter should be a number", "split");
	else
		limit = jx_int(djson->next); /* undeferred */
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
	result = jx_array();

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
		jx_append(result, jx_string(str, len));
		nelems++;

		/* If we're using regexp, there may be subexpressions too */
		if (regex && regexmatch) {
			char *tail = next;
			for (i = 1; (limit == 0 || nelems < limit - all) && i <= 9; i++) {
				if (matches[i].rm_so >= 0) {
					jx_append(result, jx_string(str + matches[i].rm_so, matches[i].rm_eo - matches[i].rm_so));
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
		jx_append(result, jx_string(str, -1));

	/* Return it! */
	return result;
}



/* Fetch an environment variable */
static jx_t *jfn_getenv(jx_t *args, void *agdata)
{
	char	*value;

	/* If not given a string parameter, then fail */
	if (!args->first || args->first->type != JX_STRING || args->first->next) /* undeferred */
		return jx_error_null(NULL, "string:%s() expects a string parameter", "getenv");

	/* Fetch the value of the environment variable.  If no such variable
	 * exists, then get NULL.
	 */
	value = getenv(args->first->text);
	if (!value)
		return jx_null();
	return jx_string(value, -1);
}

/* Convert data to a JSON string */
static jx_t *jfn_stringify(jx_t *args, void *agdata)
{
	char	*str;
	jx_t	*result;

	/* If there are two args and the first is an empty object, then skip it
	 * on the assumption that it is the JSON object. Convert the second
	 * argument instead.
	 */
	jx_t *data = args->first;
	if (data->next && data->type == JX_OBJECT) /* undeferred */
		data = data->next; /* undeferred */

	/* Convert to string */
	str = jx_serialize(data, NULL);

	/* Stuff the string into a jx_t and return it */
	result = jx_string(str, -1);
	free(str);
	return result;
}

/* Convert JSON string to data */
static jx_t *jfn_parse(jx_t *args, void *agdata)
{
	/* If there are two args and the first is an empty object, then skip it
	 * on the assumption that it is the JSON object. Convert the second
	 * argument instead.
	 */
	jx_t *data = args->first;
	if (data->next && data->type == JX_OBJECT) /* undeferred */
		data = data->next; /* undeferred */

	/* We can only parse strings */
	if (data->type != JX_STRING)
		return jx_error_null(NULL, "string:%s() only works on strings", "parse");

	/* Parse it */
	return jx_parse_string(data->text);
}

/* Convert a string to an integer */
static jx_t *jfn_parseInt(jx_t *args, void *agdata)
{
	int	value;
	if (args->first->type == JX_STRING
	 || (args->first->type == JX_NUMBER && args->first->text[0])) {
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
	} else if (args->first->type == JX_NUMBER && args->first->text[1] == 'i')
		value = JX_INT(args->first);
	else if (args->first->type == JX_NUMBER /* text[1] == 'f' */)
		value = (int)JX_DOUBLE(args->first);
	else
		return jx_error_null(NULL, "string:%s() expects a string", "parseInt");

	return jx_from_int(value);
}

/* Convert a string to a floating point number */
static jx_t *jfn_parseFloat(jx_t *args, void *agdata)
{
	double	value;
	if (args->first->type == JX_STRING
	 || (args->first->type == JX_NUMBER && args->first->text[0]))
		value = atof(args->first->text);
	else if (args->first->type == JX_NUMBER && args->first->text[1] == 'i')
		value = (double)JX_INT(args->first);
	else if (args->first->type == JX_NUMBER /* text[1] == 'f' */)
		value = JX_DOUBLE(args->first);
	else
		return jx_error_null(NULL, "string:%s() expects a string", "parseFloat");
	return jx_from_double(value);
}


/* Do a deep search for a given value */
static jx_t *jfn_find(jx_t *args, void *agdata)
{
	jxfuncextra_t *recon = (jxfuncextra_t *)agdata;
	regex_t *regex = recon->regex ? recon->regex->u.regex.preg : NULL;
	jx_t	*haystack, *needle, *result, *other;
	char	*defaulttable, *needkey;
	int	ignorecase;

	/* If first parameter is an object or array, that's the haystack;
	 * otherwise it's the default table.
	 */
	if (args->first->type == JX_OBJECT || args->first->type == JX_ARRAY) {
		haystack = args->first;
		needle = args->first->next; /* undeferred */
		defaulttable = NULL;
	} else {
		haystack = jx_context_default_table(recon->context, &defaulttable);
		if (!haystack)
			return jx_error_null(NULL, "noDefTable:No default table");
		needle = args->first;
	}
	if (!needle)
		return jx_error_null(NULL, "find:%s() needs to know what to search for", "find");

	/* Check for optional args after "needle" */
	ignorecase = 0;
	needkey = NULL;
	for (other = needle->next; other; other = other->next) { /* undeferred */
		if (other->type == JX_BOOLEAN)
			ignorecase = jx_is_true(other);
		else if (other->type == JX_STRING && !needkey)
			needkey = other->text;
		else {
			return jx_error_null(0, "findArg:%s() was passed an unexpected extra parameter", "find");
		}
	}

	/* Search! */
	if (regex)
		result = jx_find_regex(haystack, regex, needkey);
	else if (jx_is_null(needle))
		result = jx_find(haystack, NULL, 0, needkey);
	else
		result = jx_find(haystack, needle, ignorecase, needkey);

	/* If we were searching through the default table, then prepend its
	 * expression to the "expr" members of the results.  Note that since
	 * the default table is always an array, "expr" currently begins with
	 * a subscript, not a member name, so we don't need to * add a "."
	 * between them.
	 */
	if (result->type == JX_ARRAY && defaulttable) {
		char *buf = NULL;
		char *expr;
		size_t	bufsize = 0;
		size_t	dtlen = strlen(defaulttable);
		size_t	totlen;
		for (other = jx_first(result); other; other = jx_next(other)) {
			expr = jx_text_by_key(other, "expr");
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
			jx_append(other, jx_key("expr", jx_string(buf, -1)));
		}
		if (buf)
			free(buf);

	}

	/* Return the result */
	return result;
}


static jx_t *jfn_hash(jx_t *args, void *agdata)
{
	int	hash = 0;

	if (args->first->next) {
		if (args->first->next->type != JX_NUMBER || args->first->next->next)
			return jx_error_null(NULL, "badargs:Bad arguments passed to %s()", "hash");
		hash = jx_int(args->first->next);
	}
	return jx_from_int(jx_hash(args->first, hash));
}

static jx_t *jfn_diff(jx_t *args, void *agdata)
{
	jxfuncextra_t *recon = (jxfuncextra_t *)agdata;
	jx_t	*oldjx, *newjx;
	char	*defaulttable;

	if (args->first->next) {
		oldjx = args->first;
		newjx = args->first->next;
	} else {
		oldjx = jx_context_default_table(recon->context, &defaulttable);
		newjx = args->first;
	}
	return jx_diff(oldjx, newjx);
}


static jx_t *jfn_blob(jx_t *args, void *agdata)
{
	jx_t *in = args->first;
	jx_t *scan;
	jxblobconv_t conv, conv2;
	int	i;

	/* scan for additional arguments */
	conv = conv2 = 0; /* impossible value */
	for (scan = args->first->next; scan; scan = scan->next){/* undeferred */
		if (scan->type == JX_NUMBER) {
			i = jx_int(scan);
			if (i >= JX_BLOB_BYTES && i <= JX_BLOB_ANY) {
				if (conv)
					conv2 = jx_int(scan);
				else
					conv = jx_int(scan);
			} else
				return jx_error_null(NULL, "blobNumber:Bad number passed to the %s() function", "blob");
		} else
			return jx_error_null(NULL, "arg:Bad argument passed to the %s() function", "blob");
	}

	/* conv describes the output, and defaults to JX_BLOB_BYTES unless
	 * the input is an array, in which case it defaults to JX_BLOB_STRING.
	 */
	if (!conv)
		conv = in->type == JX_STRING ? JX_BLOB_BYTES : JX_BLOB_STRING;

	/* conv2 describes the input.  It is ignored unless the input is a
	 * string, and it defaults to JX_BLOB_UTF8.
	 */
	if (!conv2)
		conv2 = JX_BLOB_UTF8;

	/* Do it.  The real guts are in blob.c */
	return jx_blob(in, conv, conv2);
}

/******************************************************************************/
/* Date/time functions. The real guts are in datetime.c */


/* Return an ISO date string */
static jx_t *jfn_date(jx_t *args, void *agdata)
{
	return jx_datetime_fn(args, "date");
}

/* Return an ISO time string, possibly tweaking the time zone */
static jx_t *jfn_time(jx_t *args, void *agdata)
{
	return jx_datetime_fn(args, "time");
}

/* Return an ISO dateTime string, possibly tweaking the time zone */
static jx_t *jfn_dateTime(jx_t *args, void *agdata)
{
	return jx_datetime_fn(args, "datetime");
}

/* Extract the time zone from an ISO time or dateTime */
static jx_t *jfn_timeZone(jx_t *args, void *agdata)
{
	return NULL;
}

/* Convert ISO period between string and number. */
static jx_t *jfn_period(jx_t *args, void *agdata)
{
	return jx_datetime_fn(args, "period");
}

/******************************************************************************/
/* Math functions.  For the sake of JavaScript compatibility, the first
 * argument to these may optionally be the "Math" object, which is ignored.
 */

static jx_t *jfn_abs(jx_t *args, void *agdata)
{
	double d;

	/* Get the number, skipping an optional "Math" argument */
	jx_t	*num = args->first;
	if (num->type == JX_OBJECT)
		num = num->next; /* undeferred */

	/* Fail if not a number */
	if (num->type != JX_NUMBER)
		return jx_error_null(NULL, "number:The %s() function expects a number", "abs");

	/* Apply the function */
	d = jx_double(num);
	if (d < 0)
		d = -d;

	/* Return the result */
	return jx_from_double(d);
}

/* Random number */
static jx_t *jfn_random(jx_t *args, void *agdata)
{
	/* Look for an optional limit, after an optional "Math" argument */
	int	limit;
	jx_t	*num = args->first;
	if (num->type == JX_OBJECT)
		num = num->next; /* undeferred */
	if (num && num->type == JX_NUMBER && (limit = jx_int(num)) >= 2) {
		/* Return an int in the range [0,limit-1] */
		return jx_from_int((int)lrand48() % limit);
	} else {
		/* Return a double in the range [0.0,1.0) */
		return jx_from_double(drand48());
	}
}

/* Sign */
static jx_t *jfn_sign(jx_t *args, void *agdata)
{
	double d;
	int	sign;

	/* Get the number, skipping an optional "Math" argument */
	jx_t	*num = args->first;
	if (num->type == JX_OBJECT)
		num = num->next; /* undeferred */

	/* Fail if not a number */
	if (num->type != JX_NUMBER)
		return jx_error_null(NULL, "number:The %s() function expects a number", "sign");

	/* Apply the function */
	d = jx_double(num);
	if (d < 0)
		sign = -1;
	else if (d > 0)
		sign = 1;
	else	
		sign = 0;

	/* Return the result */
	return jx_from_int(sign);
}


static jx_t *jfn_wrap(jx_t *args, void *agdata)
{
	char	*str;
	int	width;
	size_t	len;
	jx_t	*result;

	/* Check args */
	if (args->first->type != JX_STRING)
		return jx_error_null(NULL, "wrapStr:The %s() function's first argument should be a string to wrap", "wrap");
	str = args->first->text;
	if (args->first->next && args->first->next->type != JX_NUMBER) /* undeferred */
		return jx_error_null(NULL, "wrapWidth:The %s() function's second argument should be wrap width", "wrap");
	if (args->first->next) /* undeferred */
		width = jx_int(args->first->next); /* undeferred */
	else
		width = 0;

	/* Passing width=0 uses the default width */
	if (width == 0)
		width = 80; /* !!! should probably be a setting */

	/* Positive means word wrap, negative means character wrap */
	if (width < 0)
		len = jx_mbs_wrap_char(NULL, str, -width);
	else
		len = jx_mbs_wrap_word(NULL, str, width);
	result = jx_string("", len);
	if (width < 0)
		len = jx_mbs_wrap_char(result->text, str, -width);
	else
		len = jx_mbs_wrap_word(result->text, str, width);
	return result;
}

static jx_t *jfn_sleep(jx_t *args, void *agdata)
{
	struct timespec ts;
	double	seconds;

	/* Get the sleep duration */
	if (args->first->type == JX_NUMBER) {
		seconds = jx_double(args->first);
	} else if (jx_is_period(args->first)) {
		jx_t *jseconds;
		jx_t p, *oldnext;

		/* Build argument array containing args->first and "s". */
		memset(&p, 0, sizeof p);
		p.type = JX_STRING;
		p.text[0] = 's';
		oldnext = args->first->next; /* undeferred */
		args->first->next = &p; /* undeferred */

		/* Pass that into jx_datetime_fn() to get seconds. */
		jseconds = jx_datetime_fn(args, "period");
		if (jx_is_error(jseconds)) {
			args->first->next = oldnext; /* undeferred */
			return jseconds;
		}
		seconds = jx_double(jseconds);

		/* Clean up */
		args->first->next = oldnext; /* undeferred */
		jx_free(jseconds);
	} else {
		return jx_error_null(NULL, "sleep:The %s() function should be passed a number of seconds on an ISO-8601 period string");
	}

	/* Sanity check */
	if (seconds < 0.0)
		return jx_error_null(NULL, "sleepSign:The %s() function's sleep time must be positive", "sleep");

	/* Sleep */
	ts.tv_sec = (time_t)seconds;
	ts.tv_nsec = 1000000000 * (seconds - ts.tv_sec);
	nanosleep(&ts, NULL);

	/* Return null */
	return jx_null();
}

/* Write data to a file in JSON format */
static jx_t *jfn_writeJSON(jx_t *args, void *agdata)
{
	jx_t	*data, *nocomma;
	char	*filename;
	jxformat_t tweaked;
	FILE	*fp;

	/* Check the args */
	data = args->first;
	if (!args->first->next || args->first->next->type != JX_STRING)
		return jx_error_null(NULL, "needFileName:The %s() function's second parameter must be a file name", "writeJSON");
	filename = args->first->next->text;

	/* Open the file */
	fp = fopen(filename, "w");
	if (!fp)
		return jx_error_null(NULL, "writeFile:Can't open %s for writing", filename);

	/* Tweak the output format */
	tweaked = jx_format_default;
	tweaked.color = 0;
	tweaked.graphic = 0;
	tweaked.pretty = 1;
	tweaked.tab = 2;
	tweaked.oneline = 0;
	tweaked.elem = 0;
	strcpy(tweaked.table, "json");
	tweaked.fp = fp;

	/* Write the data.  Temporarily set ->next to NULL so we don't get
	 * an extra comma on the end.
	 */
	nocomma = data->next;
	data->next = NULL;
	jx_print(data, &tweaked);
	data->next = nocomma;

	/* close the file */
	fclose(fp);

	/* Return true */
	return jx_boolean(1);
}


/**************************************************************************
 * The following are aggregate functions.  These are implemented as pairs *
 * of C functions -- jag_xxxx() to accumulate data from each row, and     *
 * jfn_xxx() to return the final result.                                  *
 **************************************************************************/

/* count(arg) count non-null and non-false values */
static jx_t *jfn_count(jx_t *args, void *agdata)
{
	return jx_from_int(*(int *)agdata);
}
static void jag_count(jx_t *args, void *agdata)
{
	if (jx_is_null(args->first))
		return;
	if (args->first->type == JX_BOOLEAN && !jx_is_true(args->first))
		return;
	(*(int *)agdata)++;
}

/* rowNumber(arg) returns a different value for each element in the group */
static jx_t *jfn_rowNumber(jx_t *args, void *agdata)
{
	int *counter = (int *)agdata;
	int tmp;
	char	buf[20], *p, base;

	/* First arg defines the counting style.  If it is null or false then
	 * no item is returned and the count isn't incremented.
	 */
	if (args->first->type == JX_NULL
	 || (args->first->type == JX_BOOLEAN && !jx_is_true(args->first)))
		return NULL;

	/* If it is a number, then add that to the counter */
	if (args->first->type == JX_NUMBER)
		return jx_from_int(jx_int(args->first) + (*counter)++);

	/* If it is 'a' or "A" then use upper or lowercase ASCII letters */
	if (args->first->type == JX_STRING) {
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
			return jx_string(p, -1);

		/* Maybe put roman numerals here some day? case 'i'/'I'
		 * ivxlcdm: i,ii,iii,iv,v,vi,vii,viii,ix
		 */
		}
	}

	/* As a last resort, just return it as a 1-based number. */
	return jx_from_int(1 + (*counter)++);
}
static void jag_rowNumber(jx_t *args, void *agdata)
{
}

/* min(arg) returns the minimum value */
static jx_t *jfn_min(jx_t *args, void *agdata)
{
	agmaxdata_t *data = (agmaxdata_t *)agdata;

	/* NOTE: data->json and data->sval, if used, will be automatically freed */
	if (data->count == 0)
		return jx_null();
	if (data->json)
		return jx_copy(data->json);
	if (data->sval)
		return jx_string(data->sval, -1);
	return jx_from_double(data->dval);

}
static void jag_min(jx_t *args, void *agdata)
{
	agmaxdata_t *data = (agmaxdata_t *)agdata;

	/* If this is a number and we're comparing numbers ... */
	if (args->first->type == JX_NUMBER && !data->sval) {
		/* If this is first, or less than previous, use it */
		double d = jx_double(args->first);
		if (data->count == 0 || d < data->dval) {
			data->dval = d;
			if (data->json) {
				jx_free(data->json);
				data->json = NULL;
			}
			if (args->first->next) /* undeferred */
				data->json = jx_copy(args->first->next); /* undeferred */
		}
		data->count++;
	} else if (args->first->type == JX_STRING) {
		if (!data->sval || jx_mbs_casecmp(args->first->text, data->sval) < 0) {
			if (data->sval)
				free(data->sval);
			data->sval = strdup(args->first->text);
			if (data->json) {
				jx_free(data->json);
				data->json = NULL;
			}
			if (args->first->next) /* undeferred */
				data->json = jx_copy(args->first->next); /* undeferred */
		}
		data->count++;
	}
}

/* max(arg) returns the maximum value */
static jx_t *jfn_max(jx_t *args, void *agdata)
{
	agmaxdata_t *data = (agmaxdata_t *)agdata;

	/* NOTE: data->json and data->sval, if used, will be automatically freed */
	if (data->count == 0)
		return jx_null();
	if (data->json)
		return jx_copy(data->json);
	if (data->sval)
		return jx_string(data->sval, -1);
	return jx_from_double(data->dval);

}
static void jag_max(jx_t *args, void *agdata)
{
	agmaxdata_t *data = (agmaxdata_t *)agdata;

	/* If this is a number, and we're comparing numbers... */
	if (args->first->type == JX_NUMBER && !data->sval) {
		double d = jx_double(args->first);

		/* If this is first, or more than previous max, use it*/
		if (data->count == 0 || d > data->dval) {
			data->dval = d;
			if (data->json) {
				jx_free(data->json);
				data->json = NULL;
			}
			if (args->first->next) /* undeferred */
				data->json = jx_copy(args->first->next); /* undeferred */
		}
		data->count++;
	} else if (args->first->type == JX_STRING) {
		if (!data->sval || jx_mbs_casecmp(args->first->text, data->sval) > 0) {
			if (data->sval)
				free(data->sval);
			data->sval = strdup(args->first->text);
			if (data->json) {
				jx_free(data->json);
				data->json = NULL;
			}
			if (args->first->next) /* undeferred */
				data->json = jx_copy(args->first->next); /* undeferred */
		}
		data->count++;
	}
}

/* avg(arg) returns the average value of arg */
static jx_t *jfn_avg(jx_t *args, void *agdata)
{
	agdata_t *data = (agdata_t *)agdata;

	if (data->count == 0)
		return jx_null();
	return jx_from_double(data->val / (double)data->count);
}
static void jag_avg(jx_t *args, void *agdata)
{
	agdata_t *data = (agdata_t *)agdata;

	if (args->first->type == JX_NUMBER) {
		double d = jx_double(args->first);
		if (data->count == 0)
			data->val = d;
		else
			data->val += d;
		data->count++;
	}
}

/* sum(arg) returns the sum of arg */
static jx_t *jfn_sum(jx_t *args, void *agdata)
{
	agdata_t *data = (agdata_t *)agdata;

	if (data->count == 0)
		return jx_from_int(0);
	return jx_from_double(data->val);
}
static void jag_sum(jx_t *args, void *agdata)
{
	agdata_t *data = (agdata_t *)agdata;

	if (args->first->type == JX_NUMBER) {
		double d = jx_double(args->first);
		if (data->count == 0)
			data->val = d;
		else
			data->val += d;
		data->count++;
	}
}

/* product(arg) returns the product of arg */
static jx_t *jfn_product(jx_t *args, void *agdata)
{
	agdata_t *data = (agdata_t *)agdata;

	if (data->count == 0)
		return jx_from_int(1);
	return jx_from_double(data->val);
}
static void jag_product(jx_t *args, void *agdata)
{
	agdata_t *data = (agdata_t *)agdata;

	if (args->first->type == JX_NUMBER) {
		double d = jx_double(args->first);
		if (data->count == 0)
			data->val = d;
		else
			data->val *= d;
		data->count++;
	}
}

/* any(arg) returns true if any row's arg is true */
static jx_t *jfn_any(jx_t *args, void *agdata)
{
	int i = *(int *)agdata;
	return jx_boolean(i);
}
static void jag_any(jx_t *args, void *agdata)
{
	int *refi = (int *)agdata;
	*refi |= jx_is_true(args->first);
}

/* all(arg) returns true if all of row's arg is true */
static jx_t *jfn_all(jx_t *args, void *agdata)
{
	int i = *(int *)agdata;
	return jx_boolean(!i);
}
static void jag_all(jx_t *args, void *agdata)
{
	int *refi = (int *)agdata;

	*refi |= !jx_is_true(args->first);
}


/* Return column statistics about a table (array of objects) */
static jx_t *jfn_explain(jx_t *args, void *agdata)
{
	jx_t *stats = *(jx_t **)agdata;

	/* Don't free the memory -- we're returning it */
	*(jx_t **)agdata = NULL;

	if (!stats)
		stats = jx_null();
	return stats;
}

static void jag_explain(jx_t *args, void *agdata)
{
	jx_t *stats = *(jx_t **)agdata;
	int depth = 0;

	/* If second parameter is given and is true, then recursively explain
	 * any embedded objects or arrays of objects.
	 */
	if (args->first->next && jx_is_true(args->first->next)) /* undeferred */
		depth = -1;
	stats = jx_explain(stats, args->first, depth);

	*(jx_t **)agdata = stats;
}



/* Write an array out to a file */
static jx_t *jfn_writeArray(jx_t *args, void *agdata)
{
	FILE *fp = *(FILE **)agdata;
	long int size;
	if (fp) {
		fputs("\n]\n", fp);
		size = ftell(fp);
		if (fp != stdout)
			fclose(fp);
		*(FILE **)agdata = NULL;
		return jx_from_int((int)size);
	}
	return jx_from_int(0);
}

static void jag_writeArray(jx_t *args, void *agdata)
{
	FILE *fp = *(FILE **)agdata;
	jx_t	*item;
	char    *ser;

	/* For "null" or "false", do nothing.  For "true" we'd *like* to
	 * substitute "this" but unfortunately we don't have access to the
	 * context.
	 */
	item = args->first;
	if (item->type == JX_BOOLEAN) {
		if (!jx_is_true(item))
			return;
	}
	if (!fp) {
		if (args->first->next && args->first->next->type == JX_STRING) /* undeferred */
			fp = fopen(args->first->next->text, "w"); /* undeferred */
		else
			fp = stdout;
		*(FILE **)agdata = fp;
		fputs("[\n  ", fp);
	} else {
		fputs(",\n  ", fp);
	}

	/* Write this item */
	ser = jx_serialize(item, NULL);
	fwrite(ser, strlen(ser), 1, fp);
	free(ser);
}

/* Collect non-null items in an array */
static jx_t *jfn_arrayAgg(jx_t *args, void *agdata)
{
	jx_t *result = *(jx_t **)agdata;
	if (!result)
		return jx_array();
	return jx_copy(result);
}
static void  jag_arrayAgg(jx_t *args, void *agdata)
{
	jx_t *result = *(jx_t **)agdata;
	if (!result)
		result = jx_array();
	if (!jx_is_null(args->first))
		jx_append(result, jx_copy(args->first));
	*(jx_t **)agdata = result;
}


/* objectAgg(key,value) Collect key/value pairs into an object. */
static jx_t *jfn_objectAgg(jx_t *args, void *agdata)
{
	jx_t *result = *(jx_t **)agdata;
	if (!result)
		return jx_object();
	return jx_copy(result);
}
static void  jag_objectAgg(jx_t *args, void *agdata)
{
	jx_t *result = *(jx_t **)agdata;
	if (!result)
		result = jx_object();
	if (args->first->type == JX_STRING && *args->first->text && args->first->next) /* undeferred */
		jx_append(result, jx_key(args->first->text, jx_copy(args->first->next))); /* undeferred */
	*(jx_t **)agdata = result;
}

/* join(str, delim) Concatenate a series of strings into a single big string.
 * The delim is optional and defaults to ",".
 */
static jx_t *jfn_join(jx_t *args, void *agdata)
{
	agjoindata_t *data = (agjoindata_t *)agdata;

	/* Return the accumulated string.  If no string, return "" */
	if (data->ag)
		return jx_string(data->ag, -1);
	else
		return jx_string("", 0);
}
static void  jag_join(jx_t *args, void *agdata)
{
	char	*text, *mustfree, *delim;
	char	buf[40];
	agjoindata_t *data = (agjoindata_t *)agdata;
	size_t	newlen;

	/* Get the text.  Skip null, but get text for anything else */
	mustfree = NULL;
	switch (args->first->type) {
	case JX_NULL:
		return;
	case JX_STRING:
	case JX_BOOLEAN:
		text = args->first->text;
		break;
	case JX_NUMBER:
		if (*args->first->text)
			text = args->first->text; /* number in text format */
		else {
			if (args->first->text[1] == 'i')
				snprintf(buf, sizeof buf, "%i", JX_INT(args->first));
			else
				snprintf(buf, sizeof buf, "%g", JX_DOUBLE(args->first));
			text = buf;
		}
		break;
	default:
		text = mustfree = jx_serialize(args->first, NULL);
	}

	/* First string? */
	if (data->ag == NULL) {
		/* The first call always stores a string unchanged */
		data->size = (strlen(text) | 0xff) + 1;
		data->ag = (char *)malloc(data->size);
		strcpy(data->ag, text);
	} else {
		/* Get the delimiter, default to "," */
		if (args->first->next && args->first->next->type == JX_STRING) /* undeferred */
			delim = args->first->next->text; /* undeferred */
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
