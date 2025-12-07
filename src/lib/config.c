#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <jx.h>
#include "version.h"

static const char *defaultconfig = "{"
	"\"interactive\":{"
		"\"tab\":2,"
		"\"oneline\":70,"
		"\"digits\":12,"
		"\"table\":\"grid\","
		"\"table-list\":[\"json\",\"grid\",\"sh\"],"
		"\"string\":false,"
		"\"pretty\":true,"
		"\"elem\":false,"
		"\"quote\":false,"
		"\"errors\":true,"
		"\"ascii\":false,"
		"\"color\":true,"
		"\"quick\":false,"
		"\"graphic\":true,"
		"\"prefix\":\"\","
		"\"null\":\"\","
	"},"
	"\"batch\":{"
		"\"tab\":2,"
		"\"oneline\":0,"
		"\"digits\":12,"
		"\"table\":\"json\","
		"\"table-list\":[\"json\",\"grid\",\"sh\"],"
		"\"string\":false,"
		"\"pretty\":false,"
		"\"elem\":false,"
		"\"quote\":false,"
		"\"errors\":true,"
		"\"ascii\":false,"
		"\"color\":false,"
		"\"quick\":false,"
		"\"graphic\":false,"
		"\"prefix\":\"\","
		"\"null\":\"\","
	"},"
	"\"diffstyle\":13," /* JX_DIFF_BESIDE|JX_DIFF_VALUE|JX_DIFF_EDIT */
	"\"emptyobject\":\"object\","
	"\"defersize\":10000000,"
	"\"deferexplain\":100,"
	"\"styles\": ["
		"{"
			"\"style\":\"normal\","
			"\"bold\":false,"
			"\"dim\":false,"
			"\"italic\":false,"
			"\"underlined\":false,"
			"\"blinking\":false,"
			"\"boxed\":false,"
			"\"strike\":false"
			"\"fg\":\"normal\","
			"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
			"\"bg\":\"on normal\","
			"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
			"\"stderr\":false"
		"},{"
			"\"style\":\"prompt\","
			"\"bold\":true,"
			"\"dim\":false,"
			"\"italic\":false,"
			"\"underlined\":false,"
			"\"blinking\":false,"
			"\"boxed\":false,"
			"\"strike\":false"
			"\"fg\":\"cyan\","
			"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
			"\"bg\":\"on normal\","
			"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
			"\"stderr\":false"
		"},{"
			"\"style\":\"result\","
			"\"bold\":false,"
			"\"dim\":false,"
			"\"italic\":false,"
			"\"underlined\":false,"
			"\"blinking\":false,"
			"\"boxed\":false,"
			"\"strike\":false"
			"\"fg\":\"normal\","
			"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
			"\"bg\":\"on normal\","
			"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
			"\"stderr\":false"
		"},{"
			"\"style\":\"error\","
			"\"bold\":true,"
			"\"dim\":false,"
			"\"italic\":false,"
			"\"underlined\":false,"
			"\"blinking\":false,"
			"\"boxed\":false,"
			"\"strike\":false"
			"\"fg\":\"red\","
			"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
			"\"bg\":\"on normal\","
			"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
			"\"stderr\":true"
		"},{"
			"\"style\":\"debug\","
			"\"bold\":true,"
			"\"dim\":false,"
			"\"italic\":false,"
			"\"underlined\":false,"
			"\"blinking\":false,"
			"\"boxed\":false,"
			"\"strike\":false"
			"\"fg\":\"blue\","
			"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
			"\"bg\":\"on normal\","
			"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
			"\"stderr\":true"
		"},{"
			"\"style\":\"gridhead\","
			"\"bold\":true,"
			"\"dim\":false,"
			"\"italic\":false,"
			"\"underlined\":true,"
			"\"blinking\":false,"
			"\"boxed\":false,"
			"\"strike\":false"
			"\"fg\":\"blue\","
			"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
			"\"bg\":\"on normal\","
			"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
			"\"stderr\":false"
		"},{"
			"\"style\":\"gridline\","
			"\"bold\":true,"
			"\"dim\":false,"
			"\"italic\":false,"
			"\"underlined\":false,"
			"\"blinking\":false,"
			"\"boxed\":false,"
			"\"strike\":false"
			"\"fg\":\"blue\","
			"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
			"\"bg\":\"on normal\","
			"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
			"\"stderr\":false"
		"},{"
			"\"style\":\"gridcell\","
			"\"bold\":false,"
			"\"dim\":false,"
			"\"italic\":false,"
			"\"underlined\":false,"
			"\"blinking\":false,"
			"\"boxed\":false,"
			"\"strike\":false"
			"\"fg\":\"normal\","
			"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
			"\"bg\":\"on normal\","
			"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
			"\"stderr\":false"
		"},"
	"],"
	"\"plugin\":{}"
