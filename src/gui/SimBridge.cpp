/*****************************************************************************
   Project: CEDAR Logic Simulator

   SimBridge: process-wide GUI<->logic bridge accessor. See SimBridge.h.
*****************************************************************************/

#include "SimBridge.h"

SimBridge& simBridge() {
	static SimBridge instance;
	return instance;
}
