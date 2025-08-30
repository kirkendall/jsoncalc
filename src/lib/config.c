#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <jsoncalc.h>
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
	"\"emptyobject\":\"object\","
	"\"defersize\":1000000,"
	"\"prompt\":{"
		"\"bold\":true,"
		"\"dim\":false,"
		"\"italic\":false,"
		"\"underlined\":false,"
		"\"blinking\":false,"
		"\"linethru\":false"
		"\"fg\":\"cyan\","
		"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
		"\"bg\":\"on normal\","
		"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
	"},"
	"\"prompt\":{"
		"\"bold\":false,"
		"\"dim\":false,"
		"\"italic\":false,"
		"\"underlined\":false,"
		"\"blinking\":false,"
		"\"linethru\":false"
		"\"fg\":\"normal\","
		"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
		"\"bg\":\"on normal\","
		"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
	"},"
	"\"result\":{"
		"\"bold\":false,"
		"\"dim\":false,"
		"\"italic\":false,"
		"\"underlined\":false,"
		"\"blinking\":false,"
		"\"linethru\":false"
		"\"fg\":\"normal\","
		"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
		"\"bg\":\"on normal\","
		"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
	"},"
	"\"error\":{"
		"\"fg\":\"red\","
		"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
		"\"bg\":\"on normal\","
		"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
		"\"bold\":true,"
		"\"dim\":false,"
		"\"italic\":false,"
		"\"underlined\":false,"
		"\"blinking\":false,"
		"\"linethru\":false"
	"},"
	"\"gridhead\":{"
		"\"bold\":true,"
		"\"dim\":false,"
		"\"italic\":false,"
		"\"underlined\":false,"
		"\"blinking\":false,"
		"\"linethru\":false"
		"\"fg\":\"blue\","
		"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
		"\"bg\":\"on normal\","
		"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
	"},"
	"\"gridline\":{"
		"\"bold\":true,"
		"\"dim\":false,"
		"\"italic\":false,"
		"\"underlined\":false,"
		"\"blinking\":false,"
		"\"linethru\":false"
		"\"fg\":\"blue\","
		"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
		"\"bg\":\"on normal\","
		"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
	"},"
	"\"debug\":{"
		"\"bold\":true,"
		"\"dim\":false,"
		"\"italic\":false,"
		"\"underlined\":false,"
		"\"blinking\":false,"
		"\"linethru\":false"
		"\"fg\":\"blue\","
		"\"fg-list\":[\"normal\",\"black\",\"red\",\"green\",\"yellow\",\"blue\",\"magenta\",\"cyan\",\"white\"],"
		"\"bg\":\"on normal\","
		"\"bg-list\":[\"on normal\",\"on black\",\"on red\",\"on green\",\"on yellow\",\"on blue\",\"on magenta\",\"on cyan\",\"on white\"],"
	"},"
	"\"plugin\":{}"
"}";


/* This stores a pointer to the config data */
json_t *json_config;

/* This is a combination of all system data.  It is initialized by
 * json_config_load(), though other code may add to it.
 */
json_t *json_system;

/* Merge new settings into old settings. */
static void merge(json_t *old, json_t *newload)
{
	json_t *newkey, *oldmem;

	/* Only works on objects */
	if (old->type != JSON_OBJECT || newload->type != JSON_OBJECT)
		return;

	/* For each new member */
	for (newkey = newload->first; newkey; newkey = newkey->next) {
		/* Look for a corresponding old member */
		oldmem = json_by_key(old, newkey->text);

		/* If no corresponding old member, then add a copy of new */
		if (!oldmem) {
			json_append(old, json_key(newkey->text, json_copy(newkey->first)));
			continue;
		}

		/* If both are objects, merge recursively */
		if (oldmem->type == JSON_OBJECT && newkey->first->type == JSON_OBJECT) {
			merge(oldmem, newkey->first);
			continue;
		}

		/* If both are some other type, then replace the value */
		if (oldmem->type == newkey->first->type) {
			json_append(old, json_key(newkey->text, json_copy(newkey->first)));
			continue;
		}

		/* Otherwise ignore it.  This could happen if JsonCalc's
		 * option format got redefined, so the new settings 
		 */
	}
}

/* Return an array of directory names to look in for files related to JsonCalc
 * -- plugins and documentation mostly.
 */
