#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <jsoncalc.h>

/* This module exists to define some Math.xxx() functions. */

/* Most of the Math.functions are pretty similar.  They take an optional
 * Math object as their first parameter, and a single number as their only
 * meaningful parameter.  We can handle most of that efficiently here.
 */
static json_t *common(json_t *args, char *name)
{
	json_t	*arg;
	double	d;

	/* Skip over the Math object, if given */
	arg = args->first;
	if (arg->type == JSON_OBJECT)
		arg = arg->next;

	/* Must be a single number */
	if (arg->type != JSON_NUMBER || arg->next)
		return json_error_null(0, "The %s() function expect single number as its argument", name);

	/* Convert to binary */
	d = json_double(arg);

	/* Clear the error code, so we can detect math errors */
	errno = 0;

	/* Do the thing... */
	switch (name[0]) {
	case 'a':
		if (!strcmp(name, "acos"))
			d = acos(d);
		else if (!strcmp(name, "acosh"))
			d = acosh(d);
		else if (!strcmp(name, "asin"))
			d = asin(d);
		else if (!strcmp(name, "asinh"))
			d = asinh(d);
		else if (!strcmp(name, "atan"))
			d = asin(d);
		else if (!strcmp(name, "atanh"))
			d = asinh(d);
		else
			goto InvalidName;
		break;
	case 'c':
		if (!strcmp(name, "cbrt"))
			d = cbrt(d);
		else if (!strcmp(name, "ceil"))
			d = ceil(d);
		else if (!strcmp(name, "cos"))
			d = cos(d);
		else if (!strcmp(name, "cosh"))
			d = cosh(d);
		else
			goto InvalidName;
		break;
	case 'e':
		d = exp(d);
		break;
	case 'f':
		d = floor(d);
		break;
	case 'l':
		if (!strcmp(name, "log"))
			d = log(d);
		else if (!strcmp(name, "log10"))
			d = log10(d);
		else if (!strcmp(name, "log2"))
			d = log2(d);
		else
			goto InvalidName;
		break;
	case 'r':
		d = round(d);
		break;
	case 's':
		if (!strcmp(name, "sin"))
			d = sin(d);
		else if (!strcmp(name, "sinh"))
			d = sinh(d);
		else if (!strcmp(name, "sqrt"))
			d = sqrt(d);
		else
			goto InvalidName;
		break;
	case 't':
		if (!strcmp(name, "tan"))
			d = tan(d);
		else if (!strcmp(name, "trunc"))
			d = trunc(d);
		else
			goto InvalidName;
		break;
	default:
		goto InvalidName;
	}

	/* If an error occurred, say to */
	switch (errno) {
	case 0:		break; /* no error */
	case EDOM:	return json_error_null(0, "Domain error in %s() function", name);
	case ERANGE:	return json_error_null(0, "Range error in %s() function", name);
	default:	return json_error_null(0, "Error in %s() function", name);
	}

	/* Return the result */
	return json_from_double(d);

InvalidName:
	fprintf(stderr, "Invalid Math.function named \"%s\" encountered in the math plugin");
	abort();
}

static json_t *jfn_acos(json_t *args, void *agdata)
{
	return common(args, "acos");
}

static json_t *jfn_acosh(json_t *args, void *agdata)
{
	return common(args, "acosh");
}

static json_t *jfn_asin(json_t *args, void *agdata)
{
	return common(args, "asin");
}

static json_t *jfn_asinh(json_t *args, void *agdata)
{
	return common(args, "asinh");
}

static json_t *jfn_atan(json_t *args, void *agdata)
{
	return common(args, "atan");
}

static json_t *jfn_atanh(json_t *args, void *agdata)
{
	return common(args, "atanh");
}

static json_t *jfn_cbrt(json_t *args, void *agdata)
{
	return common(args, "cbrt");
}

static json_t *jfn_ceil(json_t *args, void *agdata)
{
	return common(args, "ceil");
}

static json_t *jfn_cos(json_t *args, void *agdata)
{
	return common(args, "cos");
}

static json_t *jfn_cosh(json_t *args, void *agdata)
{
	return common(args, "cosh");
}

static json_t *jfn_exp(json_t *args, void *agdata)
{
	return common(args, "exp");
}

static json_t *jfn_floor(json_t *args, void *agdata)
{
	return common(args, "floor");
}

static json_t *jfn_log(json_t *args, void *agdata)
{
	return common(args, "log");
}

static json_t *jfn_log10(json_t *args, void *agdata)
{
	return common(args, "log10");
}

static json_t *jfn_log2(json_t *args, void *agdata)
{
	return common(args, "log2");
}

static json_t *jfn_round(json_t *args, void *agdata)
{
	return common(args, "round");
}

static json_t *jfn_sin(json_t *args, void *agdata)
{
	return common(args, "sin");
}

static json_t *jfn_sinh(json_t *args, void *agdata)
{
	return common(args, "sinh");
}

static json_t *jfn_sqrt(json_t *args, void *agdata)
{
	return common(args, "sqrt");
}

static json_t *jfn_tan(json_t *args, void *agdata)
{
	return common(args, "tan");
}

static json_t *jfn_trunc(json_t *args, void *agdata)
{
	return common(args, "trunc");
}

/* The following are different, in that they take 2 arguments */

