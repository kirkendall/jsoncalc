#include <stdio.h>
#include <stdlib.h>
#define _XOPEN_SOURCE
#define __USE_XOPEN
#include <wchar.h>
#include <wctype.h>
#include <string.h>
#include "json.h"


/* This is a collection of functions for dealing with strings of multi-byte
 * characters.  Specifically UTF-8, though it should be locale-dependent.
 */

/* Count the characters (not bytes) in a mbs. */
size_t json_mbs_len(const char *s)
{
        wchar_t wc;
        int     in;
        size_t  len;
        mbstate_t state;

        /* Initialize the multibyte character state */
        memset(&state, 0, sizeof state);

        for (len = 0; *s; len++) {
                in = mbrtowc(&wc, s, MB_CUR_MAX, &state);
                s += in;
        }
        return len;
}

/* Count the width of a UTF-8 string.  This is different from the character
 * count json_mbs_len() or the byte count strlen(), partly because some
 * Unicode characters are doublewide, and diacritics are zerowide.  Also,
 * this function knows about newlines.
 */
int json_mbs_width(const char *s)
{
        wchar_t wc;
        int     in;
        int     charwidth, linewidth, width;
        mbstate_t state;

	/* Initialize the multibyte character state */
	memset(&state, 0, sizeof state);

	/* For each character... */
        for (width = linewidth = 0; *s; ) {
		/* Handle newline specially */
		if (*s == '\n') {
			if (linewidth > width)
				width = linewidth;
			linewidth = 0;
			s++;
			continue;
		}

		/* Convert the next UTF-8 character to a wc */
                in = mbrtowc(&wc, s, MB_CUR_MAX, &state);
                if (in <= 0) /* Invalid UTF-8 coding */
			break;
                s += in;

		/* Add the character's width to the string width */
                charwidth = wcwidth(wc);
                if (charwidth > 0)
			linewidth += charwidth;
        }
        if (linewidth > width)
		width = linewidth;
        return width;
}

/* Find the endpoints of a substring within s.  start is the character count
 * to the start of the substring, and if reflimit is non-NULL then it is a
 * character count coming in, and a byte count going out for the end of the
 * substring.  Returns a pointer to the start of the substring.  The string s
 * is not actually modified.
 */
const char *json_mbs_substr(const char *s, size_t start, size_t *reflimit)
{
        wchar_t wc;
        int     in;
        const char    *sptr;
        mbstate_t state;

	/* Initialize the multibyte character state */
	memset(&state, 0, sizeof state);

        /* Find the start */
        for (; start > 0 && *s; start--) {
                in = mbrtowc(&wc, s, MB_CUR_MAX, &state);
                s += in;
        }
        sptr = s;

        /* If there's a reflimit, count characters for it too */
        if (reflimit) {
                for (start = *reflimit; start > 0 && *s; start--) {
                        in = mbrtowc(&wc, s, MB_CUR_MAX, &state);
                        s += in;
                }
                *reflimit = (size_t)(s - sptr);
        }

        /* return the start pointer */
        return sptr;
}

/* Find the position of "needle" within "haystack", and return a pointer to it.
 * if refccount is non-NULL then return the character count before the start
 * of the match (if there is a match).  If reflen is non-NULL then return the
 * number of bytes (not characters) of "haystack" that match.  If the needle
 * isn't found, return NULL.
 */
