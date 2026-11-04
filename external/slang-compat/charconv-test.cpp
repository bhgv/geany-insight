// SPDX-License-Identifier: GPL-2.0-or-later
#include "CompatCharconv.h"
#include <cassert>
#include <cstdint>
#include <cstring>
int main() {
    using slang::compat::from_chars;
    int64_t s = 42; const char* min = "-9223372036854775808";
    auto r = from_chars(min, min + std::strlen(min), s);
    assert(r.ec == std::errc{} && s == INT64_MIN);
    const char* big = "9223372036854775808x";
    r = from_chars(big, big + std::strlen(big), s);
    assert(r.ec == std::errc::result_out_of_range && *r.ptr == 'x' && s == INT64_MIN);
    uint64_t u = 1; const char* max = "18446744073709551615";
    r = from_chars(max, max + std::strlen(max), u);
    assert(r.ec == std::errc{} && u == UINT64_MAX);
    const char* hex = "fF!"; r = from_chars(hex, hex + 3, u, 16);
    assert(r.ec == std::errc{} && u == 255 && *r.ptr == '!');
    for (const char* bad : {"", "-", "+1", " 1", "-1"}) {
        r = from_chars(bad, bad + std::strlen(bad), u);
        assert(r.ec == std::errc::invalid_argument && r.ptr == bad && u == 255);
    }
}
