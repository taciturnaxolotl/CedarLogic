# Vendored libavoid

Object-avoiding orthogonal connector routing, from the Adaptagrams project.
Used by CedarLogic's wire router (Workstream H, phase 3.2) for obstacle-aware
routing.

- Upstream: https://github.com/mjwybrow/adaptagrams (`cola/libavoid/`)
- Commit: `840ebcff20dbba36ad03a2160edf7cbaf9859984`
- License: LGPL 2.1-or-later (see `LICENSE.LGPL`), compatible with CedarLogic's GPL v3.

Only the library sources/headers are vendored — upstream build files
(Makefile.am, .vcxproj, Doxyfile, tests/, doc/) are omitted; CedarLogic builds
these sources via its own CMake target (`avoid`). To update, re-copy
`cola/libavoid/*.{cpp,h}` from the pinned commit and bump the hash above.
