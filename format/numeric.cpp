#include "numeric.hpp"

#include <cerrno>
#include <cstdlib>
#include <stdexcept>

namespace cl {

namespace {

// Everything after the number must be whitespace, or the text is not a number.
bool onlySpaceLeft(const char *p) {
	for (; *p; ++p)
		if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') return false;
	return true;
}

[[noreturn]] void bad(const std::string &text, const std::string &what, const char *why) {
	throw std::runtime_error("circuit file: " + std::string(why) + " \"" + text +
	                         "\" in " + what);
}

} // namespace

double parseDouble(const std::string &text, const std::string &what) {
	errno = 0;
	char *end = nullptr;
	const double v = std::strtod(text.c_str(), &end);
	if (end == text.c_str() || !onlySpaceLeft(end)) bad(text, what, "malformed number");
	if (errno == ERANGE) bad(text, what, "number out of range");
	return v;
}

long parseLong(const std::string &text, const std::string &what) {
	errno = 0;
	char *end = nullptr;
	const long v = std::strtol(text.c_str(), &end, 10);
	if (end == text.c_str() || !onlySpaceLeft(end)) bad(text, what, "malformed integer");
	if (errno == ERANGE) bad(text, what, "integer out of range");
	return v;
}

} // namespace cl
