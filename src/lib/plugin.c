#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <jsoncalc.h>

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
		val = json_by_key(info, "name");
		if (val && val->type == JSON_STRING && !strcmp(val->text, name))
			return NULL;
	}

	/* Look for a binary plugin */
	binfile = json_file_path("plugin",name,".so", 0, 0);
	if (!binfile)
		binfile = json_file_path("plugin",name,".so", 0, 0);

	/* Look for a script file */
	scriptfile = json_file_path("plugin",name,".jc", 0, 0);

	/* If neither was found, fail */
	if (!binfile && !scriptfile)
		return json_error_null(0, "The \"%s\" plugin could not be located", name);

	/* If there's a binary plugin, load it and run its "init" function */
	if (binfile) {
		/* Load it */
		dlhandle = dlopen(binfile, RTLD_LAZY | RTLD_LOCAL);
		if (!dlhandle) {
			/* Try to shorten dlerror() output */
			err = dlerror();
			if (*err == '/') {
				while (*err && *err != ':')
					err++;
				if (*err == ':')
					err++;
				while (*err == ' ')
					err++;
				if (!*err)
					err = dlerror();
			}
			return json_error_null(0, "The \"%s\" plugin could not be loaded: %s", name, err);
		}

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
		jsoncmd_t *cmd = json_cmd_parse_file(scriptfile);
		if (cmd) {
			/* We don't have a context to run it in, so all we
			 * can do is free it.  This means script plugins can
			 * only load other plugins, or define functions.
			 */
			json_cmd_free(cmd);
		}
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
