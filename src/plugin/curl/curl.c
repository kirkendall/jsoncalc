#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <jsoncalc.h>
#include <curl/curl.h>

/* This plugin gives access to the "curl" library, for sending requests over
 * the internet using a wide variety of protocols.
 */

static char *settings = "{"
	"\"buffer\":0,"
	"\"cookiejar\":\"\""
"}";

/* These bits are used to select boolean options */
typedef enum { 
	OPT_PROXY_ = 1,		/* (url) Define an optional proxy server */
	OPT_USERNAME_,		/* (user) Use Authentication: Basic */
	OPT_PASSWORD_,		/* (pass) Password to pair with OPT_USERNAME_ */
	OPT_BEARER_,		/* (token) Use Authentication: Bearer */
	OPT_REQCONTENT, 	/* Send content with the request */
	OPT_REQCONTENTTYPE_,	/* (mimetype) Define the Content-Type of the request content */
	OPT_REQHEADER_,		/* (headerlines) Add header request lines */
	OPT_COOKIES,		/* Allow cookies to be sent/received */
	OPT_FOLLOWLOCATION,	/* Follow HTTP 3xx redirects */
	OPT_DECODE,		/* Try to decode the response content */
	OPT_HEADERS		/* Return headers along with content */
} opt_t;

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
	if (rcv->used + size * nmemb + 1> rcv->size) {
		rcv->size = ((rcv->used + size * nmemb) | 0x3fff) + 1; /* 16K chunks */
		rcv->buf = (char *)realloc(rcv->buf, rcv->size);
	}

	/* Add new data to the buffer */
	memcpy(rcv->buf + rcv->used, data, size * nmemb);
	rcv->used += size * nmemb;
	rcv->buf[rcv->used] = '\0';
	return size * nmemb;
}

/* This stores the name of a temporary cookie jar (file) */
static char *tempcookiejar;

/* When exiting, delete the temporary cookie jar (if any) */
static void curlcleanup(void)
{
	if (!tempcookiejar)
		return;
	unlink(tempcookiejar);
	free(tempcookiejar);
}


