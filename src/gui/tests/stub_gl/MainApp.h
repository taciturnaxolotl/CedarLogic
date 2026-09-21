// Shadows the real MainApp.h for tests that link gl_defs.cpp. That file needs
// nothing from wxWidgets -- it is 35 lines of GLPoint2f arithmetic -- but still
// carries a DECLARE_APP, which would otherwise drag the whole GUI in.
#ifndef CEDAR_TEST_STUB_MAINAPP_H
#define CEDAR_TEST_STUB_MAINAPP_H
#define DECLARE_APP(x)
#endif