"}";


/* This stores a pointer to the config data */
jx_t *jx_config;

/* This is a combination of all system data.  It is initialized by
 * jx_config_load(), though other code may add to it.
 */
jx_t *jx_system;

/* Merge new settings into old settings. */
static void merge(jx_t *old, jx_t *newload)
{
	jx_t *newkey, *oldmem;

	/* Only works on objects */
	if (old->type != JX_OBJECT || newload->type != JX_OBJECT)
		return;

	/* For each new member */
	for (newkey = newload->first; newkey; newkey = newkey->next) { /* object */
		/* Look for a corresponding old member */
		oldmem = jx_by_key(old, newkey->text);

		/* If no corresponding old member, then add a copy of new */
		if (!oldmem) {
			jx_append(old, jx_key(newkey->text, jx_copy(newkey->first)));
			continue;
		}

		/* If both are objects, merge recursively */
		if (oldmem->type == JX_OBJECT && newkey->first->type == JX_OBJECT) {
			merge(oldmem, newkey->first);
			continue;
		}

		/* If both are some other type, then replace the value */
		if (oldmem->type == newkey->first->type) {
			jx_append(old, jx_key(newkey->text, jx_copy(newkey->first)));
			continue;
		}

		/* Otherwise ignore it.  This could happen if jx's
		 * option format got redefined, so the new settings 
		 */
	}
}

/* Add a directory name to the path, if the directory exists */
static void addpath(jx_t *path, const char *dirname)
{
	if (access(dirname, F_OK) == 0)
		jx_append(path, jx_string(dirname, -1));
}

/* Return an array of directory names to look in for files related to jx
 * -- plugins and documentation mostly.
 */
static jx_t *configpath(const char *envvar)
{
	const char *env;
	jx_t *path, *entry;
	int	isjxpath;
	size_t	len;

	/* If no envvar then just fake it completely */
	if (!envvar) {
		path = jx_array();
		jx_append(path, jx_string("~/.config/jx", -1));
		addpath(path, "/usr/local/lib64/jx");
		addpath(path, "/usr/local/lib/jx");
		addpath(path, "/usr/lib64/jx");
		addpath(path, "/usr/lib/jx");
		addpath(path, "/lib64/jx");
		addpath(path, "/lib/jx");
		addpath(path, "/usr/local/share/jx");
		addpath(path, "/usr/share/jx");
		return path;
	}

	/* If an envvar was specified but it isn't set, return NULL */
	env = getenv(envvar);
	if (!env)
		return NULL;

	/* Distinguish between $JXPATH and other paths */
	isjxpath = !strcmp(envvar, "JXPATH");

	/* Start building an array of entries.  If not $JXPATH then
	 * put the config directory first.
	 */
	path = jx_array();
	if (!isjxpath)
		jx_append(path, jx_string("~/.config/jx", -1));

	/* For each entry in the path... */
	while (*env) {
		/* Find the end of this entry */
		for (len = 0; env[len] && env[len] != JX_PATH_DELIM; len++) {
		}

		/* Convert it to a string.  If not from $JXPATH then
		 * add "/jx" to the string.
		 */
		if (len == 0)
			entry = jx_string(".", 1);
		else if (isjxpath) {
			entry = jx_string(env, len);
		} else {
			entry = jx_string(env, len + 3);
			strcat(entry->text, "/jx");
		}

		/* Add it to the path */
		jx_append(path, entry);

		/* Move to the next entry */
		env += len;
		if (*env == JX_PATH_DELIM)
			env++;
	}

	/* If not $JXPATH then add shared directories */
	if (!isjxpath) {
		addpath(path, "/usr/local/share/jx");
		addpath(path, "/usr/share/jx");
	}
	return path;
}

