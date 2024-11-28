#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "json.h"
#include "calc.h"

static char logdir[300];

static json_t *jfn_logMsg(json_t *args, void *agdata)
{
	char	logname[400];
	FILE	*fp;
	time_t	now;
	struct tm tm;
	json_t	*j;
	char	*str;
	static jsonformat_t unformatted;

	/* Require at least two parameters: log and msg */
	if (args->first->type != JSON_STRING || *logname == '.' || strchr(logname, '/'))
		return json_error_null(0, "logMsg() given a malformed log name");
	if (!args->first->next)
		return json_error_null(0, "logMsg() requires data to be logged");

	/* Open the log file */
	snprintf(logname, sizeof logname, "%s/%s", logdir, args->first->text);
	fp = fopen(logname, "a");
	if (!fp)
		return json_error_null(0, "logMsg() unable to open log file \"%s\"", logname);

	/* Write a timestamp */
	time(&now);
	localtime_r(&now, &tm);
	fprintf(fp, "%4d-%02s-%02dT%02d%02d%02d",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);

	/* Write the data.  Strings are written verbatim, everything else is
	 * serialized.
	 */
	for (j = args->first->next; j; j = j->next) {
		fputc(' ', fp);
		if (j->type == JSON_STRING)
			fputs(j->text, fp);
		else {
			str = json_serialize(j, &unformatted);
			fputs(str, fp);
			free(str);
		}
	}
	fputc('\n', fp);

	/* Close the log file */
	fclose(fp);

	/* Success */
	return json_null();
}


/* Initialize the plugin */
void json_plugin_log(json_t *config, char *options)
{
	json_t	*tmp;

	/* Find the directory.  If not configured, use "." */
	tmp = json_by_key(config, "dir");
	if (tmp && tmp->type == JSON_STRING)
		strncpy(logdir, tmp->text, sizeof logdir - 1);
	else
		strcpy(logdir, ".");

	/* Add the "logMsg()" function */
	json_calc_function_hook("logMsg", "log,msg", jfn_logMsg, NULL, 0);
}