const char *json_mbs_str(const char *haystack, const char *needle, size_t *refccount, size_t *reflen, int last, int ignorecase)
{
	wchar_t wc, nfirst;
	size_t	nlen, ccount, foundccount;
	int	in;
	const char *found;
	mbstate_t state;

	/* Initialize the multibyte state */
	memset(&state, 0, sizeof state);

	/* Get the first character of the needle.  If ignorecase then convert to lower*/
	in = mbrtowc(&nfirst, needle, MB_CUR_MAX, &state);
	if (in < 1)
		return NULL;
	if (ignorecase)
                nfirst = towlower(nfirst); 

	/* Also get the needle's length */
	nlen = json_mbs_len(needle);

	/* Scan for matches */
	ccount = foundccount = 0;
	for (found = NULL; *haystack; haystack += in, ccount++) {
		/* Check to see if the first character matches */
		in = mbrtowc(&wc, haystack, MB_CUR_MAX, &state);
		if (in < 1)
			return NULL;
		if (ignorecase)
			wc = towlower(wc);
		if (wc != nfirst)
			continue;

		/* Does the rest of the needle match too ? */
		if (ignorecase) {
			if (json_mbs_ncasecmp(haystack, needle, nlen) != 0)
				continue;
		} else {
			if (json_mbs_ncmp(haystack, needle, nlen) != 0)
				continue;
		}

		/* Found a match! */
		found = haystack;
		foundccount = ccount;

		/* If we wanted first match, we're done. */
		if (!last)
			break;
	}

	/* Return what we found */
	if (refccount && found)
		*refccount = foundccount;
	if (reflen && found) {
		/* Convert character count to byte count */
		json_mbs_substr(found, 0, &nlen);
		*reflen = nlen;
	}
	return found;
}


/* Case-sensitive comparison.  Here we don't try to do anything fancy with
 * case or even locale().
 */
int json_mbs_cmp(const char *s1, const char *s2)
{
        return strcmp(s1, s2);
}

/* Case-sensitive comparison up to a given number length.  "len" is a character
 * count, not a byte count.
 */
int json_mbs_ncmp(const char *s1, const char *s2, size_t len)
{
        /* Convert len from character count to byte count */
        const char *end = json_mbs_substr(s1, len, NULL);
        len = (end - s1);
        return strncmp(s1, s2, len);
}


/* Return a lowercase version of a string.  We expect the converted string to
 * still fit in the same buffer; if it won't, then the tail of the string is
 * *NOT* converted.
 */
void json_mbs_tolower(char *s)
{
        wchar_t wc;
        int     in;
        char    dummy[MB_CUR_MAX];
        mbstate_t state;

        /* Initialize the multibyte character state */
        memset(&state, 0, sizeof state);

        /* For each character ... */
        while (*s) {
                /* Conver to lowercase, if same size */
                in = mbrtowc(&wc, s, MB_CUR_MAX, &state);
                wc = towlower(wc); 
                if (in == wctomb(dummy, wc))
                        wctomb(s, wc);
                s += in;
        }
}

/* Return an uppercase version of a string.  We expect the converted string to
 * still fit in the same buffer; if it won't, then the tail of the string is
 * *NOT* converted.
 */
void json_mbs_toupper(char *s)
{
        wchar_t wc;
        int     in;
        char    dummy[MB_CUR_MAX];
        mbstate_t state;

        /* Initialize the multibyte character state */
        memset(&state, 0, sizeof state);

        /* For each character... */
        while (*s) {
		/* Convert to uppercase, if same size */
                in = mbrtowc(&wc, s, MB_CUR_MAX, &state);
                wc = towupper(wc); 
                if (in == wctomb(dummy, wc))
                        wctomb(s, wc);
                s += in;
        }
}

/* Return a mixed-case version of a string.  We expect the converted string to
 * still fit in the same buffer; if it won't, then the tail of the string is
 * *NOT* converted.  The "exceptions" argument should be an array of strings
 * in their preferred capitalization; if they end with "*" then only the start
 * of the string is compared.
 */
