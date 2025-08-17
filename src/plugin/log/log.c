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

/* These are the default settings.  They are added to json_config by the
 * init() function, and then loaded by the main jsoncalc program via
 * json_config_load().
 */
static char *config = "{"
	"\"dir\":\"\","
	"\"name-list\":[\"stdout\",\"stderr\",\"jsoncalc\"],"
	"\"name\":\"jsoncalc\","
	"\"ext\":\".log\","
	"\"rollover-list\":[\"daily\",\"size\",\"never\"],"
	"\"rollover\":\"never\","
	"\"bytes\":100000,"
	"\"keep\":10,"
	"\"date\":false,"
	"\"time\":false,"
	"\"utc\":false,"
	"\"pid\":false,"
	"\"file\":false,"
	"\"line\":false,"
	"\"detail\":9,"
	"\"flush\":true,"
"}";


jsoncmd_t *logset_parse(jsonsrc_t *src, jsoncmdout_t **referr);
jsoncmdout_t *logset_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
jsoncmd_t *log_parse(jsonsrc_t *src, jsoncmdout_t **referr);
jsoncmdout_t *log_run(jsoncmd_t *cmd, jsoncontext_t **refcontext);
jsoncmdname_t jcn_log = {NULL, "log", log_parse, log_run, "log"};


/* When logset is used to select a default log name, this is where it's stored.
 * Later, when parsing log commands that don't explicitly have a log name, this
 * log name will be used.
 */
static char *defaultname;

/* Get a boolean config */
int getbool(char *key)
{
	return json_is_true(json_config_get("plugin.log", key));
}

/* Generate the name of a log file.  This incorporates directory name and
 * file extension from config, and optionally a version suffix.  The returned
 * name in a buffer that gets reused, so you may need to make a local copy.
 */
char *mkfilename(const char *logname, int ver)
{
	json_t	*dir, *ext;
	char	*dirstr, *extstr, verstr[20];
	size_t	len;
	static size_t	alloclen;
	static char	*buf = NULL;


	/* Get the directory, defaulting to "." */
	dir = json_config_get("plugin.log", "dir");
	dirstr = dir ? dir->text : ".";

	/* Get the extension */
	ext = json_config_get("plugin.log", "ext");
	extstr = ext ? ext->text : "log";

	/* Convert the version number to a string */
	if (ver > 0)
		sprintf(verstr, "~%d", ver);
	else if (ver < 0)
		strcpy(verstr, "~*");
	else
		*verstr = '\0';

	/* Compute the size needed for this name */
	len = strlen(dirstr) + 1 + strlen(logname);
	if (*extstr)
		len += strlen(extstr);
	if (*verstr)
		len += strlen(verstr);
	len++; /* for the terminating '\0' */

	/* If necessary, reallocate the buffer */
	if (!buf || alloclen < len) {
		alloclen = len;
		buf = (char *)realloc(buf, alloclen);
	}

	/* Combine all of the pieces together */
	sprintf(buf, "%s/%s", dirstr, logname);
	if (*extstr) {
		strcat(buf, extstr);
	}
	if (*verstr)
		strcat(buf, verstr);

	/* Return it */
	return buf;
}

