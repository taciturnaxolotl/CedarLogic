// RenderStyle -- how a target paints the semantic scene (Workstream G).
//
// The Scene stores *what a thing is* (a wire and its logic state, a gate of a
// kind, a label), never a baked color. RenderStyle turns that semantics into
// paint, per output. Two styles, one scene:
//
//   screen(): color-by-state, live -- net colors, LED/7-seg hues, selection and
//             hover overlays, the grid. Built for a backlit display.
//   print():  white ground, solid black strokes at legible weight; no grid, no
//             selection; hierarchy by line weight (buses heavier than nets), not
//             hue; live signal state never reaches paper -- a printout is the
//             static schematic (topology), not a snapshot of the running sim.
//             A KiCad-style title block frames the page.
//
// This split is why unifying screen and export is not a recolored screenshot:
// export has historically printed poorly in black-and-white because meaning was
// encoded in color, which collapses to indistinguishable grey on paper. Here the
// print style is a first-class rendering intent.
//
// Header-only C++11; zero impact on the default build until draw sites consult a
// style (phase G1/G4).

#ifndef CL_RENDER_RENDERSTYLE_H
#define CL_RENDER_RENDERSTYLE_H

#include <string>

#include "Scene.h"

namespace cl {
namespace render {

// Logic value, carried semantically so the *style* -- not the draw site --
// picks the paint.
enum class WireState { Low, High, HiZ, Unknown, Conflict };

enum class GateKind { Generic, Input, Output, Junction, Label };

// KiCad-style page frame: title, sheet, revision, date, author, sheet N of M.
struct TitleBlock {
	bool enabled;
	std::string title;
	std::string sheet;
	std::string revision;
	std::string date;
	std::string author;
	int sheetNumber;
	int sheetCount;
	TitleBlock()
		: enabled(false), sheetNumber(1), sheetCount(1) {}
};

struct RenderStyle {
	bool showGrid;        // print: false
	bool showSelection;   // print: false (no hover/selection overlays)
	bool showLiveState;   // print: false (no signal colors on paper)
	bool colorOutput;     // print: false (black on white)
	TitleBlock titleBlock;

	RenderStyle()
		: showGrid(true), showSelection(true),
		  showLiveState(true), colorOutput(true) {}

	// Paint for a wire of the given state. On screen, color by state and widen
	// buses; on paper, always solid black with weight carrying the bus/net
	// hierarchy and state ignored (topology only).
	Stroke wire(WireState state, bool isBus) const {
		const float netWidth = isBus ? 3.0f : 1.0f;
		if (!colorOutput || !showLiveState) {
			// Print: black, hierarchy by weight.
			return Stroke(Color(0, 0, 0, 1), isBus ? 2.4f : 1.4f);
		}
		Color c;
		switch (state) {
			case WireState::High:     c = Color(0.0f, 0.9f, 0.0f); break;  // active
			case WireState::Low:      c = Color(0.0f, 0.35f, 0.0f); break; // idle
			case WireState::HiZ:      c = Color(0.0f, 0.0f, 0.9f); break;  // hi-Z
			case WireState::Conflict: c = Color(0.9f, 0.0f, 0.0f); break;  // conflict
			case WireState::Unknown:
			default:                  c = Color(0.5f, 0.5f, 0.5f); break;
		}
		return Stroke(c, netWidth);
	}

	// Stroke color for a gate body. Black on paper; near-black on screen.
	Color gateStroke(GateKind /*kind*/) const {
		if (!colorOutput) return Color(0, 0, 0, 1);
		return Color(0.05f, 0.05f, 0.05f, 1);
	}

	// Page/canvas background.
	Color background() const {
		return colorOutput ? Color(1, 1, 1, 1) : Color(1, 1, 1, 1);
	}

	// The live, color-by-state screen style.
	static RenderStyle screen() {
		return RenderStyle();  // defaults are the screen style
	}

	// The static, black-and-white, print-legible schematic style.
	static RenderStyle print() {
		RenderStyle s;
		s.showGrid = false;
		s.showSelection = false;
		s.showLiveState = false;
		s.colorOutput = false;
		s.titleBlock.enabled = true;
		return s;
	}
};

}  // namespace render
}  // namespace cl

#endif  // CL_RENDER_RENDERSTYLE_H
