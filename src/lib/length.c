#include <stdlib.h>
#include <stdio.h>
#include <jsoncalc.h>

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
		/* EEE "Attempt to find length of a non-array" */
		return 0;
	}

	/* For arrays, we might be able to use a shortcut */
	if (container->type == JSON_ARRAY)
	{
		if (container->first == 0)
			return 0;
		if (JSON_ARRAY_LENGTH(container) > 0)
			return JSON_ARRAY_LENGTH(container);
	}

	/* Count the elements */
	for (len = 0, scan = json_first(container); scan; len++, scan = json_next(scan))
	{
	}

	/* Store the count */
	JSON_ARRAY_LENGTH(container) = len;

	/* Return the count */
	return len;
}
