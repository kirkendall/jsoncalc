#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define _XOPEN_SOURCE
#define __USE_XOPEN
#include <wchar.h>
#include "json.h"
#include "calc.h"

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

/* Forward declarations of the built-in non-aggregate functions */
static json_t *jfn_toUpperCase(json_t *args, void *agdata);
static json_t *jfn_toLowerCase(json_t *args, void *agdata);
static json_t *jfn_toMixedCase(json_t *args, void *agdata);
static json_t *jfn_substr(json_t *args, void *agdata);
static json_t *jfn_hex(json_t *args, void *agdata);
static json_t *jfn_toString(json_t *args, void *agdata);
static json_t *jfn_typeOf(json_t *args, void *agdata);
static json_t *jfn_sizeOf(json_t *args, void *agdata);
static json_t *jfn_width(json_t *args, void *agdata);
static json_t *jfn_keys(json_t *args, void *agdata);
static json_t *jfn_join(json_t *args, void *agdata);
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
static json_t *jfn_charCodeAt(json_t *args, void *agdata);
static json_t *jfn_fromCharCode(json_t *args, void *agdata);

/* Forward declarations of the built-in aggregate functions */
static json_t *jfn_count(json_t *args, void *agdata);
static void    jag_count(json_t *args, void *agdata);
static json_t *jfn_index(json_t *args, void *agdata);
static void    jag_index(json_t *args, void *agdata);
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

/* A linked list of the built-in functions */
static jsonfunc_t toUpperCase_jf = {NULL,            "toUpperCase", jfn_toUpperCase};
static jsonfunc_t toLowerCase_jf = {&toUpperCase_jf, "toLowerCase", jfn_toLowerCase};
static jsonfunc_t toMixedCase_jf = {&toLowerCase_jf, "toMixedCase", jfn_toMixedCase};
static jsonfunc_t substr_jf      = {&toMixedCase_jf, "substr",      jfn_substr};
static jsonfunc_t hex_jf         = {&substr_jf,      "hex",         jfn_hex};
static jsonfunc_t toString_jf    = {&hex_jf,         "toString",    jfn_toString};
static jsonfunc_t String_jf      = {&toString_jf,    "String",      jfn_toString};
static jsonfunc_t typeOf_jf      = {&String_jf,      "typeOf",      jfn_typeOf};
static jsonfunc_t sizeOf_jf      = {&typeOf_jf,      "sizeOf",      jfn_sizeOf};
static jsonfunc_t width_jf       = {&sizeOf_jf,      "width",       jfn_width};
static jsonfunc_t keys_jf        = {&width_jf,       "keys",        jfn_keys};
static jsonfunc_t join_jf        = {&keys_jf,        "join",        jfn_join};
static jsonfunc_t orderBy_jf     = {&join_jf,        "orderBy",     jfn_orderBy};
static jsonfunc_t groupBy_jf     = {&orderBy_jf,     "groupBy",     jfn_groupBy};
static jsonfunc_t flat_jf        = {&groupBy_jf,     "flat",        jfn_flat};
static jsonfunc_t slice_jf       = {&flat_jf,        "slice",       jfn_slice};
static jsonfunc_t repeat_jf      = {&slice_jf,       "repeat",      jfn_repeat};
static jsonfunc_t toFixed_jf     = {&repeat_jf,      "toFixed",     jfn_toFixed};
static jsonfunc_t distinct_jf    = {&toFixed_jf,     "distinct",    jfn_distinct};
static jsonfunc_t unroll_jf      = {&distinct_jf,    "unroll",      jfn_unroll};
static jsonfunc_t nameBits_jf    = {&unroll_jf,      "nameBits",    jfn_nameBits};
static jsonfunc_t keysValues_jf  = {&nameBits_jf,    "keysValues",  jfn_keysValues};
static jsonfunc_t charCodeAt_jf  = {&keysValues_jf,  "charCodeAt",  jfn_charCodeAt};
static jsonfunc_t fromCharCode_jf= {&charCodeAt_jf,  "fromCharCode",jfn_fromCharCode};
static jsonfunc_t count_jf       = {&fromCharCode_jf,"count",       jfn_count, jag_count, sizeof(long)};
static jsonfunc_t index_jf       = {&count_jf,       "index",       jfn_index, jag_index, sizeof(int)};
static jsonfunc_t min_jf         = {&index_jf,       "min",         jfn_min,   jag_min, sizeof(agmaxdata_t), JSONFUNC_JSONFREE | JSONFUNC_FREE};
static jsonfunc_t max_jf         = {&min_jf,         "max",         jfn_max,   jag_max, sizeof(agmaxdata_t), JSONFUNC_JSONFREE | JSONFUNC_FREE};
static jsonfunc_t avg_jf         = {&max_jf,         "avg",         jfn_avg,   jag_avg, sizeof(agdata_t)};
static jsonfunc_t sum_jf         = {&avg_jf,         "sum",         jfn_sum,   jag_sum, sizeof(agdata_t)};
static jsonfunc_t product_jf     = {&sum_jf,         "product",     jfn_product,jag_product, sizeof(agdata_t)};
static jsonfunc_t any_jf         = {&product_jf,     "any",         jfn_any,   jag_any, sizeof(int)};
static jsonfunc_t all_jf         = {&any_jf,         "all",         jfn_all,   jag_all, sizeof(int)};
static jsonfunc_t explain_jf     = {&all_jf,         "explain",     jfn_explain,jag_explain, sizeof(json_t *), JSONFUNC_JSONFREE};
static jsonfunc_t writeArray_jf  = {&explain_jf,     "writeArray",  jfn_writeArray,jag_writeArray, sizeof(FILE *)};
static jsonfunc_t arrayAgg_jf    = {&writeArray_jf,  "arrayAgg",    jfn_arrayAgg,jag_arrayAgg, sizeof(json_t *), JSONFUNC_JSONFREE};
static jsonfunc_t objectAgg_jf   = {&arrayAgg_jf,    "objectAgg",   jfn_objectAgg,jag_objectAgg, sizeof(json_t *), JSONFUNC_JSONFREE};
static jsonfunc_t *funclist      = &objectAgg_jf;