void json_mbs_tomixed(char *s, json_t *exceptions)
{
	json_t	arraybuf;
	json_t	*ex;
	int	firstword, capfirst;
	wctype_t alnum;
	wchar_t	wc;
	int	in, more, wlen;
	char	dummy[MB_CUR_MAX];
        mbstate_t state;

        /* Initialize the multibyte character state */
        memset(&state, 0, sizeof state);

	/* Make sure the list of exceptions is an array */
	if (!exceptions || exceptions->type != JSON_ARRAY) {
		arraybuf.type = JSON_ARRAY;
		arraybuf.first = exceptions;
		exceptions = &arraybuf;
	}

	/* Detect whether the first word should be capitalized despite
	 * exceptions by scanning the exceptions list for the symbol "true".
	 */
	capfirst = 0;
	for (ex = exceptions->first; ex && !capfirst; ex = ex->next)
		if (ex->type == JSON_BOOL && json_is_true(ex))
			capfirst = 1;

	/* Get the "alnum" classifier */
	alnum = wctype("alnum");

	/* While we have words... */
	for (firstword = 1; *s; firstword = 0) {
		/* If the next char isn't wordy, leave it */
                in = mbtowc(&wc, s, MB_CUR_MAX);
                if (!iswctype(wc, alnum)) {
			s += in;
			continue;
		}

		/* We've found a word!  Count its length */
		for (wlen = in; s[wlen] && (more = mbrtowc(&wc, s + wlen, MB_CUR_MAX, &state)) && iswctype(wc, alnum); wlen += more) {
		}

		/* Check the exception list.  If we're supposed to capitalize
		 * the first word regardless of the list, and this is indeed
		 * the first word, then skip this check.
		 */
		if (capfirst && firstword)
			ex = NULL;
		else {
			for (ex = exceptions->first; ex; ex = ex->next) {
				if (ex->type == JSON_STRING && !json_mbs_ncasecmp(ex->text, s, wlen))
					break;

			}
		}

		/* If we have an exception, use it */
		if (ex) {
			strncpy(s, ex->text, wlen);
			s += wlen;
			continue;
		}

		/* Else make first letter uppercase, others lowercase */
                in = mbrtowc(&wc, s, MB_CUR_MAX, &state);
                wc = towupper(wc); 
                if (in == wctomb(dummy, wc))
                        wctomb(s, wc);
                s += in;

                in = mbrtowc(&wc, s, MB_CUR_MAX, &state);
                while (iswctype(wc, alnum)) {
			wc = towlower(wc); 
			if (in == wctomb(dummy, wc))
				wctomb(s, wc);
			s += in;
			in = mbrtowc(&wc, s, MB_CUR_MAX, &state);
		}
	}
}

/* Compare two strings in a case-insensitive way */
int json_mbs_casecmp(const char *s1, const char *s2)
{
        wchar_t wc1, wc2;
        int     in1, in2;
        mbstate_t state;

        /* Initialize the multibyte character state */
        memset(&state, 0, sizeof state);

        while (*s1 && *s2) {
                in1 = mbrtowc(&wc1, s1, MB_CUR_MAX, &state);
                in2 = mbrtowc(&wc2, s2, MB_CUR_MAX, &state);
                wc1 = towupper(wc1); 
                wc2 = towupper(wc2); 
                if (wc1 < wc2)
                        return -1;
                else if (wc1 > wc2)
                        return 1;
                s1 += in1;
                s2 += in2;
        }

        /* If we get here, then either they are equal, or one is longer than
         * the other and the shorter one should come first.
         */
        if (!*s1 && !*s2)
                return 0;
        if (!s1)
                return -1;
        return 1;
}

/* Compare two strings in a case-insensitive way, up to a given length.
 * "len" is a character count, not a byte count.
 */
int json_mbs_ncasecmp(const char *s1, const char *s2, size_t len)
{
        wchar_t wc1, wc2;
        int     in1, in2;
        mbstate_t state;

        /* Initialize the multibyte character state */
        memset(&state, 0, sizeof state);

        while (*s1 && *s2 && len > 0) {
                in1 = mbrtowc(&wc1, s1, MB_CUR_MAX, &state);
                in2 = mbrtowc(&wc2, s2, MB_CUR_MAX, &state);
                wc1 = towupper(wc1); 
                wc2 = towupper(wc2); 
                if (wc1 < wc2)
                        return -1;
                else if (wc1 > wc2)
                        return 1;
                s1 += in1;
                s2 += in2;
                len--;
        }

        /* If we get here, then either they are equal, or one is longer than
         * the other and the shorter one should come first.
         */
        if (!*s1 && !*s2)
                return 0;
        if (len == 0)
                return 0;
        if (!*s1)
                return -1;
        return 1;
}

/* Compare an abbreviated name to the (possible) full name.  In json_calc(),
 * function names may be abbreviated to the first letter and any subsequent
 * uppercase letters.  For example, toUpperCase() can be written as tuc().
 */
