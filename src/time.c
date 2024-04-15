#include <stdlib.h>
#include <string.h>
#include "time.h"

// utcLoc is separate so that get can refer to &utcLoc
// and ensure that it never returns a nil *Location,
// even if a badly behaved client has changed UTC.
static TimeLocation time__utcLoc = (TimeLocation){.name = "UTC"};

// UTC represents Universal Coordinated Time (UTC).
TimeLocation *time_UTC = &time__utcLoc;
                                               //
// localLoc is separate so that initLocal can initialize
// it even if a client has changed Local.
static TimeLocation time__localLoc;

// Local represents the system's local time zone.
// On Unix systems, Local consults the TZ environment
// variable to find the time zone to use. No TZ means
// use the system default /etc/localtime.
// TZ="" means use UTC.
// TZ="foo" means use file foo in the system timezone directory.
TimeLocation *time_Local = &time__localLoc;


static const int64_t secondsPerMinute = 60;
static const int64_t secondsPerHour   = 60 * secondsPerMinute;
static const int64_t secondsPerDay    = 24 * secondsPerHour;
static const int64_t secondsPerWeek   = 7 * secondsPerDay;
static const int64_t daysPer400Years  = 365*400 + 97;
static const int64_t daysPer100Years  = 365*100 + 24;
static const int64_t daysPer4Years    = 365*4 + 1;

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
static const int64_t	absoluteZeroYear = -292277022399;

// The year of the zero Time.
// Assumed by the unixToInternal computation below.
static const int64_t	internalYear = 1;

// Offsets to convert between internal and absolute or Unix times.
static const int64_t	absoluteToInternal = (absoluteZeroYear - internalYear) * 365.2425 * secondsPerDay;
static const int64_t	internalToAbsolute       = -absoluteToInternal;

static const int64_t	unixToInternal = (1969*365 + 1969/4 - 1969/100 + 1969/400) * secondsPerDay;
static const int64_t	internalToUnix = -unixToInternal;

static const int64_t	wallToInternal = (1884*365 + 1884/4 - 1884/100 + 1884/400) * secondsPerDay;

static const int64_t hasMonotonic = (int64_t)1 << 63;
static const int64_t maxWall      = wallToInternal + (((int64_t)1<<33) - 1); // year 2157
static const int64_t minWall      = wallToInternal;               // year 1885
static const int64_t nsecMask     = (1<<30) - 1;
static const int64_t nsecShift    = 30;

static const char *longDayNames[] = {
	"Sunday",
	"Monday",
	"Tuesday",
	"Wednesday",
	"Thursday",
	"Friday",
	"Saturday",
};

static const char *shortDayNames[] = {
	"Sun",
	"Mon",
	"Tue",
	"Wed",
	"Thu",
	"Fri",
	"Sat",
};

