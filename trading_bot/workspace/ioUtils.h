
//custom input and output (io) utilities used to speed up data parsing

#ifndef IO_UTILS_H
#define IO_UTILS_H

#include <stdexcept>
#include <string>

template <typename dataType>
constexpr inline const char* typeToString() noexcept { return "unspecified"; }

template <>
constexpr inline const char* typeToString<int>() noexcept { return "int"; }

template <>
constexpr inline const char* typeToString<long long>() noexcept { return "long long"; }

template <>
constexpr inline const char* typeToString<double>() noexcept { return "double"; }

template <>
constexpr inline const char* typeToString<long double>() noexcept { return "long double"; }

template <>
constexpr inline const char* typeToString<float>() noexcept { return "float"; }

template <typename dataType>
constexpr inline void throwRuntimeError(const std::string& reason, const std::string& number)
{
    std::string message = reason;

    message.append(" ");
    message.append(typeToString<dataType>());
    message.append(" for the number ");
    message.append(number);
    message.append(".");

    throw std::runtime_error(message);
}

//roughly 3 times as fast as std::stod and std::stoll
template <typename dataType>
constexpr inline dataType convert(const std::string& number)
{
    dataType num = 0;
    const char* c = number.c_str();

    if (*c == '\0') return num;
    if (*c == '-')
    {
        while (*(++c) != '.' && *c != '\0')
        {
            if (*c < '0' || *c > '9') throwRuntimeError<dataType>("Invalid format for data type", number);
            if (10 * num + *c - '0' <= num && num != 0) throwRuntimeError<dataType>("Number is too large for data type", number);

            num = 10 * num + *c - '0';
        }

        if (*c == '.')
        {
            dataType factor = 1;

            while (*(++c) != '\0')
            {
                if (*c < '0' || *c > '9') throwRuntimeError<dataType>("Invalid format for data type", number);

                factor = factor * 0.1;
                num += factor * (*c - '0');
            }

            return -num;

        }

        if (*c == '\0') return -num;

        throwRuntimeError<dataType>("Invalid format for data type", number);
    }

    while (*c != '.' && *c != '\0')
    {
        if (*c < '0' || *c > '9') throwRuntimeError<dataType>("Invalid format for data type", number);
        if (10 * num + *c - '0' <= num && num != 0) throwRuntimeError<dataType>("Number is too large for data type", number);

        num = 10 * num + *(c++) - '0';
    }

    if (*c == '.')
    {
        dataType factor = 1;

        while (*(++c) != '\0')
        {
            if (*c < '0' || *c > '9') throwRuntimeError<dataType>("Invalid format for data type", number);

            factor = factor * 0.1;
            num += factor * (*c - '0');
        }

        return num;

    }

    if (*c == '\0') return num;

    throwRuntimeError<dataType>("Invalid format for data type", number);
}

//calculate current time of day in nanoseconds since midnight - convert the current hour, minute, second, and fraction of the current second
inline long long convertUTC(const std::string& timestamp)
{
    if (timestamp.size() < 19) throw std::runtime_error("Invalid format for UTC timestamp.");

    const char* c = timestamp.c_str() + 10;

    if (*(c++) != 'T') throw std::runtime_error("Invalid format for UTC timestamp.");
    if (*c < '0' || *c > '9') throw std::runtime_error("Invalid format for UTC timestamp.");
    if (*(c + 1) < '0' || *(c + 1) > '9') throw std::runtime_error("Invalid format for UTC timestamp.");

    if (*(c + 2) != ':') throw std::runtime_error("Invalid format for UTC timestamp.");
    if (*(c + 3) < '0' || *(c + 3) > '9') throw std::runtime_error("Invalid format for UTC timestamp.");
    if (*(c + 4) < '0' || *(c + 4) > '9') throw std::runtime_error("Invalid format for UTC timestamp.");

    if (*(c + 5) != ':') throw std::runtime_error("Invalid format for UTC timestamp.");
    if (*(c + 6) < '0' || *(c + 6) > '9') throw std::runtime_error("Invalid format for UTC timestamp.");
    if (*(c + 7) < '0' || *(c + 7) > '9') throw std::runtime_error("Invalid format for UTC timestamp.");

    //convert hours, minutes, and whole seconds (not the fractional part) to nanoseconds
    int64_t t = (10LL * (*c - '0') + *(c + 1) - '0') * 3600000000000LL + (10LL * (*(c + 3) - '0') + *(c + 4) - '0') * 60000000000LL\
        + (10LL * (*(c + 6) - '0') + *(c + 7) - '0') * 1000000000LL;

    //convert the fractional part of seconds to nanoseconds
    if (*(c += 8) == '.')
    {
        int64_t f = 100000000LL;

        while (*(++c) != 'Z' && *c != '\0')
        {
            if (*c < '0' || *c > '9') throw std::runtime_error("Invalid format for UTC timestamp.");

            t += f * (*c - '0');
            f = (f * 0xCCCCCCCDLL) >> 35; //roughly twice as fast as dividing by 10 - only works for 64-bit integers
        }
    }

    if (*c != 'Z') throw std::runtime_error("Invalid format for UTC timestamp.");

    return t;
}

#endif