/* Return an ISO date for today, or some date a given number of days ago */
static void datedelta(char buf[12], int days)
{
	struct tm tm;
	int	utc;
	time_t	now;

	/* get the current time */
	time(&now);
	utc = getbool("utc");
	if (utc)
		gmtime_r(&now, &tm);
	else
		localtime_r(&now, &tm);

	/* Adjust "now" to be closer to noon to avoid daylight savings issues */
	if (tm.tm_hour < 2)
		now += 36000; /* 10 hours */

	/* Adjust by days */
	now -= 86400 * days;

	/* Break down again */
	if (utc)
		gmtime_r(&now, &tm);
	else
		localtime_r(&now, &tm);

	/* Generate an ISO date string from that. */
	sprintf(buf, "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

/* Return the number of days/logs to keep */
static int keep()
{
	json_t *val = json_config_get("plugin.log", "keep");
	if (val && val->type == JSON_NUMBER)
		return json_int(val);
	return 0;
}


/* Do file rollover by date */
void rolldaily(const char *logname)
{
	json_t	*val;
	time_t	now;
	struct tm tm;
	int	utc;
	char	*logfile, *oldlog;
	int	fd;
	char	today[12], logdate[12];
	glob_t	globbuf;
	size_t	len;
	int	i;

	/* Lock the file, so that if anybody else is already rolling it,
	 * we'll wait until they're done.
	 */
	logfile = mkfilename(logname, 0);
	fd = open(logfile, O_RDWR);
	if (fd < 0)
		return; /* No log to rollover */
	lockf(fd, F_LOCK, (off_t)0);

	/* Get the current date. */
	datedelta(today, 0);

	/* Get the log file's date.  Since it's possible that some log entries
	 * could have snuck in after midnight, we can't trust the file's
	 * timestamp for this.  Instead, we need to read the date from the
	 * start of the log file.
	 */
	if (read(fd, logdate, 10) != 10) {
		close(fd);
		return; /* doesn't contain a date */
	}
	logdate[10] = '\0';

	/* If they're the same or the log is newer, then don't rollover */
	if (strcmp(today, logdate) <= 0)
		return;

	/* WE WILL ROLLOVER! Generate the name of the newest version to delete */
	len = strlen(logfile);
	oldlog = (char *)malloc(len + 12);
	strcpy(oldlog, logfile);
	strcat(oldlog, "~");
	datedelta(oldlog + len + 1, keep());

	/* Scan for old versions */
	logfile = mkfilename(logname, -1);
	if (0 == glob(logfile, GLOB_NOSORT|GLOB_NOESCAPE, NULL, &globbuf)) {
		/* For each match... */
		for (i = 0; i < globbuf.gl_pathc; i++) {
			/* Skip if doesn't start with logfile.  This could
			 * happen if logfile had funny characters in it.
			 */
			if (strncmp(globbuf.gl_pathv[i], logfile, len))
				continue;

			/* Skip if newer than the newest to delete */
			if (strcmp(globbuf.gl_pathv[i], oldlog) > 0)
				continue;

			/* Remove it */
			unlink(globbuf.gl_pathv[i]);
		}
	}
	globfree(&globbuf);

	/* Either delete (for keep=0) or rename the old log.  Note that if
	 * keep>0 then we'll always keep the outgoing log even if its date
	 * is too old.
	 */
	logfile = mkfilename(logname, 0);
	if (keep() > 0) {
		strcpy(oldlog + len + 1, logdate);
		rename(logname, oldlog);
	} else {
		unlink(logname);
	}
}


/* Do file rollover by size */
void rollsize(const char *logname)
{
	char *filename = mkfilename(logname, 0);
	char *incrname;
	size_t	len;
	struct stat st;
	json_t	*val;
	off_t	bytes;
	int	keeplogs, ver, highver;
	int	fd;
	glob_t	globbuf;
	int	i;

	/* Lock the file, so that if anybody else is already rolling it,
	 * we'll wait until they're done.
	 */
	fd = open(filename, O_RDWR);
	if (fd < 0)
		return;
	lockf(fd, F_LOCK, (off_t)0);

	/* Get the size limit */
	bytes = 100000;
	val = json_config_get("plugin.log", "bytes");
	if (val && val->type == JSON_NUMBER)
		bytes = json_int(val);

	/* Check the size of the log file */
	if (fstat(fd, &st) < 0 || st.st_size < bytes) {
		/* Either it doesn't exist, or it's small -- no rollover */
		close(fd); /* <-- also frees the lock */
		return;
	}

	/* We WILL be rolling over.  Get the number of old versions to keep */
	keeplogs = keep();

	/* Delete any old versions */
	filename = mkfilename(logname, -1);
	if (0 == glob(filename, GLOB_NOSORT|GLOB_NOESCAPE, NULL, &globbuf)) {
		/* For each match... */
		filename = mkfilename(logname, 0);
		len = strlen(filename);
		highver = 0;
		for (i = 0; i < globbuf.gl_pathc; i++) {
			/* Skip if doesn't start with logname.  This could
			 * happen if logname had funny characters in it.
			 */
			if (strncmp(globbuf.gl_pathv[i], filename, len))
				continue;

			/* Get the version number. This is at the end of the
			 * filename after a tilde.
			 */
			ver = atoi(globbuf.gl_pathv[i] + len + 1);

			/* If version number is too high, delete it */
			if (ver >= keeplogs)
				unlink(globbuf.gl_pathv[i]);
			else if (ver > highver)
				highver = ver;
		}
	}
	globfree(&globbuf);

	/* Rename all old logs to increment their version number */
	incrname = strdup(mkfilename(logname, highver + 1));
	for (i = highver; i >= 0; i--) {
		filename = mkfilename(logname, i);
		rename(logname, incrname);
		strcpy(incrname, logname); /* yes, it will fit */
	}

	/* Done!  Close the file and free the lock */
	close(fd);
}


/* Perform log rollover, if necessary, and then open a new log */
FILE *switchfile(const char *logname)
{
	static FILE *prevfp;
	static char *prevname;
	char	*filename;
	json_t	*roll;

	/* If same name, just keep using it */
	if (prevfp && prevname && !strcmp(prevname, logname))
		return prevfp;

	/* Else we need to do this the hard way.  Close previous file if any. */
	if (prevfp && prevfp != stderr && prevfp != stdout)
		fclose(prevfp);
	if (prevname)
		free(prevname);

	/* Do the rollover thing. */
	roll = json_config_get("plugin.log", "rollover");
	if (roll && roll->type == JSON_STRING && !strcmp(roll->text, "daily"))
		rolldaily(logname);
	else if (roll && roll->type == JSON_STRING && !strcmp(roll->text, "size"))
		rollsize(logname);

	/* Open the file.  If we can't open it, use stderr. */
	prevfp = fopen(mkfilename(logname, 0), "a");
	if (!prevfp)
		return stderr;
	prevname = strdup(logname);

	/* If at start of file (not reopening an existing log), then write
	 * the start date and other info.
	 */
	if (ftell(prevfp) == 0) {
		char today[12];
		json_t *bytes = json_config_get("plugin.log", "bytes");
		datedelta(today, 0);
		if (roll->type != JSON_STRING)
			fprintf(prevfp, "%s never\n", today);
		else if (!strcmp(roll->text, "daily"))
			fprintf(prevfp, "%s daily %d", today, keep());
		else if (!strcmp(roll->text, "size"))
			fprintf(prevfp, "%s size(%dK) %d", today, json_int(bytes) / 1024, keep());
		else
			fprintf(prevfp, "%s never\n", today);
	}

	/* Return the file handle */
	return prevfp;
}

/*****************************************************************************/

/* Parse a logset command.  Mostly this just sets the default output for any
 * following log commands.
 */
jsoncmd_t *logset_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	json_t	*setting, *err;
	char	*setstr;
	const char *str;
	json_cmd_parse_whitespace(src);


	/* Optional "name:", or just ":" to use the default name from config for the log, must not contain punctuation/whitespace */
	if (*src->str == ':') {
		src->str++; /* move past ":" */
		free(defaultname);
		setting = json_config_get("plugin.log", "name");
		if (setting)
			defaultname = strdup(setting->text);
		else
			defaultname = NULL;
	} else if (isalpha(*src->str)) {
		const char *before = src->str;
		char *newname = json_cmd_parse_key(src, 0);
		if (!newname || *src->str != ':') {
			src->str = before;
		} else {
			free(defaultname);
			defaultname = newname;
			src->str++; /* move past ":" */
		}
	}

	/* Optional log settings */
	json_cmd_parse_whitespace(src);
	if (*src->str && *src->str != ';' && *src->str != '}') {
		/* Find the end of the command */
		for (str = src->str; *str && !strchr(";}\n", *str); str++) {
		}
		setstr = (char *)malloc(str - src->str + 1);
		strncpy(setstr, src->str, str - src->str);
		setstr[str - src->str] = '\0';
		src->str = str;
		if (*src->str)
			src->str++;

		/* Parse it */
		setting = json_by_expr(json_config, "plugin.log", NULL);
		err = json_config_parse(setting, setstr, NULL);
		free(setstr);
		if (err) {
			*referr = json_cmd_error(src->str, "%s", err->text);
			json_free(err);
		}
	}

	/* Nothing to do at runtime */
	return NULL;
}

/* Placeholder - logset is handled entirely at compile time */
jsoncmdout_t *logset_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	return NULL;
}