/* Load the configuration data, and return it */
void jx_config_load(const char *name)
{
	char	*pathname;
	jx_t	*conf, *value;

	/* Load the default config */
	jx_config = jx_parse_string(defaultconfig);

	/* If jx_system isn't set up yet, then set it up now */
	if (!jx_system) {
		jx_system = jx_object();

		/* We also want to add the path for plugins and documentation.
		 * This is from $JXPATH, but if $JXPATH isn't set
		 * then we want to derive it from $LD_LIBRARY_PATH.  And if
		 * $LD_LIBRARY_PATH isn't set then we simulate that too.
		 */
		value = configpath("JXPATH");
		if (!value)
			value = configpath("LD_LIBRARY_PATH");
		if (!value)
			value = configpath(NULL);
		jx_append(jx_system, jx_key("path", value));

		jx_append(jx_system, jx_key("config", jx_config));
		pathname = jx_file_path(NULL, NULL, NULL);
		jx_append(jx_system, jx_key("configdir", jx_string(pathname, -1)));
		free(pathname);

		/* Add an empty list of plugins.  As plugins are loaded,
		 * this will become populated.
		 */
		if (!jx_plugins)
			jx_plugins = jx_array();
		jx_append(jx_system, jx_key("plugins", jx_plugins));

		/* Add a table of parsers.  Initially it'll contain only the
		 * built-in JSON parser, but as plugins register their parsers
		 * via jx_parse_hook(), they'll be added here too.
		 */
		conf = jx_array();
		value = jx_object();
		jx_append(value, jx_key("name", jx_string("json", -1)));
		jx_append(value, jx_key("plugin", jx_null()));
		jx_append(value, jx_key("suffix", jx_string(".json", -1)));
		jx_append(value, jx_key("mimetype", jx_string("application/json", -1)));
		jx_append(value, jx_key("writable", jx_boolean(1)));
		jx_append(conf, value);
		value = jx_object();
		jx_append(value, jx_key("name", jx_string("blob", -1)));
		jx_append(value, jx_key("plugin", jx_null()));
		jx_append(value, jx_key("suffix", jx_null()));
		jx_append(value, jx_key("mimetype", jx_string("application/octet-stream", -1)));
		jx_append(value, jx_key("writable", jx_boolean(1)));
		jx_append(conf, value);
		jx_append(jx_system, jx_key("parsers", conf));

		/* Add empty JSON and Math objects, for JS compatibility. */
		jx_append(jx_system, jx_key("JSON", jx_object()));
		jx_append(jx_system, jx_key("Math", jx_object()));

		/* Add a "Blob" object with constants selecting how to handle
		 * binary data.
		 */
		value = jx_object();
		jx_append(value, jx_key("any", jx_from_int(JX_BLOB_ANY)));
		jx_append(value, jx_key("string", jx_from_int(JX_BLOB_STRING)));
		jx_append(value, jx_key("utf8", jx_from_int(JX_BLOB_UTF8)));
		jx_append(value, jx_key("latin1", jx_from_int(JX_BLOB_LATIN1)));
		jx_append(value, jx_key("bytes", jx_from_int(JX_BLOB_BYTES)));
		jx_append(jx_system, jx_key("Blob", value));

		/* Add a "Diff" object with constants for selecting the format */
		value = jx_object();
		jx_append(value, jx_key("value", jx_from_int(JX_DIFF_VALUE)));
		jx_append(value, jx_key("span", jx_from_int(JX_DIFF_SPAN)));
		jx_append(value, jx_key("beside", jx_from_int(JX_DIFF_BESIDE)));
		jx_append(value, jx_key("edit", jx_from_int(JX_DIFF_EDIT)));
		jx_append(value, jx_key("context", jx_from_int(JX_DIFF_CONTEXT)));
		jx_append(value, jx_key("bits", jx_parse_string("[\"value\",\"span\",\"beside\",\"edit\",\"context\"]")));
		jx_append(jx_system, jx_key("Diff", value));

		/* Add members to jx_system, describing the environment */
		jx_append(jx_system, jx_key("runmode", jx_string("interactive", -1)));
		jx_append(jx_system, jx_key("update", jx_boolean(0)));
		jx_append(jx_system, jx_key("version", jx_number(JX_VERSION, -1)));
		jx_append(jx_system, jx_key("copyright", jx_string(JX_COPYRIGHT, -1)));

	}

	/* Look for the config file */
	pathname = jx_file_path(NULL, name, ".json");
	if (!pathname)
		return;

	/* Parse it */
	conf = jx_parse_file(pathname);
	if (!conf)
		return;
	if (conf->type != JX_OBJECT) {
		jx_free(conf);
		return;
	}

	/* Merge its settings into the default config */
	merge(jx_config, conf);

	/* Free the data from the file */
	jx_free(conf);
}