/* Convert a big header string into an array of header lines */
static json_t *headerArray(char *header)
{
	json_t *array = json_array();
	size_t	len;

	/* until we hit the blank line parking the end... */
	while (*header >= ' ') {
		/* Look for the end of this line */
		for (len = 1; header[len] >= ' '; len++) {
		}
		json_append(array, json_string(header, len));
		if (header[len] == '\r' && header[len + 1] == '\n')
			header += len + 2;
		else
			header += len;
	}
	return array;
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

typedef struct {
	struct curl_slist *slist;	/* List of request headers to add */
	char	*reqcontenttype;	/* MIME type of the request content */
	int	content;		/* Send data as content? */
	int	decode;			/* Decode the response? */
	int	headers;		/* Return the response headers too? */
} curlflags_t;

/* Parse a series of flags.  For each one, either set a curl option directly
 * or update the data in "flags".
 *
 * This returns NULL on success, or a json_t error on failure.  For failure,
 * the calling function is responsible for doing cleanup before returning,
 * including freeing flags->slist.
 */
static json_t *doFlags(char *fn, CURL *curl, json_t *data, curlflags_t *flags, json_t *more)
{
	json_t	*err;
	char	*str;

	/* For each item in the list... */
	for (; more; more = more->next) {
		/* If it's an array, process it recursively */
		if (more->type == JSON_ARRAY) {
			err = doFlags(fn, curl, data, flags, more->first);
			if (err)
				return err;
			continue;
		}

		/* If it isn't an option number, that's a problem */
		if (more->type != JSON_NUMBER)
			return json_error_null(0, "Bad extra argument passed to the  %s() function", fn);

		/* Process each option flag separately */
		switch (json_int(more)) {
		case OPT_PROXY_:
			if (!more->next || more->next->type != JSON_STRING)
				return json_error_null(0, "In %s(), OPT_PROXY_ needs to be followed by a URL string", fn);
			more = more->next;
			curl_easy_setopt(curl, CURLOPT_PROXY, more->text);
			curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, 1L);
			break;

		case OPT_USERNAME_:
			if (!more->next || more->next->type != JSON_STRING)
				return json_error_null(0, "In %s(), OPT_USERNAME_ needs to be followed by a username string", fn);
			more = more->next;
			curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
			curl_easy_setopt(curl, CURLOPT_USERNAME, more->text);
			break;
		case OPT_PASSWORD_:
			if (!more->next || more->next->type != JSON_STRING)
				return json_error_null(0, "In %s(), OPT_PASSWORD_ needs to be followed by a password string", fn);
			more = more->next;
			curl_easy_setopt(curl, CURLOPT_PASSWORD, more->text);
			break;
		case OPT_BEARER_:
			if (!more->next || more->next->type != JSON_STRING)
				return json_error_null(0, "In %s(), OPT_BEARER_ needs to be followed by a bearer token string", fn);
			more = more->next;
			curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
			curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, more->text);
			break;
		case OPT_REQCONTENT:
			if (!data)
				return json_error_null(0, "In %s(), CURL.reqContent only works if content is given after URL", fn);
			flags->content = 1;
			break;
		case OPT_REQCONTENTTYPE_:
			if (!more->next
			 || more->next->type != JSON_STRING
			 || strchr(more->next->text, ':')
			 || !strchr(more->next->text, '/')) {
				return json_error_null(0, "In %s(), OPT_CONTENTTYPE needs to be followed by a bearer token string", fn);
			}
			more = more->next;
			flags->reqcontenttype = more->text;
			break;
		case OPT_REQHEADER_:
			more = more->next;
			if (!more || more->type != JSON_STRING || !strchr(more->text, ':'))
				return json_error_null(0, "In %s(), OPT_CONTENTTYPE needs to be followed by a bearer token string", fn);
			flags->slist = curl_slist_append(flags->slist, more->text);
			break;
		case OPT_COOKIES:
			/* If cookiejar is "" then make one up */
			str = json_config_get("plugin.curl", "cookiejar")->text;
			if (!str || !*str)
				str = tempcookiejar;
			if (!str) {
				str = json_file_path(NULL, NULL, NULL, 0, 0);
				tempcookiejar = (char *)malloc(strlen(str) + 18);
				strcpy(tempcookiejar, str);
				strcat(tempcookiejar, "cookiejar.XXXXXX");
				close(mkstemp(tempcookiejar));
				atexit(curlcleanup);
				free(str);
				str = tempcookiejar;
			}

			/* Tell the cURL library to use it */
			curl_easy_setopt(curl, CURLOPT_COOKIEFILE, str);
			curl_easy_setopt(curl, CURLOPT_COOKIEJAR, str);
			break;
		case OPT_FOLLOWLOCATION:
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			break;
		case OPT_DECODE:
			flags->decode = 1;
			break;
		case OPT_HEADERS:
			flags->headers = 1;
			break;
		default:
			return json_error_null(0, "Invalid option number %d passed to %s()", json_int(more), fn);
		}

	}
	return NULL;
}

/* Construct a CURL request, send it, and receive the response.  "fn" is the
 * function name, for reporting purposes.  "request" is an HTTP verb -- usually
 * "GET" or "POST", but it could be "HEAD", "DELETE", or whatever.  "argsfirst"
 * is the first element of a JSON array of arguments.
 */