int json_mbs_abbrcmp(const char *abbr, const char *full)
{
        wchar_t wc1, wc2;
        int     in1, in2;
        mbstate_t state;

        /* Initialize the multibyte character state */
        memset(&state, 0, sizeof state);

	/* First character must match */
	in1 = mbrtowc(&wc1, abbr, MB_CUR_MAX, &state);
	in2 = mbrtowc(&wc2, full, MB_CUR_MAX, &state);
	wc1 = towupper(wc1); 
	wc2 = towupper(wc2); 
	if (wc1 != wc2)
		return 1;
	abbr += in1;
	full += in2;

	/* after that, each letter of abbr must match uppercase full, but
	 * lowercase in full can be skipped over.
	 */
	while (*abbr && *full) {
		/* Skip lowercase from full, get char after that */
		do {
			in2 = mbrtowc(&wc2, full, MB_CUR_MAX, &state);
			full += in2;
		} while (iswlower(wc2));

		/* Get next abbr char */
		in1 = mbrtowc(&wc1, abbr, MB_CUR_MAX, &state);
		abbr += in1;
		wc1 = towupper(wc1);

		/* If different then no match */
		if (wc1 != wc2)
			return 1;
	}

	/* Skip any trailing lowercase letters */
	if (*full) {
		do {
			in2 = mbrtowc(&wc2, full, MB_CUR_MAX, &state);
			full += in2;
		} while (iswlower(wc2));
	}

	/* If any leftover unmatched chars, then no match */
	if (*abbr || *full)
		return 1;

	/* Match! */
	return 0;
}

/* Convert a single non-ASCII character to a \uXXXX sequence, or pair of \uXXXX
 * sequences as needed.  "str" is the first byte of the non-ASCII character to
 * convert.  The sequences are stored in "buf" which must be at least 13 chars
 * long to hold the two \uXXXX sequences.  Returns a pointer to the character
 * after the converted character.
 */
const char *json_mbs_ascii(const char *str, char *buf)
{
	wchar_t	wc;
	int	mbsize;
        mbstate_t state;

        /* Initialize the multibyte character state */
        memset(&state, 0, sizeof state);

	/* Convert the multibyte character to a wchar_t */
	mbsize = mbrtowc(&wc, str, MB_CUR_MAX, &state);

	/* Error? */
	if (mbsize <= 0) {
		*buf = '\0';
		return str + 1;
	}

	/* Can it fit in a single \uXXXX sequence? */
	if (wc <= 0xff) {
		sprintf(buf, "\\x%02x", (int)wc);
	} else if (wc <= 0xffff) {
		sprintf(buf, "\\u%04x", (int)wc);
	} else if (wc <= 0x10ffff) {
		int	high, low;
		wc -= 0x10000;
		high = (wc >> 10) | 0xd800;
		low = (wc & 0x3ff) | 0xdc00;
		sprintf(buf, "\\u%04x\\u%04x", high, low);
	} else {
		sprintf(buf, "\\U%08x", (int)wc);
	}

	/* Return a pointer to the next character */
	return str + mbsize;
}

/* Convert a string's control characters and optionally non-ASCII characters
 * to backslash sequences, and return the new length.  This does *not* add a
 * NUL character to the end of the string, or include room for a terminating
 * NUL character in the returned length.  If "dst" is NULL then just compute
 * the length.  If nbytes is -1 then use strlen() to the source string's length.
 * "quote" is another character to insert a backslash in front of, usually '"'.
 * "nonascii" can be 1 to convert non-ASCII to \uxxxx sequences.
 */
