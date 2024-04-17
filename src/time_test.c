#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

#include "time.h"

typedef struct {
	int Year;
	nt_Month Month;
	int Day;
	int Hour, Minute, Second; // 15:04:05 is 15, 4, 5.
	int Nanosecond; // Fractional second.
	nt_Weekday Weekday;
	int ZoneOffset; // seconds east of UTC, e.g. -7*60*60 for -0700
	char Zone[4]; // e.g., "MST"
} parsedTime;

typedef struct {
	int64_t seconds;
	parsedTime golden;
} TimeTest;


#define ARRAY_SIZE(xs) (sizeof((xs)) / sizeof((xs[0])))

TimeTest utctests[] = {
	{0, (parsedTime){1970, nt_JANUARY, 1, 0, 0, 0, 0, nt_THURSDAY, 0, "UTC"}},
	{1221681866, (parsedTime){2008, nt_SEPTEMBER, 17, 20, 4, 26, 0, nt_WEDNESDAY, 0, "UTC"}},
	{-1221681866, (parsedTime){1931, nt_APRIL, 16, 3, 55, 34, 0, nt_THURSDAY, 0, "UTC"}},
	{-11644473600, (parsedTime){1601, nt_JANUARY, 1, 0, 0, 0, 0, nt_MONDAY, 0, "UTC"}},
	{599529660, (parsedTime){1988, nt_DECEMBER, 31, 0, 1, 0, 0, nt_SATURDAY, 0, "UTC"}},
	{978220860, (parsedTime){2000, nt_DECEMBER, 31, 0, 1, 0, 0, nt_SUNDAY, 0, "UTC"}},
};

TimeTest nanoutctests[] = {
	{0, (parsedTime){1970, nt_JANUARY, 1, 0, 0, 0, 1e8, nt_THURSDAY, 0, "UTC"}},
	{1221681866, (parsedTime){2008, nt_SEPTEMBER, 17, 20, 4, 26, 2e8, nt_WEDNESDAY, 0, "UTC"}},
};

TimeTest localtests[] = {
	{0, (parsedTime){1969, nt_DECEMBER, 31, 16, 0, 0, 0, nt_WEDNESDAY, -8 * 60 * 60, "PST"}},
	{1221681866, (parsedTime){2008, nt_SEPTEMBER, 17, 13, 4, 26, 0, nt_WEDNESDAY, -7 * 60 * 60, "PDT"}},
	{2159200800, (parsedTime){2038, nt_JUNE, 3, 11, 0, 0, 0, nt_THURSDAY, -7 * 60 * 60, "PDT"}},
	{2152173599, (parsedTime){2038, nt_MARCH, 14, 1, 59, 59, 0, nt_SUNDAY, -8 * 60 * 60, "PST"}},
	{2152173600, (parsedTime){2038, nt_MARCH, 14, 3, 0, 0, 0, nt_SUNDAY, -7 * 60 * 60, "PDT"}},
	{2152173601, (parsedTime){2038, nt_MARCH, 14, 3, 0, 1, 0, nt_SUNDAY, -7 * 60 * 60, "PDT"}},
	{2172733199, (parsedTime){2038, nt_NOVEMBER, 7, 1, 59, 59, 0, nt_SUNDAY, -7 * 60 * 60, "PDT"}},
	{2172733200, (parsedTime){2038, nt_NOVEMBER, 7, 1, 0, 0, 0, nt_SUNDAY, -8 * 60 * 60, "PST"}},
	{2172733201, (parsedTime){2038, nt_NOVEMBER, 7, 1, 0, 1, 0, nt_SUNDAY, -8 * 60 * 60, "PST"}},
};

TimeTest nanolocaltests[] = {
	{0, (parsedTime){1969, nt_DECEMBER, 31, 16, 0, 0, 1e8, nt_WEDNESDAY, -8 * 60 * 60, "PST"}},
	{1221681866, (parsedTime){2008, nt_SEPTEMBER, 17, 13, 4, 26, 3e8, nt_WEDNESDAY, -7 * 60 * 60, "PDT"}},
};

bool same(nt_Time t, parsedTime *u)
{
	// Check aggregates.
	/* year, month, day := t.Date() */
	/* hour, min, sec := t.Clock() */
	/* name, offset := t.Zone() */
	struct nt_Date date = nt_TimeDate(t);
	struct nt_Clock clock = nt_TimeClock(t);
	struct nt_TimeZone zone = nt_TimeZone(t);
/* struct nt_TimeZone nt_TimeZone(nt_Time t) */

	if (date.year != u->Year || date.month != u->Month || date.day != u->Day ||
		clock.hour != u->Hour || clock.min != u->Minute || clock.sec != u->Second ||
		zone.name != u->Zone || zone.offset != u->ZoneOffset) {
		return false;
	}
	// Check individual entries.
	return nt_TimeYear(t) == u->Year &&
		nt_TimeMonth(t) == u->Month &&
		nt_TimeDay(t) == u->Day &&
		nt_TimeHour(t) == u->Hour &&
		nt_TimeMinute(t) == u->Minute &&
		nt_TimeSecond(t) == u->Second &&
		nt_TimeNanosecond(t) == u->Nanosecond &&
		nt_TimeWeekday(t) == u->Weekday;
}

typedef struct {
    int t;
} T;

void errorf(T *t, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

void TestSecondsToUTC(T *t)
{
	/* for _, test := range utctests { */
    for (int i = 0; i < ARRAY_SIZE(utctests); i++) {
        TimeTest test = utctests[i];
		int64_t sec = test.seconds;
		parsedTime *golden = &test.golden;
		nt_Time tm = nt_TimeUTC(nt_Unix(sec, 0));
		int64_t newsec = nt_TimeUnix(tm);
		if (newsec != sec) {
			errorf(t, "SecondsToUTC(%d).Seconds() = %d", sec, newsec);
		}
		if (!same(tm, golden)) {
            printf("%d] FAIL: ", i);
			/* errorf(t, "SecondsToUTC(%d):  // %s", sec, nt_TimeString(tm)); */
			errorf(t, "  want=%+v", *golden);
			/* errorf("  have=%v", nt_TimeFormat(tm, nt_RFC3339+" MST")); */
		} else {
            printf("%d] PASS\n", i);
        }
	}
}

int main(void)
{

    printf("*** Starting Testing ... ***\n");

    T *t = &(T){0};

    TestSecondsToUTC(t);

    printf("All Test PASSED\n");
    printf("*** Fishing Testing ... ***\n");

    return 0;
}

