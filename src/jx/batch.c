#include <stdio.h>
#include <stdlib.h>
#include <jx.h>
#include "jxprog.h"


void batch(jxcontext_t **refcontext, jxcmd_t *initcmds)
{
	jx_t	*files;
	int	i;

	/* Fetch the list of filenames */
	files = jx_context_file(*refcontext, NULL, 0, NULL);

	/* If no files then just run any -c commands once and exit */
	if (jx_length(files) == 0) {
		run(initcmds, refcontext);
		return;
	}

	/* For each file... */
	for (i = 0; i < jx_length(files); i++) {

		/* Load the next file */
		jx_context_file(*refcontext, NULL, 0, &i);

		/* Run the initcmds on it */
		run(initcmds, refcontext);
	}
}
