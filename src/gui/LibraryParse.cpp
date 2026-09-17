/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   LibraryParse: Uses XMLParser to parse library files
*****************************************************************************/

#include "LibraryParse.h"
#include "GateLibrary.h"
#include "wx/msgdlg.h"
#include "MainApp.h"

// Included for sin and cos in <circle> tags:
#include <cmath>

DECLARE_APP(MainApp)

LibraryParse::LibraryParse(const string& xml) {
	if (xml.empty()) {
		wxMessageBox("The gate library is missing from this build.",
		             "Error - Missing Gate Library", wxOK | wxICON_ERROR, NULL);
		return;
	}
	// The parser lives only as long as this call: once the gates are read it has
	// nothing left to say, and the stream it reads from is a local here anyway.
	istringstream x(xml);
	XMLParser xparse(&x, false);
	parseFile(xparse);
}

LibraryParse::LibraryParse() {
}

LibraryParse::~LibraryParse() {
}

// Added by Colin Broberg 11/16/16 -- need to make this a public function so that I can use it for dynamic gates
void LibraryParse::addGate(string libName, LibraryGate newGate) {
	gates[libName][newGate.gateName] = newGate;
}

void LibraryParse::parseFile( XMLParser &xparse ) {
	do { // Outer loop to parse all libraries
		// need to throw exception
		if (xparse.readTag() != "library") return;
		xparse.readTag();
		libName = xparse.readTagValue("name");
		xparse.readCloseTag();
		
		string hsName, hsType;
		float x1, y1;
		char dump;
		
		do {
			xparse.readTag();
			LibraryGate newGate;
			string temp = xparse.readTag();
			newGate.gateName = xparse.readTagValue(temp);
			xparse.readCloseTag();
			do {
				temp = xparse.readTag();

				if ( (temp == "input") || (temp == "output") ) {

					string hsType = temp; // The type is determined by the tag name.
					// Assign defaults:
					hsName = "";
					x1 = y1 = 0.0;
					string isInverted = "false";
					string logicEInput = "";
					int busLines = 1;
					
					do {
						temp = xparse.readTag();
						if (temp == "") break;
						if( temp == "name" ) {
							hsName = xparse.readTagValue("name");
							xparse.readCloseTag();
						} else if( temp == "point" ) {
							temp = xparse.readTagValue("point");
							istringstream iss(temp);
							iss >> x1 >> dump >> y1;
							xparse.readCloseTag(); //point
						} else if( temp == "inverted" ) {
							isInverted = xparse.readTagValue("inverted");
							xparse.readCloseTag();
						} else if( temp == "enable_input" ) {
							if( hsType == "output" ) { // Only outputs can have <enable_input> tags.
								logicEInput = xparse.readTagValue("enable_input");
							}
							xparse.readCloseTag();
						}
						else if (temp == "bus") {
							busLines = atoi(xparse.readTagValue("bus").c_str());
							xparse.readCloseTag();
						}

					} while (!xparse.isCloseTag(xparse.getCurrentIndex())); // end input/output

					newGate.hotspots.push_back( lgHotspot( hsName, (hsType == "input"), x1, y1, (isInverted == "true"), logicEInput, busLines));

					xparse.readCloseTag(); //input or output

				} else if (temp == "shape") {

					// Each <label_offset> block is one caption, numbered so the
					// renderer can keep its strokes together when the gate turns.
					int nextLabelGroup = 0;
					do {
						temp = xparse.readTag();
						if (temp == "") break;
						if( temp == "offset" || temp == "label_offset" ) {
							int labelGroup = (temp == "label_offset") ? nextLabelGroup++ : -1;
							float offX = 0.0, offY = 0.0;
							temp = xparse.readTag();
							if( temp == "point" ) {
								temp = xparse.readTagValue("point");
								xparse.readCloseTag();
								istringstream iss(temp);
								iss >> offX >> dump >> offY;
							} else {
								//barf
								break;
							}
	
							do {
								temp = xparse.readTag();
								if (temp == "") break;
								parseShapeObject( xparse, temp, &newGate, offX, offY, labelGroup );
							} while (!xparse.isCloseTag(xparse.getCurrentIndex())); // end offset
							xparse.readCloseTag();
						} else {
							parseShapeObject( xparse, temp, &newGate );
						}
					} while (!xparse.isCloseTag(xparse.getCurrentIndex())); // end shape
					xparse.readCloseTag();

				} else if (temp == "param_dlg_data") {

					// Parse the parameters for the params dialog.
					do {
						temp = xparse.readTag();
						if (temp == "") break;
						if( temp == "param" ) {
							string type = "STRING";
							string textLabel = "";
							string name = "";
							string logicOrGui = "GUI";
							float Rmin = -FLT_MAX, Rmax = FLT_MAX;
	
							do {
								temp = xparse.readTag();
								if (temp == "") break;
								if( temp == "type" ) {
									type = xparse.readTagValue("type");
									xparse.readCloseTag();
								} else if( temp == "label" ) {
									textLabel = xparse.readTagValue("label");
									xparse.readCloseTag();
								} else if( temp == "varname" ) {
									temp = xparse.readTagValue("varname");
									istringstream iss(temp);
									iss >> logicOrGui >> name;
									xparse.readCloseTag();
								} else if( temp == "range" ) {
									temp = xparse.readTagValue("range");
									istringstream iss(temp);
									iss >> Rmin >> dump >> Rmax;
									xparse.readCloseTag();
								}
							} while (!xparse.isCloseTag(xparse.getCurrentIndex())); // end param
							newGate.dlgParams.push_back( lgDlgParam( textLabel, name, type, (logicOrGui == "GUI"), Rmin, Rmax ) );
							xparse.readCloseTag();
						}
					} while (!xparse.isCloseTag(xparse.getCurrentIndex())); // end param_dlg_data
					xparse.readCloseTag();

				} else if (temp == "gui_type") {
					newGate.guiType = xparse.readTagValue("gui_type");
					xparse.readCloseTag();
				} else if (temp == "logic_type") {
					newGate.logicType = xparse.readTagValue("logic_type");
					xparse.readCloseTag();
				} else if (temp == "gui_param") {
					string paramName, paramVal;
					istringstream iss(xparse.readTagValue("gui_param"));
					iss >> paramName >> paramVal;
					newGate.guiParams[paramName] = paramVal;
					xparse.readCloseTag();
				} else if (temp == "logic_param") {
					string paramName, paramVal;
					istringstream iss(xparse.readTagValue("logic_param"));
					iss >> paramName >> paramVal;
					newGate.logicParams[paramName] = paramVal;
					xparse.readCloseTag();
				} else if (temp == "caption") {
					newGate.caption = xparse.readTagValue("caption");
					if (newGate.caption == "Inverter" && (time(0) % 1001 == 0)) { // Easter egg, rename inverters once in a while :)
						newGate.caption = "Santa Hat (Inverter)";
					}
					xparse.readCloseTag();
				}
			} while (!xparse.isCloseTag(xparse.getCurrentIndex())); // end gate
			gateLibrary().gateNameToLibrary[newGate.gateName] = libName;
			gateLibrary().libraries[libName][newGate.gateName] = newGate;
			gates[libName][newGate.gateName] = newGate;
			xparse.readCloseTag(); //gate
		} while (!xparse.isCloseTag(xparse.getCurrentIndex())); // end library
		xparse.readCloseTag(); // clear the close tag
	} while (true); // end file
}

