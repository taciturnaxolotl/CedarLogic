#include "PaletteDrag.h"

PaletteDrag& paletteDrag() {
	static PaletteDrag instance;
	return instance;
}
