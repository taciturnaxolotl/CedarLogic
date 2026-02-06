
#pragma once
#include <string>

// Get version major, minor, and build date in lexicographically sortable order.
// It may seem silly to use date instead of version number, but I know we'll forget
// to manually update the version number.
// Besides, the version number is still the primary sort because it comes first in the
// VERSION_NUMBER_STRING anyway.
std::string VERSION_NUMBER_STRING();

// Get just the version number (e.g., "2.3.8")
std::string VERSION_NUMBER();

#ifdef _WIN32
// Get version as wide string for Windows APIs
std::wstring VERSION_NUMBER_W();
#endif

// Get title bar text.
std::string VERSION_TITLE();

// Get about dialog text.
std::string VERSION_ABOUT_TEXT();