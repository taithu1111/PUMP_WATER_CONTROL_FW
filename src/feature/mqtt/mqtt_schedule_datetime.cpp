#include "mqtt_schedule_datetime.h"

#include <cstring>
#include <time.h>

namespace MqttScheduleDateTime {
namespace {

constexpr uint32_t UTC7_OFFSET_SECONDS = 7UL * 60UL * 60UL;

bool parseDigits(const char* text, size_t offset, size_t count, int& value) {
  value = 0;
  for (size_t i = 0; i < count; ++i) {
    const char digit = text[offset + i];
    if (digit < '0' || digit > '9') return false;
    value = value * 10 + digit - '0';
  }
  return true;
}

bool leapYear(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

uint8_t daysInMonth(int year, int month) {
  constexpr uint8_t DAYS[] = {31, 28, 31, 30, 31, 30,
                              31, 31, 30, 31, 30, 31};
  return month == 2 && leapYear(year) ? 29 : DAYS[month - 1];
}

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear =
      (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const unsigned dayOfEra =
      yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
}

}  // namespace

bool parseTimestamp(const char* timestamp, uint64_t& epoch) {
  if (timestamp == nullptr || strlen(timestamp) != 25 ||
      timestamp[4] != '-' || timestamp[7] != '-' ||
      timestamp[10] != 'T' || timestamp[13] != ':' ||
      timestamp[16] != ':' || strcmp(timestamp + 19, "+07:00") != 0)
    return false;

  int year = 0;
  int month = 0;
  int monthDay = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (!parseDigits(timestamp, 0, 4, year) ||
      !parseDigits(timestamp, 5, 2, month) ||
      !parseDigits(timestamp, 8, 2, monthDay) ||
      !parseDigits(timestamp, 11, 2, hour) ||
      !parseDigits(timestamp, 14, 2, minute) ||
      !parseDigits(timestamp, 17, 2, second) ||
      year < 1970 || year > 2099 || month < 1 || month > 12 ||
      monthDay < 1 || monthDay > daysInMonth(year, month) || hour > 23 ||
      minute > 59 || second > 59) return false;

  const int64_t localSeconds =
      daysFromCivil(year, static_cast<unsigned>(month),
                    static_cast<unsigned>(monthDay)) * 86400LL +
      hour * 3600LL + minute * 60LL + second;
  if (localSeconds < UTC7_OFFSET_SECONDS) return false;
  epoch = static_cast<uint64_t>(localSeconds - UTC7_OFFSET_SECONDS);
  return true;
}

void formatTimestamp(uint64_t epoch, char timestamp[26]) {
  const time_t localEpoch =
      static_cast<time_t>(epoch + UTC7_OFFSET_SECONDS);
  struct tm local = {};
  gmtime_r(&localEpoch, &local);
  snprintf(timestamp, 26, "%04d-%02d-%02dT%02d:%02d:%02d+07:00",
           local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
           local.tm_hour, local.tm_min, local.tm_sec);
}

}  // namespace MqttScheduleDateTime
