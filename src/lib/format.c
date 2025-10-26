#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <jsoncalc.h>

/* This is the default format.  It is used if you pass NULL to functions
 * that use jsonformat_t, especially json_print().
 */
jsonformat_t json_format_default = {
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


/* Initialize the format and colors from json_config or some other config.
 * "format" is the format to be modified; if NULL then json_format_default
 * is used.  "config" is the json_t object describing the configuration,
 * will usually be NULL to indicate json_config should be used.
 */
void json_format_set(jsonformat_t *format, json_t *config)
{
	json_t	*section; /* Format section to use - interactive or batch */

	/* If no config was specified (i.e., NULL) then use json_config */
	if (!config)
		config = json_config;

	/* If no format was specified, use the json_format_default */
	if (!format)
		format = &json_format_default;

	/* Choose a section - "interactive" or "batch" */
	section = json_by_key(config, json_text_by_key(json_system, "runmode"));

	/* Copy fields from config object to the format */
	format->tab = json_int(json_by_key(section, "tab"));
	format->oneline = json_int(json_by_key(section, "oneline"));
	format->digits = json_int(json_by_key(section, "digits"));
	strncpy(format->table, json_text_by_key(section, "table"), sizeof format->table - 1);;
	format->string = json_is_true(json_by_key(section, "string"));
	format->pretty = json_is_true(json_by_key(section, "pretty"));
	format->elem = json_is_true(json_by_key(section, "elem"));
	format->sh = json_is_true(json_by_key(section, "sh"));
	format->errors = json_is_true(json_by_key(section, "errors"));
	format->ascii = json_is_true(json_by_key(section, "ascii"));
	format->color = json_is_true(json_by_key(section, "color"));
	format->quick = json_is_true(json_by_key(section, "quick"));
	format->graphic = json_is_true(json_by_key(section, "graphic"));
	strncpy(format->prefix, json_text_by_key(section, "prefix"), sizeof format->prefix - 1);
	format->prefix[sizeof format->prefix - 1] = '\0';
	strncpy(format->null, json_text_by_key(section, "null"), sizeof format->null - 1);
	format->null[sizeof format->null - 1] = '\0';
}
