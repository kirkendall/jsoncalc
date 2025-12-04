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
#include <jx.h>

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
	jx_t	*dir;
	char	*dirstr, *extstr, verstr[20];
	size_t	len;
	char	*buf;


	/* Get the directory, defaulting to "." */
	dir = jx_config_get("plugin.cache", "dir");
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

/* This is used with jx_copy_filter() to copy config settings other than "dir"
 */
static int omitDir(jx_t *json)
{
	if (json->type != JX_KEY || strcmp(json->text, "dir"))
		return 1;
	return 0;
}


/* Clean a cache.  Can also be used to store settings.  Returns the current
 * settings, which the calling function must free via jx_free() even if
 * passed settings.
 *
 * If an error is detected in the settings, then it returns a "null" jx_t
 * containing an appropriate error message.
 */
static jx_t *cleanCache(char *cache, jx_t *newSettings)
{
	jx_t	*settings;
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
		settings = jx_copy_filter(jx_by_expr(jx_config, "plugin.cache", NULL), omitDir);
	else
		settings = jx_parse_file(filename);

	/* If given new settings, merge them into the current settings */
	if (newSettings) {
		/* Use the plugin settings as a list of valid names/types
		 * of cache settings (other than "dir").
		 */
		jx_t *scan, *tmp;
		jx_t *pluginSettings = jx_by_expr(jx_config, "plugin.cache", NULL);
		for (scan = newSettings->first; scan; scan = scan->next) {
			/* Validate the name/type via pluginSettings */
			tmp = jx_by_key(pluginSettings, scan->text);
			if (!tmp) {
				jx_free(settings);
				free(filename);
				return jx_error_null(0, "Invalid setting name \"%s\" for %s()", scan->text, "cache");
			}
			if (tmp->type != scan->first->type) {
				jx_free(settings);
				free(filename);
				return jx_error_null(0, "Setting \"%s\" has wrong data type for %s()", scan->text, "cache");
			}

			/* Merge the value into settings */
			jx_append(settings, jx_copy(scan));
		}

		/* Save these settings */
		FILE *fp = jx_file_update(filename);
		if (fp) {
			char *tmp = jx_serialize(settings, NULL);
			fputs(tmp, fp);
			free(tmp);
			fclose(fp);
		}
	}
	free(filename);

	/* Extract the cleaning settings */
	time(&now);
	seconds = jx_int(jx_by_key(settings, "seconds"));
	maxbytes = jx_int(jx_by_key(settings, "bytes"));
	touch = jx_is_true(jx_by_key(settings, "touch"));

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
	free(filename);

	/* Return the settings */
	return settings;
}

/*****************************************************************************/

static jx_t *jfn_cache(jx_t *args, void *agdata)
{
	char	*cache, *index, *err;
	jx_t	*settings;
	jx_t	*data;
	char	*filename;
	time_t	now;
	time_t	seconds;	/* derived from from "seconds" */
	int	touch;
	struct stat st;

	/* Get the cache name */
	if (args->first->type != JX_STRING)
		return jx_error_null(0, "%s() wants a cache name as first argument", "cache");
	cache = args->first->text;
	err = safeName(cache);
	if (err)
		return jx_error_null(0, err, "cache", "cacheName");

	/* If nothing else, then fetch settings and return them */
	if (!args->first->next)
		return cleanCache(cache, NULL);

	/* If given settings, store them */
	if (args->first->next->type == JX_OBJECT )
		return cleanCache(cache, args->first->next);

	/* Otherwise the second argument must be an index */
	if (args->first->next->type != JX_STRING)
		return jx_error_null(0, "%s() wants an index string or settings object as the second argument", "cache");
	index = args->first->next->text;
	err = safeName(index);
	if (err)
		return jx_error_null(0, err, "cache", "index");

	/* The third argument is data and can be absent or anything.  There
	 * must not be a third item.
	 */
	if (!args->first->next->next)
		data = NULL;
	else if (args->first->next->next->next)
		return jx_error_null(0, "The %() function takes at most 3 arguments", "cache");
	else
		data = args->first->next->next;

	/* Get the name of this cache item */
	filename = cacheFile(cache, index);

	/* Do we have new data? */
	settings = jx_by_expr(jx_config, "plugin.cache", NULL);
	if (data) {
		/* Write the data to the cache */
		if (jx_is_null(data))
			unlink(filename);
		else {
			FILE *fp = jx_file_update(filename);
			if (fp) {
				char *tmp = jx_serialize(data, NULL);
				fputs(tmp, fp);
				free(tmp);
				fclose(fp);
			}
		}

		/* If limiting the cache by size, then clean the cache */
		if (jx_int(jx_by_key(settings, "bytes")) > 0)
			jx_free(cleanCache(cache, NULL));

		/* Make a copy of the data to return */
		data = jx_copy(data);
	} else {
		/* If the file exists but is old, then clean the cache */
		time(&now);
		seconds = jx_int(jx_by_key(settings, "seconds"));
		touch = jx_is_true(jx_by_key(settings, "touch"));
		if (stat(filename, &st) >= 0
		 && (touch ? st.st_atime : st.st_mtime) + seconds < now)
			jx_free(cleanCache(cache, NULL));

		/* Try to read the data from a file */
		data = jx_parse_file(filename);

		/* If unreadable FOR ANY REASON, just return NULL */
		if (!data)
			data = jx_null();
		if (jx_is_null(data))
			memset(data->text, 0, sizeof data->text);
	}
	free(filename);

	return data;
}

/*****************************************************************************/


/* Initialize the plugin */
char *plugincache(void)
{
	jx_t	*plugin, *jd;
	char	*dir;

	/* Set the default options.  The config file hasn't been loaded yet
	 * so jx_config just contains the default options which don't
	 * include any settings for this plugin yet.
	 */
	plugin = jx_by_key(jx_config, "plugin");
	jx_append(plugin, jx_key("cache", jx_parse_string(config)));

	/* Most default options are hardcoded, but the "dir" setting should
	 * be the first writable directory in the JXPATH.
	 */
	dir = jx_file_path(NULL, NULL, NULL);
	jd = jx_string(dir, strlen(dir) + 6);
	if (dir[strlen(dir) - 1] == '/')
		strcat(jd->text, "cache");
	else
		strcat(jd->text, "cache");
	jx_config_set("plugin.cache", "dir", jd);
	free(dir);

	/* Register the cache() function */
	jx_calc_function_hook("cache", "cache:string, index?:string|object, data?:any", ":any", jfn_cache);

	/* Success! */
	return NULL;
}
