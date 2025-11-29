#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <jx.h>

/* This module exists to define some Math.xxx() functions. */

/* Most of the Math.functions are pretty similar.  They take an optional
 * Math object as their first parameter, and a single number as their only
 * meaningful parameter.  We can handle most of that efficiently here.
 */
static jx_t *common(jx_t *args, char *name)
{
	jx_t	*arg;
	double	d;

	/* Skip over the Math object, if given */
	arg = args->first;
	if (arg->type == JX_OBJECT)
		arg = arg->next;

	/* Must be a single number */
	if (arg->type != JX_NUMBER || arg->next)
		return jx_error_null(0, "The %s() function expect single number as its argument", name);

	/* Convert to binary */
	d = jx_double(arg);

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
	case EDOM:	return jx_error_null(0, "Domain error in %s() function", name);
	case ERANGE:	return jx_error_null(0, "Range error in %s() function", name);
	default:	return jx_error_null(0, "Error in %s() function", name);
	}

	/* Return the result */
	return jx_from_double(d);

InvalidName:
	fprintf(stderr, "Invalid Math.function named \"%s\" encountered in the math plugin");
	abort();
}

static jx_t *jfn_acos(jx_t *args, void *agdata)
{
	return common(args, "acos");
}

static jx_t *jfn_acosh(jx_t *args, void *agdata)
{
	return common(args, "acosh");
}

static jx_t *jfn_asin(jx_t *args, void *agdata)
{
	return common(args, "asin");
}

static jx_t *jfn_asinh(jx_t *args, void *agdata)
{
	return common(args, "asinh");
}

static jx_t *jfn_atan(jx_t *args, void *agdata)
{
	return common(args, "atan");
}

static jx_t *jfn_atanh(jx_t *args, void *agdata)
{
	return common(args, "atanh");
}

static jx_t *jfn_cbrt(jx_t *args, void *agdata)
{
	return common(args, "cbrt");
}

static jx_t *jfn_ceil(jx_t *args, void *agdata)
{
	return common(args, "ceil");
}

static jx_t *jfn_cos(jx_t *args, void *agdata)
{
	return common(args, "cos");
}

static jx_t *jfn_cosh(jx_t *args, void *agdata)
{
	return common(args, "cosh");
}

static jx_t *jfn_exp(jx_t *args, void *agdata)
{
	return common(args, "exp");
}

static jx_t *jfn_floor(jx_t *args, void *agdata)
{
	return common(args, "floor");
}

static jx_t *jfn_log(jx_t *args, void *agdata)
{
	return common(args, "log");
}

static jx_t *jfn_log10(jx_t *args, void *agdata)
{
	return common(args, "log10");
}

static jx_t *jfn_log2(jx_t *args, void *agdata)
{
	return common(args, "log2");
}

static jx_t *jfn_round(jx_t *args, void *agdata)
{
	return common(args, "round");
}

static jx_t *jfn_sin(jx_t *args, void *agdata)
{
	return common(args, "sin");
}

static jx_t *jfn_sinh(jx_t *args, void *agdata)
{
	return common(args, "sinh");
}

static jx_t *jfn_sqrt(jx_t *args, void *agdata)
{
	return common(args, "sqrt");
}

static jx_t *jfn_tan(jx_t *args, void *agdata)
{
	return common(args, "tan");
}

static jx_t *jfn_trunc(jx_t *args, void *agdata)
{
	return common(args, "trunc");
}

/* The following are different, in that they take 2 arguments */

static jx_t *jfn_atan2(jx_t *args, void *agdata)
{
	jx_t	*arg;
	double	x, y;

	/* Skip over the Math object, if given */
	arg = args->first;
	if (arg->type == JX_OBJECT)
		arg = arg->next;

	/* Must be two numbers */
	if (arg->type != JX_NUMBER || !arg->next || arg->next->type != JX_NUMBER || arg->next->next)
		return jx_error_null(0, "The %s() function expect two numbers number as its arguments", atan2);

	/* Convert to binary */
	x = jx_double(arg);
	y = jx_double(arg->next);

	/* Clear errno so we can detect errors */
	errno = 0;

	/* Do it */
	x = atan2(x, y);

	/* If error, say so */
	if (errno)
		return jx_error_null(0, "Error in %s() function", "atan2");

	/* Return the result */
	return jx_from_double(x);
}

