#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <json.h>
#include <calc.h>
#include "jsoncalc.h"

char *save_config(void)
{
	json_t	*config;
	char *tmp;
	static char	filename[200];
	FILE	*fp;
	jsonformat_t	fmt;

	/* Collect the config data into a JSON object */
	config = json_object();
	tmp = json_format_str(NULL);
	json_append(config, json_key("format", json_string(tmp, -1)));
	free(tmp);
	tmp = json_format_color_str();
	json_append(config, json_key("colors", json_string(tmp, -1)));
	free(tmp);
	if (autoload_dir)
		json_append(config, json_key("autoload", json_string(autoload_dir, -1)));

	/* Write the object to a file */
	sprintf(filename, "%s/.config", getenv("HOME"));
	mkdir(filename, 0700); /* Almost certainly already exists */
	strcat(filename, "/jsoncalc");
	mkdir(filename, 0700); /* Probably already exists */
	strcat(filename, "/config");
	fp = fopen(filename, "w");
	if (!fp) {
		perror(filename);
		json_free(config);
		return NULL;
	}
	fmt = json_format_default;
	fmt.fp = fp;
	json_print(config, &fmt);
	fclose(fp);
	json_free(config);
	return filename;
}

/* Attempt to load jsoncalc's configuration from a file.  Ignore errors. */
void load_config(void)
{
	char	filename[200];
	json_t	*config;
	char	*tmp;

	/* Figure out where the config file is located */
	sprintf(filename, "%s/.config/jsoncalc/config", getenv("HOME"));
	if (access(filename, R_OK | W_OK) != 0)
		return;

	/* Load it */
	config = json_parse_file(filename);
	if (!config)
		return;

	/* Extract info from it */
	tmp = json_text_by_key(config, "format");
	if (tmp)
		json_format(NULL, tmp);
	tmp = json_text_by_key(config, "colors");
	if (tmp)
		json_format_color(tmp);
	tmp = json_text_by_key(config, "autoload");
	if (tmp)
		autoload_dir = strdup(tmp);

	/* Free the config */
	json_free(config);
}