static json_t *configpath(const char *envvar)
{
	const char *env;
	json_t *path, *entry;
	int	isjsoncalcpath;
	size_t	len;

	/* If no envvar then just fake it completely */
	if (!envvar) {
		path = json_array();
		json_append(path, json_string("~/.config/jsoncalc", -1));
		json_append(path, json_string("/usr/local/lib64/jsoncalc", -1));
		json_append(path, json_string("/usr/local/lib/jsoncalc", -1));
		json_append(path, json_string("/usr/lib64/jsoncalc", -1));
		json_append(path, json_string("/usr/lib/jsoncalc", -1));
		json_append(path, json_string("/lib64/jsoncalc", -1));
		json_append(path, json_string("/lib/jsoncalc", -1));
		json_append(path, json_string("/usr/local/share/jsoncalc", -1));
		json_append(path, json_string("/usr/share/jsoncalc", -1));
		return path;
	}

	/* Fetch the value.  If unset, return NULL */
	env = getenv(envvar);
	if (!env)
		return NULL;

	/* Distinguish between $JSONCALCPATH and other paths */
	isjsoncalcpath = !strcmp(envvar, "JSONCALCPATH");

	/* Start building an array of entries.  If not $JSONCALCPATH then
	 * put the config directory first.
	 */
	path = json_array();
	if (!isjsoncalcpath)
		json_append(path, json_string("~/.config/jsoncalc", -1));

	/* For each entry in the path... */
	while (*env) {
		/* Find the end of this entry */
		for (len = 0; env[len] && env[len] != JSON_PATH_DELIM; len++) {
		}

		/* Convert it to a string.  If not from $JSONCALCPATH then
		 * add "/jsoncalc" to the string.
		 */
		if (len == 0)
			entry = json_string(".", 1);
		else if (isjsoncalcpath) {
			entry = json_string(env, len);
		} else {
			entry = json_string(env, len + 9);
			strcat(entry->text, "/jsoncalc");
		}

		/* Add it to the path */
		json_append(path, entry);

		/* Move to the next entry */
		env += len;
		if (*env == JSON_PATH_DELIM)
			env++;
	}

	/* If not $JSONCALCPATH then add shared directories */
	if (!isjsoncalcpath) {
		json_append(path, json_string("/usr/local/share/jsoncalc", -1));
		json_append(path, json_string("/usr/share/jsoncalc", -1));
	}
	return path;
}

/* Load the configuration data, and return it */
void json_config_load(const char *name)
{
	char	*pathname;
	json_t	*conf, *value;

	/* Load the default config */
	json_config = json_parse_string(defaultconfig);

	/* If json_system isn't set up yet, then set it up now */
	if (!json_system) {
		json_system = json_object();

		/* We also want to add the path for plugins and documentation.
		 * This is from $JSONCALCPATH, but if $JSONCALCPATH isn't set
		 * then we want to derive it from $LD_LIBRARY_PATH.  And if
		 * $LD_LIBRARY_PATH isn't set then we simulate that too.
		 */
		value = configpath("JSONCALCPATH");
		if (!value)
			value = configpath("LD_LIBRARY_PATH");
		if (!value)
			value = configpath(NULL);
		json_append(json_system, json_key("path", value));

		json_append(json_system, json_key("config", json_config));
		pathname = json_file_path(NULL, NULL, NULL);
		json_append(json_system, json_key("configdir", json_string(pathname, -1)));
		free(pathname);
		if (!json_plugins)
			json_plugins = json_array();
		json_append(json_system, json_key("plugins", json_plugins));
		conf = json_array();
		json_append(conf, json_string("json", -1));
		json_append(json_system, json_key("extensions", conf));

		json_append(json_system, json_key("runmode", json_string("interactive", -1)));
		json_append(json_system, json_key("update", json_bool(0)));
		json_append(json_system, json_key("JSON", json_object()));
		json_append(json_system, json_key("Math", json_object()));
		json_append(json_system, json_key("version", json_number(JSON_VERSION, -1)));
		json_append(json_system, json_key("copyright", json_string(JSON_COPYRIGHT, -1)));

	}

	/* Look for the config file */
	pathname = json_file_path(NULL, name, ".json");
	if (!pathname)
		return;

	/* Parse it */
	conf = json_parse_file(pathname);
	if (!conf)
		return;
	if (conf->type != JSON_OBJECT) {
		json_free(conf);
		return;
	}

	/* Merge its settings into the default config */
	merge(json_config, conf);

	/* Free the data from the file */
	json_free(conf);
}

/* Select whether this part of json_config gets saved.  It should save
 * everything except members that have names ending with "-list", and a
 * few others.
 */
