/*****************************************************************************
   Project: CEDAR Logic Simulator

   RenderMode: global rendering-mode flags. Extracted from the MainApp
   God-singleton (Workstream C; the code even noted doingBitmapExport
   "shouldn't be here"). Reach it via renderMode().
     doingBitmapExport - offscreen bitmap export in progress (skips GL display
                         lists / connection dots that don't survive it).
     headlessRender    - --render mode: load, dump a PNG, exit; suppress modals.
*****************************************************************************/

#pragma once

class RenderMode {
public:
	bool doingBitmapExport = false;
	bool headlessRender = false;
};

RenderMode& renderMode();
