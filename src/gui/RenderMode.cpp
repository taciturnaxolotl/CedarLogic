#include "RenderMode.h"

RenderMode& renderMode() {
	static RenderMode instance;
	return instance;
}