jsoncmd_t *log_parse(jsonsrc_t *src, jsoncmdout_t **referr)
{
	jsonsrc_t start;
	char	*name = NULL;
	int	detail = 1;
	jsoncalc_t	*list;
	const char	*err;
	jsoncmd_t *cmd;

	/* If the defaultname hasn't been set yet, then set it now */
	if (!defaultname) {
		json_t *j = json_config_get("plugin.log", "name");
		defaultname = strdup(j->text);
	}

	/* Optional "name:", or just ":" to use the default name, must not
	 * contain punctuation/whitespace
	 */
	start = *src;
	if (*src->str == ':') {
		json_t *setting = json_config_get("plugin.log", "name");
		if (setting)
			name = strdup(setting->text);
	} else if (isalpha(*src->str)) {
		name = json_cmd_parse_key(src, 0);
		if (!name || *src->str != ':') {
			src->str = start.str;
			if (name)
				free(name);
			name = NULL;
		}
	}
	if (!name)
		name = strdup(defaultname);

	/* Optional "n:" to set the detail number */
	json_cmd_parse_whitespace(src);
	if (*src->str >= '1' && *src->str <= '9' && src->str[1] == ':') {
		detail = *src->str - '0';
		src->str += 2;
	}

	/* The rest of the line is a comma-delimited list of expressions to
	 * print.  Collect them as an array generator.  If there are no
	 * expressions, then use an empty array generator.
	 */
 	list = NULL;
	err = NULL;
	do {
		jsoncalc_t *item = json_calc_parse(src->str, &src->str, &err, FALSE);
		if (!item || err || (*src->str && !strchr(";},", *src->str))) {
			if (list)
				json_calc_free(list);
			if (item)
				json_calc_free(item);
			*referr = json_cmd_error(start.str, err ? err : "Syntax error in \"%s\" expression", "log");
			return NULL;
		}
		list = json_calc_list(list, item);
	} while (*src->str++ == ',');

	/* Build the command */
	cmd = json_cmd(&start, &jcn_log);
	cmd->calc = list;
	cmd->key = name;
	cmd->var = detail + '0';
	return cmd;
}

