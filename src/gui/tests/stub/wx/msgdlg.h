// Stands in for wxWidgets when building the gate-library test.
//
// LibraryParse reaches for wxWidgets in exactly one place -- a message box when
// the embedded gate library is missing -- and names the app class through
// DECLARE_APP. Standing both in lets the parser compile and run with no
// wxWidgets present, so the test can read the real shipped cl_gatedefs.xml on
// any CI runner. LibraryParse.cpp includes this header before MainApp.h, so
// claiming MainApp.h's include guard here keeps all of wxWidgets out.
#ifndef CEDAR_TEST_STUB_WX_MSGDLG_H
#define CEDAR_TEST_STUB_WX_MSGDLG_H

#define MAINAPP_H_   // suppress include/gui/MainApp.h, which pulls in wxWidgets
#define DECLARE_APP(x)
class MainApp;

#define wxOK 0x00000004
#define wxICON_ERROR 0x00000200

inline int wxMessageBox(const char *, const char * = 0, long = 0, void * = 0) {
	return 0;
}

#endif