/* Select whether this part of jx_config gets saved.  It should save
 * everything except members that have names ending with "-list", and a
 * few others.
 */
static int notlist(jx_t *mem)
{
	/* Non-members are always kept */
	if (mem->type != JX_KEY)
		return 1;

	/* Members named "batch" or "*-list" are skipped */
	if (!strcmp(mem->text, "batch"))
		return 0;
	if (!strcmp(mem->text, "pluginloaded"))
		return 0;
	if (strlen(mem->text) >= 5
	 && !strcmp(mem->text + strlen(mem->text) - 5, "-list"))
		return 0;

	/* Other members are kept */
	return 1;
}

/* Save the config data */
void jx_config_save(const char *name)
{
	char	*pathname;
	jx_t	*copy;
	FILE	*fp;

	/* We generally want to save options in the first writable directory
	 * in the JXPATH, even if the "config.json" file doesn't exist
	 * there yet.
	 */
	pathname = jx_file_path(NULL, NULL, NULL);
	if (pathname) {
		char *tmp = malloc(strlen(pathname) + strlen(name) + 6);
		strcpy(tmp, pathname);
		strcat(tmp, name);
		strcat(tmp, ".json");
		free(pathname);
		pathname = tmp;
	} else {
		pathname = jx_file_path(NULL, name, ".json");
		if (!pathname)
			return; /* no place to save it */
	}

	/* We want to save everything EXCEPT members with names ending with
	 * "-list".  Make a copy of the config with "-list" members removed.
	 */
	copy = jx_copy_filter(jx_config, notlist);

	/* Write it to the file */
	fp = jx_file_update(pathname);
	if (fp) {
		jxformat_t fmt = jx_format_default;
		fmt.string = fmt.elem = fmt.sh = fmt.ascii = fmt.color = 0;
		fmt.fp = fp;
		jx_print(copy, &fmt);
		fclose(fp);
	}
	jx_free(copy);
}

/* Look up the section in config.styles for a given style.  There are two ways
 * this is used.
 *
 * Plugins can call it with NULL as the second argument, during their config
 * initialization.  Used this way, if the requested style isn't found then it
 * will be added and returned.  It will initially be a copy of "normal" but
 * the plugin can directly modify member values as appropriate.  Note that
 * settings from -s/-S flags or the config file are loaded later.
 *
 * If called with a reference to a (jx_t*) as the second argument, then it'll
 * store the "config.styles" pointer there if the style is found.  If the style
 * is not found, the function will return NULL.  The config code itself uses
 * this method to avoid creating bogus/misspelled names.
 */
