#include <stdlib.h>
#include <string.h>
#include "time.h"

const int64_t secondsPerMinute = 60;
const int64_t secondsPerHour   = 60 * secondsPerMinute;
const int64_t secondsPerDay    = 24 * secondsPerHour;
const int64_t secondsPerWeek   = 7 * secondsPerDay;
const int64_t daysPer400Years  = 365*400 + 97;
const int64_t daysPer100Years  = 365*100 + 24;
const int64_t daysPer4Years    = 365*4 + 1;

// Computations on time.
//
// The zero value for a Time is defined to be
//	January 1, year 1, 00:00:00.000000000 UTC
// which (1) looks like a zero, or as close as you can get in a date
// (1-1-1 00:00:00 UTC), (2) is unlikely enough to arise in practice to
// be a suitable "not set" sentinel, unlike Jan 1 1970, and (3) has a
// non-negative year even in time zones west of UTC, unlike 1-1-0
// 00:00:00 UTC, which would be 12-31-(-1) 19:00:00 in New York.
//
// The zero Time value does not force a specific epoch for the time
// representation. For example, to use the Unix epoch internally, we
// could define that to distinguish a zero value from Jan 1 1970, that
// time would be represented by sec=-1, nsec=1e9. However, it does
// suggest a representation, namely using 1-1-1 00:00:00 UTC as the
// epoch, and that's what we do.
//
// The Add and Sub computations are oblivious to the choice of epoch.
//
// The presentation computations - year, month, minute, and so on - all
// rely heavily on division and modulus by positive constants. For
// calendrical calculations we want these divisions to round down, even
// for negative values, so that the remainder is always positive, but
// Go's division (like most hardware division instructions) rounds to
// zero. We can still do those computations and then adjust the result
// for a negative numerator, but it's annoying to write the adjustment
// over and over. Instead, we can change to a different epoch so long
// ago that all the times we care about will be positive, and then round
// to zero and round down coincide. These presentation routines already
// have to add the zone offset, so adding the translation to the
// alternate epoch is cheap. For example, having a non-negative time t
// means that we can write
//
//	sec = t % 60
//
// instead of
//
//	sec = t % 60
//	if sec < 0 {
//		sec += 60
//	}
//
// everywhere.
//
// The calendar runs on an exact 400 year cycle: a 400-year calendar
// printed for 1970-2369 will apply as well to 2370-2769. Even the days
// of the week match up. It simplifies the computations to choose the
// cycle boundaries so that the exceptional years are always delayed as
// long as possible. That means choosing a year equal to 1 mod 400, so
// that the first leap year is the 4th year, the first missed leap year
// is the 100th year, and the missed missed leap year is the 400th year.
// So we'd prefer instead to print a calendar for 2001-2400 and reuse it
// for 2401-2800.
//
// Finally, it's convenient if the delta between the Unix epoch and
// long-ago epoch is representable by an int64 constant.
//
// These three considerations—choose an epoch as early as possible, that
// uses a year equal to 1 mod 400, and that is no more than 2⁶³ seconds
// earlier than 1970—bring us to the year -292277022399. We refer to
// this year as the absolute zero year, and to times measured as a uint64
// seconds since this year as absolute times.
//
// Times measured as an int64 seconds since the year 1—the representation
// used for Time's sec field—are called internal times.
//
// Times measured as an int64 seconds since the year 1970 are called Unix
// times.
//
// It is tempting to just use the year 1 as the absolute epoch, defining
// that the routines are only valid for years >= 1. However, the
// routines would then be invalid when displaying the epoch in time zones
// west of UTC, since it is year 0. It doesn't seem tenable to say that
// printing the zero time correctly isn't supported in half the time
// zones. By comparison, it's reasonable to mishandle some times in
// the year -292277022399.
//
// All this is opaque to clients of the API and can be changed if a
// better implementation presents itself.

	// The unsigned zero year for internal calculations.
	// Must be 1 mod 400, and times before it will not compute correctly,
	// but otherwise can be changed at will.
const int64_t	absoluteZeroYear = -292277022399;

// The year of the zero Time.
// Assumed by the unixToInternal computation below.
const int64_t	internalYear = 1;

// Offsets to convert between internal and absolute or Unix times.
const int64_t	absoluteToInternal = (absoluteZeroYear - internalYear) * 365.2425 * secondsPerDay;
const int64_t	internalToAbsolute       = -absoluteToInternal;

const int64_t	unixToInternal = (1969*365 + 1969/4 - 1969/100 + 1969/400) * secondsPerDay;
const int64_t	internalToUnix = -unixToInternal;

