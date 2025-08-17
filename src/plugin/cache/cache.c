#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <jsoncalc.h>

/* This is the name of the file that stores a cache's settings */
#define SETTINGS ".config"

/* These are the default settings, used for new caches. */
static char *config = "{"
	"\"seconds\":600,"
	"\"bytes\":0,"
	"\"touch\":false,"
	"\"dir\":\"\","
"}";


/* Generate the name of a cache file.  This incorporates directory name and
 * file extension from config, and optionally a version suffix.  The returned
 * name in a buffer that gets reused, so you may need to make a local copy.
 */
static char *cacheFile(const char *cache, char *index)
{
	json_t	*dir;
	char	*dirstr, *extstr, verstr[20];
	size_t	len;
	char	*buf;


	/* Get the directory, defaulting to "." */
	dir = json_config_get("plugin.cache", "dir");
	dirstr = dir ? dir->text : ".";

	/* Compute the size needed for this name */
	len = strlen(dirstr) + 1 + strlen(cache) + 1 + strlen(index);
	len++; /* for the terminating '\0' */

	/* Allocate the buffer */
	buf = (char *)malloc(len);

	/* Combine all of the pieces together.  Create the directory if it
	 * doesn't exist already
	 */
	if (access(dirstr, F_OK) < 0)
		mkdir(dirstr, 0700);
	sprintf(buf, "%s/%s", dirstr, cache);
	if (access(buf, F_OK) < 0)
		mkdir(buf, 0700);
	strcat(buf, "/");
	strcat(buf, index);

	/* Return it */
	return buf;
}

/* Test whether a cache or index name is safe.  If safe, return NULL, otherwise
 * return a string indicating why it is unsafe.
 */
static char *safeName(char *name)
{
	/* Must be non-empty, not start with ".", and not contain "/" */
	if (!*name)
		return "%s() %s can't be empty";
	if (*name == '.')
		return "%s() %s can't start with \".\"";
	if (strchr(name, '/'))
		return "%s() %s can't contain a \"/\"";
	if (strlen(name) > 255)
		return "%s() %s is too long";
	return NULL;
}

/* This is used with json_copy_filter() to copy config settings other than "dir"
 */
static int omitDir(json_t *json)
{
	if (json->type != JSON_KEY || strcmp(json->text, "dir"))
		return 1;
	return 0;
}


/* Clean a cache.  Can also be used to store settings.  Returns the current
 * settings, which the calling function must free via json_free() even if
 * passed settings.
 *
 * If an error is detected in the settings, then it returns a "null" json_t
 * containing an appropriate error message.
 */
static json_t *cleanCache(char *cache, json_t *newSettings)
{
	json_t	*settings;
	char	*filename;
	time_t	now;
	time_t	seconds;	/* derived from from "seconds" */
	off_t	maxbytes;	/* from "bytes" */
	off_t	totbytes;
	int	touch;	/* from "touch" */
	glob_t	globbuf;
	struct stat st;
	int	i;

	/* Load the settings */
	filename = cacheFile(cache, SETTINGS);
	if (access(filename, R_OK) < 0)
		settings = json_copy_filter(json_by_expr(json_config, "plugin.cache", NULL), omitDir);
	else
		settings = json_parse_file(filename);

	/* If given new settings, merge them into the current settings */
	if (newSettings) {
		/* Use the plugin settings as a list of valid names/types
		 * of cache settings (other than "dir").
		 */
		json_t *scan, *tmp;
		json_t *pluginSettings = json_by_expr(json_config, "plugin.cache", NULL);
		for (scan = newSettings->first; scan; scan = scan->next) {
			/* Validate the name/type via pluginSettings */
			tmp = json_by_key(pluginSettings, scan->text);
			if (!tmp) {
				json_free(settings);
				free(filename);
				return json_error_null(0, "Invalid setting name \"%s\" for %s()", scan->text, "cache");
			}
			if (tmp->type != scan->first->type) {
				json_free(settings);
				free(filename);
				return json_error_null(0, "Setting \"%s\" has wrong data type for %s()", scan->text, "cache");
			}

			/* Merge the value into settings */
			json_append(settings, json_copy(scan));
		}

		/* Save these settings */
		FILE *fp = json_file_update(filename);
		if (fp) {
			char *tmp = json_serialize(settings, NULL);
			fputs(tmp, fp);
			free(tmp);
			fclose(fp);
		}
	}
	free(filename);

	/* Extract the cleaning settings */
	time(&now);
	seconds = json_int(json_by_key(settings, "seconds"));
	maxbytes = json_int(json_by_key(settings, "bytes"));
	touch = json_is_true(json_by_key(settings, "touch"));

	/* Scan the files in the cache */
	filename = cacheFile(cache, "*");
	if (0 == glob(filename, GLOB_NOSORT|GLOB_NOESCAPE, NULL, &globbuf)) {
		/* For each match... */
		totbytes = 0;
		for (i = 0; i < globbuf.gl_pathc; i++) {
			/* Get its statistics */
			if (stat(globbuf.gl_pathv[i], &st) < 0)
				continue;

			/* If old, delete it.  Else count its size. */
			if (seconds > 0 && (touch ? st.st_atime : st.st_mtime) < now - seconds)
				unlink(globbuf.gl_pathv[i]);
			else
				totbytes += st.st_size;
		}

		/* If totbytes is bigger than the requested limit, then scale
		 * down the seconds and try one more time.
		 */
		if (maxbytes > 0 && totbytes > maxbytes) {
			seconds = seconds * ( (double)maxbytes / (double)totbytes );
			if (seconds < 5) /* even if 0! */
				seconds = 5;
			for (i = 0; i < globbuf.gl_pathc; i++) {
				/* Get its statistics */
				if (stat(globbuf.gl_pathv[i], &st) < 0)
					continue;

				/* If old, delete it. */
				if ((touch ? st.st_atime : st.st_mtime) < now - seconds)
					unlink(globbuf.gl_pathv[i]);
			}
		}
	}

	/* Return the settings */
	return settings;
}

