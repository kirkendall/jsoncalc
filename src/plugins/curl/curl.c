#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <json.h>
#include <sys/types.h>
#include <fcntl.h>
#include <calc.h>
#include <curl/curl.h>

/* This plugin gives access to the "curl" library, for sending requests over
 * the internet using a wide variety of protocols.
 */


/* This data type is used as a read buffer */
typedef struct {
	size_t	used;	/* used size */
	size_t	size;	/* allocated size */
	size_t	max;	/* maximum in-memory size */
	char	*buf;	/* in-memory buffer */
	FILE	*fp;	/* temp file buffer */
} receiver_t;


/* This is a callback function for CURL to send us the received data */
static size_t receive(char *data, size_t size, size_t nmemb, void *clientp)
{
	receiver_t *rcv = (receiver_t *)clientp;

	/* If necessary, enlarge the buffer */
	if (rcv->used + size * nmemb > rcv->size) {
		rcv->size = ((rcv->used + size * nmemb) | 0x3fff) + 1; /* 16K chunks */
		rcv->buf = (char *)realloc(rcv->buf, rcv->size);
	}

	/* Add new data to the buffer */
	memcpy(rcv->buf + rcv->used, data, size * nmemb);
	rcv->used += size * nmemb;
	return size * nmemb;
}



/* This internal utility function generates URL-encoded form parameters from
 * an object or string.  Returns the length of the string not counting the
 * terminating NUL byte.  If buf is non-NULL then the text is stored there.
 */
static size_t urlencode(json_t *data, char *buf, int component)
{
	size_t	len, chunk;
	char	*text;
	json_t	*scan;
	char	*special = " ";

	if (component)
		special = " :/?&#";

	if (data->type == JSON_STRING
	 || data->type == JSON_BOOL
	 || (data->type == JSON_NUMBER && *data->text)) {
		/* Printable ASCII is left unchanged except that spaces become
		 * "+", "+" and "&" become %2B and %26 respectively, and every
		 * other byte is converted to %xx hex.  This means multibyte
		 * UTF-8 characters are converted to multiple %xx sequences.
		 */
		len = 0;
		for (text = data->text; *text; text++) {
			if (*text < ' '
			 || *text > '~'
			 || strchr(special, *text)) {
				if (buf)
					sprintf(&buf[len], "%%%02X", *text & 0xff);
				len += 3;
			} else {
				if (buf)
					buf[len] = *text;
				len++;
			}
		}
		if (buf)
			buf[len] = '\0';
		return len;
	} else if (data->type == JSON_NUMBER) {
		/* Convert to a string, and convert it recursively */
		scan = json_string("", 40);
		if (data->text[1] == 'i')
			snprintf(scan->text, 40, "%d", JSON_INT(data));
		else
			snprintf(scan->text, 40, "%g", JSON_DOUBLE(data));
		len = urlencode(scan, buf, 1);
		json_free(scan);
		return len;
	} else if (data->type == JSON_OBJECT) {
		/* Convert to a series of name=value strings */
		len = 0;
		for (scan = data->first; scan; scan = scan->next) {
			/* Skip if value is another object or an array.  Note
			 * that since this is an object member, "scan" points
			 * to a JSON_KEY and the value is scan->first.
			 */
			if (scan->first->type == JSON_OBJECT || scan->first->type == JSON_ARRAY)
				continue;

			/* If not first, then add a "&" */
			if (len > 0) {
				if (buf)
					buf[len] = '&';
				len++;
			}

			/* Add the name */
			for (text = scan->text; *text; text++) {
				if (buf)
					buf[len] = *text;
				len++;
			}

			/* Add an "=" */
			if (buf)
				buf[len] = '=';
			len++;

			/* Add the value */
			if (buf)
				chunk = urlencode(scan->first, buf + len, 1);
			else
				chunk = urlencode(scan->first, NULL, 1);
			len += chunk;
		}
		if (buf)
			buf[len] = '\0';
		return len;
	}

	/* For anything else -- mostly null or array -- return 0 */
	return 0;
}

/* curlGet(url:string, data?:string|object, headers?:string[], decode:?boolean):string
 * Read a URL using HTTP "GET"
 */