const int64_t	wallToInternal = (1884*365 + 1884/4 - 1884/100 + 1884/400) * secondsPerDay;

const int64_t hasMonotonic = (int64_t)1 << 63;
const int64_t maxWall      = wallToInternal + (((int64_t)1<<33) - 1); // year 2157
const int64_t minWall      = wallToInternal;               // year 1885
const int64_t nsecMask     = (1<<30) - 1;
const int64_t nsecShift    = 30;

static char *longDayNames[] = {
	"Sunday",
	"Monday",
	"Tuesday",
	"Wednesday",
	"Thursday",
	"Friday",
	"Saturday",
};

static char *shortDayNames[] = {
	"Sun",
	"Mon",
	"Tue",
	"Wed",
	"Thu",
	"Fri",
	"Sat",
};

static char *shortMonthNames[] = {
	"Jan",
	"Feb",
	"Mar",
	"Apr",
	"May",
	"Jun",
	"Jul",
	"Aug",
	"Sep",
	"Oct",
	"Nov",
	"Dec",
};

static char *longMonthNames[] = {
	"January",
	"February",
	"March",
	"April",
	"May",
	"June",
	"July",
	"August",
	"September",
	"October",
	"November",
	"December",
};

// Load local timezone data??
void time_init(void)
{
}

/* // utcLoc is separate so that get can refer to &utcLoc */
/* // and ensure that it never returns a nil *Location, */
/* // even if a badly behaved client has changed UTC. */
/* static TimeLocation utcLoc = (TimeLocation){.name = "UTC"}; */

// These helpers for manipulating the wall and monotonic clock readings
// take pointer receivers, even when they don't modify the time,
// to make them cheaper to call.

// nsec returns the time's nanoseconds.
int32_t time_nsec(Time *t)
{
	return t->wall & nsecMask;
}

// sec returns the time's seconds since Jan 1 year 1.
int64_t time_sec(Time *t)
{
	if ((t->wall&hasMonotonic) != 0) {
		return wallToInternal + (t->wall<<1>>(nsecShift+1));
	}
	return t->ext;
}

// unixSec returns the time's seconds since Jan 1 1970 (Unix time).
int64_t time_unixSec(Time *t)
{
    return time_sec(t) + internalToUnix;
}


// stripMono strips the monotonic clock reading in t.
void time_stripMono(Time *t)
{
	if ((t->wall&hasMonotonic) != 0) {
		t->ext = time_sec(t);
		t->wall &= nsecMask;
	}
}

// addSec adds d seconds to the time.
void time_addSec(Time *t, int64_t d)
{
	if ((t->wall&hasMonotonic) != 0) {
		int64_t sec = t->wall << 1 >> (nsecShift + 1);
		int64_t dsec = sec + d;
		if (0 <= dsec && dsec <= ((int64_t)1<<33)-1) {
			t->wall = t->wall&nsecMask | dsec<<nsecShift | hasMonotonic;
			return;
		}
		// Wall second now out of range for packed field.
		// Move to ext.
		time_stripMono(t);
	}

	// Check if the sum of t.ext and d overflows and handle it properly.
	int64_t sum = t->ext + d;
	if ((sum > t->ext) == (d > 0)) {
		t->ext = sum;
	} else if (d > 0) {
		t->ext = ((int64_t)1<<63) - 1; // TODO FIX: suppress overflow warning
	} else {
		t->ext = -(((int64_t)1<<63) - 1); // TODO FIX: suppress overflow warning
	}
}

// setLoc sets the location associated with the time.
void time_setLoc(Time *t, TimeLocation *loc)
{
	if (strcmp(loc->name, time__utcLoc.name) == 0) {
		loc = NULL;
	}
	time_stripMono(t);
	t->loc = loc;
}

// setMono sets the monotonic clock reading in t.
// If t cannot hold a monotonic clock reading,
// because its wall time is too large,
// setMono is a no-op.
void time_setMono(Time *t, int64_t m)
{
	if ((t->wall&hasMonotonic) == 0) {
		int64_t sec = t->ext;
		if (sec < minWall || maxWall < sec) {
			return;
		}
		t->wall |= hasMonotonic | (sec-minWall)<<nsecShift;
	}
	t->ext = m;
}

// mono returns t's monotonic clock reading.
// It returns 0 for a missing reading.
// This function is used only for testing,
// so it's OK that technically 0 is a valid
// monotonic clock reading as well.
int64_t time_mono(Time *t)
{
	if ((t->wall&hasMonotonic) == 0) {
		return 0;
	}
	return t->ext;
}

