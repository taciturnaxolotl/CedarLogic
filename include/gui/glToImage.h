#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/glcanvas.h>

class glImageCtx {
	int width, height;

#ifdef _WINDOWS
	wxBitmap theBM;
	wxMemoryDC *myDC;
	WXHDC theHDC;
    HGLRC hRC;
    HGLRC oldhRC;
    HDC oldDC;
#elif defined(__linux__) || defined(__APPLE__)
	GLuint fbo;
	GLuint renderTexture;
	GLint oldFBO;
#endif

public:
	glImageCtx(int width, int height, wxWindow *parent);
	wxImage getImage();
	~glImageCtx();
};
