#pragma once

#include "circuit_file.hpp"
#include <string>

namespace cl {

// Serialize a CircuitFile to the v3 S-expression text form.
std::string writeCircuitFile(const CircuitFile &cf);

// Parse v3 S-expression text into a CircuitFile. Throws std::runtime_error on
// malformed input or an unsupported formatVersion.
CircuitFile readCircuitFile(const std::string &text);

} // namespace cl
