// Shared byte-pattern fixtures. Two matchers must agree on every row:
//   - tests/host/pattern_parity_tests.cpp runs them through Util/PatternMatch.h,
//     the matcher exu.dll uses at runtime (BuildValidation, SignatureResolver);
//   - tools/test_bzr_qualification.py runs them through qualify_bzr_build.py,
//     the offline qualifier that generates the runtime build profile.
// A pattern the qualifier accepts must be the pattern the DLL accepts, or a
// build can pass qualification and then fail (or pass) the runtime gate on
// different grounds.
//
// PARITY_CASE(name, IDA-style pattern, haystack hex, every match offset)
// Offsets are comma-separated, in ascending order, overlapping matches
// included; "" means no match. Keep each case on one line: the Python side
// reads them with a line-anchored regex.

PARITY_CASE("exact literal, unique", "55 8B EC", "90 55 8B EC C3", "1")
PARITY_CASE("no match", "12 34", "55 8B EC", "")
PARITY_CASE("pattern equals haystack", "55 8B", "55 8B", "0")
PARITY_CASE("haystack shorter than pattern", "55 8B EC 90", "55 8B EC", "")
PARITY_CASE("empty haystack", "55", "", "")
PARITY_CASE("?? wildcard, ambiguous", "55 ?? EC", "55 11 EC 55 22 EC", "0,3")
PARITY_CASE("? and ?? are the same wildcard", "55 ? EC", "55 11 EC 55 22 EC", "0,3")
PARITY_CASE("leading wildcard", "?? 8B EC", "55 8B EC", "0")
PARITY_CASE("leading wildcard cannot start before the data", "?? 55 8B", "55 8B EC", "")
PARITY_CASE("trailing wildcard at the last byte", "C3 ??", "90 90 C3 90", "2")
PARITY_CASE("trailing wildcards never run past the end", "C3 ?? ??", "90 90 C3 90", "")
PARITY_CASE("all wildcards match every full window", "?? ??", "01 02 03", "0,1")
PARITY_CASE("overlapping matches are all counted", "AA AA", "AA AA AA", "0,1")
PARITY_CASE("0xFF is a literal, not a wildcard", "FF", "00 FE FF", "2")
PARITY_CASE("0x00 is a literal, not a wildcard", "00 01", "01 01 00 01", "2")
PARITY_CASE("regex metacharacters are literal bytes", "2E 2A 3F 5C 5B", "2E 2A 3F 5C 5B 41 42", "0")
PARITY_CASE("a metacharacter does not act as a wildcard", "2E", "41 42 2E", "2")
PARITY_CASE("a wildcard matches a newline byte", "?? 0A", "0A 0A 0A", "0,1")
PARITY_CASE("mid-pattern wildcard, two copies", "55 8B EC ?? 90", "90 90 55 8B EC 11 90 CC CC CC CC CC 55 8B EC 22 90 C3", "2,12")
PARITY_CASE("pinning the wildcard makes it unique", "55 8B EC 22 90", "90 90 55 8B EC 11 90 CC CC CC CC CC 55 8B EC 22 90 C3", "12")
PARITY_CASE("rel32 call operand wildcarded", "E8 ?? ?? ?? ?? 85 C0", "E8 10 20 30 40 85 C0 E8 00 00 00 00 85 C1", "0")