static json_t *jfn_atan2(json_t *args, void *agdata)
{
	json_t	*arg;
	double	x, y;

	/* Skip over the Math object, if given */
	arg = args->first;
	if (arg->type == JSON_OBJECT)
		arg = arg->next;

	/* Must be two numbers */
	if (arg->type != JSON_NUMBER || !arg->next || arg->next->type != JSON_NUMBER || arg->next->next)
		return json_error_null(0, "The %s() function expect two numbers number as its arguments", atan2);

	/* Convert to binary */
	x = json_double(arg);
	y = json_double(arg->next);

	/* Clear errno so we can detect errors */
	errno = 0;

	/* Do it */
	x = atan2(x, y);

	/* If error, say so */
	if (errno)
		return json_error_null(0, "Error in %s() function", "atan2");

	/* Return the result */
	return json_from_double(x);
}

static json_t *jfn_hypot(json_t *args, void *agdata)
{
	json_t	*arg;
	double	d, sumsquared;

	/* Skip over the Math object, if given */
	arg = args->first;
	if (arg->type == JSON_OBJECT)
		arg = arg->next;

	/* If given a single number, just return its absolute value */
	if (arg->type == JSON_NUMBER && !arg->next)
		return json_from_double(abs(json_double(arg)));

	/* Reset errno so we can detect errors */
	errno = 0;

	/* Sum up the squares of the arguments. */
	for(sumsquared = 0; arg && errno == 0; arg = arg->next) {
		d = json_double(arg);
		sumsquared += d * d;
	}

	/* Take the square root */
	if (!errno)
		d = sqrt(sumsquared);

	/* If error, say so */
	if (errno)
		return json_error_null(0, "Error in %s() function", "hypot");

	/* Return the result */
	return json_from_double(d);

BadArgs:
	return json_error_null(0, "The %s() function expects at least two numbers number as its arguments", "hypot");
}

static json_t *jfn_pow(json_t *args, void *agdata)
{
	json_t	*arg;
	double	base, power;

	/* Skip over the Math object, if given */
	arg = args->first;
	if (arg->type == JSON_OBJECT)
		arg = arg->next;

	/* Must be two numbers */
	if (arg->type != JSON_NUMBER || !arg->next || arg->next->type != JSON_NUMBER || arg->next->next)
		return json_error_null(0, "The %s() function expect two numbers number as its arguments", "pow");

	/* Convert to binary */
	base = json_double(arg);
	power = json_double(arg->next);

	/* Clear errno so we can detect errors */
	errno = 0;

	/* Do it */
	base = pow(base, power);

	/* If error, say so */
	if (errno)
		return json_error_null(0, "Error in %s() function", "pow");

	/* Return the result */
	return json_from_double(base);
}

/* This is the init function.  It registers all of the above functions, and
 * adds some constants to the Math object.
 */
char *pluginmath()
{
	json_t	*math;

	/* Register the functions */
	json_calc_function_hook("acos",  "n:number", "number", jfn_acos);
	json_calc_function_hook("acosh", "n:number", "number", jfn_acosh);
	json_calc_function_hook("asin",  "n:number", "number", jfn_asin);
	json_calc_function_hook("asinh", "n:number", "number", jfn_asinh);
	json_calc_function_hook("atan",  "n:number", "number", jfn_atan);
	json_calc_function_hook("atanh", "n:number", "number", jfn_atanh);
	json_calc_function_hook("cbrt",  "n:number", "number", jfn_cbrt);
	json_calc_function_hook("ceil",  "n:number", "number", jfn_ceil);
	json_calc_function_hook("cos",   "n:number", "number", jfn_cos);
	json_calc_function_hook("cosh",  "n:number", "number", jfn_cosh);
	json_calc_function_hook("exp",   "n:number", "number", jfn_exp);
	json_calc_function_hook("floor", "n:number", "number", jfn_floor);
	json_calc_function_hook("log",   "n:number", "number", jfn_log);
	json_calc_function_hook("log10", "n:number", "number", jfn_log10);
	json_calc_function_hook("log2",  "n:number", "number", jfn_log2);
	json_calc_function_hook("round", "n:number", "number", jfn_round);
	json_calc_function_hook("sin",   "n:number", "number", jfn_sin);
	json_calc_function_hook("sinh",  "n:number", "number", jfn_sinh);
	json_calc_function_hook("sqrt",  "n:number", "number", jfn_sqrt);
	json_calc_function_hook("tan",   "n:number", "number", jfn_tan);
	json_calc_function_hook("trunc", "n:number", "number", jfn_trunc);

	json_calc_function_hook("atan2", "x:number, y:number", "number", jfn_atan2);
	json_calc_function_hook("hypot", "n1:number, ...", "number", jfn_hypot);
	json_calc_function_hook("pow",   "base:number, power:number", "number", jfn_pow);

	/* Insert constants into the Math object */
	math = json_by_key(json_system, "Math");
	if (!math) {
		math = json_object();
		json_append(json_system, json_key("Math", math));
	}
	json_append(math, json_key("E", json_from_double(M_E)));
	json_append(math, json_key("LN10", json_from_double(M_LN10)));
	json_append(math, json_key("LN2", json_from_double(M_LN2)));
	json_append(math, json_key("LOG10E", json_from_double(M_LOG10E)));
	json_append(math, json_key("LOG2E", json_from_double(M_LOG2E)));
	json_append(math, json_key("PI", json_from_double(M_PI)));
	json_append(math, json_key("SQRT1_2", json_from_double(M_SQRT1_2)));
	json_append(math, json_key("SQRT2", json_from_double(M_SQRT2)));

	/* Success */
	return NULL;
}
