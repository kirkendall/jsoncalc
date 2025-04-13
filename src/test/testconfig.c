#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <locale.h>
#include "json.h"

/* Run a single test */
static int singletest(char *settings)
{
	json_t	*err;

	err = json_config_parse(NULL, settings, 0);
	if (!err) {
		json_print(json_config, NULL);
		return 1;
	}
	if (isatty(0))
		printf("\e[31m%s\e[m\n", err->text);
	else {
		puts(err->text);
	}
	json_free(err);
	return 0;
}

int main(int argc, char **argv)
{
	int	i;
	char	buf[100], *eol;
	json_t	*dummy;

	setlocale(LC_ALL,"");
	json_config_load("textconfig");

	/* Add a dummy plugin */
	dummy = json_object();
	json_append(dummy, json_key("host", json_string("localhost", -1)));
	json_append(dummy, json_key("db", json_string("", -1)));
	json_append(dummy, json_key("user", json_string("", -1)));
	json_append(json_by_key(json_config, "plugin"), json_key("dummy",dummy));

	json_print(json_config, NULL);
	if (argc <= 1) {
		for (;;) {
			if (isatty(0))
				printf("testconfig> ");
			if (!fgets(buf, sizeof buf, stdin))
				break;
			eol = strchr(buf, '\n');
			if (eol)
				*eol = '\0';
			singletest(buf);
		}
	}
	for (i = 1; i < argc; i++)
		singletest(argv[i]);
	return 0;
}
