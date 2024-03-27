#include <stdlib.h>
#include <stdio.h>
#include "json.h"

/* Return the number of elements in an array, or members in an object */
int json_length(json_t *container)
{
	int	len;
	json_t	*scan;

	/* Defend against NULL */
	if (!container)
		return 0;

	/* Defend against invalid arguments */
	if (container->type != JSON_ARRAY && container->type != JSON_OBJECT)
	{
		json_throw(NULL, "Attempt to find length of a non-array");
		return 0;
	}

	/* Count the elements */
	for (len = 0, scan = container->first; scan; len++, scan = scan->next)
	{
	}

	/* Return the count */
	return len;
}
