/*****************************************************************************
   Project: CEDAR Logic Simulator

   GateLibrary: the loaded gate libraries and the parser that fills them.

   Extracted from the MainApp God-singleton (Workstream C). Consumers reach it
   through the free accessor gateLibrary() instead of wxGetApp(), so the library
   data is a cohesive, independently-constructible unit rather than four public
   fields on the application object.
*****************************************************************************/

#pragma once

#include <map>
#include <string>
#include "LibraryParse.h"

class GateLibrary {
public:
	// The library currently shown in the palette.
	std::string currentLibrary;
	// Parser that reads gate-definition files into the maps below.
	LibraryParse libParser;
	// library name -> (gate name -> definition).
	std::map< std::string, std::map< std::string, LibraryGate > > libraries;
	// gate name -> the library that owns it (child -> parent).
	std::map< std::string, std::string > gateNameToLibrary;
};

// The process-wide gate library. Lives for the whole run, like the app it was
// carved out of; constructed on first use.
GateLibrary& gateLibrary();
