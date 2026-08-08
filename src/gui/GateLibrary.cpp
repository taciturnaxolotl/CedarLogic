/*****************************************************************************
   Project: CEDAR Logic Simulator

   GateLibrary: process-wide gate-library accessor. See GateLibrary.h.
*****************************************************************************/

#include "GateLibrary.h"

GateLibrary& gateLibrary() {
	static GateLibrary instance;
	return instance;
}
