// Shim for the trimmed Skia dist (Workstream G).
//
// Skia's public docs/SkPDFDocument.h does `#include "src/base/SkTime.h"`, but
// the header no longer uses anything from it -- SkPDF::Metadata carries its own
// DateTime struct. The CI-built dist ships only public headers (no src/ tree),
// so that stray include fails to resolve. This empty shim satisfies it.
//
// A full Skia checkout (system/Nix builds) puts the real src/base/SkTime.h on
// the include path ahead of this shim, so the real header wins there.

#ifndef CL_SKIA_SHIM_SKTIME_H
#define CL_SKIA_SHIM_SKTIME_H
#endif
