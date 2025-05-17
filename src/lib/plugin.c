#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include "json.h"

/* This is an array of objects describing loaded plugins.  The "plugin"
 * member is name of the plugin.
 */
json_t *json_plugins;

/* Load a plugin.  Returns NULL on success, or an error null on failure */
json_t *json_plugin_load(const char *name, int major, int minor)
{
	char	*binfile;
	char	*scriptfile;
	void	*dlhandle;
	char	*(*initfn)(void);
	char	*err;
	json_t	*info, *val;

	/* If already loaded, do nothing */
	for (info = json_plugins->first; info; info = info->next) {
		val = json_by_key(info, "plugin");
		if (val && val->type == JSON_STRING && !strcmp(val->text, name))
			return NULL;
	}

	/* Look for a binary plugin */
	binfile = json_file_path("lib",name,".so", 0, 0);
	if (!binfile)
		binfile = json_file_path("lib",name,".so", 0, 0);

	/* Look for a script file */
	scriptfile = NULL;

	/* If neither was found, fail */
	if (!binfile && !scriptfile)
		return json_error_null(0, "The \"%s\" plugin could not be located", name);

	/* If there's a binary plugin, load it and run its "init" function */
	if (binfile) {
		/* Load it */
		dlhandle = dlopen(binfile, RTLD_LAZY | RTLD_LOCAL);
		if (!dlhandle)
			return json_error_null(0, "The \"%s\" plugin could not be loaded", name);

		/* Find the init function */
		dlerror(); /* clear the error status */
		*(void **)(&initfn) = dlsym(dlhandle, "init");
		if (!initfn) {
			char *error = dlerror();
			info = json_error_null(0, "The \"%s\" plugin has no init() function (%s)", name, error);
			dlclose(dlhandle);
			return info;
		}

		/* Invoke the init function */
		err = initfn();
		if (err) {
			dlclose(dlhandle);
			return json_error_null(0, "The \"%s\" plugin failed to initialize: %s", name, err);
		}
	}

	/* If there's a script file, load it */
	if (scriptfile) {
	}

	/* IF WE GET HERE, IT WAS SUCCESSFULLY LOADED */

	/* Add it to the json_plugin object */
	info = json_object();
	json_append(info, json_key("name", json_string(name, -1)));
	json_append(info, json_key("major", major ? json_from_int(major) : json_null()));
	json_append(info, json_key("minor", minor ? json_from_int(minor) : json_null()));
	json_append(info, json_key("binary", binfile ? json_string(binfile, -1) : json_null()));
	json_append(info, json_key("script", scriptfile ? json_string(scriptfile, -1) : json_null()));
	json_append(json_plugins, info);

	/* Success */
	return NULL;
}