size_t json_mbs_escape(char *dst, const char *src, size_t nbytes, int quote, jsonformat_t *format)
{
        const char *end;
        size_t size;
        char	escape[13];

        /* If nbytes is -1 then use strlen to find the true length */
        if (nbytes == (size_t)-1)
                nbytes = strlen(src);

        /* For each character... */
        for (size = 0, end = src + nbytes; *src && src < end; src++) {
                /* non-ascii? */
                if (*src & 0x80) {
                        /* Non-ASCII either copy verbatim, or convert the whole
                         * multibyte character to a single \u sequence.
                         */
                        if (format->ascii) {
                                src = json_mbs_ascii(src, escape);
                                src--; /* because of src++ in the for-loop */
				if (dst)
					strcpy(dst + size, escape);
				size += strlen(escape);
                        } else {
                                /* Copy each byte verbatim */
                                if (dst)
                                        dst[size] = *src;
                                size++;
                        }
                } else if (*src < ' ' || *src == 127) {
                        /* Control characters become '\t' or '\x7f' */
                        int ch = 0;
                        switch (*src) {
                          case '\b':    ch = 'b';       break;
                          case '\f':    ch = 'f';       break;
                          case '\n':    ch = 'n';       break;
                          case '\r':    ch = 'r';       break;
                          case '\t':    ch = 't';       break;
                        }
                        if (ch) {
                                if (dst) {
                                        dst[size++] = '\\';
                                        dst[size++] = ch;
                                } else
					size += 2;
                        } else {
                                if (dst)
                                        sprintf(dst + size, "\\x%02x", *src);
                                size += 4;
                        }
                } else if (format->sh && *src == '\'') {
			/* For shell quoting, the entire output will be
			 * enclosed in ' quotes which is great for everything
			 * except ' itself.  For ' we need to end the quote,
			 * add backslash-', and start new ' quotes.
			 */
			if (dst) {
				dst[size] = '\'';
				dst[size + 1] = '\\';
				dst[size + 2] = '\'';
				dst[size + 3] = '\'';
			}
			size += 4;
                } else if (*src == quote || *src == '\\') {
                        /* quote and backslash need a backslash */
                        if (dst) {
                                dst[size++] = '\\';
                                dst[size++] = *src;
                        } else
				size += 2;
                } else {
                        /* ASCII, just copy it */
                        if (dst)
                                dst[size] = *src;
                        size++;
                }
        }

        /* Return the length */
        return size;
}

/* Convert a string's backslash sequences to characters, and return the new
 * length in bytes.  This does *not* add a NUL character to the end of the
 * string, or include room for a terminating NUL character in the returned
 * length.  If "dst" is NULL then just compute the length.  If nbytes is -1
 * then use strlen() to find the source string's length in bytes.
 */
