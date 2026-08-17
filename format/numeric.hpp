#pragma once

#include <string>

namespace cl {

// Number parsing for both .cdl readers.
//
// The readers used to call std::stod / std::stoi directly, which threw
// std::invalid_argument / std::out_of_range -- a different exception family
// from every other error here, carrying messages like "stod: no conversion"
// that ended up in front of users. These throw std::runtime_error with the
// same shape as the rest of the format errors, and they name what failed.
//
// They are also stricter than std::stod: trailing junk is an error rather than
// a value. "3abc" is a malformed coordinate, not 3.

// `what` is the context shown in the message, e.g. "(at ...)" or "<position>".
double parseDouble(const std::string &text, const std::string &what);
long parseLong(const std::string &text, const std::string &what);

// The nesting limit both parsers enforce. A well-formed document reaches 5
// levels in v3 and 7 in v1/v2, and neither grows with circuit size: gates,
// pages, and wire segments widen a level, they never add one. The cap exists
// because both parsers recurse per level, so an unbounded file is a stack
// overflow waiting to happen.
constexpr int kMaxDepth = 64;

} // namespace cl
