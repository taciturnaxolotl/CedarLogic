/*****************************************************************************
   Project: CEDAR Logic Simulator

   PaletteDrag: transient palette->canvas gate-drag state. Extracted from the
   MainApp God-singleton (Workstream C); reach it via paletteDrag(). The palette
   sets the gate being dragged; the canvas reads it on drop. (Named PaletteDrag,
   not DragState, to avoid clashing with GUICanvas's drag-mode enum.)
*****************************************************************************/

#pragma once

#include <string>

class PaletteDrag {
public:
	std::string newGateToDrag;
	bool showDragImage = false;
};

PaletteDrag& paletteDrag();
