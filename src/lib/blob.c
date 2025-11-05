#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <jsoncalc.h>

/* This file contains functions to help jsoncalc handle binary data */

/*============================================================================*/
/* The following implement a deferred array, so we can store the data in binary
 * instead of a json_t array.  Much smaller, much faster.
 */
typedef struct {
	jsondef_t def;
	const char	*data;
	size_t	len;
	int	index;
} jsonblob_t;
static json_t *blobFirst(json_t *array);
static json_t *blobNext(json_t *elem);
static int blobIsLast(const json_t *elem);
static void blobFree(json_t *array_or_elem);
static json_t *blobByIndex(json_t *array, int index);
static jsondeffns_t blobfns = {
	sizeof(jsonblob_t),
	"Blob",
	blobFirst,
	blobNext,
	blobIsLast,
	blobFree,
	blobByIndex,
	NULL
};


/* Return the first element of a blob */
static json_t *blobFirst(json_t *array)
{
	jsonblob_t *blob = (jsonblob_t *)array->first;
	jsonblob_t *nextblob;
	json_t *result;
	if (blob->len == 0)
		return NULL;
	result = json_from_int(blob->data[0] & 0xff);

	/* We need a new copy of the JSON_DEFER node.  It can share the same
	 * data buffer as the  array's JSON_DEFER though.
	 */
	result->next = json_defer(&blobfns);
	nextblob = (jsonblob_t *)result->next;
	nextblob->data = blob->data;
	nextblob->len = blob->len;
	nextblob->index = 0;
	return result;
}

/* Return the next element of a blob */
static json_t *blobNext(json_t *elem)
{
	jsonblob_t *blob = (jsonblob_t *)elem->next;
	json_t *result;
	blob->index++;
	if (blob->index >= blob->len)
		return NULL;
	elem->next = NULL;
	json_free(elem);
	result = json_from_int(blob->data[blob->index] & 0xff);
	result->next = (json_t *)blob;
	return result;
}

/* Test whether a given element is the last in the deferred array */
static int blobIsLast(const json_t *elem)
{
	jsonblob_t *blob = (jsonblob_t *)elem->next;
	return (blob->index + 1 >= blob->len);
}

/* Do the specific cleanup needed for deferred blobs */
static void blobFree(json_t *array_or_elem)
{
	jsonblob_t *blob = (jsonblob_t *)array_or_elem->next;

	/* No special action for an element */
	if (array_or_elem->type != JSON_ARRAY)
		return;

	/* Free the blob's memory, unless it is a memory-mapped file in which
	 * case decrement the reference count.
	 */
	if (blob->def.file)
		blob->def.file->refs--;
	else
		free((void *)blob->data); /* to loose the "const" qualifier */
}

/* Look up a byte at a given index */
static json_t *blobByIndex(json_t *array, int index)
{
	jsonblob_t *blob = (jsonblob_t *)array->first;
	jsonblob_t *nextblob;
	json_t *result;
	if (index < 0 || index >= blob->len)
		return NULL;
	result = json_from_int(blob->data[index] & 0xff);

	/* We need to mark this as a deferred element, by making ->next point
	 * to a JSON_DEFER node.  This is similar to what json_first() does.
	 */
	result->next = json_defer(&blobfns);
	nextblob = (jsonblob_t *)result->next;
	nextblob->data = blob->data;
	nextblob->len = blob->len;
	nextblob->index = index;
	return result;
}

/*============================================================================*/

/* Choose the best method to convert binary data to JSON.  Also return the
 * length that Latin1 text would be after conversion to UTF-8.  (Binary and
 * UTF-8 are both "len" bytes long.)
 */
