// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <limits>
#include <system_error>
#include <type_traits>
namespace slang { namespace compat {
struct ParseResult { const char* ptr; std::errc ec; };
// Integer-only from_chars semantics needed by slang; no whitespace or '+' prefix.
template<typename T>
ParseResult from_chars(const char* first, const char* last, T& value, int base = 10) {
    static_assert(std::is_integral<T>::value, "integer parser only");
    using U = typename std::make_unsigned<T>::type;
    const char* p = first;
    bool negative = false;
    if (p != last && *p == '-' && std::is_signed<T>::value) { negative = true; ++p; }
    const char* digits = p;
    U limit = static_cast<U>(std::numeric_limits<T>::max());
    if (negative) ++limit;
    U number = 0;
    bool overflow = false;
    if (base < 2 || base > 36) return {first, std::errc::invalid_argument};
    for (; p != last; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        unsigned digit = c >= '0' && c <= '9' ? c - '0' :
                         c >= 'a' && c <= 'z' ? c - 'a' + 10 :
                         c >= 'A' && c <= 'Z' ? c - 'A' + 10 : 36;
        if (digit >= static_cast<unsigned>(base)) break;
        if (number > limit / base || (number == limit / base && digit > limit % base))
            overflow = true;
        else if (!overflow) number = number * base + digit;
    }
    if (p == digits) return {first, std::errc::invalid_argument};
    if (overflow) return {p, std::errc::result_out_of_range};
    if (negative && number) value = static_cast<T>(-static_cast<T>(number - 1) - 1);
    else value = static_cast<T>(number);
    return {p, std::errc{}};
}
}}