size_t json_mbs_unescape(char *dst, const char *src, size_t nbytes)
{
        const char *end;
        size_t size;
        int mbsize;
        wchar_t wc;
        int limit, skipcurly;
        char dummy[MB_CUR_MAX];


size=nbytes;
        /* If nbytes is -1 then use strlen to find the true length */
        if (nbytes == (size_t)-1)
                nbytes = strlen(src);
        end = src + nbytes;

        /* For each character up to the end... */
        for (size = 0; src < end; src++) {
                /* We can copy characters verbatim except for backslashes.
                 * Even the bytes of multibyte characters.
                 */
                if (*src != '\\') {
                        if (dst)
                                *dst++ = *src;
                        size++;
                        continue;
                }

                /* Backslash! */
                src++;
                limit = 0;
                switch (*src) {
                  case '\0':
                        /* premature end of string, omit it */
                        break;
                  case 'b':
                        if (dst)
                                *dst++ = '\b';
                        size++;
                        break;
                  case 'e':
			if (dst)
				*dst++ = '\033'; /* ESC */
			size++;
			break;
                  case 'f':
                        if (dst)
                                *dst++ = '\f';
                        size++;
                        break;
                  case 'n':
                        if (dst)
                                *dst++ = '\n';
                        size++;
                        break;
                  case 'r':
                        if (dst)
                                *dst++ = '\r';
                        size++;
                        break;
                  case 't':
                        if (dst)
                                *dst++ = '\t';
                        size++;
                        break;
                  case 'u':
                        limit = 4;
                        break;
                  case 'U':
                        limit = 8;
                        break;
                  case 'x':
                        limit = 2;
                        break;
                  default:
                        if (dst)
                                *dst++ = *src;
                        size++;
                        break;
                }

                /* hex digits needed? */
                if (limit > 0) {
                        /* if \u{ then parse through the } */
			skipcurly = 0;
                        if (src[1] == '{') {
				limit = 8;
				skipcurly = 1;
				src++;
                        }

                        /* Convert hex digits to a wide character */
                        wc = 0;
                        while (limit > 0) {
                                limit--;
                                src++;
                                if (*src >= '0' && *src <= '9')
                                        wc = (wc << 4) + *src - '0';
                                else if (*src >= 'a' && *src <= 'f')
                                        wc = (wc << 4) + *src - 'a' + 10;
                                else if (*src >= 'A' && *src <= 'F')
                                        wc = (wc << 4) + *src - 'A' + 10;
                                else {
					/* We moved onto a non-hex digit, which
					 * is usually NOT part of the sequence.
					 */
					if (!skipcurly || *src != '}')
						src--;
                                        break;
				}
                        }

                        /* If it is a UTF-16 surrogate pair high codepoint,
                         * then look for a low codepoint to combine with it.
                         */
			if (wc >= 0xd800
		 	 && wc < 0xdc00
			 && src[1] == '\\'
			 && src[2] == 'u'
			 && (src[3] == 'd' || src[3] == 'D')
			 && strchr("cdefCDEF", src[4])) {
				/* Decode the second surrogate pair */
				wchar_t	wc2 = 0;
				src += 2;
				limit = 4;
				while (limit > 0) {
					limit--;
					src++;
					if (*src >= '0' && *src <= '9')
						wc2 = (wc2 << 4) + *src - '0';
					else if (*src >= 'a' && *src <= 'f')
						wc2 = (wc2 << 4) + *src - 'a' + 10;
					else if (*src >= 'A' && *src <= 'F')
						wc2 = (wc2 << 4) + *src - 'A' + 10;
					else {
						/* went one too far again */
						src--;
						break;
					}
				}

				/* Combine them */
				wc -= 0xd800;
				wc2 -= 0xdc00;
				wc = (wc << 10) + wc2 + 65536;
			}

                        /* Add the wide character to the string */
                        if (dst) {
                                mbsize = wctomb(dst, wc);
                                if (mbsize > 0)
                                        dst += mbsize;
                                else {
abort();
                                        *dst++ = '?';
                                        mbsize = 1;
                                }
                        } else {
                                mbsize = wctomb(dummy, wc);
                                if (mbsize <= 0)
                                        mbsize = 1;
                        }
                        size += mbsize;
                }
        }
        return size;
}

/* Compare a string to an SQL "LIKE" pattern.  In the pattern, % matches any
 * sequence of characters, _ matches any single character, and everything else
 * is compared for equality in a case-insensitive way.  Return 1 for a match,
 * 0 for mismatch.
 */
int json_mbs_like(const char *text, const char *pattern)
{
        wchar_t wc1, wc2;
        int     in1, in2;
        mbstate_t state;

        /* Initialize the multibyte character state */
        memset(&state, 0, sizeof state);

        /* Compare as much literal text as possible.  Also handle '%' */
        while (*text && *pattern && *pattern != '%') {
                in1 = mbrtowc(&wc1, text, MB_CUR_MAX, &state);
                in2 = mbrtowc(&wc2, pattern, MB_CUR_MAX, &state);
                if (wc2 != '_' && towupper(wc1) != towupper(wc2))
                        return 0;
                text += in1;
                pattern += in2;
        }

        /* If both ended, or pattern just has '%', then it matches */
        if (!*text && (!*pattern || (pattern[0] == '%' && !pattern[1])))
                return 1;

        /* If text ended before pattern, or pattern before text, NO MATCH */
        if ((*text && !*pattern) || (*pattern && !*text))
                return 0;

        /* If we get here then we have more text, and a pattern that starts
         * with % and has more text after that.  Test it.
         */
        pattern++;
        while (*text) {
                if (json_mbs_like(text, pattern))
                        return 1;
                text += mbrtowc(&wc1, text, MB_CUR_MAX, &state);
        }

        /* Nope, never found a match */
        return 0;
}

