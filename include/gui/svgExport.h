/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.

   svgExport: SVG export functionality for circuits
*****************************************************************************/

#ifndef SVGEXPORT_H_
#define SVGEXPORT_H_

#include <string>
#include <sstream>
#include <fstream>

class GUICanvas;

class SVGExporter {
public:
    // Export the canvas to an SVG file
    // Parameters:
    //   canvas - the canvas to export
    //   filename - output SVG file path
    //   showGrid - whether to include grid lines
    //   noColor - if true, render as black line drawings (for printing)
    //   scale - scale factor for the output (similar to resolution multiplier)
    static bool exportToSVG(GUICanvas* canvas, const std::string& filename,
                           bool showGrid, bool noColor, float scale = 1.0);

private:
    // Helper functions for SVG generation
    static std::string getSVGHeader(float width, float height, float viewX, float viewY,
                                   float viewWidth, float viewHeight);
    static std::string getSVGFooter();
    static std::string getWireColorSVG(int state, bool noColor);
    static std::string escapeXML(const std::string& str);
};

#endif // SVGEXPORT_H_