static json_t *jfn_curlGet(json_t *args, void *agdata)
{
	CURL	*curl;
	CURLcode result;
	struct curl_slist *slist = NULL;
	char	*url, *str, *mustfree;
	int	decode;
	size_t	arglen;
	receiver_t rcv = {0};
	json_t	*more, *scan, *data, *headers, *response;

	/* Check required parameters */
	if (!args->first || args->first->type != JSON_STRING)
		return json_error_null(0, "The %s() function requires a URL string", "curlGet");

	/* Check optional parameters. */
	data = headers = NULL;
	decode = 0;
	for (more = args->first->next; more; more = more->next) {
		if (more->type == JSON_ARRAY) {
			/* List of request headers.  Must be all strings */
			headers = more;
			for (scan = headers->first; scan; scan = scan->next) {
				if (scan->type != JSON_STRING)
					return json_error_null(0, "For %s() then array of request headers must be all strings", "curlGet");
			}
		} else if (more->type == JSON_BOOL)
			decode = json_is_true(more);
		else if (more->type == JSON_OBJECT || more->type == JSON_STRING)
			data = more;
		else
			return json_error_null(0, "Extra parameter added to %s()", "curlGet");
	}

	/* Build a list of request headers */
	if (decode)
		slist = curl_slist_append(slist, "Accept: application/json");
	if (headers) {
		for (scan = headers->first; scan; scan = scan->next) {
			slist = curl_slist_append(slist, scan->text);
		}
	}

	/* Open a handle */
	curl = curl_easy_init();
	if (!curl)
		return json_error_null(0, "Failed to allocate a CURL handle");

	/* Get the URL */
	url = args->first->text;
	mustfree = NULL;

	/* If there is data, append it to the URL */
	if (data) {
		if (data->type == JSON_STRING) {
			str = data->text;
			arglen = strlen(str);
			mustfree = (char *)malloc(strlen(url) + 2 + arglen);
			strcpy(mustfree, url);
			if (strchr(url, '?'))
				strcat(mustfree, "&");
			else
				strcat(mustfree, "?");
			strcat(mustfree, str);
		} else /* JSON_OBJECT */ {
			arglen = urlencode(data, NULL, 1);
			mustfree = (char *)malloc(strlen(url) + 2 + arglen);
			strcpy(mustfree, url);
			if (strchr(url, '?'))
				strcat(mustfree, "&");
			else
				strcat(mustfree, "?");
			urlencode(data, mustfree + strlen(mustfree), 1);
		}
		url = mustfree;
	}

	/* Set up the transfer */
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&rcv);

	/* Do it */
	result = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	/* Detect errors */
	if (result != CURLE_OK)
		return json_error_null(0, "CURL error %d", (int)result);

	/* Maybe try to parse it; otherwise convert the returned data to a
	 * string.  If the returned data isn't really text, this could be
	 * embarrassing.
	 */
	response = NULL;
	if (decode)
		response = json_parse_string(rcv.buf);
	if (!response)
		response = json_string(rcv.buf ? rcv.buf : "", rcv.used);

	/* Clean up */
	if (mustfree)
		free(mustfree);
	if (rcv.buf)
		free(rcv.buf);

	/* Return the response text */
	return response;
}

/* curlPost(url:string, data:string|object|array, mimetype?:string, headers?:string[], decode?:boolean):string
 * Send data via an HTTP "POST" request, and return the response string.
 */
