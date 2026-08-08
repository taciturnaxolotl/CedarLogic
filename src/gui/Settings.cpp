/*****************************************************************************
   Project: CEDAR Logic Simulator

   Settings: process-wide settings accessor. See Settings.h.
*****************************************************************************/

#include "Settings.h"

Settings& appConfig() {
	static Settings instance;
	return instance;
}