/* Register a function that can be called via json_calc().  The function
 * should look like...
 *
 *    json_t *myFunction(json_t *args, void *agdata).
 *
 * ... where "args" is a JSON_ARRAY of the actual parameter values; if invoked as
 * a member function then "this" is the first parameter.  Your function should
 * return newly allocated json_t data, or NULL to represent the JSON "null" symbol.
 * In particular, it should *NOT* attempt to reuse the argument data in the response.
 * The json_copy() function is your friend!  Upon return, json_calc() will free the
 * parameters immediately and the returned value eventually.
 *
 * The agfn and agsize parameters are only for aggregate functions.  They
 * should be NULL and 0 for non-aggregate functions.  For aggregate functions,
 * agfn is a function that will be called for each item (row or array element)
 * and agsize is the amount of storage space it needs to accumulate results
 * typically sizeof(int) or something like that.  The agdata starts out all
 * zeroes.  The idea is that agfn() will accumulate data, and fn() will return
 * the final result.
 */
void json_calc_function(
	char    *name,
	json_t *(*fn)(json_t *args, void *agdata),
	void   (*agfn)(json_t *args, void *agdata),
	size_t  agsize)
{
	jsonfunc_t *f;

	/* Round agsize up to a multiple of 8 bytes */
	if (agsize > 0)
		agsize = ((agsize - 1) | 0x7) + 1;

	/* If it's already in the table then update it */
	for (f = funclist; f; f = f->next) {
		if (!strcmp(f->name, name)) {
			f->fn = fn;
			f->agfn = agfn;
			f->agsize = agsize;
			return;
		}
	}

	/* Add it */
	f = (jsonfunc_t *)malloc(sizeof(jsonfunc_t));
	f->name = name;
	f->fn = fn;
	f->agfn = agfn;
	f->agsize = agsize;
	f->next = funclist;
	funclist = f;
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

	/* Try abbreviation */
	for (scan = funclist; scan; scan = scan->next) {
		if (!json_mbs_abbrcmp(name, scan->name))
			return scan;
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
		return json_symbol("null", -1);
	str = args->first->text;

	/* Get the length of the string.  We'll need that to adjust bounds */
	len = json_mbs_len(str);

	/* Get the starting position */
	if (args->first->next->type != JSON_NUMBER)
		return json_symbol("null", -1);
	start = json_int(args->first->next);
	if (start < 0 && start + len >= 0)
		start = len + start;
	else if (start < 0 || start > len)
		start = len;

	/* Get the length limit */
	if (!args->first->next->next)
		limit = len - start; /* all the way to the end */
	else if (args->first->next->next->type != JSON_NUMBER)
		return json_symbol("null", -1);
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
	long    n;

	if (args->first->type == JSON_STRING) {
		/* Allocate a big enough string */
		str = args->first->text;
		result = json_string("", strlen(str) * 2);

		/* Convert each byte of the string to hex */
		for (len = 0; str[len]; len++)
			sprintf(&result->text[2 * len], "%02x", str[len] & 0xff);

		/* Return that */
		return result;
	} else if (args->first->type == JSON_NUMBER) {
		/* Get the args */
		n = json_int(args->first);
		len = 0;
		if (args->first->next && args->first->next->type == JSON_NUMBER)
			len = json_int(args->first->next);
		if (len > sizeof(n) * 2)
			len = sizeof(n) * 2;

		/* Allocate the return buffer -- probably bigger than we need */
		result = json_string("", sizeof(n) * 2);

		/* Fill it */
		sprintf(result->text, "%0*lx", len, n);

		return result;
	}
	return json_symbol("null", -1);
}

/* toString(arg) converts arg to a string */
static json_t *jfn_toString(json_t *args, void *agdata)
{
	char    *tmpstr;
	json_t  *tmp;

	/* If already a string, return a copy of it as-is */
	if (args->first->type == JSON_STRING)
		return json_copy(args->first);

	/* If symbol or non-binary number, convert its text to a string. */
	if (args->first->type == JSON_NUMBER
	 || (args->first->type == JSON_SYMBOL && args->first->text[0] != '\0'))
		return json_string(args->first->text, -1);

	/* For anything else, use json_serialize() */
	tmpstr = json_serialize(args->first, 0);
	tmp = json_string(tmpstr, -1);
	free(tmpstr);
	return tmp;
}

/* typeOf(data) returns a string identifying the data's type */
static json_t *jfn_typeOf(json_t *args, void *agdata)
{
	return json_string(json_typeof(args->first), -1);
}

/* Estimate the memory usage of a json_t datum */
static json_t *jfn_sizeOf(json_t *args, void *agdata)
{
	return json_from_int(json_sizeof(args->first));
}

/* Estimate the width of a string.  Some characters may be wider than others,
 * even in a fixed-pitch font.
 */
static json_t *jfn_width(json_t *args, void *agdata)
{
	switch (args->first->type) {
	case JSON_STRING:
	case JSON_NUMBER:
	case JSON_SYMBOL:
		return json_from_int(json_mbs_width(json_text(args->first)));
	default:
		return NULL;
	}
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

/* join(arr, delim) returns a string combining values.  The delim parameter
 * is optional, and defaults to ",".
 */
static json_t *jfn_join(json_t *args, void *agdata)
{
	json_t  *scan;
	size_t  len, delimlen;
	char    *delim;
	json_t  *result;

	/* If first parameter isn't an array, return null */
	if (args->first->type != JSON_ARRAY)
		return json_symbol("null", -1);

	/* Get the delimiter */
	if (args->first->next && args->first->next->type == JSON_STRING)
		delim = args->first->next->text;
	else
		delim = ",";
	delimlen = strlen(delim);

	/* Compute the size of the combined string */
	for (len = 0, scan = args->first->first; scan; scan = scan->next) {
		if (scan->type == JSON_STRING) {
			if (len != 0)
				len += delimlen;
			len += strlen(scan->text);
		}
	}

	/* Allocate a string */
	result = json_simple("", len, JSON_STRING);

	/* Loop over the array again, appending strings */
	for (scan = args->first->first; scan; scan = scan->next) {
		if (scan->type == JSON_STRING) {
			if (result->text[0])
				strcat(result->text, delim);
			strcat(result->text, scan->text);
		}
	}

	/* Return the result */
	return result;
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
		return json_symbol("null", -1);

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

/* slice(arr, start, end) - return part of an array */
json_t *jfn_slice(json_t *args, void *agdata)
{
	int	start, end;
	int	length = -1;
	json_t	*result, *scan;

	/* If first param isn't an array, then return null */
	if (args->first->type != JSON_ARRAY)
		return NULL;

	/* Get the start and end parameters */
	if (!args->first->next || args->first->next->type != JSON_NUMBER) {
		/* No endpoints, why bother? */
		start = 0, end = json_length(args->first); /* the whole array */
	} else {
		/* We at least have a start */
		start = json_int(args->first->next);
		if (start < 0) {
			length = json_length(args->first);
			start = length + start;
			if (start < 0)
				start = 0;
		}

		/* Do we also have an end? */
		if (!args->first->next->next || args->first->next->next->type != JSON_NUMBER) {
			end = json_length(args->first);
		} else {
			end = json_int(args->first->next->next);
			if (end < 0) {
				if (length < 0)
					length = json_length(args->first);
				end = length + end;
			}
			if (end < start)
				end = start;
		}
	}

	/* Copy the slice to a new array */
	result = json_array();
	for (scan = json_by_index(args->first, start); scan && start < end; start++, scan = scan->next) {
		json_append(result, json_copy(scan));
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
	sprintf(buf, "%.*f", digits, num);
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
		if (fieldlist->type == JSON_SYMBOL && json_is_true(fieldlist)) {
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
		/* Strict!  We want to compare each prospectiveelement against
		 * all elemends currently in the result, and add only if new.
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
	if (json_is_true(args->first))
		(*(int *)agdata)++;
}

/* index(arg) returns a different value for each element in the group */
static json_t *jfn_index(json_t *args, void *agdata)
{
	int *counter = (int *)agdata;
	int tmp;
	char	buf[20], *p, base;

	/* First arg defines the counting style.  If it is null or false then
	 * no item is returned and the count isn't incremented.
	 */
	if (args->first->type == JSON_SYMBOL && args->first->text[0] != 't')
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
static void jag_index(json_t *args, void *agdata)
{
}

/* min(arg) returns the minimum value */
static json_t *jfn_min(json_t *args, void *agdata)
{
	agmaxdata_t *data = (agmaxdata_t *)agdata;

	/* NOTE: data->json and data->sval, if used, will be automatically freed */
	if (data->count == 0)
		return json_symbol("null", -1);
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
		return json_symbol("null", -1);
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
		return json_symbol("null", -1);
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
	return json_symbol(i ? "true" : "false", -1);
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
	return json_symbol(i ? "false" : "true", -1);
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
	if (!stats)
		stats = json_symbol("null", -1);
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
	return json_symbol("null", -1);
}

static void jag_writeArray(json_t *args, void *agdata)
{
	FILE *fp = *(FILE **)agdata;
	json_t	*item;
	char    *ser;

	/* For "null" or "false", do nothing.  For "true" we'd *like* to substitute
	 * "this" but unfortunately we don't have access to the context.
	 */
	item = args->first;
	if (item->type == JSON_SYMBOL) {
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

