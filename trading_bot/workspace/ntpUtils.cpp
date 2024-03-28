
#include "ntpUtils.h"

ntpClient::ntpClient() { update(); }

void ntpClient::update()
{
	/*
	it would be better to get the UNIX time from an ntp server
	the system time can be a few seconds off the actual time but for this bot its not super important
	*/

	time_t sec_since_epoch = time(nullptr);

	if (sec_since_epoch <= 0) throw exceptions::exception("Could not retrieve current UNIX time.");

	tm local_time;

	if (localtime_pa(local_time, sec_since_epoch)) throw exceptions::exception("Could not retrieve current local time.");

	local_time.tm_hour = 0;
	local_time.tm_min = 0;
	local_time.tm_sec = 0;

	sec_since_epoch_trunc_day = mktime(&local_time);

	if (local_time.tm_isdst < 0) throw exceptions::exception("Could not retrieve information about daylight savings.");

	is_daylight_savings = local_time.tm_isdst;
	iso_weekday = local_time.tm_wday;

	std::strftime(date, sizeof(date), "%Y-%m-%d", &local_time);

	date[10] = '\0';
}

void ntpClient::getPastDate(char(&buffer)[11], int days)
{
	tm local_time;

	localtime_pa(local_time, sec_since_epoch_trunc_day);

	// Subtract days from the current date
	local_time.tm_mday -= days;

	// Normalize the date to handle negative day values
	mktime(&local_time);

	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &local_time);

	buffer[10] = '\0';
}

void ntpClient::getCurrentTime(char(&buffer)[9])
{
	/*
	it would be better to get the UNIX time from an ntp server
	the system time can be a few seconds off the actual time but for this bot its not super important
	*/

	time_t sec_since_epoch = time(nullptr);

	if (sec_since_epoch <= 0) throw exceptions::exception("Could not retrieve current UNIX time.");

	tm local_time;

	if (localtime_pa(local_time, sec_since_epoch)) throw exceptions::exception("Could not retrieve current local time.");

	std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local_time);

	buffer[8] = '\0';
}

time_t ntpClient::getSecondsSinceEpoch(int days_foward, int hour, int minute, int second)
{
	/*
	it would be better to get the UNIX time from an ntp server
	the system time can be a few seconds off the actual time but for this bot its not super important
	*/

	time_t sec_since_epoch = time(nullptr);

	if (sec_since_epoch <= 0) throw exceptions::exception("Could not retrieve current UNIX time.");

	tm local_time;

	if (localtime_pa(local_time, sec_since_epoch)) throw exceptions::exception("Could not retrieve current local time.");

	local_time.tm_mday += days_foward;

	local_time.tm_hour = hour;
	local_time.tm_min = minute;
	local_time.tm_sec = second;

	return mktime(&local_time);
}