jsoncmdout_t *log_run(jsoncmd_t *cmd, jsoncontext_t **refcontext)
{
	json_t *list, *scan;
	FILE	*out;
	int	showdate, showtime, showpid, showfile, showline;
	int	lastchar;

	/* If this detail level is too high, skip it */
	scan = json_config_get("plugin.log", "detail");
	if (cmd->var - '0' > json_int(scan))
		return NULL;

	/* Decide where to log */
	if (!strcmp("stderr", cmd->key)) {
		out = stderr;
	} else if (!strcmp("stdout", cmd->key) || !strcmp("-", cmd->key)) {
		out = stdout;
	} else {
		out = switchfile(cmd->key);
	}

	/* If it's a tty, maybe use colors */
	if (isatty(fileno(out)) && json_format_default.color)
		fputs(json_format_default.escdebug, out);

	/* Write any line info */
	showdate = getbool("date");
	showtime = getbool("time");
	showtime = getbool("pid");
	showfile = getbool("file");
	showline = getbool("line");
	if (showdate || showtime) {
		time_t now;
		struct tm tm;
		int utc = getbool("utc");
		time(&now);
		if (utc)
			gmtime_r(&now, &tm);
		else
			localtime_r(&now, &tm);
		if (showdate && showtime)
			fprintf(out, "%4d-%02d-%02dT%02d:%02d:%02d%s",
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
				tm.tm_hour, tm.tm_min, tm.tm_sec, utc ? "Z" : "");
		else if (showdate)
			fprintf(out, "%4d-%02d-%02d",
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
		else
			fprintf(out, "%02d%02d%02d%s",
				tm.tm_hour, tm.tm_min, tm.tm_sec, utc ? "Z" : "");
	}
	if (showpid) {
		if (showdate || showtime || showpid)
			putc(' ', out);
		fprintf(out, "[%5d]", (int)getpid());
	}
	if (showfile || showline) {
		int lineno;
		jsonfile_t *jf = json_file_containing(cmd->where, &lineno);
		if (jf) { 
			if (showdate || showtime || showpid)
				putc(' ', out);
			if (showfile)
				fputs(jf->filename, out);
			if (showfile && showline)
				fputc(':', out);
			if (showline)
				fprintf(out, "%d", lineno);
		}
	}
	if (showdate || showtime || showfile || showline)
		fputc('\t', out);

	/* Evaluate the expression list. */
	list = json_calc(cmd->calc, *refcontext, NULL);

	/* If it's an error then log the error instead of the expression */
	if (json_is_null(list) && *list->text) {
		fprintf(out, "Expression error: %s\n", list->text);
	} else {
		/* Output each expression with a space delimiter */
		lastchar = '\n';
		for (scan = list->first; scan; scan = scan->next) {
			/* Space between items */
			if (scan != list->first)
				fputc(' ', out);

			/* Output strings plainly (no quotes), but convert
			 * anything else to a JSON string.
			 */
			if (scan->type == JSON_STRING) {
				fputs(scan->text, out);
				if (*scan->text)
					lastchar = scan->text[strlen(scan->text) - 1];
			} else {
				char *tmp = json_serialize(scan, NULL);
				fputs(tmp, out);
				free(tmp);
				lastchar = 'x'; /* Never empty, never '\n' */
			}
		}

		/* If the last character wasn't a newline, then add a newline */
		if (lastchar != '\n')
			fputc('\n', out);
	}

	/* Clean up */
	json_free(list);

	/* If it's a tty, turn off colors */
	if (isatty(fileno(out)) && json_format_default.color)
		fputs(json_format_color_end, out);

	/* If supposed to flush, then do that */
	if (getbool("flush"))
		fflush(out);

	/* Success! */
	return NULL;
}

/* Initialize the plugin */
char *pluginlog(void)
{
	json_t	*settings, *plugin;
	char	*dir;

	/* Set the default options.  The config file hasn't been loaded yet
	 * so json_config just contains the default options which don't
	 * include any settings for this plugin yet.
	 */
	plugin = json_by_key(json_config, "plugin");
	json_append(plugin, json_key("log", json_parse_string(config)));

	/* Most default options are hardcoded, but the "dir" setting should
	 * be the first writable directory in the JSONCALCPATH.
	 */
	dir = json_file_path(NULL, NULL, NULL);
	json_config_set("plugin.log", "dir", json_string(dir, -1));
	free(dir);

	/* Add the "log" and "logset" commands */
	json_cmd_hook("log", "log", log_parse, log_run);
	json_cmd_hook("log", "logset", logset_parse, logset_run);

	/* Success! */
	return NULL;
}