static json_t *jfn_curlPost(json_t *args, void *agdata)
{
	CURL	*curl;
	CURLcode result;
	struct curl_slist *slist = NULL;
	char	*url;
	json_t	*content, *headers, *response, *more, *scan;
	char	*contentType;
	char	*contentstr, *headerstr;
	int	decode;
	receiver_t rcv = {0};

	/* Check required parameters */
	if (!args->first || args->first->type != JSON_STRING)
		return json_error_null(0, "The %s() function requires a URL string", "curlPost");
	url = args->first->text;
	content = args->first->next;
	if (!content || (content->type != JSON_STRING && content->type != JSON_OBJECT && content->type != JSON_ARRAY))
		return json_error_null(0, "The %s() function requires data as its second argument", "curlPost");

	/* Allow optional parameters in any order */
	headers = NULL;
	contentType = NULL;
	decode = 0;
	for (more = content->next; more; more = more->next) {
		if (more->type == JSON_STRING) {
			contentType = more->text;
			if (strchr(contentType, ':') || !strchr(contentType, '/'))
				return json_error_null(0, "Any extra string passed to %s() should be a MIME type", "curlPost");
		} else if (more->type == JSON_ARRAY) {
			/* Make sure they're all strings */
			for (scan = more->first; scan; scan = scan->next)
				if (scan->type != JSON_STRING || !strchr(scan->text, ':'))
					return json_error_null(0, "An array passed to %s() should contain only header strings", "curlPost");
			headers = more;
		} else if (more->type == JSON_BOOL) {
			decode = json_is_true(more);
		} else
			return json_error_null(0, "Invalid type of optional parameter passed to %s()", "curlPost");
	}

	/* If a content type was given, and the content isn't a string, then
	 * convert the content to a string in an appropriate way.  This only
	 * works for HTML form data or 
	 */
	if (contentType) {
		if (content->type == JSON_STRING)
			contentstr = content->text;
		else if (strstr(contentType, "json") || strstr(contentType, "JSON")) {
			contentstr = json_serialize(content, NULL);
		} else if (strstr(contentType, "form") || strstr(contentType, "FORM")) {
			size_t arglen = urlencode(content, NULL, 1);
			contentstr = (char *)malloc(arglen + 1);
			urlencode(content, contentstr, 1);
		} else
			return json_error_null(0, "The %s() function can't convert data to %s", "curlPost", contentType);
	} else if (content->type == JSON_STRING) {
		switch (*content->text) {
		case '{':	/* } */
		case '[':	contentType = "application/json";	break;
		case '<':	contentType = "application/xml";	break;
		default:	contentType = "application/x-www-form-urlencoded";
		}
		contentstr = content->text;
	} else if (content->type == JSON_ARRAY) {
		contentType = "application/json";
		contentstr = json_serialize(content, NULL);
	} else if (content->type == JSON_OBJECT) {
		/* If all member values are strings or numbers, assume HTML
		 * form otherwise assume JSON
		 */
		for (scan = content->first; scan; scan = scan->next) {
			if (scan->first->type != JSON_STRING && scan->first->type != JSON_NUMBER)
				break;
		}
		if (scan) {
			/* complex values, can't be a form so assume JSON */
			contentstr = json_serialize(content, NULL);
			contentType = "application/json";
		} else {
			/* simple values, it's probably form data */
			size_t arglen = urlencode(content, NULL, 1);
			contentstr = (char *)malloc(arglen + 1);
			urlencode(content, contentstr, 1);
			contentType = "application/x-www-form-urlencoded";
		}
	} else
		return json_error_null(0, "The %s() function can't guess the content type");

	/* Build a list of request headers */
	headerstr = (char *)malloc(strlen(contentType) + 15);
	strcpy(headerstr, "Content-Type: ");
	strcat(headerstr, contentType);
	slist = curl_slist_append(slist, headerstr);
	free(headerstr);
	if (decode)
		slist = curl_slist_append(slist, "Accept: application/json");
	if (headers) {
		for (scan = headers->first; scan; scan = scan->next) {
			slist = curl_slist_append(slist, scan->text);
		}
	}

	/* Open a handle */
	curl = curl_easy_init();
	if (!curl)
		return json_error_null(0, "Failed to allocate a CURL handle");

	/* Set up the transfer */
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, contentstr);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&rcv);

	/* Do it */
	result = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	if (contentstr != content->text)
		free(contentstr);

	/* Detect errors */
	if (result != CURLE_OK)
		return json_error_null(0, "CURL error %d", (int)result);

	/* Maybe try to parse it; otherwise convert the returned data to a
	 * string.  If the returned data isn't really text, this could be
	 * embarrassing.
	 */
	response = NULL;
	if (decode)
		response = json_parse_string(rcv.buf);
	if (!response)
		response = json_string(rcv.buf ? rcv.buf : "", rcv.used);

	/* Clean up */
	if (rcv.buf)
		free(rcv.buf);

	/* Return the response text */
	return response;
}

static json_t *jfn_encodeURI(json_t *args, void *agdata)
{
	size_t	len;
	json_t	*result;

	/* Predict the length */
	len = urlencode(args->first, NULL, 0);
	if (len == 0)
		return json_error_null(0, "Bad argument for %s()", "encodeURI");

	/* Allocate a result buffer */
	result = json_string("", len);

	/* Store the text in the result */
	urlencode(args->first, result->text, 0);

	/* Return it */
	return result;
}

static json_t *jfn_encodeURIComponent(json_t *args, void *agdata)
{
	size_t	len;
	json_t	*result;

	/* Predict the length */
	len = urlencode(args->first, NULL, 1);
	if (len == 0)
		return json_error_null(0, "Bad argument for %s()", "encodeURIComponent");

	/* Allocate a result buffer */
	result = json_string("", len);

	/* Store the text in the result */
	urlencode(args->first, result->text, 1);

	/* Return it */
	return result;
}

