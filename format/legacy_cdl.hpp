#pragma once

#include "circuit_file.hpp"
#include <string>

namespace cl {

// The on-disk format of a .cdl document, sniffed from its structure.
enum class SourceFormat { XmlV1, XmlV2, SexprV3, Unknown };

// Detect the format without a labeled version: (cedarlogic -> v3; a <throw_away>
// / <version> wrapper -> v2; a bare <circuit> -> v1.
SourceFormat detectFormat(const std::string &text);

// Read a legacy v1 or v2 XML .cdl into a CircuitFile. v1 and v2 share the same
// gate/wire structure; v2 just wraps it in a decoy + <throw_away> + <version>.
// formatVersion is set to 3 (the migration target). Throws std::runtime_error
// on malformed input.
CircuitFile readLegacyCdl(const std::string &text);

} // namespace cl
