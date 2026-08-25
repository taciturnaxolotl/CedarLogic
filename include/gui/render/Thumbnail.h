/*****************************************************************************
   Project: CEDAR Logic Simulator

   Thumbnail: fitting one gate into a square tile.
*****************************************************************************/

// The palette draws every gate in the library at tile size, and it has to look
// the same wherever the palette is -- a wx panel or a grid of canvases in a
// browser. So the arithmetic that frames a gate lives here rather than in
// either, and both ask for the same transform before calling the same
// drawToScene.

#ifndef CL_RENDER_THUMBNAIL_H
#define CL_RENDER_THUMBNAIL_H

#include "klsBBox.h"
#include "render/Scene.h"

namespace cl {
namespace render {

// Fit `box` into a `size`-square tile: half a world unit of padding, then
// letterboxed on the shorter axis so the gate keeps its proportions.
inline Transform thumbnailTransform(const klsBBox& box, int size) {
	// minCorner is (left, top) and maxCorner is (right, bottom), so y decreases
	// from min to max -- the world has y up and the tile has y down.
	const double minX = box.getLeft() - 0.5;
	const double minY = box.getTop() + 0.5;
	const double maxX = box.getRight() + 0.5;
	const double maxY = box.getBottom() - 0.5;

	double mapWidth = maxX - minX;
	double mapHeight = minY - maxY;
	if (mapWidth <= 0.0) mapWidth = 1.0;
	if (mapHeight <= 0.0) mapHeight = 1.0;

	double left = minX, right = maxX;
	double top = minY, bottom = maxY;
	if (mapWidth >= mapHeight) {
		const double pad = 0.5 * (mapWidth - mapHeight);
		top += pad;
		bottom -= pad;
	} else {
		const double pad = 0.5 * (mapHeight - mapWidth);
		left -= pad;
		right += pad;
	}

	Transform t;
	t.a = (float)(size / (right - left));
	t.c = 0.0f;
	t.e = (float)(-left * size / (right - left));
	t.b = 0.0f;
	t.d = (float)(-size / (top - bottom));
	t.f = (float)(top * size / (top - bottom));
	return t;
}

}  // namespace render
}  // namespace cl

#endif  // CL_RENDER_THUMBNAIL_H
