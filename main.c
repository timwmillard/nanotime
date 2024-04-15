#include <stdio.h>
#include <stdlib.h>

#define NANOTIME_IMPLEMENTATION
#include "nanotime.h"

int main(void)
{

    Time *t;
    printf("Time test\n");

    char *weekday = time_WeekdayString(MONDAY);
    printf("weekday = %s\n", weekday);
    free(weekday);

    char *month = time_MonthString(JANUARY);
    printf("month = %s\n", month);
    free(month);

    TimeDuration t1 = 60 * HOUR;
    TimeDuration t2 = 2 * SECOND;
    TimeDuration t3 = time_DurationTruncate(t1, t2);
    printf("duration = %s\n", time_DurationString(t3));

    /* Time d = {0}; */
    /* TimeDate td = time_Date(d); */
    /* printf("month = %d\n", td.month); */

    return 0;
}

