#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

#include "time.h"
#include "std.h"

// utcLoc is separate so that get can refer to &utcLoc
// and ensure that it never returns a nil *Location,
// even if a badly behaved client has changed UTC.
static nt_Location nt_utcLoc = (nt_Location){.name = "UTC"};

// UTC represents Universal Coordinated Time (UTC).
nt_Location *nt_UTC = &nt_utcLoc;
                                               
// localLoc is separate so that initLocal can initialize
// it even if a client has changed Local.
static nt_Location nt_localLoc;

// Local represents the system's local time zone.
// On Unix systems, Local consults the TZ environment
// variable to find the time zone to use. No TZ means
// use the system default /etc/localtime.
// TZ="" means use UTC.
// TZ="foo" means use file foo in the system timezone directory.
nt_Location *nt_Local = &nt_localLoc;


const int64_t nt_secondsPerMinute = 60;
const int64_t nt_secondsPerHour   = 60 * nt_secondsPerMinute;
const int64_t nt_secondsPerDay    = 24 * nt_secondsPerHour;
const int64_t nt_secondsPerWeek   = 7 * nt_secondsPerDay;
const int64_t nt_daysPer400Years  = 365*400 + 97;
const int64_t nt_daysPer100Years  = 365*100 + 24;
const int64_t nt_daysPer4Years    = 365*4 + 1;

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
static const int64_t	nt_absoluteZeroYear = -292277022399;

// The year of the zero Time.
// Assumed by the unixToInternal computation below.
static const int64_t	nt_internalYear = 1;

// Offsets to convert between internal and absolute or Unix times.
static const int64_t	nt_absoluteToInternal = (nt_absoluteZeroYear - nt_internalYear) * 365.2425 * nt_secondsPerDay;
static const int64_t	nt_internalToAbsolute       = -nt_absoluteToInternal;

static const int64_t	nt_unixToInternal = (1969*365 + 1969/4 - 1969/100 + 1969/400) * nt_secondsPerDay;
static const int64_t	nt_internalToUnix = -nt_unixToInternal;

static const int64_t	nt_wallToInternal = (1884*365 + 1884/4 - 1884/100 + 1884/400) * nt_secondsPerDay;

static const int64_t nt_hasMonotonic = (int64_t)1 << 63;
static const int64_t nt_maxWall      = nt_wallToInternal + (((int64_t)1<<33) - 1); // year 2157
static const int64_t nt_minWall      = nt_wallToInternal;               // year 1885
static const int64_t nt_nsecMask     = (1<<30) - 1;
static const int64_t nt_nsecShift    = 30;

static const char *nt_longDayNames[] = {
	"Sunday",
	"Monday",
	"Tuesday",
	"Wednesday",
	"Thursday",
	"Friday",
	"Saturday",
};

static const char *nt_shortDayNames[] = {
	"Sun",
	"Mon",
	"Tue",
	"Wed",
	"Thu",
	"Fri",
	"Sat",
};

