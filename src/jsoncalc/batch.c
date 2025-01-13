#include <stdio.h>
#include <stdlib.h>
#include <json.h>
#include <calc.h>
#include "jsoncalc.h"


void batch(jsoncontext_t **refcontext, jsoncmd_t *initcmds)
{
	json_t	*files;
	int	i;

	/* Fetch the list of filenames */
	files = json_context_file(*refcontext, NULL, 0, NULL);

	/* For each file... */
	for (i = 0; i < json_length(files); i++) {

		/* Load the next file */
		json_context_file(*refcontext, NULL, 0, &i);

		/* Run the initcmds on it */
		run(initcmds, refcontext);
	}
}