// Parse one shape object out of the document, adding an offset if needed:
bool LibraryParse::parseShapeObject( XMLParser &xparse, string type, LibraryGate* newGate, double offX, double offY, int labelGroup ) {
	const bool isLabel = (labelGroup >= 0);
	float x1, y1, x2, y2;
	char dump;
	string temp;

	if( type == "line" ) {
		temp = xparse.readTagValue("line");
		xparse.readCloseTag();
		istringstream iss(temp);
		iss >> x1 >> dump >> y1 >> dump >> x2 >> dump >> y2;

		// Apply the offset:
		x1 += offX; x2 += offX;
		y1 += offY; y2 += offY;
		newGate->shape.push_back( lgLine( x1, y1, x2, y2, labelGroup) );
		return true;
	} else if( type == "arc" ) {
		// "arc cx,cy,radius,startDeg,sweepDeg" -- a structured curve kept whole so
		// the renderer strokes it smooth (see lgArc). Degrees from +Y, clockwise.
		temp = xparse.readTagValue("arc");
		xparse.readCloseTag();
		istringstream iss(temp);
		float cx = 0, cy = 0, radius = 1, startDeg = 0, sweepDeg = 360;
		iss >> cx >> dump >> cy >> dump >> radius >> dump >> startDeg >> dump >> sweepDeg;
		cx += offX; cy += offY;
		newGate->arcs.push_back( lgArc( cx, cy, radius, startDeg, sweepDeg, isLabel ) );
		return true;
	} else if( type == "circle" ) {
		temp = xparse.readTagValue("circle");
		xparse.readCloseTag();
		istringstream iss(temp);

		double radius = 1.0;
		long numSegs = 12;
		iss >> x1 >> dump >> y1 >> dump >> radius >> dump >> numSegs;
		// Apply the offset:
		x1 += offX; y1 += offY;

		// Keep the circle whole (Workstream G) rather than tessellating it to
		// lines here: Skia strokes it smooth, and the GL path reproduces this same
		// segs-gon at draw time (see guiGate). segs is preserved for that.
		newGate->circles.push_back( lgCircle( x1, y1, (float)radius, (int)numSegs, isLabel ) );
		return true;
	}
	
	return false; // Invalid type.
}

bool LibraryParse::getGate(string gateName, LibraryGate &lgGate) {
	map < string, string >::iterator findGate = gateLibrary().gateNameToLibrary.find(gateName);
	if (findGate == gateLibrary().gateNameToLibrary.end()) return false;
	map < string, LibraryGate >::iterator findVal = gates[findGate->second].find(gateName);
	if (findVal != gates[findGate->second].end()) lgGate = (findVal->second);
	return (findVal != gates[findGate->second].end());
}

// Return the logic type of a particular gate:
string LibraryParse::getGateLogicType( string gateName ) {
	map < string, string >::iterator findGate = gateLibrary().gateNameToLibrary.find(gateName);
	if (findGate == gateLibrary().gateNameToLibrary.end()) return "";
	if ( gates[findGate->second].find(gateName) == gates[findGate->second].end() ) return "";
	return gates[findGate->second][gateName].logicType;
}

// Return the gui type of a particular gate type:
string LibraryParse::getGateGUIType( string gateName ) {
	map < string, string >::iterator findGate = gateLibrary().gateNameToLibrary.find(gateName);
	if (findGate == gateLibrary().gateNameToLibrary.end()) return "";
	if ( gates[findGate->second].find(gateName) == gates[findGate->second].end() ) return "";
	return gates[findGate->second][gateName].guiType;
}