static json_t *jfn_uuid(json_t *args, void *agdata)
{
	int	fd, i;
	char	bytes[16];
	json_t	*result;
	char	*build;

	/* Read 16 bytes from /dev/random or /dev/urandom.  If that fails,
	 * use lrand48() even though it isn't really random enough.
	 */
	fd = open("/dev/random", O_RDONLY);
	if (fd < 0)
		fd = open("/dev/urandom", O_RDONLY);
	if (fd >= 0) {
		read(fd, bytes, sizeof bytes);
		close(fd);
	} else {
		for (i = 0; i < sizeof bytes; i++)
			bytes[i] = lrand48();
	}

	/* Convert it to a string in 8-4-4-4-12 format. */
	result = json_string("", 36);
	for (i = 0, build = result->text; i < sizeof bytes; i++) {
		if (i == 4 || i == 6 || i == 8 || i == 10)
			*build++ = '-';
		sprintf(build, "%02x", bytes[i] & 0xff);
		build += 2;
	}

	/* Return it */
	return result;
}

/* These are the "digits" in MIME base-64 encoding */
const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Convert a string to MIME base-64 encoding */
static json_t *jfn_mime64(json_t *args, void *agdata)
{
	size_t	len, i, j;
	json_t	*result;
	char	*str;
	int	digit;

	/* We expect a single string as argument */
	if (args->first->type != JSON_STRING || args->first->next)
		return json_error_null(0, "The %s() function takes a single string argument", "mime64");

	/* Each group of 3 bytes is represented by 4 digits, so we can calculate
	 * the final size of the string immediately.
	 */
	len = strlen(args->first->text);
	len = ((len + 2) / 3) * 4;

	/* Allocate the result buffer */
	result = json_string("", len);

	/* Convert each group of 3 bytes */
	for (str = args->first->text, j = 0; str[0] && str[1] && str[2]; str += 3) {
		/* Most significant 6 bits of first byte */
		digit = (*str >> 2) & 0x3f;
		result->text[j++] = b64[digit];

		/* Remaining 2 bits, plus 4 from second byte */
		digit = ((*str << 4) & 0x30) | ((str[1] >> 4) & 0x0f);
		result->text[j++] = b64[digit];

		/* Other 4 bits from second byte, plus 2 from third */
		digit = ((str[1] << 2) & 0x3c) | ((str[2] >> 6) & 0x03);
		result->text[j++] = b64[digit];

		/* Final 6 bits fro third byte */
		digit = (str[2] & 0x3f);
		result->text[j++] = b64[digit];

	}

	/* If there's any leftover bytes, convert them too */
	if (str[0] && str[1]) {
		/* TWO BYTES LEFT, NEED 3 MORE DIGITS PLUS "=" */

		/* Most significant 6 bits of first byte */
		digit = (*str >> 2) & 0x3f;
		result->text[j++] = b64[digit];

		/* Remaining 2 bits, plus 4 from second byte */
		digit = ((*str << 4) & 0x30) | ((str[1] >> 4) & 0x0f);
		result->text[j++] = b64[digit];

		/* Other 4 bits from second byte, padded with 0's */
		digit = ((str[1] << 2) & 0x3c);
		result->text[j++] = b64[digit];

		/* Pad the string with an = sign */
		result->text[j++] = '=';
	} else if (str[0]) {
		/* ONE BYTE LEFT, NEED 2 MORE DIGITS PLUS "==" */

		/* Most significant 6 bits of first byte */
		digit = (*str >> 2) & 0x3f;
		result->text[j++] = b64[digit];

		/* Remaining 2 bits, padded with 0's */
		digit = ((*str << 4) & 0x30);
		result->text[j++] = b64[digit];

		/* Pad the string with = signs */
		result->text[j++] = '=';
		result->text[j++] = '=';
	} 

	return result;
}


/* This is the init function.  It registers all of the above functions, and
 * adds some constants to the Math object.
 */
char *init()
{
	json_t	*math;

	/* Initialize CURL */
	curl_global_init(CURL_GLOBAL_DEFAULT);

	/* Register the functions */
	json_calc_function_hook("curlGet", "url:string, data?:string|object, headers?:string[], decode?:boolean", "string | any", jfn_curlGet);
	json_calc_function_hook("curlPost", "url:string, data:any, mimetype?:string, headers?:string[], decode?:boolean", "string | any", jfn_curlPost);
	json_calc_function_hook("encodeURI",  "data:object|string|number|boolean", "string", jfn_encodeURI);
	json_calc_function_hook("encodeURIComponent",  "data:object|string|number|boolean", "string", jfn_encodeURIComponent);
	json_calc_function_hook("uuid",  "", "string", jfn_uuid);
	json_calc_function_hook("mime64",  "data:string", "string", jfn_mime64);

	/* Success */
	return NULL;
}
