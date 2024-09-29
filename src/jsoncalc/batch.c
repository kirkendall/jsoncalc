#include <stdio.h>
#include <stdlib.h>
#include <json.h>
#include <calc.h>
#include "jsoncalc.h"


void batch(jsoncontext_t **contextref, jsoncmd_t *initcmds)
{
	jsoncmdout_t *result;
	json_t	*files;
	int	i;

	/* Fetch the list of filenames, and start on the first one */
	files = json_context_file(*contextref, NULL, 0);

	/* For each file... */
	for (i = 0; i < json_length(files); i++) {

		/* Load the next file */
		json_context_file(*contextref, NULL, i);

		/* Run the initcmds on it */
		result = json_cmd_run(initcmds, contextref);
		free(result);
	}
}