static int notlist(json_t *mem)
{
	/* Non-members are always kept */
	if (mem->type != JSON_KEY)
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
void json_config_save(const char *name)
{
	char	*pathname;
	json_t	*copy;
	FILE	*fp;

	/* We generally want to save options in the first writable directory
	 * in the JSONCALCPATH, even if the "config.json" file doesn't exist
	 * there yet.
	 */
	pathname = json_file_path(NULL, NULL, NULL);
	if (pathname) {
		char *tmp = malloc(strlen(pathname) + strlen(name) + 6);
		strcpy(tmp, pathname);
		strcat(tmp, name);
		strcat(tmp, ".json");
		free(pathname);
		pathname = tmp;
	} else {
		pathname = json_file_path(NULL, name, ".json");
		if (!pathname)
			return; /* no place to save it */
	}

	/* We want to save everything EXCEPT members with names ending with
	 * "-list".  Make a copy of the config with "-list" members removed.
	 */
	copy = json_copy_filter(json_config, notlist);

	/* Write it to the file */
	fp = json_file_update(pathname);
	if (fp) {
		jsonformat_t fmt = json_format_default;
		fmt.string = fmt.elem = fmt.sh = fmt.ascii = fmt.color = 0;
		fmt.fp = fp;
		json_print(copy, &fmt);
		fclose(fp);
	}
	json_free(copy);
}

/* Get an option from a given section of the settings.  If you pass NULL
 * for the section name, then it'll look in the top level of the config data,
 * or in the "interactive" or "batch" subsection as appropriate.  Returns
 * the json_t of the found value, or NULL if not found.
 */
json_t *json_config_get(const char *section, const char *key)
{
	json_t *jsect;

	/* Locate the section.  If section is NULL, use the format settings */
	if (section) {
		jsect = json_by_expr(json_config, section, NULL);
		if (!jsect)
			return NULL;
	} else {
		jsect = json_by_key(json_config, json_text_by_key(json_system, "runmode"));
		if (json_by_key(jsect, key) == NULL)
			jsect = json_config;
	}

	/* Look for the requested key in that section */
	return json_by_key(jsect, key);
}

/* Set an option in a given section of the settings.  If you pass NULL for
 * the section name, then it'll put it in the top level of the config data,
 * or in the "interactive" or "batch" subsection as appropriate.  If you
 * pass a non-NULL section name and that name doesn't exist, then it'll be
 * added.  THIS FUNCTION DOESN'T VERIFY THAT NAMES OR DATA TYPES ARE CORRECT.
 * Also, the "value" is incorporated into the json_config tree, so it can't be
 * used anywhere else in order to avoid memory issues.  Maybe use json_copy().
 */
void json_config_set(const char *section, const char *key, json_t *value)
{
	json_t	*jsect;

	/* Locate the section.  If section is NULL, use all of json_config,
	 * or the "interactive" or "batch" subsection, as appropriate.
	 * If a section name is given but the requested section doesn't exist,
	 * then create an empty object to hold that section.
	 */
	if (section) {
		/* Use the named section */
		jsect = json_by_expr(json_config, section, NULL);
		if (!jsect) {
			if (!strncmp(section, "plugin.", 7)) {
				jsect = json_by_key(json_config, "plugin");
				json_append(jsect, json_key(section + 7, json_object()));
				jsect = json_by_key(jsect, section + 7);
			} else {
				jsect = json_object();
				json_append(json_config, json_key(section, jsect));
			}
		}
	} else {
		/* Use the top level of the config... or, in the "interactive"
		 * or "batch" subsection, as appropriate, if the key already
		 * exists there.
		 */
		jsect = json_by_key(json_config, json_text_by_key(json_system, "runmode"));
		if (!json_by_key(jsect, key))
			jsect = json_config;
	}

	/* Append the value to the section */
	json_append(jsect, json_key(key, value));
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
 * Returns NULL on success, or a json_t containing an error message if error.
 */
json_t *json_config_parse(json_t *config, const char *settings, const char **refend)
{
	size_t	namelen, namealloc = 0, len;
	int	negate;
	char	*name = NULL;
	const char *value;
	json_t	*thisconfig, *found, *list, *jvalue;

	/* Passing NULL for config is equivalent to passing json_config */
	if (!config)
		config = json_config;

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
			return json_error_null(0, "Malformed option \"%s\"", settings);
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
		found = json_by_key(config, name);

		/* If not found in "config" then try the whole config */
		if (!found) {
			thisconfig = json_config;
			found = json_by_key(json_config, name);
		}

		/* Followed by "=" ?  Or, equivalently, '.' to make deeply
		 * nested objects more JavaScript-like?
		 */
		if (settings[namelen] == '=' || settings[namelen] == '.') {
			/* If the member doesn't exist, that's an error */
			if (!found) {
				found = json_error_null(0, "Unknown option \"%s\"", name);
				free(name);
				return found;
			}

			/* Value is after "=" or '.' */
			value = settings + namelen + 1;

			/* Processing of the value depends on the type */
			switch (found->type) {
			case JSON_OBJECT:
				{
					/* Recursively parse the object's settings */
					const char *end;
					jvalue = json_config_parse(found, settings + namelen + 1, &end);
					if (jvalue) {
						/* Oops, we found an error */
						free(name);
						return jvalue;
					}
					settings = end;
				}
				continue;

			case JSON_BOOL:
				/* should be true or false */
				if (!strncasecmp(value, "true", 4) && !isalnum(value[4])) {
					strcpy(found->text, "true");
					settings = value + 4;
				} else if (!strncasecmp(value, "false", 5) && !isalnum(value[5])) {
					strcpy(found->text, "false");
					settings = value + 5;
				} else {
					free(name);
					return json_error_null(0, "Invalid boolean option value \"%s\"", value);
				}
				continue;

			case JSON_NUMBER:
				{
					char	*lend, *dend;
					long	l;
					double	d;

					/* May be int or float */
					d = strtod(value, &dend);
					l = strtol(value, &lend, 0);
					if (lend == value) {
						free(name);
						return json_error_null(0, "Invalid number value \"%s\"", value);
					}
					if (lend < dend) {
						/* It's floating-point */
						found->text[0] = 0;
						found->text[1] = 'd';
						JSON_DOUBLE(found) = d;
						settings = dend;
					} else {
						/* It's integer */
						found->text[0] = 0;
						found->text[1] = 'i';
						JSON_INT(found) = (int)l;
						settings = lend;
					}
				}
				continue;

			case JSON_STRING:
				/* May be quoted or unquoted */
				if (*value == '"' || *value == '\'') {
					size_t mbslen;

					/* Quoted */
					for (len = 1; value[len] && value[len] != value[0]; len++) {
						if (value[len] == '\\' && value[len + 1])
							len++;
					}
					mbslen = json_mbs_unescape(NULL, value + 1, len - 1);
					jvalue = json_string("", mbslen);
					json_mbs_unescape(jvalue->text, value + 1, len - 1);
					settings = value + len + 1;
				} else {
					/* Unquoted */
					for (len = 0; value[len] && value[len] != ',' && value[len] != ' '; len++) {
					}
					jvalue = json_string(value, len);
					settings = value + len;
				}
				json_append(thisconfig, json_key(name, jvalue));
				continue;

			default:
				found = json_error_null(0, "Bad type for option \"%s\"", name);
				free(name);
				return found;
			}

		} else { /* name or noname without = */
			if (found && found->type == JSON_BOOL) {
				strcpy(found->text, negate ? "false" : "true" );
				settings += namelen;
				continue;
			} else if (found) {
				found = json_error_null(0, "Option \"%s\" is not boolean", name);
				free(name);
				return found;
			} else if (!strncmp(name, "no", 2)) {
				thisconfig = config;
				found = json_by_key(config, name + 2);
				if (!found) {
					thisconfig = json_config;
					found = json_by_key(json_config, name + 2);
				}
				if (found && found->type == JSON_BOOL) {
					strcpy(found->text, "false");
					settings += namelen;
					continue;
				}
			}

			/* If we get here, then the only remaining possibility
			 * for success is if it's a value from an enumerated
			 * list.  The list could only be in "config", not at
			 * the top-level "json_config".  Look for lists!
			 */
			for (list = config->first; list; list = list->next) {
				/* Skip if not "somename-list" array */
				len = strlen(list->text);
				if (len <= 5
				 || strcasecmp(list->text + len - 5, "-list")
				 || list->first->type != JSON_ARRAY)
					continue;

				/* For each element in the list... */
				for (found = list->first->first; found; found = found->next) {
					/* Skip if not a string */
					if (found->type != JSON_STRING)
						continue;

					/* Skip if not a match.  Since the list
					 * elements can contain spaces, we can't
					 * compare to "name" -- we need to look
					 * at the "settings" string.
					 */
					len = json_mbs_len(found->text);
					if (!json_mbs_ncasecmp(found->text, settings, len)
						&& !isalnum(settings[strlen(found->text)])) {
						/* FOUND! */
						goto BreakBreak;
					}
				}
			}

			/* Dang.  What is this? */
			found = json_error_null(0, "Unknown option \"%s\"", name);
			free(name);
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
			json_append(config, json_key(name, json_string(found->text, -1)));

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
