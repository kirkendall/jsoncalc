#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <jx.h>

/* This is the default format.  It is used if you pass NULL to functions
 * that use jxformat_t, especially jx_print().
 */
jxformat_t jx_format_default = {
	2,	/* tab - indentation to use when pretty-printing */
	50,	/* oneline - JSON shorter than this will always be compact */
	12,	/* digits - precission when converting from double to text */
	"grid",	/* table - output tables in grid format (sh/grid/json) */
	0,	/* string - output strings as plain text */
	1,	/* pretty - pretty-print (add whitespace to show structure) */
	0,	/* elem - force one array element per line */
	0,	/* sh - quote output for shell */
	1,	/* error - write error nulls to stderr */
	0,	/* ascii - use \uXXXX for non-ASCII characters */
	1,	/* color - use colors on ANSI terminals */
	0,	/* quick - for csv/grid, use first record to find columns */
	1,	/* graphic - use Unicode graphic characters */
	"",	/* prefix - prepended to names for sh format */
	"",	/* null - text to show as null for grid format */
	NULL	/* output - NULL for stdout, else the fp to write to */
};


/* Initialize the format and colors from jx_config or some other config.
 * "format" is the format to be modified; if NULL then jx_format_default
 * is used.  "config" is the jx_t object describing the configuration,
 * will usually be NULL to indicate jx_config should be used.
 */
void jx_format_set(jxformat_t *format, jx_t *config)
{
	jx_t	*section; /* Format section to use - interactive or batch */

	/* If no config was specified (i.e., NULL) then use jx_config */
	if (!config)
		config = jx_config;

	/* If no format was specified, use the jx_format_default */
	if (!format)
		format = &jx_format_default;

	/* Choose a section - "interactive" or "batch" */
	section = jx_by_key(config, jx_text_by_key(jx_system, "runmode"));

	/* Copy fields from config object to the format */
	format->tab = jx_int(jx_by_key(section, "tab"));
	format->oneline = jx_int(jx_by_key(section, "oneline"));
	format->digits = jx_int(jx_by_key(section, "digits"));
	strncpy(format->table, jx_text_by_key(section, "table"), sizeof format->table - 1);;
	format->string = jx_is_true(jx_by_key(section, "string"));
	format->pretty = jx_is_true(jx_by_key(section, "pretty"));
	format->elem = jx_is_true(jx_by_key(section, "elem"));
	format->sh = jx_is_true(jx_by_key(section, "sh"));
	format->errors = jx_is_true(jx_by_key(section, "errors"));
	format->ascii = jx_is_true(jx_by_key(section, "ascii"));
	format->color = jx_is_true(jx_by_key(section, "color"));
	format->quick = jx_is_true(jx_by_key(section, "quick"));
	format->graphic = jx_is_true(jx_by_key(section, "graphic"));
	strncpy(format->prefix, jx_text_by_key(section, "prefix"), sizeof format->prefix - 1);
	format->prefix[sizeof format->prefix - 1] = '\0';
	strncpy(format->null, jx_text_by_key(section, "null"), sizeof format->null - 1);
	format->null[sizeof format->null - 1] = '\0';
}