static json_t *curlHelper(char *fn, char *request, json_t *argsfirst)
{
	CURL	*curl;
	CURLcode result;
	char	*url, *str, *mustfree;
	json_t	*data, *err;
	curlflags_t flags;
	size_t	arglen;
	receiver_t rcv = {0}, hdr = {0};
	json_t	*more, *scan, *response;

	/* Allocate a CURL handle */
	curl = curl_easy_init();
	if (!curl)
		return json_error_null(0, "Failed to allocate a CURL handle in %s()", fn);

	/* First argument must be URL */
	if (!argsfirst || argsfirst->type != JSON_STRING)
		return json_error_null(0, "The %s() function requires a URL string", fn);
	url = argsfirst->text;

	/* If next arg isn't a number, then it must be data... except that if
	 * it's null then there is no data.
	 */
	data = NULL;
	more = argsfirst->next;
	if (more && more->type == JSON_NULL) {
		more = more->next;
		/* but leave data set to NULL */
	}
	else if (more && more->type != JSON_NUMBER) {
		data = more;
		more = more->next;
	}

	/* Scan the args for option numbers.  Some options are followed by
	 * other data.
	 */
	flags.reqcontenttype = NULL;
	flags.content = !strcmp(request, "POST");
	flags.decode = flags.headers = 0;
	flags.slist = NULL;
	err = doFlags(fn, curl, data, &flags, more);
	if (err) {
		curl_easy_cleanup(curl);
		curl_slist_free_all(flags.slist);
		return json_error_null(0, "In %s(), OPT_PROXY_ needs to be followed by a URL string", fn);
	}

	/* If we're supposed to send content but have no data, fail */
	if (flags.content && !data) {
		curl_easy_cleanup(curl);
		curl_slist_free_all(flags.slist);
		return json_error_null(0, "The %s() needs data to send", fn);
	}

	/* If a content type was given, and the content isn't a string, then
	 * convert the content to a string in an appropriate way.  This only
	 * works for HTML form data or JSON.
	 */
	mustfree = str = NULL;
	if (flags.reqcontenttype) {
		if (data->type == JSON_STRING)
			str = data->text;
		else if (strstr(flags.reqcontenttype, "json") || strstr(flags.reqcontenttype, "JSON")) {
			mustfree = str = json_serialize(data, NULL);
		} else if (strstr(flags.reqcontenttype, "form") || strstr(flags.reqcontenttype, "FORM")) {
			size_t arglen = urlencode(data, NULL, 1);
			mustfree = str = (char *)malloc(arglen + 1);
			urlencode(data, str, 1);
		} else {
			curl_easy_cleanup(curl);
			curl_slist_free_all(flags.slist);
			return json_error_null(0, "The %s() function can't convert data to %s", fn, flags.reqcontenttype);
		}
	} else if (data && data->type == JSON_STRING) {
		switch (*data->text) {
		case '{': /* } */
		case '[': flags.reqcontenttype = "application/json";	break;
		case '<': flags.reqcontenttype = "application/xml";	break;
		default:  flags.reqcontenttype = "application/x-www-form-urlencoded";
		}
		str = data->text;
	} else if (data && data->type == JSON_ARRAY) {
		flags.reqcontenttype = "application/json";
		mustfree = str = json_serialize(data, NULL);
	} else if (data && data->type == JSON_OBJECT) {
		/* If all member values are strings or numbers, assume HTML
		 * form otherwise assume JSON
		 */
		for (scan = data->first; scan; scan = scan->next) {
			if (scan->first->type != JSON_STRING && scan->first->type != JSON_NUMBER)
				break;
		}
		if (scan) {
			/* complex values, can't be a form so assume JSON */
			str = json_serialize(data, NULL);
			flags.reqcontenttype = "application/json";
		} else {
			/* simple values, it's probably form data */
			size_t arglen = urlencode(data, NULL, 1);
			str = (char *)malloc(arglen + 1);
			urlencode(data, str, 1);
			flags.reqcontenttype = "application/x-www-form-urlencoded";
		}
	} else if (data) {
		curl_easy_cleanup(curl);
		curl_slist_free_all(flags.slist);
		return json_error_null(0, "The %s() function can't guess the content type", fn);
	}

	/* If we have data but aren't sending content, append it to the URL. */
	if (str && !flags.content) {
		char	*newurl = (char *)malloc(strlen(url) + 2 + strlen(str));
		strcpy(newurl, url);
		if (strchr(url, '?'))
			strcat(newurl, "&");
		else
			strcat(newurl, "?");
		strcat(newurl, str);
		if (mustfree)
			free(mustfree);
		mustfree = url = newurl;
	}

	/* If sending data as content, and we have a content-type, then add
	 * it to the list of header lines.
	 */
	if (str && flags.content && flags.reqcontenttype)
	{
		char	*tmp = (char *)malloc(15 + strlen(flags.reqcontenttype));
		strcpy(tmp, "Content-Type: ");
		strcat(tmp, flags.reqcontenttype);
		flags.slist = curl_slist_append(flags.slist, tmp);
		free(tmp);
	}

	/* Almost there!  Set the last few options */
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	if (flags.slist)
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, flags.slist);
	if (str && flags.content)
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, str);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&rcv);
	if (flags.headers) {
		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, receive);
		curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)&hdr);
	}

	/* Do it */
	result = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	curl_slist_free_all(flags.slist);
	if (mustfree)
		free(mustfree);

	/* Detect errors */
	if (result != CURLE_OK) {
		if (rcv.buf)
			free(rcv.buf);
		if (hdr.buf)
			free(hdr.buf);
		return json_error_null(0, "CURL error: %s", curl_easy_strerror(result));
	}

	/* Maybe try to parse it; otherwise convert the returned data to a
	 * string.  If the returned data isn't really text, this could be
	 * embarrassing.
	 */
	response = NULL;
	if (flags.decode && rcv.buf)
		response = json_parse_string(rcv.buf);
	if (!response)
		response = json_string(rcv.buf ? rcv.buf : "", rcv.used);

	/* If supposed to return headers, then build an object containing
	 * both the headers and the response
	 */
	if (flags.headers) {
		json_t *obj = json_object();
		json_append(obj, json_key("headers", headerArray(hdr.buf)));
		json_append(obj, json_key("response", response));
		response = obj;
	}

	/* Return the response */
	if (rcv.buf)
		free(rcv.buf);
	if (hdr.buf)
		free(hdr.buf);
	return response;
}