jsonblobconv_t json_blob_best(const char *data, size_t len, size_t *reflatin1len)
{
	int notutf8, zero;
	size_t latin1len, chlen, pos, i;

	/* Check whether this could  be UTF-8.  Also count the byte length
	 * of the UTF-8 text, and latin-1 text converted to UTF-8.
	 */
	for (notutf8 = zero = 0, latin1len = pos = 0; pos < len; pos++) {
		/* If we can make our decision now, then stop checking. */
		if (notutf8 && zero && reflatin1len == NULL)
			break;

		/* Detect whether there's a zero byte, for "bytes" option */
		if (data[pos] == 0)
			zero = 1;

		/* Add the length needed for converting Latin1 to UTF-8 */
		if (data[pos] == 0)
			latin1len += 3;
		else if (data[pos] & 0x80)
			latin1len += 2;
		else
			latin1len++;

		/* UTF-8.  If we already encountered trouble skip it.  Otherwise
		 * we want to make sure it looks like valid UTF-8 data.
		 */
		if (notutf8)
			continue;
		if (data[pos] == 0) {
			notutf8 = 1;
			continue;
		}
		if ((data[pos] & 0x80) == 0)
			continue;
		if ((data[pos] & 0xe0) == 0xc0)
			chlen = 2;
		else if ((data[pos] & 0xf0) == 0xe0)
			chlen = 3;
		else if ((data[pos] & 0xf8) == 0xf0)
			chlen = 4;
		else {
			notutf8 = 1;
			continue;
		}
		if (pos + chlen > len) {
			notutf8 = 1;
			continue;
		}
		for (i = 1; i < chlen; i++) {
			if ((data[pos + i] & 0xc0) != 0x80) {
				notutf8 = 1;
				break;
			}
		}
		pos += chlen - 1; /* and +1 as part of the for-loop */
	}

	/* Make the choice */
	if (reflatin1len)
		*reflatin1len = latin1len;
	if (zero)
		return JSON_BLOB_BYTES;
	else if (notutf8)
		return JSON_BLOB_LATIN1;
	else
		return JSON_BLOB_UTF8;
}

/* Convert binary data to a json_t.  If it can't be converted (because UTF-8
 * was requested but the data isn't valid UTF-8) then return NULL.
 */
json_t *json_blob_convert(const char *data, size_t len, jsonblobconv_t conversion)
{
	jsonblobconv_t best;
	size_t	latin1len;
	char	*c;
	const char *scan;
	jsonfile_t *file;
	json_t	*result;
	jsonblob_t *blob;

	/* Scan the data to determine how to convert */
	best = json_blob_best(data, len, conversion == JSON_BLOB_BYTES ? NULL : &latin1len);

	/* If UTF-8 was requested but data is malformed, then return NULL */
	if (conversion == JSON_BLOB_UTF8 && best != JSON_BLOB_UTF8)
		return NULL;

	/* If "any" was requested, use the best.  If "STRING" then use "utf8" 
	 * or "latin1", even if the best was "binary".
	 */
	if (conversion == JSON_BLOB_ANY)
		conversion = best;
	else if (conversion == JSON_BLOB_STRING)
		conversion = (best == JSON_BLOB_BYTES ? JSON_BLOB_LATIN1 : best);

	/* Convert it */
	switch (conversion) {

	case JSON_BLOB_UTF8:
		/* Easiest, just allocate a string */
		return json_string(data, len);

	case JSON_BLOB_LATIN1:
		/* Allocate space for the conversion */
		result = json_string("", latin1len);

		/* Convert it */
		c = result->text;
		for (scan = data; scan < &data[len]; scan++) {
			if (*scan == 0) {
				/* U+ffff, which is designated as an
				 * internal-use-only non-character.
				 * In UTF-8 that represented by efbfbf.
				 */
				*c++ = (char)0xef;
				*c++ = (char)0xbf;
				*c++ = (char)0xbf;
			} else if (*(unsigned char *)scan < 128) {
				/* ASCII */
				*c++ = *scan;
			} else if (*(unsigned char *)scan < 192) {
				/* UTF-8 in range 127-191 starts with c2 */
				*c++ = (char)0xc2;
				*c++ = *scan;
			} else {
				/* UTF-8 in range 192-255 starts with c3 */
				*c++ = (char)0xc3;
				*c++ = *scan - 64;
			}
		}

		return result;

	case JSON_BLOB_BYTES:
		/* Is this the entire contents of a file?  If so, we can use
		 * its buffer.
		 */
		file = json_file_containing(data, NULL);
		if (file && (data != file->base || len != file->size))
			file = NULL;

		/* Allocate a deferred array */
		result = json_array();
		result->first = json_defer(&blobfns);
		blob = (jsonblob_t *)result->first;
		blob->def.file = file;
		if (file) {
			blob->data = file->base;
			blob->len = file->size;
			file->refs++;
		} else {
			blob->data = (char *)malloc(len);
			memcpy((char *)blob->data, data, len);
			blob->len = len;
		}
		JSON_ARRAY_LENGTH(result) = len;
		result->text[1] = 'n';
		return result;

	case JSON_BLOB_ANY:
	case JSON_BLOB_STRING:
		/* Can't happen.  These values get mapped to one of the others*/
		;
	}
	abort();
}

