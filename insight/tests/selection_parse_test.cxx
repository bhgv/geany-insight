// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the production parser and selection serialization without GTK.
#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
#include "../../scintilla/src/Selection.cxx"

void Scintilla::Internal::Platform::Assert(const char *, const char *, int) noexcept {
	std::abort();
}

template <typename T>
static void roundTrip(T expected) {
	const std::string text = std::to_string(expected) + ",tail";
	std::string_view remaining(text);
	T value = 7;
	ValueFromString(remaining, value);
	assert(value == expected);
	assert(remaining == ",tail");
}

template <typename T>
static void rejects(std::string_view text, const char *message) {
	const std::string_view original = text;
	T value = 7;
	bool failed = false;
	try {
		ValueFromString(text, value);
	} catch (const std::runtime_error &error) {
		failed = true;
		assert(std::string_view(error.what()) == message);
	}
	assert(failed && value == 7 && text == original);
}

int main() {
	for (const auto value : {INT64_MIN, INT64_MIN + 1, INT64_C(-1), INT64_C(0), INT64_C(1), INT64_MAX})
		roundTrip(value);
	roundTrip(std::numeric_limits<Sci::Position>::min());
	roundTrip(std::numeric_limits<Sci::Position>::max());
	roundTrip(std::numeric_limits<size_t>::max());
	roundTrip(std::numeric_limits<uint64_t>::max());
	roundTrip(std::numeric_limits<int16_t>::min());
	roundTrip(std::numeric_limits<int16_t>::max());
	roundTrip(std::numeric_limits<uint16_t>::max());
	for (const char *text : {"", "-", "+1", " 1", "\t1", "--1", "v1"})
		rejects<int64_t>(text, "from_chars failed.");
	rejects<size_t>("-1", "from_chars failed.");
	for (const char *text : {"9223372036854775808", "-9223372036854775809", "9999999999999999999999999999999"})
		rejects<int64_t>(text, "from_chars out of range.");
	rejects<uint64_t>("18446744073709551616", "from_chars out of range.");
	rejects<int16_t>("32768", "from_chars out of range.");
	rejects<int16_t>("-32769", "from_chars out of range.");
	rejects<uint16_t>("65536", "from_chars out of range.");
	std::string_view zero("-0000v3");
	int64_t value = 7;
	ValueFromString(zero, value);
	assert(value == 0 && zero == "v3");
	const char bounded[] = {'1', '2', '\0', '3'};
	std::string_view partial(bounded, sizeof bounded);
	ValueFromString(partial, value);
	assert(value == 12 && partial.size() == 2 && partial.front() == '\0');
	std::mt19937_64 random(20261022);
	for (unsigned n = 0; n < 10000; n++) {
		const int64_t positive = static_cast<int64_t>(random() % INT64_MAX);
		roundTrip(n & 1 ? -positive : positive);
		roundTrip(random());
	}
	for (const char *text : {"0", "17v4", "-1", "9223372036854775807"}) {
		// The last position is exercised only on 64-bit builds.
		if (sizeof(Sci::Position) < 8 && std::string_view(text) == "9223372036854775807")
			continue;
		std::string_view input(text);
		const SelectionPosition position(input);
		assert(input.empty() && position.ToString() == text);
	}
	for (const char *text : {"0", "0-8,12-16", "#1,0-8,12v2-16v3", "R0-8", "L0-8", "T0-8"}) {
		const Selection first(text);
		const Selection restored(first.ToString());
		assert(restored.ToString() == first.ToString());
	}
	std::cout << "Selection parsing: signed/unsigned limits, rejected signs/overflow, exact suffixes, 20000 random round trips and selection restoration passed without <charconv>\n";
}
