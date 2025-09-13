#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <assert.h>
#include <jsoncalc.h>

/* This is an array of objects describing loaded plugins.  The "plugin"
 * member is name of the plugin.
 */
json_t *json_plugins;

#if defined(STATICCACHE) || defined(STATICCSV) || defined(STATICCURL) || defined(STATICEXAMPLE) || defined(STATICLOG) || defined(STATICMATH)
/* The following code is only needed when the jsoncalc library is compiled as
 * a static library.  We still want access to some plugins, but the -ldl
 * library for loading plugins at runtime doesn't work for statically-linked
 * programs, so we need to simulate parts of it.
 */
#define STATICPLUGINS

/* These "extern" declarations of plugins' initialization functions are needed
 * for the staticplugins[] array to compile correctly.  Extern declarations
 * do not cause the function (and the rest of each plugin) to be included in
 * the statically-linked program.
 */
extern char *plugincache();
extern char *plugincsv();
extern char *plugincurl();
extern char *pluginexample();
extern char *pluginlog();
extern char *pluginmath();

/* This is a list of plugins and their initialization functions.  Each line is
 * conditionally compiled because referencing a plugin's init function *WILL*
 * cause the whole plugin to be included in the statically-linked program.
 */
static struct staticplugins_s {
	char	*name;
	char	*(*init)();
} staticplugins[] = {
#ifdef STATICCACHE
	{"cache", plugincache},
#endif
#ifdef STATICCSV
	{"csv", plugincsv},
#endif
#ifdef STATICCURL
	{"curl", plugincurl},
#endif
#ifdef STATICEXAMPLE
	{"example", pluginexample},
#endif
#ifdef STATICLOG
	{"log", pluginlog},
#endif
#ifdef STATICMATH
	{"math", pluginmath},
#endif
	{NULL}
};

/* Emulate the -ldl functions */
void *dlopen(const char *filename, int flags)
{
	int	i;

	assert(!strncmp(filename, "static ", 7));

	/* The "filename" is really just the unadorned plugin name, for the
	 * statically-linked version.  Search for it in staticplugins[].
	 */
	for (i = 0; staticplugins[i].name; i++) {
		if (!strcmp(filename + 7, staticplugins[i].name))
			return (void *)&staticplugins[i];
	}

	/* Not found, return NULL */
	return NULL;
}

void *dlsym(void *handle, const char *name)
{
	/* The only thing we ever look up is the plugin's init function */
	return ((struct staticplugins_s *)handle)->init;
}

int dlclose(void *handle)
{
	return 0;
}

char *dlerror(void)
{
	/* The only error we can encounter is "plugin not found" */
	return "NoStaticPlugin:The requested plugin is not included in this statically-linked program";
}


#endif /* defined(STATICPLUGINS) */


/* Load a plugin.  Returns NULL on success, or an error null on failure */
json_t *json_plugin_load(const char *name)
{
	char	*binfile;
	char	*scriptfile;
	void	*dlhandle;
	char	initname[100];
	char	*(*initfn)(void);
	char	*err;
	json_t	*info, *val;

	/* If already loaded, do nothing */
	for (info = json_plugins->first; info; info = info->next) { /* undeferred */
		val = json_by_key(info, "name");
		if (val && val->type == JSON_STRING && !strcmp(val->text, name))
			return NULL;
	}

	/* Look for a binary plugin */
#ifdef STATICPLUGINS
	binfile = (char *)malloc(8 + strlen(name));
	sprintf(binfile, "static %s", name);
	if (!dlopen(binfile, 0)) {
		free(binfile);
		binfile = NULL;
	}
#else
	binfile = json_file_path("plugin",name,".so");
	if (!binfile)
		binfile = json_file_path("plugin",name,".so");
#endif

	/* Look for a script file */
	scriptfile = json_file_path("plugin",name,".jc");

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
		snprintf(initname, sizeof initname, "plugin%s", name);
		*(void **)(&initfn) = dlsym(dlhandle, initname);
		if (!initfn) {
			char *error = dlerror();
			info = json_error_null(0, "The \"%s\" plugin has no %s() function (%s)", initname, error);
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
	json_append(info, json_key("binary", binfile ? json_string(binfile, -1) : json_null()));
	json_append(info, json_key("script", scriptfile ? json_string(scriptfile, -1) : json_null()));
	json_append(json_plugins, info);

	/* Success */
	return NULL;
}
