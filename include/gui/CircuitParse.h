/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   CircuitParse: uses XMLParser to load and save user circuit files.
*****************************************************************************/

#ifndef CIRCUITPARSE_H_
#define CIRCUITPARSE_H_

#include <string>
#include <vector>
#include "logic_values.h"
using namespace std;

class GUICanvas;
class XMLParser;
namespace cl { struct CircuitFile; struct WireInstance; }

// used for parsing inputs and outputs
class gateConnector {
public:
	string connectionID;
	std::vector<IDType> wireIds;
};

// holds a parameter, whether gui or logic
class parameter {
public:
	parameter(string x, string y, bool nIsGUI) { paramName = x; paramValue = y; isGUI = nIsGUI; };
	string paramName;
	string paramValue;
	bool isGUI;
};

// Class CircuitParse:
//	Uses XMLParser to read and write user circuit files
class CircuitParse {
public:
	CircuitParse(string, vector< GUICanvas* >);
	CircuitParse(GUICanvas*);
	virtual ~CircuitParse();
	
	void loadFile(string);
	//JV - Changed to return new canvases
	vector<GUICanvas*> parseFile();
	bool saveCircuit(string, vector< GUICanvas* >, unsigned int currPage = 0);
	// Save the v3 S-expression format (built from the GUI via the format model).
	bool saveCircuitV3(string, vector< GUICanvas* >, unsigned int currPage = 0);
	// Save in v1.x compatible format (no version tag, no sentinel, single wire IDs)
	bool saveCircuitLegacy(string, vector< GUICanvas* >, unsigned int currPage = 0);
	// Get detailed error message from last save operation
	string getLastError() const { return lastError; }

private:
	XMLParser* mParse;
	string fileName;
	string lastError;  // Detailed error message from last save operation

	vector< GUICanvas* > gCanvases;
	GUICanvas* gCanvas;

	// Takes the pieces of gate info found in parseFile and implements them
	void parseGateToSend(string type, string ID, string position, vector < gateConnector > &inputs, vector < gateConnector > &outputs, vector < parameter > &params);
	// Build the GUI (gates, wires, routing) from a parsed circuit model.
	void applyCircuitFile(const cl::CircuitFile &cf);
	// Rebuild one wire's segment tree from the model and set it on the guiWire.
	void applyWireShape(const cl::WireInstance &wire);
	// Write text to a file, setting lastError and returning false on failure.
	bool writeToFile(const string &filename, const string &text);
};

#endif /*CIRCUITPARSE_H_*/