/* curlGet(url:string, data?:string|object, headers?:string[], decode:?boolean):any
 * Read a URL using HTTP "GET"
 */
static json_t *jfn_curlGet(json_t *args, void *agdata)
{
	return curlHelper("curlGet", "GET", args->first);
}

/* curlPost(url:string, data:string|object|array, ...):any
 * Send data via an HTTP "POST" request, and return the response string.
 */
static json_t *jfn_curlPost(json_t *args, void *agdata)
{
	return curlHelper("curlPost", "POST", args->first);
}

/* curlOther(verb:string, url:string, data:string|object|array, ...):any
 * Send data via an HTTP "POST" request, and return the response string.
 */
static json_t *jfn_curlOther(json_t *args, void *agdata)
{
	if (args->first->type != JSON_STRING)
		return json_error_null(0, "The first argument to %s() should be a request verb such as \"%s\"", "curlOther", "DELETE");
	return curlHelper("curlOther", args->first->text, args->first->next);
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

	/* Because this is version 4 (all random), the version digit should
	 * always be '4'.  That's the first digit in the third grouping.
	 */
	result->text[14] = '4';

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

		/* Final 6 bits from third byte */
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
char *plugincurl()
{
	json_t	*curl;

	/* Store the curl plugin's settings */
	curl = json_by_key(json_config, "plugin");
	json_append(curl, json_key("curl", json_parse_string(settings)));

	/* Add a "curl" object to json_system, to hold option consts */
	curl = json_object();
	json_append(curl, json_key("proxy_", json_from_int(OPT_PROXY_)));
	json_append(curl, json_key("username_", json_from_int(OPT_USERNAME_)));
	json_append(curl, json_key("password_", json_from_int(OPT_PASSWORD_)));
	json_append(curl, json_key("bearer_", json_from_int(OPT_BEARER_)));
	json_append(curl, json_key("reqContent", json_from_int(OPT_REQCONTENT)));
	json_append(curl, json_key("reqContentType_", json_from_int(OPT_REQCONTENTTYPE_)));
	json_append(curl, json_key("reqHeader_", json_from_int(OPT_REQHEADER_)));
	json_append(curl, json_key("cookies", json_from_int(OPT_COOKIES)));
	json_append(curl, json_key("followLocation", json_from_int(OPT_FOLLOWLOCATION)));
	json_append(curl, json_key("decode", json_from_int(OPT_DECODE)));
	json_append(curl, json_key("headers", json_from_int(OPT_HEADERS)));
	json_append(json_system, json_key("CURL", curl));

	/* Initialize CURL */
	curl_global_init(CURL_GLOBAL_DEFAULT);

	/* Register the functions */
	json_calc_function_hook("curlGet", "url:string, data?:string|object, ...", "string | any", jfn_curlGet);
	json_calc_function_hook("curlPost", "url:string, data:any, ...", "string | any", jfn_curlPost);
	json_calc_function_hook("curlOther", "verb:string, url:string, data?:any, ...", "string | any", jfn_curlOther);
	json_calc_function_hook("encodeURI",  "data:object|string|number|boolean", "string", jfn_encodeURI);
	json_calc_function_hook("encodeURIComponent",  "data:object|string|number|boolean", "string", jfn_encodeURIComponent);
	json_calc_function_hook("uuid",  "", "string", jfn_uuid);
	json_calc_function_hook("mime64",  "data:string", "string", jfn_mime64);

	/* Success */
	return NULL;
}
