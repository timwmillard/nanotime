#include <stdio.h>
#include <stdlib.h>

#define NANOTIME_IMPLEMENTATION
#include "nanotime.h"

int main(void)
{

    /* nt_Time *t; */
    /* printf("Time test\n"); */

    /* char *weekday = nt_WeekdayString(nt_MONDAY); */
    /* printf("weekday = %s\n", weekday); */
    /* free(weekday); */

    /* char *month = nt_MonthString(nt_JANUARY); */
    /* printf("month = %s\n", month); */
    /* free(month); */

    /* nt_Duration t1 = 60 * nt_HOUR; */
    /* nt_Duration t2 = 2 * nt_SECOND; */
    /* nt_Duration t3 = nt_DurationTruncate(t1, t2); */
    /* printf("duration = %s\n", nt_DurationString(t3)); */

    /* nt_Time d = {0}; */
    /* nt_Date td = nt_Date(d); */
    /* printf("month = %d\n", td.month); */
    
    nt_init();

    nt_Time now = nt_Now();
    printf("day = %d\n", nt_TimeDay(now));
    printf("month = %d\n", nt_TimeMonth(now));
    printf("year = %d\n", nt_TimeYear(now));
    printf("hour = %d\n", nt_TimeHour(now));
    printf("min = %d\n", nt_TimeMinute(now));
    printf("sec = %d\n", nt_TimeSecond(now));


    return 0;
}

