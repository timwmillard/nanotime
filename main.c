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

    Time d = {0};
    TimeDate td = time_Date(d);

    printf("month = %d\n", td.month);

    return 0;
}

