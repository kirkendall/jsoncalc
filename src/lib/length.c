#include <stdlib.h>
#include <stdio.h>
#include <jx.h>

/* Return the number of elements in an array, or members in an object */
int jx_length(jx_t *container)
{
	int	len;
	jx_t	*scan;

	/* Defend against NULL */
	if (!container)
		return 0;

	/* Defend against invalid arguments */
	if (container->type != JX_ARRAY && container->type != JX_OBJECT)
	{
		/* EEE "Attempt to find length of a non-array" */
		return 0;
	}

	/* For arrays, we might be able to use a shortcut */
	if (container->type == JX_ARRAY)
	{
		if (container->first == 0)
			return 0;
		if (JX_ARRAY_LENGTH(container) > 0)
			return JX_ARRAY_LENGTH(container);
	}

	/* Count the elements */
	for (len = 0, scan = jx_first(container); scan; len++, scan = jx_next(scan))
	{
	}

	/* Store the count */
	JX_ARRAY_LENGTH(container) = len;

	/* Return the count */
	return len;
}