static const char *nt_shortMonthNames[] = {
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

static const char *nt_longMonthNames[] = {
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

const nt_Duration minDuration = -((uint64_t)1 << 63);
const nt_Duration maxDuration = ((uint64_t)1<<63) - 1;

int32_t nt_daysBefore[] = {
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

struct nt_now {
    int64_t sec;
    int32_t nsec;
    int64_t mono;
};


struct nt_now nt_now()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    return (struct nt_now){
        .sec = ts.tv_sec,
        .nsec = ts.tv_nsec,
        .mono = 0,
    };
}

// Monotonic times are reported as offsets from startNano.
// We initialize startNano to runtimeNano() - 1 so that on systems where
// monotonic time resolution is fairly low (e.g. Windows 2008
// which appears to have a default resolution of 15ms),
// we avoid ever reporting a monotonic time of 0.
// (Callers may want to use 0 as "time not set".)
// Note: need to call nt_init to set startNano to runtimeNano()
int64_t nt_startNano = 0;


#define nt_EMPTY_STR(s) ((s) == NULL || (s)[0] == '\0')

// Private function definitions
// time.go
int32_t nt_Time_nsec(nt_Time *t);
int64_t nt_Time_sec(nt_Time *t);
int64_t nt_Time_unixSec(nt_Time *t);
void nt_Time_addSec(nt_Time *t, int64_t d);
void nt_Time_setLoc(nt_Time *t, nt_Location *loc);
void nt_Time_stripMono(nt_Time *t);

void nt_Time_stripMono(nt_Time *t);
void nt_Time_setMono(nt_Time *t, int64_t m);
int64_t nt_Time_mono(nt_Time *t);
struct nt_Timelocabs nt_Time_locabs(nt_Time t);
nt_Location *nt_Location_get(nt_Location *l);
uint64_t nt_Time_abs(nt_Time t);
struct nt_Clock nt_Time_absClock(uint64_t abs);
int nt_Duration_format(nt_Duration d, char buf[32]);
struct nt_fmtFrac {
    int nw;
    uint64_t nv;
};
struct nt_fmtFrac nt_fmtFrac(char buf[], size_t bufLen, uint64_t v, int prec);
int nt_fmtInt(char buf[], size_t bufLen, uint64_t v);
nt_Duration nt_subMono(int64_t t, int64_t u);
struct nt_date date(nt_Time t, bool full);
struct nt_date nt_absDate(uint64_t abs , bool full);
//
int64_t nt_runtimeNano();
bool nt_isLeap(int year);
//

/*** time.go Implementation ***/

// These helpers for manipulating the wall and monotonic clock readings
// take pointer receivers, even when they don't modify the time,
// to make them cheaper to call.

// nsec returns the time's nanoseconds.
int32_t nt_Time_nsec(nt_Time *t)
{
	return t->wall & nt_nsecMask;
}

// sec returns the time's seconds since Jan 1 year 1.
int64_t nt_Time_sec(nt_Time *t)
{
	if ((t->wall&nt_hasMonotonic) != 0) {
		return nt_wallToInternal + (t->wall<<1>>(nt_nsecShift+1));
	}
	return t->ext;
}

// unixSec returns the time's seconds since Jan 1 1970 (Unix time).
int64_t nt_Time_unixSec(nt_Time *t)
{
    return nt_Time_sec(t) + nt_internalToUnix;
}

// addSec adds d seconds to the time.
void nt_Time_addSec(nt_Time *t, int64_t d)
{
	if ((t->wall&nt_hasMonotonic) != 0) {
		int64_t sec = t->wall << 1 >> (nt_nsecShift + 1);
		int64_t dsec = sec + d;
		if (0 <= dsec && dsec <= ((int64_t)1<<33)-1) {
			t->wall = (t->wall&nt_nsecMask) | dsec<<nt_nsecShift | nt_hasMonotonic;
			return;
		}
		// Wall second now out of range for packed field.
		// Move to ext.
		nt_Time_stripMono(t);
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
void nt_Time_setLoc(nt_Time *t, nt_Location *loc)
{
	if (strcmp(loc->name, nt_utcLoc.name) == 0) {
		loc = NULL;
	}
	nt_Time_stripMono(t);
	t->loc = loc;
}

// stripMono strips the monotonic clock reading in t.
void nt_Time_stripMono(nt_Time *t)
{
	if ((t->wall&nt_hasMonotonic) != 0) {
		t->ext = nt_Time_sec(t);
		t->wall &= nt_nsecMask;
	}
}

// setMono sets the monotonic clock reading in t.
// If t cannot hold a monotonic clock reading,
// because its wall time is too large,
// setMono is a no-op.
void nt_Time_setMono(nt_Time *t, int64_t m)
{
	if ((t->wall&nt_hasMonotonic) == 0) {
		int64_t sec = t->ext;
		if (sec < nt_minWall || nt_maxWall < sec) {
			return;
		}
		t->wall |= nt_hasMonotonic | (sec-nt_minWall)<<nt_nsecShift;
	}
	t->ext = m;
}

// mono returns t's monotonic clock reading.
// It returns 0 for a missing reading.
// This function is used only for testing,
// so it's OK that technically 0 is a valid
// monotonic clock reading as well.
int64_t nt_Time_mono(nt_Time *t)
{
	if ((t->wall&nt_hasMonotonic) == 0) {
		return 0;
	}
	return t->ext;
}

// After reports whether the time instant t is after u.
bool nt_TimeAfter(nt_Time t, nt_Time u)
{
	if ((t.wall&u.wall&nt_hasMonotonic) != 0) {
		return t.ext > u.ext;
	}
	int64_t ts = nt_Time_sec(&t);
	int64_t us = nt_Time_sec(&u);
	return ts > us || (ts == us && nt_Time_nsec(&t)) > nt_Time_nsec(&u);
}

// Before reports whether the time instant t is before u.
bool nt_TimeBefore(nt_Time t, nt_Time u)
{
	if ((t.wall&u.wall&nt_hasMonotonic) != 0) {
		return t.ext < u.ext;
	}
	int64_t ts = nt_Time_sec(&t);
	int64_t us = nt_Time_sec(&u);
	return ts < us || (ts == us && nt_Time_nsec(&t)) < nt_Time_nsec(&u);
}

// Compare compares the time instant t with u. If t is before u, it returns -1;
// if t is after u, it returns +1; if they're the same, it returns 0.
int nt_TimeCompare(nt_Time t, nt_Time u)
{
	int64_t tc, uc;
	if ((t.wall&u.wall&nt_hasMonotonic) != 0) {
		tc = t.ext;
        uc = u.ext;
	} else {
        tc = nt_Time_sec(&t);
        uc = nt_Time_sec(&u);
		if (tc == uc) {
			tc = nt_Time_nsec(&t);
            uc = nt_Time_nsec(&u);
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
bool nt_TimeEqual(nt_Time t, nt_Time u)
{
	if ((t.wall&u.wall&nt_hasMonotonic) != 0) {
		return t.ext == u.ext;
	}
	return nt_Time_sec(&t) == nt_Time_sec(&u) && nt_Time_nsec(&t) == nt_Time_nsec(&u);
}

// String returns the English name of the month ("January", "February", ...).
// Caller should free the returned string.
char *nt_MonthString(nt_Month m)
{
    char *str;
	if (nt_JANUARY <= m && m <= nt_DECEMBER) {
        str = malloc(strlen(nt_longMonthNames[m-1]) + 1);
        strcpy(str, nt_longMonthNames[m-1]);
		return str;
	}
    const size_t bufLen = 20;
    char buf[bufLen];
	int n = nt_fmtInt(buf, bufLen, m);

    str = malloc(8 + bufLen + 1 + 1);
    str[0] = '\0';
    strncat(str, "%!Month(", 8);
    strncat(str, &buf[n], bufLen - n);
    strncat(str, ")", 1);
    return str;
}

// String returns the English name of the day ("Sunday", "Monday", ...).
// Caller should free the returned string.
char *nt_WeekdayString(nt_Weekday d)
{
    char *str;
	if (nt_SUNDAY <= d && d <= nt_SATURDAY) {
        str = malloc(strlen(nt_longDayNames[d]) + 1);
        strcpy(str, nt_longDayNames[d]);
		return str;
	}
    const size_t bufLen = 20;
    char buf[bufLen];
	int n = nt_fmtInt(buf, bufLen, d);

    str = malloc(10 + bufLen + 1 + 1);
    str[0] = '\0';
    strncat(str, "%!Weekday(", 10);
    strncat(str, &buf[n], bufLen - n);
    strncat(str, ")", 1);
    return str;
}

// IsZero reports whether t represents the zero time instant,
// January 1, year 1, 00:00:00 UTC.
bool nt_TimeIsZero(nt_Time t)
{
	return nt_Time_sec(&t) == 0 && nt_Time_nsec(&t) == 0;
}

// abs returns the time t as an absolute time, adjusted by the zone offset.
// It is called when computing a presentation property like Month or Hour.
// TODO: Unfinished, needs work.
uint64_t nt_Time_abs(nt_Time t)
{
	nt_Location *l = t.loc;
	// Avoid function calls when possible.
	if (l == NULL || l == &nt_localLoc) {
        l = nt_Location_get(l);
	}
	int64_t sec = nt_Time_unixSec(&t);
	if (l != &nt_utcLoc) {
		if (l->cacheZone != NULL && l->cacheStart <= sec && sec < l->cacheEnd) {
			sec += l->cacheZone->offset;
		} else {
			/* _, offset, _, _, _ := l.lookup(sec); */
			/* sec += offset; */
		}
	}
	return sec + (nt_unixToInternal + nt_internalToAbsolute);
}

struct nt_date{
    int year;
    nt_Month month;
    int day;
    int yday;
};

struct nt_Timelocabs{
    char *name;
    int offset;
    uint64_t abs;
};
// locabs is a combination of the Zone and abs methods,
// extracting both return values from a single zone lookup.
 struct nt_Timelocabs nt_Time_locabs(nt_Time t) 
{
    struct nt_Timelocabs ret = {0};
	nt_Location *l = t.loc;
	if (l == NULL || l == &nt_localLoc) {
		/* l = l.get(); */
	}
	// Avoid function call if we hit the local time cache.
	int64_t sec = nt_Time_unixSec(&t);
	if (l != &nt_utcLoc) {
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
	ret.abs = (sec + (nt_unixToInternal + nt_internalToAbsolute));
	return ret;
}

// date computes the year, day of year, and when full=true,
// the month and day in which t occurs.
struct nt_date nt_Time_date(nt_Time t, bool full)
{
	return nt_absDate(nt_Time_abs(t), full);
}

// Date returns the year, month, and day in which t occurs.
struct nt_Date nt_TimeDate(nt_Time t)
{
	struct nt_date d = nt_Time_date(t, true);
    return (struct nt_Date) {
        .year = d.year,
        .month = d.month,
        .day = d.day,
    };
}

// Year returns the year in which t occurs.
int nt_TimeYear(nt_Time t)
{
	struct nt_date d = nt_Time_date(t, false);
    return d.year;
}

// Month returns the month of the year specified by t.
nt_Month nt_TimeMonth(nt_Time t)
{
	struct nt_date d = nt_Time_date(t, true);
	return d.month;
}

// Day returns the day of the month specified by t.
int nt_TimeDay(nt_Time t)
{
	struct nt_date d = nt_Time_date(t, true);
    return d.day;
}

// absWeekday is like Weekday but operates on an absolute time.
nt_Weekday nt_Time_absWeekday(uint64_t abs)
{
	// January 1 of the absolute year, like January 1 of 2001, was a Monday.
	uint64_t sec = (abs + nt_MONDAY*nt_secondsPerDay) % nt_secondsPerWeek;
	return sec / nt_secondsPerDay;
}

// Weekday returns the day of the week specified by t.
nt_Weekday nt_TimeWeekday(nt_Time t)
{
	return nt_Time_absWeekday(nt_Time_abs(t));
}

// ISOWeek returns the ISO 8601 year and week number in which t occurs.
// Week ranges from 1 to 53. Jan 01 to Jan 03 of year n might belong to
// week 52 or 53 of year n-1, and Dec 29 to Dec 31 might belong to week 1
// of year n+1.
struct nt_Week nt_TimeISOWeek(nt_Time t)
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
	uint64_t abs = nt_Time_abs(t);
	nt_Weekday d = nt_THURSDAY - nt_Time_absWeekday(abs);
	// handle Sunday
	if (d == 4) {
		d = -3;
	}
	// find the Thursday of the calendar week
	abs += d * nt_secondsPerDay;
	struct nt_date td = nt_absDate(abs, false);
    return (struct nt_Week){
        .year = td.year,
        .week = td.yday/7 + 1,
    };
}

// Clock returns the hour, minute, and second within the day specified by t.
struct nt_Clock  nt_TimeClock(nt_Time t)
{
	return nt_Time_absClock(nt_Time_abs(t));
}

// absClock is like clock but operates on an absolute time.
struct nt_Clock nt_Time_absClock(uint64_t abs)
{
    struct nt_Clock ret = {0};
	ret.sec = abs % nt_secondsPerDay;
	ret.hour = ret.sec / nt_secondsPerHour;
	ret.sec -= ret.hour * nt_secondsPerHour;
	ret.min = ret.sec / nt_secondsPerMinute;
	ret.sec -= ret.min * nt_secondsPerMinute;
	return ret;
}

// Hour returns the hour within the day specified by t, in the range [0, 23].
int nt_TimeHour(nt_Time t)
{
	return (nt_Time_abs(t)%nt_secondsPerDay) / nt_secondsPerHour;
}

// Minute returns the minute offset within the hour specified by t, in the range [0, 59].
int nt_TimeMinute(nt_Time t)
{
	return (nt_Time_abs(t)%nt_secondsPerHour) / nt_secondsPerMinute;
}

// Second returns the second offset within the minute specified by t, in the range [0, 59].
int nt_TimeSecond(nt_Time t)
{
	return (nt_Time_abs(t) % nt_secondsPerMinute);
}

// Nanosecond returns the nanosecond offset within the second specified by t,
// in the range [0, 999999999].
int nt_TimeNanosecond(nt_Time t)
{
	return nt_Time_nsec(&t);
}

// YearDay returns the day of the year specified by t, in the range [1,365] for non-leap years,
// and [1,366] in leap years.
int nt_TimeYearDay(nt_Time t)
{
    struct nt_date td = nt_Time_date(t, false);
	return td.yday + 1;
}
// String returns a string representing the duration in the form "72h3m0.5s".
// Leading zero units are omitted. As a special case, durations less than one
// second format use a smaller unit (milli-, micro-, or nanoseconds) to ensure
// that the leading digit is non-zero. The zero duration formats as 0s.
char *nt_DurationString(nt_Duration d)
{
	// This is inlinable to take advantage of "function outlining".
	// Thus, the caller can decide whether a string must be heap allocated.
	char arr[32];
	int n = nt_Duration_format(d, arr);
    char *str = malloc(32 - n + 1);
    strncpy(str, &arr[n], 32-n);
    return str;
}

// format formats the representation of d into the end of buf and
// returns the offset of the first character.
int nt_Duration_format(nt_Duration d, char buf[32])
{
	// Largest time is 2540400h10m10.000000000s
    size_t w = 32;

	uint64_t u = d;
	bool neg = d < 0;
	if (neg) {
		u = -u;
	}

	if (u < nt_SECOND) {
		// Special case: if duration is smaller than a second,
		// use smaller units, like 1.2ms
		int prec = 0;
		w--;
		buf[w] = 's';
		w--;
		if (u == 0) {
			buf[w] = '0';
			return w;
        } else if (u < nt_MICROSECOND) {
			// print nanoseconds
			prec = 0;
			buf[w] = 'n';
        } else if (u < nt_MILLISECOND) {
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
        struct nt_fmtFrac ff = nt_fmtFrac(buf, w, u, prec);
        w = ff.nw;
        u = ff.nv;
		w = nt_fmtInt(buf, w, u);
	} else {
		w--;
		buf[w] = 's';

        struct nt_fmtFrac ff = nt_fmtFrac(buf, w, u, 9);
        w = ff.nw;
        u = ff.nv;

		// u is now integer seconds
		w = nt_fmtInt(buf, w, u%60);
		u /= 60;

		// u is now integer minutes
		if (u > 0) {
			w--;
			buf[w] = 'm';
            w = nt_fmtInt(buf, w, u%60);
			u /= 60;

			// u is now integer hours
			// Stop at hours because days can be different lengths.
			if (u > 0) {
				w--;
				buf[w] = 'h';
                w = nt_fmtInt(buf, w, u);
			}
		}
	}

	if (neg) {
		w--;
		buf[w] = '-';
	}

	return w;
}

// fmtFrac formats the fraction of v/10**prec (e.g., ".12345") into the
// tail of buf, omitting trailing zeros. It omits the decimal
// point too when the fraction is 0. It returns the index where the
// output bytes begin and the value v/10**prec.
struct nt_fmtFrac nt_fmtFrac(char buf[], size_t bufLen, uint64_t v, int prec)
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
	return (struct nt_fmtFrac){ .nw = w, .nv = v };
}

// fmtInt formats v into the tail of buf.
// It returns the index where the output begins.
int nt_fmtInt(char buf[], size_t bufLen, uint64_t v)
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

// Nanoseconds returns the duration as an integer nanosecond count.
int64_t nt_DurationNanoseconds(nt_Duration d) { return d; }

// Microseconds returns the duration as an integer microsecond count.
int64_t nt_DurationMicroseconds(nt_Duration d) { return d / 1e3; }

// Milliseconds returns the duration as an integer millisecond count.
int64_t nt_DurationMilliseconds(nt_Duration d) { return d / 1e6; }


// These methods return float64 because the dominant
// use case is for printing a floating point number like 1.5s, and
// a truncation to integer would make them not useful in those cases.
// Splitting the integer and fraction ourselves guarantees that
// converting the returned float64 to an integer rounds the same
// way that a pure integer conversion would have, even in cases
// where, say, float64(d.Nanoseconds())/1e9 would have rounded
// differently.

// Seconds returns the duration as a floating point number of seconds.
double nt_DurationSeconds(nt_Duration d)
{
	int64_t sec = d / nt_SECOND;
	int64_t nsec = d % nt_SECOND;
	return (double)sec + (double)nsec/1e9;
}

// Minutes returns the duration as a floating point number of minutes.
double nt_DurationMinutes(nt_Duration d)
{
	int64_t min = d / nt_MINUTE;
	int64_t nsec = d % nt_MINUTE;
	return (double)min + (double)nsec/(60*1e9);
}

// Hours returns the duration as a floating point number of hours.
double nt_DurationHours(nt_Duration d)
{
	int64_t hour = d / nt_HOUR;
	int64_t nsec = d % nt_HOUR;
	return (double)hour + (double)nsec/(60*60*1e9);
}

// Truncate returns the result of rounding d toward zero to a multiple of m.
// If m <= 0, Truncate returns d unchanged.
nt_Duration  nt_DurationTruncate(nt_Duration d, nt_Duration m) {
	if (m <= 0) {
		return d;
	}
	return d - d%m;
}

// lessThanHalf reports whether x+x < y but avoids overflow,
// assuming x and y are both positive (Duration is signed).
bool nt_lessThanHalf(nt_Duration x, nt_Duration y ) 
{
	return x+x < y;
}

// Round returns the result of rounding d to the nearest multiple of m.
// The rounding behavior for halfway values is to round away from zero.
// If the result exceeds the maximum (or minimum)
// value that can be stored in a Duration,
// Round returns the maximum (or minimum) duration.
// If m <= 0, Round returns d unchanged.
nt_Duration nt_DurationRound(nt_Duration d, nt_Duration m)
{
	if (m <= 0) {
		return d;
	}
	int64_t r = d % m;
	if (d < 0) {
		r = -r;
		if (nt_lessThanHalf(r, m)) {
			return d + r;
		}
        int64_t d1 = d - m + r;
		if (d1 < d) {
			return d1;
		}
		return minDuration; // overflow
	}
	if (nt_lessThanHalf(r, m)) {
		return d - r;
	}
	int64_t d1 = d + m - r;
    if (d1 > d) {
		return d1;
	}
	return maxDuration; // overflow
}

// Abs returns the absolute value of d.
// As a special case, math.MinInt64 is converted to math.MaxInt64.
nt_Duration nt_DurationAbs(nt_Duration d) 
{
    if (d >= 0)
        return d;
    else if (d == minDuration)
        return maxDuration;
    else
        return -d;
}

// Add returns the time t+d.
nt_Time nt_TimeAdd(nt_Time t , nt_Duration d) 
{
	int64_t dsec = d / 1e9;
	int32_t nsec = nt_Time_nsec(&t) + d%(int32_t)1e9;
	if (nsec >= 1e9) {
		dsec++;
		nsec -= 1e9;
	} else if (nsec < 0) {
		dsec--;
		nsec += 1e9;
	}
	t.wall = (t.wall & ~nt_nsecMask) | nsec; // update nsec
	nt_Time_addSec(&t, dsec);
	if ((t.wall&nt_hasMonotonic) != 0) {
		int64_t te = t.ext + d;
		if ((d < 0 && te > t.ext) || (d > 0 && te < t.ext)) {
			// Monotonic clock reading now out of range; degrade to wall-only.
			nt_Time_stripMono(&t);
		} else {
			t.ext = te;
		}
	}
	return t;
}

// Sub returns the duration t-u. If the result exceeds the maximum (or minimum)
// value that can be stored in a Duration, the maximum (or minimum) duration
// will be returned.
// To compute t-d for a duration d, use t.Add(-d).
nt_Duration nt_TimeSub(nt_Time t, nt_Time u)
{
	if ((t.wall&u.wall&nt_hasMonotonic) != 0) {
		return nt_subMono(t.ext, u.ext);
	}
	nt_Duration d = (nt_Time_sec(&t)-nt_Time_sec(&u)) * nt_SECOND + (nt_Time_nsec(&t)-nt_Time_nsec(&u));
	// Check for overflow or underflow.
    if (nt_TimeEqual(nt_TimeAdd(u, d), t))
        return d; // d is correct
    else if (nt_TimeBefore(t, u))
        return minDuration; // t - u is negative out of range
    else
        return maxDuration; // t - u is positive out of range
}

nt_Duration nt_subMono(int64_t t, int64_t u)
{
	nt_Duration d = t - u;
	if (d < 0 && t > u) {
		return maxDuration; // t - u is positive out of range
	}
	if (d > 0 && t < u) {
		return minDuration; // t - u is negative out of range
	}
	return d;
}

// Since returns the time elapsed since t.
// It is shorthand for time.Now().Sub(t).
nt_Duration nt_Since(nt_Time t)
{
	if ((t.wall&nt_hasMonotonic) != 0) {
		// Common case optimization: if t has monotonic time, then Sub will use only it.
		return nt_subMono(nt_runtimeNano()-nt_startNano, t.ext);
	}
	return nt_TimeSub(nt_Now(), t);
}

// Until returns the duration until t.
// It is shorthand for t.Sub(time.Now()).
nt_Duration nt_Until(nt_Time t)
{
	if ((t.wall&nt_hasMonotonic) != 0) {
		// Common case optimization: if t has monotonic time, then Sub will use only it.
		return nt_subMono(t.ext, nt_runtimeNano()-nt_startNano);
	}
	return nt_TimeSub(t, nt_Now());
}

// AddDate returns the time corresponding to adding the
// given number of years, months, and days to t.
// For example, AddDate(-1, 2, 3) applied to January 1, 2011
// returns March 4, 2010.
//
// Note that dates are fundamentally coupled to timezones, and calendrical
// periods like days don't have fixed durations. AddDate uses the Location of
// the Time value to determine these durations. That means that the same
// AddDate arguments can produce a different shift in absolute time depending on
// the base Time value and its Location. For example, AddDate(0, 0, 1) applied
// to 12:00 on March 27 always returns 12:00 on March 28. At some locations and
// in some years this is a 24 hour shift. In others it's a 23 hour shift due to
// daylight savings time transitions.
//
// AddDate normalizes its result in the same way that Date does,
// so, for example, adding one month to October 31 yields
// December 1, the normalized form for November 31.
nt_Time nt_TimeAddDate(nt_Time t, int years, int months, int days)
{
	struct nt_Date date = nt_TimeDate(t);
    int year = date.year;
    nt_Month month = date.month;
    int day = date.day;
	struct nt_Clock clock = nt_TimeClock(t);
    int hour = clock.hour;
    int min = clock.min;
    int sec = clock.sec;
	return nt_Date(year+years, month+months, day+days, hour, min, sec, nt_Time_nsec(&t), nt_TimeLocation(t));
}

// date computes the year, day of year, and when full=true,
// the month and day in which t occurs.
struct nt_date date(nt_Time t, bool full)
{
	return nt_absDate(nt_Time_abs(t), full);
}

 /* (year int, month Month, day int, yday int) */ 
// absDate is like date but operates on an absolute time.
struct nt_date nt_absDate(uint64_t abs , bool full)
{
    struct nt_date ret = {0};
	// Split into time and day.
	uint64_t d = abs / nt_secondsPerDay;

	// Account for 400 year cycles.
	uint64_t n = d / nt_daysPer400Years;
	uint64_t y = 400 * n;
	d -= nt_daysPer400Years * n;

	// Cut off 100-year cycles.
	// The last cycle has one extra leap year, so on the last day
	// of that year, day / daysPer100Years will be 4 instead of 3.
	// Cut it back down to 3 by subtracting n>>2.
	n = d / nt_daysPer100Years;
	n -= n >> 2;
	y += 100 * n;
	d -= nt_daysPer100Years * n;

	// Cut off 4-year cycles.
	// The last cycle has a missing leap year, which does not
	// affect the computation.
	n = d / nt_daysPer4Years;
	y += 4 * n;
	d -= nt_daysPer4Years * n;

	// Cut off years within a 4-year cycle.
	// The last year is a leap year, so on the last day of that year,
	// day / 365 will be 4 instead of 3. Cut it back down to 3
	// by subtracting n>>2.
	n = d / 365;
	n -= n >> 2;
	y += n;
	d -= 365 * n;

	ret.year = y + nt_absoluteZeroYear;
	ret.yday = d;

	if (!full) {
		return ret;
	}

	ret.day = ret.yday;
	if (nt_isLeap(ret.year)) {
		// Leap year
		if (ret.day > 31+29-1) {
			// After leap day; pretend it wasn't there.
			ret.day--;
        } else if (ret.day == 31+29-1) {
			// Leap day.
			ret.month = nt_FEBRUARY;
			ret.day = 29;
			return ret;
		}
	}

	// Estimate month on assumption that every month has 31 days.
	// The estimate may be too low by at most one month, so adjust.
	ret.month = ret.day / 31;
	int end = nt_daysBefore[ret.month+1];
	int begin;
	if (ret.day >= end) {
		ret.month++;
		begin = end;
	} else {
		begin = nt_daysBefore[ret.month];
	}

	ret.month++; // because January is 1
	ret.day = ret.day - begin + 1;
	return ret;
}

// UPDATE //////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// runtimeNano returns the current value of the runtime clock in nanoseconds.
//
//go:linkname runtimeNano runtime.nanotime
int64_t nt_runtimeNano() 
{
    struct nt_now now = nt_now();
    return now.nsec;
}

// Now returns the current local time.
nt_Time nt_Now(void)
{
    struct nt_now now = nt_now();
	now.mono -= nt_startNano;
	now.sec += nt_unixToInternal - nt_minWall;
	if ((now.sec>>33) != 0) {
		// Seconds field overflowed the 33 bits available when
		// storing a monotonic time. This will be true after
		// March 16, 2157.
		return (nt_Time){now.nsec, now.sec + nt_minWall, nt_Local};
	}
	return (nt_Time){nt_hasMonotonic | now.sec<<nt_nsecShift | now.nsec, now.mono, nt_Local};
}


// Load local timezone data??
void nt_init(void)
{
    nt_startNano = nt_runtimeNano() - 1;
}

nt_Time nt_unixTime(int64_t sec, int32_t nsec)
{
	return (nt_Time){nsec, sec + nt_unixToInternal, nt_Local};
}

// UTC returns t with the location set to UTC.
//
nt_Time nt_TimeUTC(nt_Time t)
{
	nt_Time_setLoc(&t, &nt_utcLoc);
	return t;
}

// Local returns t with the location set to local time.
nt_Time nt_TimeLocal(nt_Time t)
{
	nt_Time_setLoc(&t, nt_Local);
	return t;
}

bool nt_isLeap(int year)
{
	return year%4 == 0 && (year%100 != 0 || year%400 == 0);
}


// zoneinfo.go

// alpha and omega are the beginning and end of time for zone
// transitions.
const int64_t nt_alpha = -((int64_t)1<<63);  // math.MinInt64
const int64_t nt_omega = ((int64_t)1<<63) - 1; // math.MaxInt64


/* var localOnce sync.Once */

nt_Location *nt_Location_get(nt_Location *l) 
{
	if (l == NULL) {
		return &nt_utcLoc;
	}
	if (l == &nt_localLoc) {
		/* localOnce.Do(initLocal) */
	}
	return l;
}

// String returns a descriptive name for the time zone information,
// corresponding to the name argument to LoadLocation or FixedZone.
char *nt_LocationString(nt_Location *l) 
{
	return nt_Location_get(l)->name;
}

struct nt_Location_lookup {
    char *name;
    int offset;
    int start;
    int64_t end;
    bool isDST;
};
// lookup returns information about the time zone in use at an
// instant in time expressed as seconds since January 1, 1970 00:00:00 UTC.
//
// The returned information gives the name of the zone (such as "CET"),
// the start and end times bracketing sec when that zone is in effect,
// the offset in seconds east of UTC (such as -5*60*60), and whether
// the daylight savings is being observed at that time.
struct nt_Location_lookup nt_Location_lookup(nt_Location *l, int64_t sec) {
	l = nt_Location_get(l);

    struct nt_Location_lookup ret = {0};

	if (l->zoneLen == 0) {
		ret.name = "UTC";
		ret.offset = 0;
		ret.start = nt_alpha;
		ret.end = nt_omega;
		ret.isDST = false;
		return ret;
	}
    nt_zone *zone = l->cacheZone; 
	if (zone != NULL && l->cacheStart <= sec && sec < l->cacheEnd) {
		ret.name = zone->name;
		ret.offset = zone->offset;
		ret.start = l->cacheStart;
		ret.end = l->cacheEnd;
		ret.isDST = zone->isDST;
		return ret;
	}

	if (l->txLen == 0 || sec < l->tx[0].when) {
		/* nt_zone *zone = &l.zone[l.lookupFirstZone()]; */
		ret.name = zone->name;
		ret.offset = zone->offset;
		ret.start = nt_alpha;
		if (l->txLen > 0) {
			ret.end = l->tx[0].when;
		} else {
			ret.end = nt_omega;
		}
		ret.isDST = zone->isDST;
		return ret;
	}

	// Binary search for entry with largest time <= sec.
	// Not using sort.Search to avoid dependencies.
	nt_zoneTrans *tx = l->tx;
    size_t txLen = l->txLen;
	ret.end = nt_omega;
	size_t lo = 0;
	size_t hi = txLen;
	while (hi-lo > 1) {
		int m = (lo+hi) >> 1;
		int64_t lim = tx[m].when;
		if (sec < lim) {
			ret.end = lim;
			hi = m;
		} else {
			lo = m;
		}
	}
	zone = &l->zone[tx[lo].index];
	ret.name = zone->name;
	ret.offset = zone->offset;
	ret.start = tx[lo].when;
	// end = maintained during the search
	ret.isDST = zone->isDST;

	// If we're at the end of the known zone transitions,
	// try the extend string.
	if (lo == txLen-1 && !nt_EMPTY_STR(l->extend)) {
       /* ename, eoffset, estart, eend, eisDST, ok := tzset(l.extend, start, sec); */ // TODO: implment tzset function
		/* if (ok) { */
		/* 	return (struct nt_Location_lookup){ename, eoffset, estart, eend, eisDST}; */
		/* } */
	}

	return ret;
}

// firstZoneUsed reports whether the first zone is used by some
// transition.
bool nt_Location_firstZoneUsed(nt_Location *l)
{
    for (int i = 0; i < l->txLen; i++ ) {
        nt_zoneTrans tx = l->tx[i];
		if (tx.index == 0) {
			return true;
		}
	}
	return false;
}

// lookupFirstZone returns the index of the time zone to use for times
// before the first transition time, or when there are no transition
// times.
//
// The reference implementation in localtime.c from
// https://www.iana.org/time-zones/repository/releases/tzcode2013g.tar.gz
// implements the following algorithm for these cases:
//  1. If the first zone is unused by the transitions, use it.
//  2. Otherwise, if there are transition times, and the first
//     transition is to a zone in daylight time, find the first
//     non-daylight-time zone before and closest to the first transition
//     zone.
//  3. Otherwise, use the first zone that is not daylight time, if
//     there is one.
//  4. Otherwise, use the first zone.
int nt_Location_lookupFirstZone(nt_Location *l) {
	// Case 1.
	if (!nt_Location_firstZoneUsed(l)) {
		return 0;
	}

	// Case 2.
	if (l->txLen > 0 && l->zone[l->tx[0].index].isDST) {
		for (int zi = l->tx[0].index - 1; zi >= 0; zi--) {
			if (!l->zone[zi].isDST) {
				return zi;
			}
		}
	}

	// Case 3.
	/* for zi := range l.zone { */
    for (int zi = 0; zi < l->zoneLen; zi++) {
		if (!l->zone[zi].isDST) {
			return zi;
		}
	}

	// Case 4.
	return 0;
}

struct nt_tzsetName {
    char *tzName;
    char *remainder;
    bool ok;
};
// tzsetName returns the timezone name at the start of the tzset string s,
// and the remainder of s, and reports whether the parsing is OK.
struct nt_tzsetName nt_tzsetName(char *s)
{
    size_t sLen = strlen(s);

	if (sLen == 0) {
		return (struct nt_tzsetName){"", "", false};
	}
	if (s[0] != '<') {
        for (int i = 0; i < sLen; i++) {
            char r = s[i];
			switch (r) {
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
                case ',':
                case '-':
                case '+':
                    if (i < 3) {
                        return (struct nt_tzsetName){"", "", false};
                    }
                    /* return (struct nt_tzsetName){s[:i], s[i:], true}; */
			}
		}
		if (sLen < 3) {
            return (struct nt_tzsetName){"", "", false};
		}
		return (struct nt_tzsetName){s, "", true};
	} else {
        for (int i = 0; i < sLen; i++) {
            char r = s[i];
			if (r == '>') {
				/* return s[1:i], s[i+1:], true; */
			}
		}
        return (struct nt_tzsetName){"", "", false};
	}
}

/* struct nt_tzset { */
/*     char *name; */
/*     int offset; */
/*     int start; */
/*     int64_t end; */
/*     bool isDST; */
/*     bool ok; */
/* }; */
/* // tzset takes a timezone string like the one found in the TZ environment */
/* // variable, the time of the last time zone transition expressed as seconds */
/* // since January 1, 1970 00:00:00 UTC, and a time expressed the same way. */
/* // We call this a tzset string since in C the function tzset reads TZ. */
/* // The return values are as for lookup, plus ok which reports whether the */
/* // parse succeeded. */
/* struct nt_tzset nt_tzset(char *s, char *lastTxSec, int64_t sec) */
/* { */
/* 	char *tdName = NULL; */
/*     char *dstName = NULL; */
/* 	int	stdOffset = 0; */
/*     int dstOffset = 0; */

/* 	stdName, s, ok = tzsetName(s) */
/* 	if ok { */
/* 		stdOffset, s, ok = tzsetOffset(s) */
/* 	} */
/* 	if !ok { */
/* 		return "", 0, 0, 0, false, false */
/* 	} */

/* 	// The numbers in the tzset string are added to local time to get UTC, */
/* 	// but our offsets are added to UTC to get local time, */
/* 	// so we negate the number we see here. */
/* 	stdOffset = -stdOffset */

/* 	if len(s) == 0 || s[0] == ',' { */
/* 		// No daylight savings time. */
/* 		return stdName, stdOffset, lastTxSec, omega, false, true */
/* 	} */

/* 	dstName, s, ok = tzsetName(s) */
/* 	if ok { */
/* 		if len(s) == 0 || s[0] == ',' { */
/* 			dstOffset = stdOffset + secondsPerHour */
/* 		} else { */
/* 			dstOffset, s, ok = tzsetOffset(s) */
/* 			dstOffset = -dstOffset // as with stdOffset, above */
/* 		} */
/* 	} */
/* 	if !ok { */
/* 		return "", 0, 0, 0, false, false */
/* 	} */

/* 	if len(s) == 0 { */
/* 		// Default DST rules per tzcode. */
/* 		s = ",M3.2.0,M11.1.0" */
/* 	} */
/* 	// The TZ definition does not mention ';' here but tzcode accepts it. */
/* 	if s[0] != ',' && s[0] != ';' { */
/* 		return "", 0, 0, 0, false, false */
/* 	} */
/* 	s = s[1:] */

/* 	var startRule, endRule rule */
/* 	startRule, s, ok = tzsetRule(s) */
/* 	if !ok || len(s) == 0 || s[0] != ',' { */
/* 		return "", 0, 0, 0, false, false */
/* 	} */
/* 	s = s[1:] */
/* 	endRule, s, ok = tzsetRule(s) */
/* 	if !ok || len(s) > 0 { */
/* 		return "", 0, 0, 0, false, false */
/* 	} */

/* 	year, _, _, yday := absDate(uint64(sec+unixToInternal+internalToAbsolute), false) */

/* 	ysec := int64(yday*secondsPerDay) + sec%secondsPerDay */

/* 	// Compute start of year in seconds since Unix epoch. */
/* 	d := daysSinceEpoch(year) */
/* 	abs := int64(d * secondsPerDay) */
/* 	abs += absoluteToInternal + internalToUnix */

/* 	startSec := int64(tzruleTime(year, startRule, stdOffset)) */
/* 	endSec := int64(tzruleTime(year, endRule, dstOffset)) */
/* 	dstIsDST, stdIsDST := true, false */
/* 	// Note: this is a flipping of "DST" and "STD" while retaining the labels */
/* 	// This happens in southern hemispheres. The labelling here thus is a little */
/* 	// inconsistent with the goal. */
/* 	if endSec < startSec { */
/* 		startSec, endSec = endSec, startSec */
/* 		stdName, dstName = dstName, stdName */
/* 		stdOffset, dstOffset = dstOffset, stdOffset */
/* 		stdIsDST, dstIsDST = dstIsDST, stdIsDST */
/* 	} */

/* 	// The start and end values that we return are accurate */
/* 	// close to a daylight savings transition, but are otherwise */
/* 	// just the start and end of the year. That suffices for */
/* 	// the only caller that cares, which is Date. */
/* 	if ysec < startSec { */
/* 		return stdName, stdOffset, abs, startSec + abs, stdIsDST, true */
/* 	} else if ysec >= endSec { */
/* 		return stdName, stdOffset, endSec + abs, abs + 365*secondsPerDay, stdIsDST, true */
/* 	} else { */
/* 		return dstName, dstOffset, startSec + abs, endSec + abs, dstIsDST, true */
/* 	} */
/* } */

// In returns a copy of t representing the same time instant, but
// with the copy's location information set to loc for display
// purposes.
//
// In panics if loc is nil.
nt_Time nt_TimeIn(nt_Time t, nt_Location *loc) 
{
	if (loc == NULL) {
		nt_panic("time: missing Location in call to nt_TimeIn");
	}
	nt_Time_setLoc(&t, loc);
	return t;
}

// Location returns the time zone information associated with t.
nt_Location *nt_TimeLocation(nt_Time t)
{
	nt_Location *l = t.loc;
	if (l == NULL) {
		l = nt_UTC;
	}
	return l;
}

// Zone computes the time zone in effect at time t, returning the abbreviated
// name of the zone (such as "CET") and its offset in seconds east of UTC.
struct nt_TimeZone nt_TimeZone(nt_Time t)
{
	struct nt_Location_lookup lookup = nt_Location_lookup(t.loc, nt_Time_unixSec(&t));
	return (struct nt_TimeZone){
        .name = lookup.name,
        .offset = lookup.offset,
    };
}

struct nt_TimeZoneBounds {
    nt_Time start, end;
};
// ZoneBounds returns the bounds of the time zone in effect at time t.
// The zone begins at start and the next zone begins at end.
// If the zone begins at the beginning of time, start will be returned as a zero Time.
// If the zone goes on forever, end will be returned as a zero Time.
// The Location of the returned times will be the same as t.
struct nt_TimeZoneBounds nt_TimeZoneBounds(nt_Time t)
{
	/* _, _, startSec, endSec, _ := t.loc.lookup(t.unixSec()) */
    struct nt_TimeZoneBounds ret = {0};
	struct nt_Location_lookup lookup = nt_Location_lookup(t.loc, nt_Time_unixSec(&t));
    int startSec = lookup.start;
    int endSec = lookup.end;
	if (startSec != nt_alpha) {
		ret.start = nt_unixTime(startSec, 0);
		nt_Time_setLoc(&ret.start, t.loc);
	}
	if (endSec != nt_omega) {
		ret.end = nt_unixTime(endSec, 0);
		nt_Time_setLoc(&ret.end, t.loc);
	}
	return ret;
}

// Unix returns t as a Unix time, the number of seconds elapsed
// since January 1, 1970 UTC. The result does not depend on the
// location associated with t.
// Unix-like operating systems often record time as a 32-bit
// count of seconds, but since the method here returns a 64-bit
// value it is valid for billions of years into the past or future.
int64_t nt_TimeUnix(nt_Time t)
{
	return nt_Time_unixSec(&t);
}

// UnixMilli returns t as a Unix time, the number of milliseconds elapsed since
// January 1, 1970 UTC. The result is undefined if the Unix time in
// milliseconds cannot be represented by an int64 (a date more than 292 million
// years before or after 1970). The result does not depend on the
// location associated with t.
int64_t nt_TimeUnixMilli(nt_Time t)
{
	return nt_Time_unixSec(&t)*1e3 + nt_Time_nsec(&t)/1e6;
}

// UnixMicro returns t as a Unix time, the number of microseconds elapsed since
// January 1, 1970 UTC. The result is undefined if the Unix time in
// microseconds cannot be represented by an int64 (a date before year -290307 or
// after year 294246). The result does not depend on the location associated
// with t.
int64_t nt_TimeUnixMicro(nt_Time t)
{
	return nt_Time_unixSec(&t)*1e6 + nt_Time_nsec(&t)/1e3;
}

// UnixNano returns t as a Unix time, the number of nanoseconds elapsed
// since January 1, 1970 UTC. The result is undefined if the Unix time
// in nanoseconds cannot be represented by an int64 (a date before the year
// 1678 or after 2262). Note that this means the result of calling UnixNano
// on the zero Time is undefined. The result does not depend on the
// location associated with t.
int64_t nt_TimeUnixNano(nt_Time t)
{
	return nt_Time_unixSec(&t)*1e9 + nt_Time_nsec(&t);
}

// Unix returns the local Time corresponding to the given Unix time,
// sec seconds and nsec nanoseconds since January 1, 1970 UTC.
// It is valid to pass nsec outside the range [0, 999999999].
// Not all sec values have a corresponding time value. One such
// value is 1<<63-1 (the largest int64 value).
nt_Time nt_Unix(int64_t sec, int64_t nsec)
{
	if (nsec < 0 || nsec >= 1e9) {
		int64_t n = nsec / 1e9;
		sec += n;
		nsec -= n * 1e9;
		if (nsec < 0) {
			nsec += 1e9;
			sec--;
		}
	}
	return nt_unixTime(sec, nsec);
}

struct nt_norm {
    int nhi, nlo;
};
// norm returns nhi, nlo such that
//
//	hi * base + lo == nhi * base + nlo
//	0 <= nlo < base
struct nt_norm nt_norm(int hi, int lo, int base) 
{
	if (lo < 0) {
		int n = (-lo-1)/base + 1;
		hi -= n;
		lo += n * base;
	}
	if (lo >= base) {
		int n = lo / base;
		hi += n;
		lo -= n * base;
	}
	return (struct nt_norm){.nhi = hi, .nlo = lo};
}

// daysSinceEpoch takes a year and returns the number of days from
// the absolute epoch to the start of that year.
// This is basically (year - zeroYear) * 365, but accounting for leap days.
uint64_t nt_daysSinceEpoch(int year) 
{
	uint64_t y =  year - nt_absoluteZeroYear;

	// Add in days from 400-year cycles.
	uint64_t n = y / 400;
	y -= 400 * n;
	uint64_t d = nt_daysPer400Years * n;

	// Add in 100-year cycles.
	n = y / 100;
	y -= 100 * n;
	d += nt_daysPer100Years * n;

	// Add in 4-year cycles.
	n = y / 4;
	y -= 4 * n;
	d += nt_daysPer4Years * n;

	// Add in non-leap years.
	n = y;
	d += 365 * n;

	return d;
}

// Date returns the Time corresponding to
//
//	yyyy-mm-dd hh:mm:ss + nsec nanoseconds
//
// in the appropriate zone for that time in the given location.
//
// The month, day, hour, min, sec, and nsec values may be outside
// their usual ranges and will be normalized during the conversion.
// For example, October 32 converts to November 1.
//
// A daylight savings time transition skips or repeats times.
// For example, in the United States, March 13, 2011 2:15am never occurred,
// while November 6, 2011 1:15am occurred twice. In such cases, the
// choice of time zone, and therefore the time, is not well-defined.
// Date returns a time that is correct in one of the two zones involved
// in the transition, but it does not guarantee which.
//
// Date panics if loc is nil.
nt_Time nt_Date(int year, nt_Month month, int day, int hour, int min, int sec, int nsec, nt_Location *loc)
{
	if (loc == NULL) {
        nt_panic("time: missing Location in call to Date\n");
	}

	// Normalize month, overflowing into year.
	int m = month - 1;
    struct nt_norm norm;
	norm = nt_norm(year, m, 12);
    year = norm.nhi;
    m = norm.nlo;
	month = m + 1;

	// Normalize nsec, sec, min, hour, overflowing into day.
	norm = nt_norm(sec, nsec, 1e9);
    sec = norm.nhi;
    nsec = norm.nlo;
	norm = nt_norm(min, sec, 60);
    min = norm.nhi;
    sec = norm.nlo;
	norm = nt_norm(hour, min, 60);
    hour = norm.nhi;
    min = norm.nlo;
	norm = nt_norm(day, hour, 24);
    day = norm.nhi;
    hour = norm.nlo;

	// Compute days since the absolute epoch.
	uint64_t d = nt_daysSinceEpoch(year);

	// Add in days before this month.
	d += nt_daysBefore[month-1];
	if (nt_isLeap(year) && month >= nt_MARCH) {
		d++; // February 29
	}

	// Add in days before today.
	d += day - 1;

	// Add in time elapsed today.
	uint64_t abs = d * nt_secondsPerDay;
    abs += hour*nt_secondsPerHour + min*nt_secondsPerMinute + sec;

	int64_t unix = abs + (nt_absoluteToInternal + nt_internalToUnix);

	// Look for zone offset for expected time, so we can adjust to UTC.
	// The lookup function expects UTC, so first we pass unix in the
	// hope that it will not be too close to a zone transition,
	// and then adjust if it is.
	struct nt_Location_lookup lookup = nt_Location_lookup(loc, unix);
    int offset = lookup.offset;
    int start = lookup.start;
    int64_t end = lookup.end;
	if (offset != 0) {
		int64_t utc = unix - offset;
		// If utc is valid for the time zone we found, then we have the right offset.
		// If not, we get the correct offset by looking up utc in the location.
		if (utc < start || utc >= end) {
            nt_Location_lookup(loc, utc);
            offset = lookup.offset;
		}
		unix -= offset;
	}

	nt_Time t = nt_unixTime(unix, nsec);
    nt_Time_setLoc(&t, loc);
	return t;
}

