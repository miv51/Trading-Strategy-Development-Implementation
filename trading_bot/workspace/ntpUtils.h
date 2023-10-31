
//suppose to be network time protocol but ended up settling on system time

#ifndef NTP_UTILS_H
#define NTP_UTILS_H

#include "exceptUtils.h"

#include <stdexcept>
#include <ctime>

#ifdef _WIN32
#define localtime_pa(local_time, sec_since_epoch) localtime_s(&local_time, &sec_since_epoch) != 0 //platform agnostic localtime function
#else
#define localtime_pa(local_time, sec_since_epoch) localtime_r(&sec_since_epoch, &local_time) == nullptr //platform agnostic localtime function
#endif

class ntpClient
{
public:
	ntpClient();

	void update();
	void getPastDate(char(&)[11], int); //get the past date some number of days ago
	void getCurrentTime(char(&)[9]); //get the current time in the following format : HH:MM:SS

	time_t getSecondsSinceEpoch(int, int, int, int); //get the seconds since the epoch for a specified time of day (days_foward, hour, minute, second)

	time_t sec_since_epoch_trunc_day; //seconds since the epoch truncated to midnight of today
	int iso_weekday; //integer representing the weekday - sunday == 0, saturday == 6, etc...

	char date[11];
};

#endif
