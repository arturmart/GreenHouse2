#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace tools {

struct UnixMs {
    long long value = 0;

    UnixMs() = default;
    explicit UnixMs(long long v) : value(v) {}
};

struct DateTime {
    int year{};
    int month{};
    int day{};

    int hour{};
    int minute{};
    int second{};
    int millisecond{};

    long long unixMs{};
};

// ------------------------------------------------------------
// get current unix ms
// ------------------------------------------------------------
inline long long nowUnixMs() {
    using namespace std::chrono;

    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()
    ).count();
}

// ------------------------------------------------------------
// convert unix ms -> DateTime
// ------------------------------------------------------------
inline DateTime fromUnixMs(long long unixMs) {
    using namespace std::chrono;

    DateTime dt;
    dt.unixMs = unixMs;

    const auto tp = system_clock::time_point{milliseconds(unixMs)};
    const std::time_t tt = system_clock::to_time_t(tp);

    std::tm local_tm{};

#ifdef _WIN32
    localtime_s(&local_tm, &tt);
#else
    localtime_r(&tt, &local_tm);
#endif

    dt.year  = local_tm.tm_year + 1900;
    dt.month = local_tm.tm_mon + 1;
    dt.day   = local_tm.tm_mday;

    dt.hour   = local_tm.tm_hour;
    dt.minute = local_tm.tm_min;
    dt.second = local_tm.tm_sec;

    dt.millisecond = static_cast<int>(unixMs % 1000);
    if (dt.millisecond < 0) dt.millisecond += 1000;

    return dt;
}

// ------------------------------------------------------------
// convert DateTime -> string
// ------------------------------------------------------------
inline std::string toString(const DateTime& dt) {
    std::ostringstream oss;

    oss << std::setfill('0')
        << std::setw(4) << dt.year << '-'
        << std::setw(2) << dt.month << '-'
        << std::setw(2) << dt.day << ' '
        << std::setw(2) << dt.hour << ':'
        << std::setw(2) << dt.minute << ':'
        << std::setw(2) << dt.second << '.'
        << std::setw(3) << dt.millisecond;

    return oss.str();
}

// ------------------------------------------------------------
// unixMs -> formatted string
// ------------------------------------------------------------
inline std::string unixMsToString(long long unixMs) {
    return toString(fromUnixMs(unixMs));
}

} // namespace tools