/*****************************************************************************/

static json_t *jfn_cache(json_t *args, void *agdata)
{
	char	*cache, *index, *err;
	json_t	*settings;
	json_t	*data;
	char	*filename;
	time_t	now;
	time_t	seconds;	/* derived from from "seconds" */
	int	touch;
	struct stat st;

	/* Get the cache name */
	if (args->first->type != JSON_STRING)
		return json_error_null(0, "%s() wants a cache name as first argument", "cache");
	cache = args->first->text;
	err = safeName(cache);
	if (err)
		return json_error_null(0, err, "cache", "cacheName");

	/* If nothing else, then fetch settings and return them */
	if (!args->first->next)
		return cleanCache(cache, NULL);

	/* If given settings, store them */
	if (args->first->next->type == JSON_OBJECT )
		return cleanCache(cache, args->first->next);

	/* Otherwise the second argument must be an index */
	if (args->first->next->type != JSON_STRING)
		return json_error_null(0, "%s() wants an index string or settings object as the second argument", "cache");
	index = args->first->next->text;
	err = safeName(index);
	if (err)
		return json_error_null(0, err, "cache", "index");

	/* The third argument is data and can be absent or anything.  There
	 * must not be a third item.
	 */
	if (!args->first->next->next)
		data = NULL;
	else if (args->first->next->next->next)
		return json_error_null(0, "The %() function takes at most 3 arguments", "cache");
	else
		data = args->first->next->next;

	/* Get the name of this cache item */
	filename = cacheFile(cache, index);

	/* Do we have new data? */
	if (data) {
		/* Write the data to the cache */
		if (json_is_null(data))
			unlink(filename);
		else {
			FILE *fp = json_file_update(filename);
			if (fp) {
				char *tmp = json_serialize(data, NULL);
				fputs(tmp, fp);
				free(tmp);
				fclose(fp);
			}
		}

		/* If limiting the cache by size, then clean the cache */
		if (json_int(json_by_key(settings, "bytes")) > 0)
			json_free(cleanCache(cache, NULL));

		/* Make a copy of the data to return */
		data = json_copy(data);
	} else {
		/* If the file exists but is old, then clean the cache */
		time(&now);
		seconds = json_int(json_by_key(settings, "seconds"));
		touch = json_is_true(json_by_key(settings, "touch"));
		if (stat(filename, &st) >= 0
		 && (touch ? st.st_atime : st.st_mtime) + seconds < now)
			json_free(cleanCache(cache, NULL));

		/* Try to read the data from a file */
		data = json_parse_file(filename);

		/* If unreadable FOR ANY REASON, just return NULL */
		if (!data)
			data = json_null();
		if (json_is_null(data))
			memset(data->text, 0, sizeof data->text);
	}

	return data;
}

/*****************************************************************************/


/* Initialize the plugin */
char *plugincache(void)
{
	json_t	*settings, *plugin, *jd;
	char	*dir;

	/* Set the default options.  The config file hasn't been loaded yet
	 * so json_config just contains the default options which don't
	 * include any settings for this plugin yet.
	 */
	plugin = json_by_key(json_config, "plugin");
	json_append(plugin, json_key("cache", json_parse_string(config)));

	/* Most default options are hardcoded, but the "dir" setting should
	 * be the first writable directory in the JSONCALCPATH.
	 */
	dir = json_file_path(NULL, NULL, NULL);
	jd = json_string(dir, strlen(dir) + 6);
	if (dir[strlen(dir) - 1] == '/')
		strcat(jd->text, "cache");
	else
		strcat(jd->text, "cache");
	json_config_set("plugin.cache", "dir", jd);
	free(dir);

	/* Register the cache() function */
	json_calc_function_hook("cache", "cache:string, index?:string|object, data?:any", ":any", jfn_cache);

	/* Success! */
	return NULL;
}
