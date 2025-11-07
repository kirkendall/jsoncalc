#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#undef _XOPEN_SOURCE
#undef __USE_XOPEN
#include <wchar.h>
#include <jsoncalc.h>

/* This plugin adds support for XML parsing and generation */

/* Include the other source files in this one.  The reason we don't compile
 * them separately is, we want everything to be "static" scope except for
 * the pluginxml() function.
 */
#include "parse.c"
#include "genplain.c"
#include "gentemplate.c"

/*----------------------------------------------------------------------------*/

/* These are the default settings */
static char *SETTINGS = "{"
	"\"attributeSuffix\":\"_\","
	"\"parseNumber\":true,"
	"\"parseEntity\":true,"
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

/* Converts a json_t object to XML.  Basically the inverse of the parser. */
static json_t *jfn_toXML(json_t *args, void *agdata)
{
	size_t len;
	json_t *result;

	len = xml_plain(NULL, args->first);
	result = json_string("", len);
	(void)xml_plain(result->text, args->first);

	return result;
}

/* Generates an XML document from a template. */
static json_t *jfn_toTemplateXML(json_t *args, void *agdata)
{
	return NULL;
}

/*----------------------------------------------------------------------------*/

static jsoncmdname_t *jcn_xmlEntity;

/* Parse an xmlEntity command */
static jsoncmd_t *xmlEntity_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	jsonsrc_t	start;
	char		*key = NULL;
	jsoncalc_t	*calc = NULL;
	const char	*err;
	jsoncmd_t	*cmd;

	/* xmlEntity with no arguments is legitimate.  It will dump the table */
	start = *src;
	json_cmd_parse_whitespace(src);
	if (!*src->str || *src->str == ';' || *src->str == '}') {
		return json_cmd(&start, jcn_xmlEntity);
	}

	/* Parse the key.  If no key, that's an error */
	key = json_cmd_parse_key(src, 0);
	if (!key)
		goto Error;

	/* Parse the "=".  If no "=", that's an error. */
	json_cmd_parse_whitespace(src);
	if (*src->str != '=')
		goto Error;
	src->str++;
	json_cmd_parse_whitespace(src);

	/* Parse the value, as an expression */
	calc = json_calc_parse(src->str, &src->str, &err, FALSE);
	if (!calc || err || (*src->str && !strchr(";},", *src->str)))
		goto Error;

	/* Construct the command */
	cmd = json_cmd(&start, jcn_xmlEntity);
	cmd->key = key;
	cmd->calc = calc;
	return cmd;

Error:
	if (key)
		free(key);
	if (calc)
		json_calc_free(calc);
	*referr = json_cmd_error(start.str, "xmlEntity:The %s command expects an entity=value argument");
	return NULL;
}

/* Run an xmlEntity command */
static jsoncmdout_t *xmlEntity_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	jsoncmd_t *dump;
	jsoncmdout_t *result;
	json_t	*value, *entity;

	/* If no key, then dump the entity list */
	if (!cmd->key) {
		dump = json_cmd_parse_string("config.plugin.xml.entity.keysValues().orderBy('key') # {entity:'&'+key+';',codepoint:value.length==1?('U+'+(value.charCodeAt()).hex(5)),text:value}");
		result = json_cmd_run(dump, refcontext);
		json_cmd_free(dump);
		return result;
	}

	/* Evaluate the value.  Watch for errors. */
	value = json_calc(cmd->calc, *refcontext, NULL);
	if (json_is_error(value)) {
		result = json_cmd_error(cmd->where, "%s", value->text);
		json_free(value);
		return result;
	}

	/* If the value is a number, convert it to a single character string */
	if (value->type == JSON_NUMBER) {
		wchar_t	number = (wchar_t)json_int(value);
		int	in;
		json_free(value);
		value = json_string("", MB_CUR_MAX);
		in = wctomb(value->text, number);
		if (in > 0)
			value->text[in] = '\0';
	}

	/* If the value still isn't a string, that's an error */
	if (value->type != JSON_STRING)
		return json_cmd_error(cmd->where, "xmlEntityType:The value of an entity should be either a string or a number");

	/* Add/update the entity list */
	entity = json_by_expr(json_config, "plugin.xml.entity", NULL);
	json_append(entity, json_key(cmd->key, value));

	/* Success! */
	return NULL;
}


/*----------------------------------------------------------------------------*/

/* This is the init function.  It registers all of the options, functions and
 * parsers.
 */
char *pluginxml()
{
	json_t	*section, *settings;

	/* Register the settings */
	settings = json_parse_string(SETTINGS);
	section = json_by_key(json_config, "plugin");
	json_append(section, json_key("xml", settings));

	/* Register the functions */
	json_calc_function_hook("toXML", "document:object", "string", jfn_toXML);
	json_calc_function_hook("toTemplateXML", "data:any, template:string", "string", jfn_toTemplateXML);

	/* Register the commands */
	jcn_xmlEntity = json_cmd_hook(NULL, "xmlEntity", xmlEntity_parse, xmlEntity_run);

	/* Register the XML data parser */
	json_parse_hook("xml", "xml", ".xml", "application/xml", xml_test, xml_parse);

	/* Success */
	return NULL;
}
