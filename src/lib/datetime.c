#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include "json.h"

/* This file implements most of JsonCalc's date and time functions.  This is
 * messy!  C has poor date handling abilities, and the purpose of this file
 * is to encapsulate this messiness, so other files can be clean.
 */

/* This is used to store both absolute dates/times and periods */
typedef struct {
	int	year, month, day, hour, minute, second;
	int	tz;	/* minutes east of UTC */
	int	localtz;/* Use the local timezone, including daylight savings */
	int	z;	/* Prefer "Z" for UTC instead of "+00:00" */
} jsondatetime_t;


/* Return the number of days in the given month.  This is used by normalizedt()
 * and it also has the side-effect of normalizing the month and year, since we
 * need that to know the days.
 */
static int dayspermonth(jsondatetime_t *dt)
{
	static int daysper[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

	/* Normalize months, adjusting years */
	while (dt->month < 1) {
		dt->month += 12;
		dt->year--;
	}
	while (dt->month > 12) {
		dt->month -= 12;
		dt->year++;
	}

	if (dt->month == 2 && dt->year % 4 == 0)
		return 29;
	return daysper[dt->month - 1];
}

/* Normalize a date/time.  After editing one or more of the datetime fields, 
 * it's possible for some of them to get out of range; this adjusts those
 * fields and related fields to get everthing back in range again.  For example
 * if you *dt is 1999-12-32 then after normalization it'll be 2000-01-01.
 */
static void normalize(jsondatetime_t *dt)
{
	/* normalize seconds, adjusting minutes */
	while (dt->second < 0) {
		dt->second += 60;
		dt->minute--;
	}
	while (dt->second >= 60) {
		dt->second -= 60;
		dt->minute++;
	}

	/* normalize minutes, adjusting hours */
	while (dt->minute < 0) {
		dt->minute += 60;
		dt->hour--;
	}
	while (dt->minute >= 60) {
		dt->minute -= 60;
		dt->hour++;
	}

	/* normalize hours, adjusting days */
	while (dt->hour < 0) {
		dt->hour += 24;
		dt->day--;
	}
	while (dt->hour >= 24) {
		dt->hour -= 24;
		dt->day++;
	}

	/* Normalize days, adjusting months.  This is tricky since different
	 * months have different numbers of days.  The dayspermonth() function
	 * also has the side-effect of normalizing the month and year.
	 */
	while (dt->day < 1) {
		dt->month--; /* yes, adjust the month first */
		dt->day += dayspermonth(dt);
	}
	while (dt->day > dayspermonth(dt)) {
		dt->day -= dayspermonth(dt);
		dt->month++;
	}
}

/* Parse an ISO period string.  Return 0 if successful, 1 if malformed */
static int parseperiod(const char *str, jsondatetime_t *dt)
{
	int	num, sign, intime, infrac, weeks;

	/* Expect an initial 'P' */
	if (*str != 'P' && *str != 'p')
		return 1;
	num = 0;
	sign = 1;
	intime = infrac = 0;
	weeks = 0;
	memset(dt, 0, sizeof *dt);
	while (*++str) {
		switch (*str) {
		case '-':
			sign = -sign;
			break;
		case '\xe2':
			/* Unicode minus sign works!  U+2212, UTF-8 e28892 */
			if (str[1] == '\x88' && str[2] == '\x92') {
				sign = -sign;
				str += 2;
				break;
			}
			return 1;
		case '.':
		case ',':
			infrac = 1;
			break;
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			if (infrac)
				/* silently ignore fractions */
				break;
			num = num * 10 + *str - '0';
			break;
		case 'Y':
		case 'y':
			if (intime)
				return 0; /* no year in time */
			dt->year = num * sign;
			num = 0;
			sign = 1;
			infrac = 0;
			break;
		case 'M':
		case 'm':
			if (intime)
				dt->minute = num * sign;
			else
				dt->month = num * sign;
			num = 0;
			sign = 1;
			infrac = 0;
			break;
		case 'W':
		case 'w':
			if (intime)
				return 1; /* no weeks in time */
			weeks = num * sign;
			num = 0;
			sign = 1;
			infrac = 0;
			continue;
		case 'D':
		case 'd':
			if (intime)
				return 1; /* no day in time */
			dt->day = num * sign;
			num = 0;
			sign = 1;
			infrac = 0;
			break;
		case 'T':
		case 't':
			if (intime)
				return 1; /* do double T allowed */
			if (num != 0 || sign != 1)
				return 1; /* no number allowed before T */
			intime = 1;
			infrac = 0;
			break;
		case 'H':
		case 'h':
			if (!intime)
				return 1; /* no hour in date */
			dt->hour = num * sign;
			num = 0;
			sign = 1;
			infrac = 0;
			break;
		case 'S':
		case 's':
			if (!intime)
				return 1; /* no second in date */
			dt->second = num * sign;
			num = 0;
			sign = 1;
			infrac = 0;
			break;
		case ' ':
			/* Ignore spaces */
			break;
		default:
			/* Any other character means it's malformed */
			return 0;
		}
	}

	/* If we have a number without a letter, that's malformed */
	if (num != 0 || sign != 1)
		return 1;

	/* Merge weeks into days */
	dt->day += 7 * weeks;

	/* Looks like success to me! */
	return 0;
}

/* Add a period to a date or another period.  If "dt" really is a date then
 * you'll probably want to call normalize(dt) after this.
 */
static void addperiod(jsondatetime_t *dt, const jsondatetime_t *period)
{
	dt->year += period->year;
	dt->month += period->month;
	dt->day += period->day;
	dt->hour += period->hour;
	dt->minute += period->minute;
	dt->second += period->second;
}

/* Subtract a period to a date or another period.  If "dt" really is a date
 * then you'll probably want to call normalize(dt) after this.
 */
static void subtractperiod(jsondatetime_t *dt, const jsondatetime_t *period)
{
	dt->year -= period->year;
	dt->month -= period->month;
	dt->day -= period->day;
	dt->hour -= period->hour;
	dt->minute -= period->minute;
	dt->second -= period->second;
}

/* Parse timezone information at the end of str.  Str can also contain a
 * datetime or time, which is ignored.  The non-timezone fields of *dt are
 * NOT altered in any way.
 */
static void parsetz(const char *str, jsondatetime_t *dt)
{
	int tzhours, tzminutes;
	char tzsign[2];
	size_t len;

	/* Look for a timezone at the end */
	len = strlen(str);
	if (len >= 1 && str[len - 1] == 'Z') {
		dt->tz = 0;
		dt->localtz = 0;
		dt->z = 1;
		return;
	}
	if ((len >= 6 && sscanf(&str[len - 6], "%1[-+]%2d:%2d", tzsign, &tzhours, &tzminutes) == 3)
	 || (len >= 5 && sscanf(&str[len - 5], "%1[-+]%2d%2d", tzsign, &tzhours, &tzminutes) == 3)) {
		dt->tz = tzhours * 60 + tzminutes;
		if (*tzsign == '-')
			dt->tz = -dt->tz;
		dt->localtz = 0;
		dt->z = 0;
		return;
	}

	/* No timezone so assume local timezone */
	dt->localtz = 1;
}

/* Set the tzminutes for the local timezone of a given date.  (This takes a
 * some time, and usually when dealing with local timezones we don't care
 * about how far east/west of Greenwich we are.
 */
static void setlocaltz(jsondatetime_t *dt)
{
	time_t	when;	/* date/time in binary */
	struct tm tmlocal, tmutc;
	int	hours, minutes;

	/* Convert the localtime to a time_t */
	memset(&tmlocal, 0, sizeof tmlocal);
	tmlocal.tm_sec = dt->second;
	tmlocal.tm_min = dt->minute;
	tmlocal.tm_hour = dt->hour;
	tmlocal.tm_mday = dt->day;
	tmlocal.tm_mon = dt->month - 1;
	tmlocal.tm_year = dt->year - 1900;
	tmlocal.tm_isdst = -1;
	when = mktime(&tmlocal);

	/* Convert it back to struct tm in the UTC timezone */
	(void)gmtime_r(&when, &tmutc);

	/* Find the difference, in hours and minutes */
	hours = tmutc.tm_hour - tmlocal.tm_hour;
	minutes = tmutc.tm_min - tmlocal.tm_min;

	/* Hours should be in the range [-12,12].  If outside that range,
	 * that probably means dates changed.  Tweak the hour.
	 */
	if (hours < -12)
		hours += 24;
	else if (hours > 12)
		hours -= 24;

	/* Combine hours and minutes, and store in *dt */
	dt->tz = hours * 60 + minutes;
	dt->localtz = 1;
}

/* Convert a parsed datetime to a time_t */
static time_t dtbinary(jsondatetime_t *dt)
{
	jsondatetime_t localdt;
	struct tm tm;

	/* Make a copy of *dt that we can tweak */
	localdt = *dt;

	/* If not local timezone, then adjust to be local timezone */
	if (!localdt.localtz) {
		/* Subtract timezone minutes and normalize to get UTC date */
		localdt.minute -= localdt.tz;
		normalize(&localdt);

		/* Add local timezone minutes for that date and normalize to
		 * get local date time.
		 */
		localdt.localtz = 1;
		setlocaltz(&localdt);
		localdt.minute += localdt.tz;
		normalize(&localdt);
	}

	/* Convert to struct tm, and then to time_t */
	memset(&tm, 0, sizeof tm);
	tm.tm_sec = localdt.second;
	tm.tm_min = localdt.minute;
	tm.tm_hour = localdt.hour;
	tm.tm_mday = localdt.day;
	tm.tm_mon = localdt.month - 1;
	tm.tm_year = localdt.year - 1900;
	tm.tm_isdst = -1;
	return mktime(&tm);
}

/* Parse an ISO time string, with optional timezone */
static int parsetime(const char *str, jsondatetime_t *dt)
{
	int fields;

	/* Parse the date and (if present) time */
	memset(dt, 0, sizeof *dt);
	fields = sscanf(str, "%2d:%2d:%2d",
		&dt->hour, &dt->minute, &dt->second);
	if (fields < 2)
		return 0;

	/* Parse the timezone */
	parsetz(str, dt);
}

/* Parse a date or datetime, including the timezone (if any).  Returns 0 on
 * success, or 1 if str is malformed.
 */
static int parsedatetime(const char *str, jsondatetime_t *dt)
{
	int fields;

	/* Parse the date and (if present) time */
	memset(dt, 0, sizeof *dt);
	fields = sscanf(str, "%4d-%2d-%2dT%2d:%2d:%2d",
		&dt->year, &dt->month, &dt->day,
		&dt->hour, &dt->minute, &dt->second);
	if (fields != 6 && fields != 5 && fields != 3)
		return 1;

	/* Parse the timezone */
	parsetz(str, dt);
	return 0;
}

/* Convert a jsondatetime_t to a string.  "dtz" is "D" for date only, "T"
 * for time, "DT" for datetime, and "Z" to include the timezone.
 */
static void datetimestr(char *result, const jsondatetime_t *dt, const char *dtz)
{
	int	tz;

	/* Date */
	if (*dtz == 'D') {
		dtz++;

		sprintf(result, "%04d-%02d-%02d", dt->year, dt->month, dt->day);
		result += strlen(result);

		/* If also doing time, we need a "T" */
		if (*dtz == 'T')
			*result++ = 'T';
	}

	/* Time */
	if (*dtz == 'T') {
		dtz++;

		sprintf(result, "%02d:%02d:%02d", dt->hour, dt->minute, dt->second);
		result += strlen(result);
	}

	/* Timezone */
	if (*dtz == 'Z' && !dt->localtz) {
		tz = dt->tz;
		if (dt->z && tz == 0)
			*result++ = 'Z';
		else {
			if (tz < 0) {
				*result++ = '-';
				tz = -tz;
			} else {
				*result++ = '+';
			}
			sprintf(result, "%02d:%02d", tz / 60, tz % 60);
			result += strlen(result);
		}
	}

	/* Mark the end of the string */
	*result = '\0';
}

/* Return 1 iff str matches pattern.  "#" in pattern matches any digit in str,
 * "*" matches any text, "-" matches ASCII hyphen or Unicode minus sign,
 * "Z" is any timezone specifier (including none) and anything else must match
 * exactly (case-insensitive).
 */
static int match(const char *pattern, const char *str)
{
	/* Match text other than "*" */
	while (*pattern && *str && *pattern != '*') {
		if (*pattern == '#') {
			if (!isdigit(*str))
				return 0;
		} else if (*pattern == 'Z' && !pattern[1]) {
			if (!*str
			 || toupper(*str) == 'Z'
			 || match("+##:##", str)
			 || match("-##:##", str)
			 || match("+####", str)
			 || match("-####", str))
				return 1;
			return 0;
		} else if (*pattern == '-' && *str == '\xe2') {
			/* Unicode minus sign works!  U+2212, UTF-8 e28892 */
			if (str[1] != '\x88' || str[2] != '\x92')
				return 0;
			str += 2;
		} else if (toupper(*str) != *pattern)
			return 0;

		/* This char is good.  Check the next */
		pattern++;
		str++;
	}

	/* If we hit "*", check that recursively */
	if (*pattern == '*') {
		pattern++;
		if (!*pattern)
			return 1;
		while (*str) {
			if (match(pattern, str))
				return 1;
			else
				str++;
		}
	}

	/* If pattern still has chars left (except "Z") after str is exhausted,
	 * no match.  */
	if (!*str && *pattern && (*pattern != 'Z' || pattern[1]))
		return 0;

	/* And vice-versa */
	if (*str && !*pattern)
		return 0;

	/* okay, it matched */
	return 1;
}

/******************************************************************************/
/* Finally we get to the exposed functions                                    */

/* Return 1 iff *str looks like an ISO date "YYYY-MM-DD" */
int json_str_date(const char *str)
{
	return match("####-##-##", str);
}

/* Return 1 iff *str looks like an ISO time "YYYY-MM-DD" with optional TZ */
int json_str_time(const char *str)
{
	return match("##:##:##Z", str) || match("##:##Z", str);
}

/* Return 1 iff *str looks like an ISO datetime "YYYY-MM-DDThh:mm:ss" optional TAZ */
int json_str_datetime(const char *str)
{
	return match("####-##-##T##:##:##Z", str);
}

/* Return 1 iff *str looks like an ISO period "DnYnMnWnDTnHnMnS" */
int json_str_period(const char *str)
{
	jsondatetime_t period;

	/* If we can parse it as a period, then its a period */
	return parseperiod(str, &period) == 0;
}


/* This is a helper function, combining common parts of json_date(),
 * json_time(), and json_datetime().  The "dtz" parameter controls behavior.
 * Return 0 on success or non-zero on error.
 */
static int dtzhelper(char *result, const char *str, const char *tz, const char *dtz)
{
	jsondatetime_t dt, newtz;

	/* Parse it */
	if (*dtz == 'D') {
		if (parsedatetime(str, &dt))
			return 1;
	} else {
		if (parsetime(str, &dt))
			return 1;
	}

	/* If "tz" is given, convert the timezone */
	if (tz) {
		/* Parse the new timezone */
		memset(&newtz, 0, sizeof newtz);
		parsetz(tz, &newtz);

		/* If one is local and one isn't, then fill in tz as local */
		if (newtz.localtz && !dt.localtz)
			setlocaltz(&newtz);
		if (!newtz.localtz && dt.localtz)
			setlocaltz(&dt);

		/* If different, then convert */
		if (newtz.tz != dt.tz) {
			dt.minute += (dt.tz - newtz.tz);
			normalize(&dt);
			dt.tz = newtz.tz;
			dt.localtz = newtz.localtz;
			dt.z = newtz.z;
		}
	}

	/* Convert back to a string */
	datetimestr(result, &dt, dtz);
	return 0;
}

/* Return the date portion of a date or datetime.  Return 0 on success,
 * else non-0 for error.
 */
int json_date(char *result, const char *str)
{
	return dtzhelper(result, str, NULL, "D");
}

/* Return the time portion of a time or datetime.  Return 0 on success,
 * else non-0 for error.  If "tz" is given then convert the time to the
 * given timezone, and include the timezone string in the response.
 * "tz" can be "" for local time, "Z" for UTC, or "+00:00" or "+0000"
 * for other timezones.
 */
int json_time(char *result, const char *str, const char *tz)
{
	return dtzhelper(result, str, tz, "TZ");
}

/* Return a full datetime.  If passed just a date, then the time is assumed
 * to be midnight at the start of the date.  Return 0 on success,  else non-0
 * for error.  If "tz" is given then convert to the given timezone, and include
 * the timezone string in the response.  "tz" can be "" for local time,
 * "Z" for UTC, or "+00:00" or "+0000" for other timezones.
 */
int json_datetime(char *result, const char *str, const char *tz)
{
	return dtzhelper(result, str, tz, "DTZ");
}

/* Return the timezone from an ISO time or dateTime string.  For the local
 * time zone, this will be "" unless you pass a format string.  UTC time zone
 * will be the same as the string says ("Z" or "+00:00") unless you pass a
 * format string.  The format string should be "-00:00", "Z", or "#".  The
 * last returns the number of minutes east of Greenwich, as a string.
 * Returns 0 on success or non-zero on failure.
 */
int json_timezone(char *result, const char *str, const char *format)
{
	return 1; //!!!
}


/* Add an ISO period to an ISO datetime, and return the result.  Returns 0
 * on success, or non-zero on error.
 */
int json_datetime_add(char *result, const char *str, const char *period)
{
	jsondatetime_t	dt, per;

	/* Parse the strings */
	if (parsedatetime(str, &dt) || parseperiod(period, &per))
		return 1;

	/* Add the period from the datetime */
	addperiod(&dt, &per);
	normalize(&dt);

	/* Convert back to an ISO datetime string */
	datetimestr(result, &dt, "DTZ");
	return 0;
}

/* Subtract an ISO period from an ISO datetime, and return the result.
 * Returns 0 on success, or non-zero on error.
 */
int json_datetime_subtract(char *result, const char *str, const char *period)
{
	jsondatetime_t	dt, per;

	/* Parse the strings */
	if (parsedatetime(str, &dt) || parseperiod(period, &per))
		return 1;

	/* Subtract the period from the datetime */
	subtractperiod(&dt, &per);
	normalize(&dt);

	/* Convert back to an ISO datetime string */
	datetimestr(result, &dt, "DTZ");
	return 0;
}

/* Return the difference between two ISO datetimes, as an ISO period.
 * Returns 0 on success or non-zero on error.
 */
int json_datetime_diff(char *result, const char *str1, const char *str2)
{
	jsondatetime_t dt1, dt2;
	time_t when1, when2, diff;
	int	neg;

	/* Convert both datetimes to time_t values */
	if (parsedatetime(str1, &dt1) || parsedatetime(str2, &dt2))
		return 1;
	when1 = dtbinary(&dt1);
	when2 = dtbinary(&dt2);

	/* Find their difference */
	diff = when1 - when2;

	/* If no difference, return "P0D" */
	if (diff == 0) {
		strcpy(result, "P0D");
		return 0;
	}

	/* If negative, then we want all numbers to be negative.  Its easier
	 * to do the math on positive numbers, though so negate it and
	 * remember.
	 */
	neg = 0;
	if (diff < 0) {
		neg = 1;
		diff = -diff;
	}

	/* We don't do months and years since they vary.  We start with days */
	*result++ = 'P';
	if (diff >= 86400) {
		sprintf(result, "%s%ldD", neg ? "-" : "", (long)diff / 86400);
		diff %= 86400;
		result += strlen(result);
	}
	if (diff > 0)
		*result++ = 'T';
	if (diff >= 3600) {
		sprintf(result, "%s%ldH", neg ? "-" : "", (long)diff / 3600);
		diff %= 3600;
		result += strlen(result);
	}
	if (diff >= 60) {
		sprintf(result, "%s%ldM", neg ? "-" : "", (long)diff / 60);
		diff %= 60;
		result += strlen(result);
	}
	if (diff > 0) {
		sprintf(result, "%s%ldS", neg ? "-" : "", (long)diff);
	}

	return 0;
}