static const char *shortMonthNames[] = {
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

static const char *longMonthNames[] = {
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
		t->ext = ((uint64_t)1<<63) - 1;
	} else {
		t->ext = -(((uint64_t)1<<63) - 1);
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

struct time_locabs{
    char *name;
    int offset;
    uint64_t abs;
};
// locabs is a combination of the Zone and abs methods,
// extracting both return values from a single zone lookup.
 struct time_locabs time_locabs(Time t) {
    struct time_locabs ret = {0};
	TimeLocation *l = t.loc;
	if (l == NULL || l == &time__localLoc) {
		/* l = l.get(); */
	}
	// Avoid function call if we hit the local time cache.
	int64_t sec = time_unixSec(&t);
	if (l != &time__utcLoc) {
		if (l->cacheZone != NULL && l->cacheStart <= sec && sec < l->cacheEnd) {
			ret.name = l->cacheZone->name;
			ret.offset = l->cacheZone->offset;
		} else {
			/* name, offset, _, _, _ = l.lookup(sec) */
		}
		sec += ret.offset;
	} else {
		ret.name = "UTC";
	}
	ret.abs = (sec + (unixToInternal + internalToAbsolute));
	return ret;
}

bool time_isLeap(int year)
{
	return year%4 == 0 && (year%100 != 0 || year%400 == 0);
}

int32_t daysBefore[] = {
	0,
	31,
	31 + 28,
	31 + 28 + 31,
	31 + 28 + 31 + 30,
	31 + 28 + 31 + 30 + 31,
	31 + 28 + 31 + 30 + 31 + 30,
	31 + 28 + 31 + 30 + 31 + 30 + 31,
	31 + 28 + 31 + 30 + 31 + 30 + 31 + 31,
	31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30,
	31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31,
	31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30,
	31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30 + 31,
};

struct time_date{
    int year;
    TimeMonth month;
    int day;
    int yday;
};
 /* (year int, month Month, day int, yday int) */ 
// absDate is like date but operates on an absolute time.
struct time_date time_absDate(uint64_t abs , bool full)
{
    struct time_date ret = {0};
	// Split into time and day.
	uint64_t d = abs / secondsPerDay;

	// Account for 400 year cycles.
	uint64_t n = d / daysPer400Years;
	uint64_t y = 400 * n;
	d -= daysPer400Years * n;

	// Cut off 100-year cycles.
	// The last cycle has one extra leap year, so on the last day
	// of that year, day / daysPer100Years will be 4 instead of 3.
	// Cut it back down to 3 by subtracting n>>2.
	n = d / daysPer100Years;
	n -= n >> 2;
	y += 100 * n;
	d -= daysPer100Years * n;

	// Cut off 4-year cycles.
	// The last cycle has a missing leap year, which does not
	// affect the computation.
	n = d / daysPer4Years;
	y += 4 * n;
	d -= daysPer4Years * n;

	// Cut off years within a 4-year cycle.
	// The last year is a leap year, so on the last day of that year,
	// day / 365 will be 4 instead of 3. Cut it back down to 3
	// by subtracting n>>2.
	n = d / 365;
	n -= n >> 2;
	y += n;
	d -= 365 * n;

	ret.year = y + absoluteZeroYear;
	ret.yday = d;

	if (!full) {
		return ret;
	}

	ret.day = ret.yday;
	if (time_isLeap(ret.year)) {
		// Leap year
		if (ret.day > 31+29-1) {
			// After leap day; pretend it wasn't there.
			ret.day--;
        } else if (ret.day == 31+29-1) {
			// Leap day.
			ret.month = FEBRUARY;
			ret.day = 29;
			return ret;
		}
	}

	// Estimate month on assumption that every month has 31 days.
	// The estimate may be too low by at most one month, so adjust.
	ret.month = ret.day / 31;
	int end = daysBefore[ret.month+1];
	int begin;
	if (ret.day >= end) {
		ret.month++;
		begin = end;
	} else {
		begin = daysBefore[ret.month];
	}

	ret.month++; // because January is 1
	ret.day = ret.day - begin + 1;
	return ret;
}

// date computes the year, day of year, and when full=true,
// the month and day in which t occurs.
struct time_date time_date(Time t, bool full)
{
	return time_absDate(time_abs(t), full);
}

// Date returns the year, month, and day in which t occurs.
TimeDate time_Date(Time t)
{
	struct time_date d = time_date(t, true);
    return (TimeDate) {
        .year = d.year,
        .month = d.month,
        .day = d.day,
    };
}

// Year returns the year in which t occurs.
int time_Year(Time t)
{
	struct time_date d = time_date(t, false);
    return d.year;
}

// Month returns the month of the year specified by t.
TimeMonth time_Month(Time t)
{
	struct time_date d = time_date(t, true);
	return d.month;
}

// Day returns the day of the month specified by t.
int time_Day(Time t)
{
	struct time_date d = time_date(t, true);
    return d.day;
}

// absWeekday is like Weekday but operates on an absolute time.
TimeWeekday time_absWeekday(uint64_t abs)
{
	// January 1 of the absolute year, like January 1 of 2001, was a Monday.
	uint64_t sec = (abs + MONDAY*secondsPerDay) % secondsPerWeek;
	return sec / secondsPerDay;
}

// Weekday returns the day of the week specified by t.
TimeWeekday time_Weekday(Time t)
{
	return time_absWeekday(time_abs(t));
}

// ISOWeek returns the ISO 8601 year and week number in which t occurs.
// Week ranges from 1 to 53. Jan 01 to Jan 03 of year n might belong to
// week 52 or 53 of year n-1, and Dec 29 to Dec 31 might belong to week 1
// of year n+1.
TimeWeek time_ISOWeek(Time t)
{
	// According to the rule that the first calendar week of a calendar year is
	// the week including the first Thursday of that year, and that the last one is
	// the week immediately preceding the first calendar week of the next calendar year.
	// See https://www.iso.org/obp/ui#iso:std:iso:8601:-1:ed-1:v1:en:term:3.1.1.23 for details.

	// weeks start with Monday
	// Monday Tuesday Wednesday Thursday Friday Saturday Sunday
	// 1      2       3         4        5      6        7
	// +3     +2      +1        0        -1     -2       -3
	// the offset to Thursday
	uint64_t abs = time_abs(t);
	TimeWeekday d = THURSDAY - time_absWeekday(abs);
	// handle Sunday
	if (d == 4) {
		d = -3;
	}
	// find the Thursday of the calendar week
	abs += d * secondsPerDay;
	struct time_date td = time_absDate(abs, false);
    return (TimeWeek){
        .year = td.year,
        .week = td.yday/7 + 1,
    };
}

// absClock is like clock but operates on an absolute time.
TimeClock time_absClock(uint64_t abs)
{
    TimeClock ret = {0};
	ret.sec = abs % secondsPerDay;
	ret.hour = ret.sec / secondsPerHour;
	ret.sec -= ret.hour * secondsPerHour;
	ret.min = ret.sec / secondsPerMinute;
	ret.sec -= ret.min * secondsPerMinute;
	return ret;
}

// Clock returns the hour, minute, and second within the day specified by t.
TimeClock  time_Clock(Time t)
{
	return time_absClock(time_abs(t));
}

// Hour returns the hour within the day specified by t, in the range [0, 23].
int time_Hour(Time t)
{
	return (time_abs(t)%secondsPerDay) / secondsPerHour;
}

// Minute returns the minute offset within the hour specified by t, in the range [0, 59].
int time_Minute(Time t)
{
	return (time_abs(t)%secondsPerHour) / secondsPerMinute;
}

// Second returns the second offset within the minute specified by t, in the range [0, 59].
int time_Second(Time t)
{
	return (time_abs(t) % secondsPerMinute);
}

// Nanosecond returns the nanosecond offset within the second specified by t,
// in the range [0, 999999999].
int time_Nanosecond(Time t)
{
	return time_nsec(&t);
}

// YearDay returns the day of the year specified by t, in the range [1,365] for non-leap years,
// and [1,366] in leap years.
int time_YearDay(Time t)
{
    struct time_date td = time_date(t, false);
	return td.yday + 1;
}

static const TimeDuration minDuration = -((uint64_t)1 << 63);
static const TimeDuration maxDuration = ((uint64_t)1<<63) - 1;

struct time_fmtFrac {
    int nw;
    uint64_t nv;
};
// fmtFrac formats the fraction of v/10**prec (e.g., ".12345") into the
// tail of buf, omitting trailing zeros. It omits the decimal
// point too when the fraction is 0. It returns the index where the
// output bytes begin and the value v/10**prec.
struct time_fmtFrac time_fmtFrac(char buf[], size_t bufLen, uint64_t v, int prec)
{
	// Omit trailing zeros up to and including decimal point.
	size_t w = bufLen;
	bool print = false;
	for (int i = 0; i < prec; i++) {
		int digit = v % 10;
		print = print || digit != 0;
		if (print) {
			w--;
			buf[w] = digit + '0';
		}
		v /= 10;
	}
	if (print) {
		w--;
		buf[w] = '.';
	}
	return (struct time_fmtFrac){ .nw = w, .nv = v };
}

// format formats the representation of d into the end of buf and
// returns the offset of the first character.
int time_durationFormat(TimeDuration d, char buf[32])
{
	// Largest time is 2540400h10m10.000000000s
    size_t w = 32;

	uint64_t u = d;
	bool neg = d < 0;
	if (neg) {
		u = -u;
	}

	if (u < SECOND) {
		// Special case: if duration is smaller than a second,
		// use smaller units, like 1.2ms
		int prec = 0;
		w--;
		buf[w] = 's';
		w--;
		if (u == 0) {
			buf[w] = '0';
			return w;
        } else if (u < MICROSECOND) {
			// print nanoseconds
			prec = 0;
			buf[w] = 'n';
        } else if (u < MILLISECOND) {
			// print microseconds
			prec = 3;
			// U+00B5 'µ' micro sign == 0xC2 0xB5
			w--; // Need room for two bytes.
            memcpy(&buf[w], "µ", 2);
        } else {
			// print milliseconds
			prec = 6;
			buf[w] = 'm';
		}
        struct time_fmtFrac ff = time_fmtFrac(buf, w, u, prec);
        w = ff.nw;
        u = ff.nv;
		w = time_fmtInt(buf, w, u);
	} else {
		w--;
		buf[w] = 's';

        struct time_fmtFrac ff = time_fmtFrac(buf, w, u, 9);
        w = ff.nw;
        u = ff.nv;

		// u is now integer seconds
		w = time_fmtInt(buf, w, u%60);
		u /= 60;

		// u is now integer minutes
		if (u > 0) {
			w--;
			buf[w] = 'm';
            w = time_fmtInt(buf, w, u%60);
			u /= 60;

			// u is now integer hours
			// Stop at hours because days can be different lengths.
			if (u > 0) {
				w--;
				buf[w] = 'h';
                w = time_fmtInt(buf, w, u);
			}
		}
	}

	if (neg) {
		w--;
		buf[w] = '-';
	}

	return w;
}

// String returns a string representing the duration in the form "72h3m0.5s".
// Leading zero units are omitted. As a special case, durations less than one
// second format use a smaller unit (milli-, micro-, or nanoseconds) to ensure
// that the leading digit is non-zero. The zero duration formats as 0s.
char *time_DurationString(TimeDuration d)
{
	// This is inlinable to take advantage of "function outlining".
	// Thus, the caller can decide whether a string must be heap allocated.
	char arr[32];
	int n = time_durationFormat(d, arr);
    char *str = malloc(32 - n + 1);
    strncpy(str, &arr[n], 32-n);
    return str;
}