// After reports whether the time instant t is after u.
bool time_After(Time t, Time u)
{
	if ((t.wall&u.wall&hasMonotonic) != 0) {
		return t.ext > u.ext;
	}
	int64_t ts = time_sec(&t);
	int64_t us = time_sec(&u);
	return ts > us || ts == us && time_nsec(&t) > time_nsec(&u);
}

// Before reports whether the time instant t is before u.
bool time_Before(Time t, Time u)
{
	if ((t.wall&u.wall&hasMonotonic) != 0) {
		return t.ext < u.ext;
	}
	int64_t ts = time_sec(&t);
	int64_t us = time_sec(&u);
	return ts < us || ts == us && time_nsec(&t) < time_nsec(&u);
}

// Compare compares the time instant t with u. If t is before u, it returns -1;
// if t is after u, it returns +1; if they're the same, it returns 0.
int time_Compare(Time t, Time u)
{
	int64_t tc, uc;
	if ((t.wall&u.wall&hasMonotonic) != 0) {
		tc = t.ext;
        uc = u.ext;
	} else {
        tc = time_sec(&t);
        uc = time_sec(&u);
		if (tc == uc) {
			tc = time_nsec(&t);
            uc = time_nsec(&u);
		}
	}
	if (tc < uc)
		return -1;
	if (tc > uc)
		return +1;
	return 0;
}

// Equal reports whether t and u represent the same time instant.
// Two times can be equal even if they are in different locations.
// For example, 6:00 +0200 and 4:00 UTC are Equal.
// See the documentation on the Time type for the pitfalls of using == with
// Time values; most code should use Equal instead.
bool time_Equal(Time t, Time u)
{
	if ((t.wall&u.wall&hasMonotonic) != 0) {
		return t.ext == u.ext;
	}
	return time_sec(&t) == time_sec(&u) && time_nsec(&t) == time_nsec(&u);
}

// fmtInt formats v into the tail of buf.
// It returns the index where the output begins.
int time_fmtInt(char buf[], size_t bufLen, uint64_t v)
{
	size_t w = bufLen;
	if (v == 0) {
		w--;
		buf[w] = '0';
	} else {
		while (v > 0) {
			w--;
			buf[w] = (v%10) + '0';
			v /= 10;
		}
	}
	return w;
}

// String returns the English name of the month ("January", "February", ...).
// Caller should free the returned string.
char *time_MonthString(TimeMonth m) {
    char *str;
	if (JANUARY <= m && m <= DECEMBER) {
        str = malloc(strlen(longMonthNames[m-1]) + 1);
        strcpy(str, longMonthNames[m-1]);
		return str;
	}
    const size_t bufLen = 20;
    char buf[bufLen];
	int n = time_fmtInt(buf, bufLen, m);

    str = malloc(8 + bufLen + 1 + 1);
    str[0] = '\0';
    strncat(str, "%!Month(", 8);
    strncat(str, &buf[n], bufLen - n);
    strncat(str, ")", 1);
    return str;
}

// String returns the English name of the day ("Sunday", "Monday", ...).
// Caller should free the returned string.
char *time_WeekdayString(TimeWeekday d)
{
    char *str;
	if (SUNDAY <= d && d <= SATURDAY) {
        str = malloc(strlen(longDayNames[d]) + 1);
        strcpy(str, longDayNames[d]);
		return str;
	}
    const size_t bufLen = 20;
    char buf[bufLen];
	int n = time_fmtInt(buf, bufLen, d);

    str = malloc(10 + bufLen + 1 + 1);
    str[0] = '\0';
    strncat(str, "%!Weekday(", 10);
    strncat(str, &buf[n], bufLen - n);
    strncat(str, ")", 1);
    return str;
}

// IsZero reports whether t represents the zero time instant,
// January 1, year 1, 00:00:00 UTC.
bool time_IsZero(Time t)
{
	return time_sec(&t) == 0 && time_nsec(&t) == 0;
}

// abs returns the time t as an absolute time, adjusted by the zone offset.
// It is called when computing a presentation property like Month or Hour.
// TODO: Unfinished, needs work.
uint64_t time_abs(Time t)
{
	TimeLocation *l = t.loc;
	// Avoid function calls when possible.
	if (l == NULL || l == &time__localLoc) {
		/* l = l.get(); */
	}
	int64_t sec = time_unixSec(&t);
	if (l != &time__utcLoc) {
		if (l->cacheZone != NULL && l->cacheStart <= sec && sec < l->cacheEnd) {
			sec += l->cacheZone->offset;
		} else {
			/* _, offset, _, _, _ := l.lookup(sec); */
			/* sec += offset; */
		}
	}
	return sec + (unixToInternal + internalToAbsolute);
}