static jx_t *jfn_hypot(jx_t *args, void *agdata)
{
	jx_t	*arg;
	double	d, sumsquared;

	/* Skip over the Math object, if given */
	arg = args->first;
	if (arg->type == JX_OBJECT)
		arg = arg->next;

	/* If given a single number, just return its absolute value */
	if (arg->type == JX_NUMBER && !arg->next)
		return jx_from_double(abs(jx_double(arg)));

	/* Reset errno so we can detect errors */
	errno = 0;

	/* Sum up the squares of the arguments. */
	for(sumsquared = 0; arg && errno == 0; arg = arg->next) {
		d = jx_double(arg);
		sumsquared += d * d;
	}

	/* Take the square root */
	if (!errno)
		d = sqrt(sumsquared);

	/* If error, say so */
	if (errno)
		return jx_error_null(0, "Error in %s() function", "hypot");

	/* Return the result */
	return jx_from_double(d);

BadArgs:
	return jx_error_null(0, "The %s() function expects at least two numbers number as its arguments", "hypot");
}

static jx_t *jfn_pow(jx_t *args, void *agdata)
{
	jx_t	*arg;
	double	base, power;

	/* Skip over the Math object, if given */
	arg = args->first;
	if (arg->type == JX_OBJECT)
		arg = arg->next;

	/* Must be two numbers */
	if (arg->type != JX_NUMBER || !arg->next || arg->next->type != JX_NUMBER || arg->next->next)
		return jx_error_null(0, "The %s() function expect two numbers number as its arguments", "pow");

	/* Convert to binary */
	base = jx_double(arg);
	power = jx_double(arg->next);

	/* Clear errno so we can detect errors */
	errno = 0;

	/* Do it */
	base = pow(base, power);

	/* If error, say so */
	if (errno)
		return jx_error_null(0, "Error in %s() function", "pow");

	/* Return the result */
	return jx_from_double(base);
}

/* This is the init function.  It registers all of the above functions, and
 * adds some constants to the Math object.
 */
char *pluginmath()
{
	jx_t	*math;

	/* Register the functions */
	jx_calc_function_hook("acos",  "n:number", "number", jfn_acos);
	jx_calc_function_hook("acosh", "n:number", "number", jfn_acosh);
	jx_calc_function_hook("asin",  "n:number", "number", jfn_asin);
	jx_calc_function_hook("asinh", "n:number", "number", jfn_asinh);
	jx_calc_function_hook("atan",  "n:number", "number", jfn_atan);
	jx_calc_function_hook("atanh", "n:number", "number", jfn_atanh);
	jx_calc_function_hook("cbrt",  "n:number", "number", jfn_cbrt);
	jx_calc_function_hook("ceil",  "n:number", "number", jfn_ceil);
	jx_calc_function_hook("cos",   "n:number", "number", jfn_cos);
	jx_calc_function_hook("cosh",  "n:number", "number", jfn_cosh);
	jx_calc_function_hook("exp",   "n:number", "number", jfn_exp);
	jx_calc_function_hook("floor", "n:number", "number", jfn_floor);
	jx_calc_function_hook("log",   "n:number", "number", jfn_log);
	jx_calc_function_hook("log10", "n:number", "number", jfn_log10);
	jx_calc_function_hook("log2",  "n:number", "number", jfn_log2);
	jx_calc_function_hook("round", "n:number", "number", jfn_round);
	jx_calc_function_hook("sin",   "n:number", "number", jfn_sin);
	jx_calc_function_hook("sinh",  "n:number", "number", jfn_sinh);
	jx_calc_function_hook("sqrt",  "n:number", "number", jfn_sqrt);
	jx_calc_function_hook("tan",   "n:number", "number", jfn_tan);
	jx_calc_function_hook("trunc", "n:number", "number", jfn_trunc);

	jx_calc_function_hook("atan2", "x:number, y:number", "number", jfn_atan2);
	jx_calc_function_hook("hypot", "n1:number, ...", "number", jfn_hypot);
	jx_calc_function_hook("pow",   "base:number, power:number", "number", jfn_pow);

	/* Insert constants into the Math object */
	math = jx_by_key(jx_system, "Math");
	if (!math) {
		math = jx_object();
		jx_append(jx_system, jx_key("Math", math));
	}
	jx_append(math, jx_key("E", jx_from_double(M_E)));
	jx_append(math, jx_key("LN10", jx_from_double(M_LN10)));
	jx_append(math, jx_key("LN2", jx_from_double(M_LN2)));
	jx_append(math, jx_key("LOG10E", jx_from_double(M_LOG10E)));
	jx_append(math, jx_key("LOG2E", jx_from_double(M_LOG2E)));
	jx_append(math, jx_key("PI", jx_from_double(M_PI)));
	jx_append(math, jx_key("SQRT1_2", jx_from_double(M_SQRT1_2)));
	jx_append(math, jx_key("SQRT2", jx_from_double(M_SQRT2)));

	/* Success */
	return NULL;
}
