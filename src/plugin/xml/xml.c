#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#undef _XOPEN_SOURCE
#undef __USE_XOPEN
#include <wchar.h>
#include <jx.h>

/* This plugin adds support for XML parsing and generation */

/* Include the other source files in this one.  The reason we don't compile
 * them separately is, we want everything to be "static" scope except for
 * the pluginxml() function.
 */
#include "parse.c"
#include "unparse.c"
#include "template.c"

/*----------------------------------------------------------------------------*/

/* These are the default settings */
static char *SETTINGS = "{"
	"\"attributeSuffix\":\"_\","
	"\"parseNumber\":true,"
	"\"strictPair\":true,"
	"\"generateCRLF\":true,"
	"\"empty-list\":[\"string\",\"object\",\"array\"],"
	"\"empty\":\"string\","
	"\"entity\":{"
	    "\"quot\":\"\\\"\","
	    "\"apos\":\"'\","
	    "\"amp\":\"&\","
	    "\"lt\":\"<\","
	    "\"gt\":\">\""
	"}"
"}";

/*----------------------------------------------------------------------------*/

/* Converts a jx_t object to XML.  Basically the inverse of the parser. */
static jx_t *jfn_toXML(jx_t *args, void *agdata)
{
	size_t len;
	jx_t *result;

	len = xml_unparse(NULL, args->first);
	result = jx_string("", len);
	(void)xml_unparse(result->text, args->first);

	return result;
}

/* Generates an XML document from a template. */
static jx_t *jfn_toTemplateXML(jx_t *args, void *agdata)
{
	return NULL;
}

/*----------------------------------------------------------------------------*/

static jxcmdname_t *jcn_xmlEntity;

/* Parse an xmlEntity command */
static jxcmd_t *xmlEntity_parse(jxsrc_t *src, jxcmdout_t **referr)
{
	jxsrc_t	start;
	char		*key = NULL;
	jxcalc_t	*calc = NULL;
	const char	*err;
	jxcmd_t	*cmd;

	/* xmlEntity with no arguments is legitimate.  It will dump the table */
	start = *src;
	jx_cmd_parse_whitespace(src);
	if (!*src->str || *src->str == ';' || *src->str == '}') {
		return jx_cmd(&start, jcn_xmlEntity);
	}

	/* Parse the key.  If no key, that's an error */
	key = jx_cmd_parse_key(src, 0);
	if (!key)
		goto Error;

	/* Parse the "=".  If no "=", that's an error. */
	jx_cmd_parse_whitespace(src);
	if (*src->str != '=')
		goto Error;
	src->str++;
	jx_cmd_parse_whitespace(src);

	/* Parse the value, as an expression */
	calc = jx_calc_parse(src->str, &src->str, &err, FALSE);
	if (!calc || err || (*src->str && !strchr(";},", *src->str)))
		goto Error;

	/* Construct the command */
	cmd = jx_cmd(&start, jcn_xmlEntity);
	cmd->key = key;
	cmd->calc = calc;
	return cmd;

Error:
	if (key)
		free(key);
	if (calc)
		jx_calc_free(calc);
	*referr = jx_cmd_error(start.str, "xmlEntity:The %s command expects an entity=value argument");
	return NULL;
}

/* Run an xmlEntity command */
static jxcmdout_t *xmlEntity_run(jxcmd_t *cmd, jxcontext_t **refcontext)
{
	jxcmd_t *dump;
	jxcmdout_t *result;
	jx_t	*value, *entity;

	/* If no key, then dump the entity list */
	if (!cmd->key) {
		dump = jx_cmd_parse_string("config.plugin.xml.entity.keysValues().orderBy('key') # {entity:'&'+key+';',codepoint:value.length==1?('U+'+(value.charCodeAt()).hex(5)),text:value}");
		result = jx_cmd_run(dump, refcontext);
		jx_cmd_free(dump);
		return result;
	}

	/* Evaluate the value.  Watch for errors. */
	value = jx_calc(cmd->calc, *refcontext, NULL);
	if (jx_is_error(value)) {
		result = jx_cmd_error(cmd->where, "%s", value->text);
		jx_free(value);
		return result;
	}

	/* If the value is a number, convert it to a single character string */
	if (value->type == JX_NUMBER) {
		wchar_t	number = (wchar_t)jx_int(value);
		int	in;
		jx_free(value);
		value = jx_string("", MB_CUR_MAX);
		in = wctomb(value->text, number);
		if (in > 0)
			value->text[in] = '\0';
	}

	/* If the value still isn't a string, that's an error */
	if (value->type != JX_STRING)
		return jx_cmd_error(cmd->where, "xmlEntityType:The value of an entity should be either a string or a number");

	/* Add/update the entity list */
	entity = jx_by_expr(jx_config, "plugin.xml.entity", NULL);
	jx_append(entity, jx_key(cmd->key, value));

	/* Success! */
	return NULL;
}


/*----------------------------------------------------------------------------*/

/* This is the init function.  It registers all of the options, functions and
 * parsers.
 */
char *pluginxml()
{
	jx_t	*section, *settings;

	/* Register the settings */
	settings = jx_parse_string(SETTINGS);
	section = jx_by_key(jx_config, "plugin");
	jx_append(section, jx_key("xml", settings));

	/* Register the functions */
	jx_calc_function_hook("toXML", "document:object", "string", jfn_toXML);
	jx_calc_function_hook("toTemplateXML", "data:any, template:string", "string", jfn_toTemplateXML);

	/* Register the commands */
	jcn_xmlEntity = jx_cmd_hook(NULL, "xmlEntity", xmlEntity_parse, xmlEntity_run);

	/* Register the XML data parser */
	jx_parse_hook("xml", "xml", ".xml", "application/xml", xml_test, xml_parse, NULL);

	/* Success */
	return NULL;
}