jx_t *jx_config_style(const char *name, jx_t **refstyles)
{
	jx_t *styles, *scan, *style;

	/* Find "config.styles" */
	styles = jx_by_key(jx_config, "styles");
	assert(styles != NULL && styles->type == JX_ARRAY);

	/* Scan for the requested style.  We don't use jx_by_key_value()
	 * for this because we hope to avoid converting the name from (char *)
	 * to (jx_t *).
	 */
	for (scan = jx_first(styles); scan; scan = jx_next(scan)) {
		assert(scan->type == JX_OBJECT);
		style = jx_by_key(scan, "style");
		assert(style && style->type == JX_STRING);
		if (!jx_mbs_casecmp(style->text, name)) {
			/* Found!  Return it.  Note that config.styles is
			 * not a deferred array, so we don't need to call
			 * jx_break()
			 */
			if (refstyles)
				*refstyles = styles;
			return scan;
		}
	}

	/* Not found.  If called from the config code below, (i.e. if refstyles
	 * is not NULL) then return NULL.
	 */
	if (refstyles)
		return NULL;

	/* Okay, we were called from a plugin's initialization code, so we need
	 * to add it.  Start with a copy of "normal" (the first element of
	 * config.styles) and stuff the new name into the copy.
	 */
	scan = jx_copy(styles->first);
	jx_append(scan, jx_key("style", jx_string(name, -1)));
	jx_append(styles, scan);
	return scan;
}

/* Get an option from a given section of the settings.  If you pass NULL
 * for the section name, then it'll look in the top level of the config data,
 * or in the "interactive" or "batch" subsection as appropriate.  Returns
 * the jx_t of the found value, or NULL if not found.
 */
jx_t *jx_config_get(const char *section, const char *key)
{
	jx_t *jsect;

	/* Locate the section.  If section is NULL, use the format settings */
	if (section) {
		jsect = jx_by_expr(jx_config, section, NULL); /* undeferred */
		if (!jsect)
			return NULL;
	} else {
		jsect = jx_by_key(jx_config, jx_text_by_key(jx_system, "runmode"));
		if (jx_by_key(jsect, key) == NULL)
			jsect = jx_config;
	}

	/* Look for the requested key in that section */
	return jx_by_key(jsect, key);
}

/* Set an option in a given section of the settings.  If you pass NULL for
 * the section name, then it'll put it in the top level of the config data,
 * or in the "interactive" or "batch" subsection as appropriate.  If you
 * pass a non-NULL section name and that name doesn't exist, then it'll be
 * added.  THIS FUNCTION DOESN'T VERIFY THAT NAMES OR DATA TYPES ARE CORRECT.
 * Also, the "value" is incorporated into the jx_config tree, so it can't be
 * used anywhere else in order to avoid memory issues.  Maybe use jx_copy().
 */
void jx_config_set(const char *section, const char *key, jx_t *value)
{
	jx_t	*jsect;

	/* Locate the section.  If section is NULL, use all of jx_config,
	 * or the "interactive" or "batch" subsection, as appropriate.
	 * If a section name is given but the requested section doesn't exist,
	 * then create an empty object to hold that section.
	 */
	if (section) {
		/* Use the named section */
		jsect = jx_by_expr(jx_config, section, NULL); /* undeferred */
		if (!jsect) {
			if (!strncmp(section, "plugin.", 7)) {
				jsect = jx_by_key(jx_config, "plugin");
				jx_append(jsect, jx_key(section + 7, jx_object()));
				jsect = jx_by_key(jsect, section + 7);
			} else {
				jsect = jx_object();
				jx_append(jx_config, jx_key(section, jsect));
			}
		}
	} else {
		/* Use the top level of the config... or, in the "interactive"
		 * or "batch" subsection, as appropriate, if the key already
		 * exists there.
		 */
		jsect = jx_by_key(jx_config, jx_text_by_key(jx_system, "runmode"));
		if (!jx_by_key(jsect, key))
			jsect = jx_config;
	}

	/* Append the value to the section */
	jx_append(jsect, jx_key(key, value));
}

/* Parse an option string, and merge its settings into a given section.
 *
 * The "settings" string is essentially a comma-delimited list of name=value
 * settings, except that if you omit the "=value" then it tries to get clever.
 * If the name is a boolean, it'll set it to true; if name as a "no" prefix
 * with a boolean setting's suffix, then it'll set it to false.  If the name
 * appears in a "option-list" member with a corresponding string member
 * named "option", then it'll set the option to the name (e.g., if "fg-list"
 * is an array containing "red", then just saying "red" means "fg=red".
 *
 * Also, "name=value" where name is an object, then it'll be parsed as a
 * space-delimited list of words for setting boolean and "-list" members
 * within that object. (E.g., "format=sh noquote" will set format.table=sh
 * and format.quote=false.)
 *
 * In all cases, "name" may actually include "." to go into nested objects.
 *
 * "refend" is NULL normally.  When called recursively to parse settings for
 * an object (subsection), it stores the end of the parsed value there.  This
 * also has the side-effect if disabling the use of commas as delimiters,
 * so all of the object's settings must be space-delimited.
 *
 * Returns NULL on success, or a jx_t containing an error message if error.
 */