/* Convert a json_t to blob data and return the length.  If conversion isn't
 * possible then return 0.  You may pass NULL for data if you just want length.
 */
size_t json_blob_unconvert(json_t *json, char *data, jsonblobconv_t conversion){
	json_t	*scan;
	unsigned char *utf8;
	int	byte;
	size_t	len;

	if (json->type == JSON_ARRAY) {
		/* NOTE: If json is already a blob, this is easy.  However,
		 * handling that would involve returning a json_t which this
		 * function can't to.  We assume the blob test happens before
		 * this function is called.
		 */

		/* Do it the hard way -- scan the whole array */
		for (scan = json_first(json); scan; scan = json_next(scan)) {
			if (scan->type != JSON_NUMBER)
				return 0;
			byte = json_int(scan);
			if (byte < 0 || byte >= 256)
				return 0;
			if (data)
				*data++ = byte;
		}
		return json_length(json);
	} else if (json->type != JSON_STRING)
		return 0;

	/* Its a string.  If UTF-8 then that's easy. */
	if (conversion != JSON_BLOB_LATIN1) {
		len = strlen(json->text);
		if (data)
			memcpy(data, json->text, len);
		return len;
	}

	/* Latin-1 conversion is subtle because we always use UTF-8 internally
	 * so some Latin-1 characters may be two bytes before conversion.
	 * Also U+ffff is used to represent the 0x00 byte.
	 */
	for (len = 0, utf8 = (unsigned char *)json->text; *utf8; len++) {
		if (utf8[0] == 0xef && utf8[1] == 0xbf && utf8[2] == 0xbf) {
			byte = 0;
			utf8 += 3;
		} else if (*utf8 == 0xc2) {
			byte = utf8[1];
			utf8 += 2;
		} else if (*utf8 == 0xc2) {
			byte = utf8[1] + 64;
			utf8 += 2;
		} else if (*utf8 & 0x80) {
			/* out-of-range for Latin1 */
			return 0;
		} else {
			byte = *utf8++;
		}

		if (data)
			*data++ = byte;
	}
	return len;

}

/* Do a full conversion, from one json_t to another */
json_t *json_blob(json_t *in, jsonblobconv_t convout, jsonblobconv_t convin)
{
	char *data, *mustfree;
	size_t	len;
	jsonblob_t *inblob;
	json_t	*result;

	/* Convert the data to a blob.  If it is a deferred "Blob" array, this
	 * is trivial.
	 */
	inblob = (jsonblob_t *)in->first;
	mustfree = NULL;
	if (json_is_deferred_array(in) && inblob->def.fns == &blobfns) {
		/* Easy! Just use the blob data */
		data = (char *)inblob->data; /* discarding "const" */
		len = inblob->len;
	} else {
		/* Need to convert it, and store the result in a new buffer */
		len = json_blob_unconvert(in, NULL, convin);
		if (len == 0)
			return json_error_null(NULL, "badblob:Could not convert data to a blob");
		data = mustfree = (char *)malloc(len);
		(void)json_blob_unconvert(in, data, convin);
	}

	/* Convert it to the requested output format */
	result = json_blob_convert(data, len, convout);
	if (!result)
		result = json_error_null(NULL, "badutf8:Data is not valid UTF-8");
	if (mustfree)
		free(mustfree);
	return result;
}

/* Return NULL if not a blob, else return a pointer to its data and store the
 * length at *reflen.
 */
const char *json_blob_data(json_t *json, size_t *reflen)
{
	jsonblob_t *blob;
	if (!json_is_deferred_array(json))
		return NULL;
	blob = (jsonblob_t *)json->first;
	if (blob->def.fns != &blobfns)
		return NULL;

	/* It's a blob! */
	*reflen = blob->len;
	return blob->data;
}

/*============================================================================*/
/* Parser for binary files */

/* Test whether this is the best parser for the given data */
int json_blob_test(const char *str, size_t len)
{
	return json_blob_best(str, len, NULL) == JSON_BLOB_BYTES;
}


/* Parse it, and return a json_t */
json_t *json_blob_parse(const char *str, size_t len, const char **refend, const char **referr)
{
	if (refend)
		*refend = str + len;
	return json_blob_convert(str, len, JSON_BLOB_BYTES);
}
