#include "date.h"

#define FEBRUARY		2
#define	STARTOFTIME		1970
#define SECDAY			86400L
#define SECYR			(SECDAY * 365)
#define	leapyear(year)		((year) % 4 == 0)
#define	days_in_year(a) 	(leapyear(a) ? 366 : 365)
#define	days_in_month(a) 	(month_days[(a) - 1])

static int month_days[12] = {	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

/*
 * This only works for the Gregorian calendar - i.e. after 1752 (in the UK)
 */
 /*计算公历星期*/
u8 GregorianDay(u8 yy, u8 mm, u8 dd)
{
	int leapsToDate;
	int lastYear;
	int day;
	int MonthOffset[] = { 0,31,59,90,120,151,181,212,243,273,304,334 };

	lastYear = yy - 1;

	/*计算从公元元年到计数的前一年之中一共经历了多少个闰年*/
	leapsToDate = lastYear/4 - lastYear/100 + lastYear/400;      

     /*如若计数的这一年为闰年，且计数的月份在2月之后，则日数加1，否则不加1*/
	if((yy%4 == 0) && ((yy%100 != 0) || (yy%400 == 0)) && (mm > 2)) 
	{
		/* We are past Feb. 29 in a leap year */
		day=1;
	} 
	else 
	{
		day=0;
	}

	day += lastYear*365 + leapsToDate + MonthOffset[mm-1] + dd; /*计算从公元元年元旦到计数日期一共有多少天*/

	// 强制周日到周六 对应1——7
	return (u8)(day%7 + 1);
}

/* Converts Gregorian date to seconds since 1970-01-01 00:00:00.
 * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
 * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
 *
 * [For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.]
 *
 * This algorithm was first published by Gauss (I think).
 *
 * WARNING: this function will overflow on 2106-02-07 06:28:16 on
 * machines were long is 32-bit! (However, as time_t is signed, we
 * will already get problems at other places on 2038-01-19 03:14:08)

 * ADD by fire：本函数输入时间为北京时间
 */
u32 mktimev(struct rtc_time tm)
{
	u32 uSec;
	if (0 >= (int) (tm.tm_mon -= 2)) 
	{	
		/* 1..12 -> 11,12,1..10 */
		tm.tm_mon += 12;		/* Puts Feb last since it has leap day */
		tm.tm_year -= 1;
	}

	uSec = (((
		(u32) (tm.tm_year/4 - tm.tm_year/100 + tm.tm_year/400 + 367*tm.tm_mon/12 + tm.tm_mday) +
			tm.tm_year*365 - 719499
	    )*24 + tm.tm_hour /* now have hours */
	  )*60 + tm.tm_min /* now have minutes */
	)*60 + tm.tm_sec;
	uSec -= 8*60*60;
	return uSec; /* finally seconds */ 
}

uint32_t mktimevGreenwich(struct rtc_time tm)
{
	// 转换时间戳，格林威治时间转换
	u32 uSec;
	if (0 >= (int) (tm.tm_mon -= 2)) 
	{	
		/* 1..12 -> 11,12,1..10 */
		tm.tm_mon += 12;		/* Puts Feb last since it has leap day */
		tm.tm_year -= 1;
	}

	uSec = (((
		(u32) (tm.tm_year/4 - tm.tm_year/100 + tm.tm_year/400 + 367*tm.tm_mon/12 + tm.tm_mday) +
			tm.tm_year*365 - 719499
	    )*24 + tm.tm_hour /* now have hours */
	  )*60 + tm.tm_min /* now have minutes */
	)*60 + tm.tm_sec;
	return uSec; /* finally seconds */ 
}

void to_tm(u32 tim, struct rtc_time * tm)
{
	register u32    i;
	register long   hms, day;

	day = tim / SECDAY;
	hms = tim % SECDAY;

	/* Hours, minutes, seconds are easy */
	tm->tm_hour = hms / 3600;
	tm->tm_min = (hms % 3600) / 60;
	tm->tm_sec = (hms % 3600) % 60;

	/* Number of years in days */ 
	/*算出当前年份，起始的计数年份为1970年*/
	for (i = STARTOFTIME; day >= days_in_year(i); i++) 
	{
		day -= days_in_year(i);
	}
	tm->tm_year = i;

	/* Number of months in days left */ 
	/*计算当前的月份*/
	if (leapyear(tm->tm_year))
	{
		days_in_month(FEBRUARY) = 29;
	}
	for (i = 1; day >= days_in_month(i); i++) 
	{
		day -= days_in_month(i);
	}
	days_in_month(FEBRUARY) = 28;
	tm->tm_mon = i;

	/* Days are what is left over (+1) from all that. */
	/*计算当前日期*/
	tm->tm_mday = day + 1;

	/*
	 * Determine the day of week
	 */
	tm->tm_wday = GregorianDay(tm->tm_year, tm->tm_mon, tm->tm_mday);
}