jx_t *jx_config_parse(jx_t *config, const char *settings, const char **refend)
{
	size_t	namelen, namealloc = 0, len;
	int	negate;
	char	*name = NULL;
	const char *value;
	jx_t	*thisconfig, *found, *list, *jvalue;

	/* Passing NULL for config is equivalent to passing jx_config */
	if (!config)
		config = jx_config;

	/* Until we hit the end... */
	while (*settings && (!refend || *settings != ',')) {
		/* Skip whitespace or commas between settings */
		if (isspace(*settings) || *settings == ',') {
			settings++;
			continue;
		}

		/* Note whether there's a "-" */
		negate = 0;
		if (*settings == '-') {
			settings++;
			negate = 1;
		}

		/* Count characters in the name */
		for (namelen = 0; isalnum(settings[namelen]); namelen++) {
		}
		if (namelen == 0) {
			free(name);
			if (refend)
				*refend = settings;
			return jx_error_null(0, "Malformed option \"%s\"", settings);
		}

		/* Make a copy of the name */
		if (namealloc < namelen + 1) {
			namealloc = (namelen | 0x1f) + 1;
			name = (char *)realloc(name, namealloc);
		}
		strncpy(name, settings, namelen);
		name[namelen] = 0;

		/* Look for an existing member with that key */
		thisconfig = config;
		found = jx_by_key(config, name);

		/* If not found in "config" then try the whole config, or
		 * in the "styles" array.
		 */
		if (!found) {
			thisconfig = jx_config;
			found = jx_by_key(jx_config, name);
		}
		if (!found) {
			found = jx_config_style(name, &thisconfig);
		}

		/* Followed by "=" ?  Or, equivalently, '.' to make deeply
		 * nested objects more JavaScript-like?
		 */
		if (settings[namelen] == '=' || settings[namelen] == '.') {
			/* If the member doesn't exist, that's an error */
			if (!found) {
				found = jx_error_null(0, "Unknown option \"%s\"", name);
				free(name);
				if (refend)
					*refend = settings;
				return found;
			}

			/* Value is after "=" or '.' */
			value = settings + namelen + 1;

			/* Processing of the value depends on the type */
			switch (found->type) {
			case JX_OBJECT:
				{
					/* Recursively parse the object's settings */
					const char *end;
					jvalue = jx_config_parse(found, settings + namelen + 1, &end);
					if (jvalue) {
						/* Oops, we found an error */
						free(name);
						if (refend)
							*refend = settings;
						return jvalue;
					}
					settings = end;
				}
				continue;

			case JX_BOOLEAN:
				/* should be true or false */
				if (!strncasecmp(value, "true", 4) && !isalnum(value[4])) {
					strcpy(found->text, "true");
					settings = value + 4;
				} else if (!strncasecmp(value, "false", 5) && !isalnum(value[5])) {
					strcpy(found->text, "false");
					settings = value + 5;
				} else {
					free(name);
					if (refend)
						*refend = settings;
					return jx_error_null(0, "Invalid boolean option value \"%s\"", value);
				}
				continue;

			case JX_NUMBER:
				{
					char	*lend, *dend;
					long	l;
					double	d;

					/* May be int or float */
					d = strtod(value, &dend);
					l = strtol(value, &lend, 0);
					if (lend == value) {
						free(name);
						if (refend)
							*refend = settings;
						return jx_error_null(0, "Invalid number value \"%s\"", value);
					}
					if (lend < dend) {
						/* It's floating-point */
						found->text[0] = 0;
						found->text[1] = 'd';
						JX_DOUBLE(found) = d;
						settings = dend;
					} else {
						/* It's integer */
						found->text[0] = 0;
						found->text[1] = 'i';
						JX_INT(found) = (int)l;
						settings = lend;
					}
				}
				continue;

			case JX_STRING:
				/* May be quoted or unquoted */
				if (*value == '"' || *value == '\'') {
					size_t mbslen;

					/* Quoted */
					for (len = 1; value[len] && value[len] != value[0]; len++) {
						if (value[len] == '\\' && value[len + 1])
							len++;
					}
					mbslen = jx_mbs_unescape(NULL, value + 1, len - 1);
					jvalue = jx_string("", mbslen);
					jx_mbs_unescape(jvalue->text, value + 1, len - 1);
					settings = value + len + 1;
				} else {
					/* Unquoted */
					for (len = 0; value[len] && value[len] != ',' && value[len] != ' '; len++) {
					}
					jvalue = jx_string(value, len);
					settings = value + len;
				}
				jx_append(thisconfig, jx_key(name, jvalue));
				continue;

			default:
				found = jx_error_null(0, "Bad type for option \"%s\"", name);
				free(name);
				if (refend)
					*refend = settings;
				return found;
			}

		} else { /* name or noname without = */
			if (found && found->type == JX_BOOLEAN) {
				strcpy(found->text, negate ? "false" : "true" );
				settings += namelen;
				continue;
			} else if (found) {
				found = jx_error_null(0, "Option \"%s\" is not boolean", name);
				free(name);
				if (refend)
					*refend = settings;
				return found;
			} else if (!strncmp(name, "no", 2)) {
				thisconfig = config;
				found = jx_by_key(config, name + 2);
				if (!found) {
					thisconfig = jx_config;
					found = jx_by_key(jx_config, name + 2);
				}
				/* NOTE: We don't need to check for a color name
				 * because you can never say "set noprompt".
				 */
				if (found && found->type == JX_BOOLEAN) {
					strcpy(found->text, "false");
					settings += namelen;
					continue;
				}
			}

			/* If we get here, then the only remaining possibility
			 * for success is if it's a value from an enumerated
			 * list.  The list could only be in "config", not at
			 * the top-level "jx_config".  Look for lists!
			 */
			for (list = config->first; list; list = list->next) { /* object */
				/* Skip if not "somename-list" array */
				len = strlen(list->text);
				if (len <= 5
				 || strcasecmp(list->text + len - 5, "-list")
				 || list->first->type != JX_ARRAY)
					continue;

				/* For each element in the list... */
				for (found = jx_first(list->first); found; found = jx_next(found)) {
					/* Skip if not a string */
					if (found->type != JX_STRING)
						continue;

					/* Skip if not a match.  Since the list
					 * elements can contain spaces, we can't
					 * compare to "name" -- we need to look
					 * at the "settings" string.
					 */
					len = jx_mbs_len(found->text);
					if (!jx_mbs_ncasecmp(found->text, settings, len)
						&& !isalnum(settings[strlen(found->text)])) {
						/* FOUND! */
						goto BreakBreak;
					}
				}
			}

			/* Dang.  What is this? */
			found = jx_error_null(0, "Unknown option \"%s\"", name);
			free(name);
			if (refend)
				*refend = settings;
			return found;

BreakBreak:
			/* We found it, list->text is the name but with "-list"
			 * appended, and found->text is the new value.  I would
			 * rather not mangle list-text even temporarily, so
			 * we'll copy it into the "name" buffer first.
			 */
			namelen = strlen(list->text) - 5;
			if (namealloc < namelen + 1) {
				namealloc = (namelen | 0x1f) + 1;
				name = (char *)realloc(name, namealloc);
			}
			strncpy(name, list->text, namelen);
			name[namelen] = '\0';

			/* Store the setting as a string */
			jx_append(config, jx_key(name, jx_string(found->text, -1)));

			/* If the list was deferred (unlikely!) then let the
			 * library know that we abandoned the scanning loop
			 * before its end.  The "found" pointer will be invalid
			 * after this.
			 */
			jx_break(found);

			/* Move past the enumerated value */
			settings += strlen(found->text);
			continue;
		}
	}

	/* Hit the end of settings without trouble, hooray! */
	free(name);
	if (refend)
		*refend = settings;
	return NULL;
}
