/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.

   svgExport: SVG export functionality for circuits
*****************************************************************************/

#include "svgExport.h"
#include "GUICanvas.h"
#include "guiGate.h"
#include "guiWire.h"
#include "MainApp.h"
#include "logic_values.h"
#include "wireSegment.h"
#include <iomanip>
#include <cmath>
#include <string>
#include <sstream>

// Helper to convert float to string with precision
static std::string floatToStr(float value, int precision = 2) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

// Helper to flip Y coordinate from OpenGL to SVG for world coordinates
// (OpenGL has Y up, SVG has Y down)
static float flipY(float y, float viewY, float viewHeight) {
    return viewY + (viewHeight - (y - viewY));
}

// Helper to flip local/relative Y coordinates (within transformed groups)
static float flipLocalY(float y) {
    return -y;
}

std::string SVGExporter::escapeXML(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        switch (c) {
            case '&':  result += "&amp;"; break;
            case '<':  result += "&lt;"; break;
            case '>':  result += "&gt;"; break;
            case '"':  result += "&quot;"; break;
            case '\'': result += "&apos;"; break;
            default:   result += c; break;
        }
    }
    return result;
}

std::string SVGExporter::getSVGHeader(float width, float height, float viewX, float viewY,
                                      float viewWidth, float viewHeight) {
    std::ostringstream oss;
    oss << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n";
    // ViewBox: Use regular coordinates since we flip Y individually
    oss << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        << "width=\"" << floatToStr(width) << "\" "
        << "height=\"" << floatToStr(height) << "\" "
        << "viewBox=\"" << floatToStr(viewX) << " " << floatToStr(viewY) << " "
        << floatToStr(viewWidth) << " " << floatToStr(viewHeight) << "\">\n";
    oss << "  <defs>\n";
    oss << "    <style type=\"text/css\">\n";
    oss << "      .gate-line { fill: none; stroke: black; stroke-width: 0.1; }\n";
    oss << "      .gate-selected { stroke-dasharray: 0.3,0.3; }\n";
    oss << "      .wire-line { fill: none; stroke-linecap: round; stroke-linejoin: round; }\n";
    oss << "      .wire-normal { stroke-width: 0.1; }\n";
    oss << "      .wire-bus { stroke-width: 0.4; }\n";
    oss << "      .wire-dot { fill: black; }\n";
    oss << "      .grid-line { stroke: #00000014; stroke-width: 0.05; }\n";
    oss << "    </style>\n";
    oss << "  </defs>\n";
    return oss.str();
}

std::string SVGExporter::getSVGFooter() {
    // Close the canvas group and the SVG
    return "  </g>\n</svg>\n";
}

std::string SVGExporter::getWireColorSVG(int state, bool noColor) {
    if (noColor) {
        return "stroke=\"black\"";
    }

    switch (state) {
        case CONFLICT:
            return "stroke=\"rgb(0,255,255)\""; // Cyan
        case UNKNOWN:
            return "stroke=\"rgb(77,77,255)\""; // Blue
        case HI_Z:
            return "stroke=\"rgb(0,199,0)\""; // Green
        case ONE:
            return "stroke=\"rgb(255,0,0)\""; // Red
        case ZERO:
        default:
            return "stroke=\"rgb(0,0,0)\""; // Black
    }
}

bool SVGExporter::exportToSVG(GUICanvas* canvas, const std::string& filename,
                              bool showGrid, bool noColor, float scale) {
    if (!canvas) return false;

    std::ofstream svgFile(filename);
    if (!svgFile.is_open()) return false;

    // Get canvas bounds
    wxSize canvasSize = canvas->GetClientSize();
    float viewZoom = canvas->getZoom();
    GLdouble panX, panY;
    canvas->getPan(panX, panY);

    // Calculate view dimensions in world coordinates
    float viewWidth = canvasSize.GetWidth() * viewZoom;
    float viewHeight = canvasSize.GetHeight() * viewZoom;
    // panX is left edge, panY is TOP edge in OpenGL (Y-up)
    // So bottom edge is panY - viewHeight
    float viewX = panX;
    float viewY = panY - viewHeight;

    // Output dimensions (scaled)
    float outputWidth = canvasSize.GetWidth() * scale;
    float outputHeight = canvasSize.GetHeight() * scale;

    // Write SVG header (no transform, we'll flip Y coordinates manually)
    svgFile << getSVGHeader(outputWidth, outputHeight, viewX, viewY, viewWidth, viewHeight);

    // Debug comment with values
    svgFile << "  <!-- Debug: viewX=" << viewX << " viewY=" << viewY
            << " viewWidth=" << viewWidth << " viewHeight=" << viewHeight
            << " panX=" << panX << " panY=" << panY << " -->\n";

    // White background
    svgFile << "  <rect x=\"" << floatToStr(viewX) << "\" y=\"" << floatToStr(viewY)
            << "\" width=\"" << floatToStr(viewWidth) << "\" height=\"" << floatToStr(viewHeight)
            << "\" fill=\"white\"/>\n";

    // Start the canvas group
    svgFile << "  <g id=\"canvas\">\n";

    // Draw grid if requested (inside the transformed canvas group)
    if (showGrid) {
        svgFile << "    <g id=\"grid\">\n";

        // Grid spacing - use same logic as the OpenGL renderer
        float gridSpacing = 1.0; // Base grid spacing in world units

        // Calculate grid bounds
        float gridLeft = floor(viewX / gridSpacing) * gridSpacing;
        float gridRight = ceil((viewX + viewWidth) / gridSpacing) * gridSpacing;
        float gridBottom = floor(viewY / gridSpacing) * gridSpacing;
        float gridTop = ceil((viewY + viewHeight) / gridSpacing) * gridSpacing;

        // Vertical lines
        for (float x = gridLeft; x <= gridRight; x += gridSpacing) {
            svgFile << "      <line x1=\"" << floatToStr(x) << "\" y1=\"" << floatToStr(flipY(viewY, viewY, viewHeight))
                    << "\" x2=\"" << floatToStr(x) << "\" y2=\"" << floatToStr(flipY(viewY + viewHeight, viewY, viewHeight))
                    << "\" class=\"grid-line\"/>\n";
        }

        // Horizontal lines
        for (float y = gridBottom; y <= gridTop; y += gridSpacing) {
            svgFile << "      <line x1=\"" << floatToStr(viewX) << "\" y1=\"" << floatToStr(flipY(y, viewY, viewHeight))
                    << "\" x2=\"" << floatToStr(viewX + viewWidth) << "\" y2=\"" << floatToStr(flipY(y, viewY, viewHeight))
                    << "\" class=\"grid-line\"/>\n";
        }

        svgFile << "    </g>\n";
    }

    // Draw gates
    svgFile << "    <g id=\"gates\">\n";
    std::unordered_map<unsigned long, guiGate*>* gateList = canvas->getGateList();
    for (auto& gatePair : *gateList) {
        guiGate* gate = gatePair.second;
        if (!gate) continue;

        float gateX, gateY;
        gate->getGLcoords(gateX, gateY);

        // Get gate's transformation matrix
        // We need to extract the transformed vertices
        std::string pathClass = "gate-line";
        if (gate->isSelected() && !noColor) {
            pathClass += " gate-selected";
        }

        svgFile << "      <g id=\"gate_" << gate->getID() << "\">\n";

        // Get gate parameters for rotation
        std::string angleStr = gate->getGUIParam("angle");
        float angle = 0;
        if (!angleStr.empty()) {
            std::istringstream iss(angleStr);
            iss >> angle;
        }

        // Get the vertices (they are in model space, pairs for lines)
        // We need to transform them through the gate's model matrix
        // Negate angle because Y-axis is flipped in SVG
        // Note: angle is already in degrees (used with glRotatef)
        std::ostringstream transform;
        transform << "translate(" << floatToStr(gateX) << "," << floatToStr(flipY(gateY, viewY, viewHeight)) << ")";
        if (angle != 0) {
            transform << " rotate(" << floatToStr(-angle) << ")";
        }

        // Export gate shape using actual vertices
        // Vertices are stored as pairs for GL_LINES
        const auto& vertices = gate->getVertices();

        svgFile << "        <g transform=\"" << transform.str() << "\">\n";

        // Draw all the lines that make up the gate
        // Vertices come in pairs (GL_LINES format)
        // Flip Y coordinates of vertices (these are local coordinates)
        for (size_t i = 0; i + 1 < vertices.size(); i += 2) {
            svgFile << "          <line x1=\"" << floatToStr(vertices[i].x)
                    << "\" y1=\"" << floatToStr(flipLocalY(vertices[i].y))
                    << "\" x2=\"" << floatToStr(vertices[i+1].x)
                    << "\" y2=\"" << floatToStr(flipLocalY(vertices[i+1].y))
                    << "\" class=\"" << pathClass << "\"/>\n";
        }

        // Special handling for KEYPAD gates (highlight selected key)
        if (gate->getGUIType() == "KEYPAD" && !noColor) {
            // Get the current output value to determine which key is selected
            std::string outputNum = gate->getLogicParam("OUTPUT_NUM");
            std::string outputBits = gate->getLogicParam("OUTPUT_BITS");

            if (!outputNum.empty() && !outputBits.empty()) {
                // Convert to hex value
                int intVal;
                std::istringstream(outputNum) >> intVal;
                std::ostringstream ossVal;
                for (int i = 2*sizeof(int) - 1; i >= 0; i--) {
                    ossVal << "0123456789ABCDEF"[((intVal >> i*4) & 0xF)];
                }

                int numBits;
                std::istringstream(outputBits) >> numBits;
                std::string currentValue = ossVal.str().substr(ossVal.str().size()-(numBits/4), (numBits/4));

                // Get the bounding box for this key
                std::string boxParam = "KEYPAD_BOX_" + currentValue;
                std::string clickBox = gate->getGUIParam(boxParam);

                if (!clickBox.empty()) {
                    // Parse the box coordinates
                    std::istringstream iss(clickBox);
                    float minx, miny, maxx, maxy;
                    char dump;
                    iss >> minx >> dump >> miny >> dump >> maxx >> dump >> maxy;

                    // Draw the highlight rectangle
                    svgFile << "          <!-- Keypad selected key highlight -->\n";
                    svgFile << "          <rect x=\"" << floatToStr(minx)
                            << "\" y=\"" << floatToStr(flipLocalY(maxy))
                            << "\" width=\"" << floatToStr(maxx - minx)
                            << "\" height=\"" << floatToStr(maxy - miny)
                            << "\" fill=\"rgba(0,102,255,0.3)\" stroke=\"none\"/>\n";
                }
            }
        }

        // Special handling for REGISTER gates (seven segment display)
        if (gate->getGUIType() == "REGISTER") {
            std::string valueBox = gate->getGUIParam("VALUE_BOX");
            std::string currentValue = gate->getLogicParam("CURRENT_VALUE");
            std::string unknownOutputs = gate->getLogicParam("UNKNOWN_OUTPUTS");

            if (!valueBox.empty() && !currentValue.empty()) {
                // Parse VALUE_BOX
                std::istringstream iss(valueBox);
                float boxX1, boxY1, boxX2, boxY2;
                char dump;
                iss >> boxX1 >> dump >> boxY1 >> dump >> boxX2 >> dump >> boxY2;

                float diffx = boxX2 - boxX1;
                float diffy = boxY2 - boxY1;

                // Convert current value to hex string
                int intVal;
                std::istringstream(currentValue) >> intVal;
                std::ostringstream ossVal;
                for (int i = 2*sizeof(int) - 1; i >= 0; i--) {
                    ossVal << "0123456789ABCDEF"[((intVal >> i*4) & 0xF)];
                }
                std::string hexValue = ossVal.str();

                // Determine color
                std::string segColor = (unknownOutputs == "true") ? "rgb(77,77,255)" : "rgb(255,0,0)";

                // Calculate number of digits to show
                std::string inputBits = gate->getLogicParam("INPUT_BITS");
                int numDigits = 1;
                if (!inputBits.empty()) {
                    std::istringstream(inputBits) >> numDigits;
                    numDigits = (int)ceil(((double)numDigits) / 4.0);
                    if (numDigits == 0) numDigits = 1;
                }

                // Get the rightmost digits
                if (hexValue.size() > (size_t)numDigits) {
                    hexValue = hexValue.substr(hexValue.size() - numDigits);
                }

                // Draw seven segments for each digit
                svgFile << "          <!-- Seven segment display -->\n";
                for (size_t currentDigit = 0; currentDigit < hexValue.size(); currentDigit++) {
                    char c = hexValue[currentDigit];
                    float baseX = boxX1 + (diffx * currentDigit);

                    // Top segment
                    if (c != '1' && c != '4' && c != 'B' && c != 'D') {
                        svgFile << "          <line x1=\"" << floatToStr(baseX + diffx*0.1875)
                                << "\" y1=\"" << floatToStr(flipLocalY(boxY1 + diffy*0.88462))
                                << "\" x2=\"" << floatToStr(baseX + diffx*0.8125)
                                << "\" y2=\"" << floatToStr(flipLocalY(boxY1 + diffy*0.88462))
                                << "\" stroke=\"" << segColor << "\" stroke-width=\"0.2\"/>\n";
                    }
                    // Middle segment
                    if (c != '0' && c != '1' && c != '7' && c != 'C') {
                        svgFile << "          <line x1=\"" << floatToStr(baseX + diffx*0.1875)
                                << "\" y1=\"" << floatToStr(flipLocalY(boxY1 + diffy*0.5))
                                << "\" x2=\"" << floatToStr(baseX + diffx*0.8125)
                                << "\" y2=\"" << floatToStr(flipLocalY(boxY1 + diffy*0.5))
                                << "\" stroke=\"" << segColor << "\" stroke-width=\"0.2\"/>\n";
                    }
                    // Bottom segment
                    if (c != '1' && c != '4' && c != '7' && c != '9' && c != 'A' && c != 'F') {
                        svgFile << "          <line x1=\"" << floatToStr(baseX + diffx*0.1875)
                                << "\" y1=\"" << floatToStr(flipLocalY(boxY1 + diffy*0.11538))
                                << "\" x2=\"" << floatToStr(baseX + diffx*0.8125)
                                << "\" y2=\"" << floatToStr(flipLocalY(boxY1 + diffy*0.11538))
                                << "\" stroke=\"" << segColor << "\" stroke-width=\"0.2\"/>\n";
                    }
                    // Top-left segment
                    if (c != '1' && c != '2' && c != '3' && c != '7' && c != 'D') {
                        svgFile << "          <line x1=\"" << floatToStr(baseX + diffx*0.1875)
                                << "\" y1=\"" << floatToStr(flipLocalY(boxY1 + diffy*0.88462))
                                << "\" x2=\"" << floatToStr(baseX + diffx*0.1875)
                                << "\" y2=\"" << floatToStr(flipLocalY(boxY1 + diffy*0.5))
                                << "\" stroke=\"" << segColor << "\" stroke-width=\"0.2\"/>\n";
                    }
                    // Top-right segment
                    if (c != '5' && c != '6' && c != 'B' && c != 'C' && c != 'E' && c != 'F') {
                        svgFile << "          <line x1=\"" << floatToStr(baseX + diffx*0.8125)
                                << "\" y1=\"" << floatToStr(flipLocalY(boxY1 + diffy*0.88462))
                                << "\" x2=\"" << floatToStr(baseX + diffx*0.8125)
                                << "\" y2=\"" << floatToStr(flipLocalY(boxY1 + diffy*0.5))
                                << "\" stroke=\"" << segColor << "\" stroke-width=\"0.2\"/>\n";
                    }
                    // Bottom-left segment
                    if (c != '1' && c != '3' && c != '4' && c != '5' && c != '7' && c != '9') {
                        svgFile << "          <line x1=\"" << floatToStr(baseX + diffx*0.1875)
                                << "\" y1=\"" << floatToStr(flipLocalY(boxY1 + diffy*0.11538))
                                << "\" x2=\"" << floatToStr(baseX + diffx*0.1875)
                                << "\" y2=\"" << floatToStr(flipLocalY(boxY1 + diffy*0.5))
                                << "\" stroke=\"" << segColor << "\" stroke-width=\"0.2\"/>\n";
                    }
                    // Bottom-right segment
                    if (c != '2' && c != 'C' && c != 'E' && c != 'F') {
                        svgFile << "          <line x1=\"" << floatToStr(baseX + diffx*0.8125)
                                << "\" y1=\"" << floatToStr(flipLocalY(boxY1 + diffy*0.11538))
                                << "\" x2=\"" << floatToStr(baseX + diffx*0.8125)
                                << "\" y2=\"" << floatToStr(flipLocalY(boxY1 + diffy*0.5))
                                << "\" stroke=\"" << segColor << "\" stroke-width=\"0.2\"/>\n";
                    }
                }
            }
        }

        // Export text labels if present
        std::string labelText = gate->getGUIParam("LABEL_TEXT");
        if (!labelText.empty()) {
            // Get text height
            std::string textHeightStr = gate->getGUIParam("TEXT_HEIGHT");
            float textHeight = 1.0;
            if (!textHeightStr.empty()) {
                std::istringstream(textHeightStr) >> textHeight;
            }

            // For labels, text is typically centered
            // Use a reasonable font size scaled by text height
            float fontSize = textHeight * 0.8; // Approximate scale factor

            // Determine text color (red if selected, black otherwise)
            std::string textColor = (gate->isSelected() && !noColor) ? "rgb(255,64,64)" : "rgb(0,0,0)";

            svgFile << "          <!-- Gate label text -->\n";
            svgFile << "          <text x=\"0\" y=\"0\" "
                    << "font-family=\"Arial, sans-serif\" "
                    << "font-size=\"" << floatToStr(fontSize) << "\" "
                    << "font-weight=\"bold\" "
                    << "fill=\"" << textColor << "\" "
                    << "text-anchor=\"middle\" "
                    << "dominant-baseline=\"central\">"
                    << escapeXML(labelText) << "</text>\n";
        }

        svgFile << "        </g>\n";
        svgFile << "      </g>\n";
    }
    svgFile << "    </g>\n";

    // Draw wires
    svgFile << "    <g id=\"wires\">\n";
    std::unordered_map<unsigned long, guiWire*>* wireList = canvas->getWireList();
    for (auto& wirePair : *wireList) {
        guiWire* wire = wirePair.second;
        if (!wire) continue;

        bool isBus = wire->getIDs().size() > 1;
        std::string wireClass = isBus ? "wire-line wire-bus" : "wire-line wire-normal";

        // Get wire state for color
        auto states = wire->getState();
        int dominantState = states.empty() ? ZERO : states[0];

        // For buses, calculate the redness gradient
        if (isBus && !noColor) {
            float redness = 0;
            bool conflict = false, unknown = false, hiz = false;

            for (size_t i = 0; i < states.size(); i++) {
                switch (states[i]) {
                    case ONE:
                        redness += pow(2, i);
                        break;
                    case HI_Z:
                        hiz = true;
                        break;
                    case UNKNOWN:
                        unknown = true;
                        break;
                    case CONFLICT:
                        conflict = true;
                        break;
                }
            }

            if (conflict) dominantState = CONFLICT;
            else if (unknown) dominantState = UNKNOWN;
            else if (hiz) dominantState = HI_Z;
            else if (redness > 0) {
                redness /= pow(2, states.size()) - 1;
                // Use gradient for bus wires
                int r = (int)(redness * 255);
                std::ostringstream colorStream;
                colorStream << "stroke=\"rgb(" << r << ",0,0)\"";
                wireClass = "wire-line wire-bus";

                svgFile << "      <g id=\"wire_" << wire->getID() << "\">\n";

                // Draw wire segments
                auto segMap = wire->getSegmentMap();
                for (auto& seg : segMap) {
                    wireSegment& segment = seg.second;
                    svgFile << "        <line x1=\"" << floatToStr(segment.begin.x)
                            << "\" y1=\"" << floatToStr(flipY(segment.begin.y, viewY, viewHeight))
                            << "\" x2=\"" << floatToStr(segment.end.x)
                            << "\" y2=\"" << floatToStr(flipY(segment.end.y, viewY, viewHeight))
                            << "\" class=\"" << wireClass << "\" "
                            << colorStream.str() << "/>\n";
                }

                svgFile << "      </g>\n";
                continue;
            }
        }

        std::string colorAttr = getWireColorSVG(dominantState, noColor);

        svgFile << "      <g id=\"wire_" << wire->getID() << "\">\n";

        // Draw wire segments
        auto segMap = wire->getSegmentMap();
        for (auto& seg : segMap) {
            wireSegment& segment = seg.second;
            svgFile << "        <line x1=\"" << floatToStr(segment.begin.x)
                    << "\" y1=\"" << floatToStr(flipY(segment.begin.y, viewY, viewHeight))
                    << "\" x2=\"" << floatToStr(segment.end.x)
                    << "\" y2=\"" << floatToStr(flipY(segment.end.y, viewY, viewHeight))
                    << "\" class=\"" << wireClass << "\" "
                    << colorAttr << "/>\n";
        }

        svgFile << "      </g>\n";
    }
    svgFile << "    </g>\n";

    // Draw connection dots at wire intersections and pin connections
    svgFile << "    <g id=\"connection-dots\">\n";
    for (auto& wirePair : *wireList) {
        guiWire* wire = wirePair.second;
        if (!wire) continue;

        // Draw dots at wire-to-wire intersection points
        const auto& intersectPoints = wire->getIntersectPoints();
        for (const auto& pt : intersectPoints) {
            svgFile << "      <circle cx=\"" << floatToStr(pt.x)
                    << "\" cy=\"" << floatToStr(flipY(pt.y, viewY, viewHeight))
                    << "\" r=\"0.15\" class=\"wire-dot\"/>\n";
        }

        // Draw dots at wire-to-gate connection points
        auto connections = wire->getConnections();
        for (const auto& conn : connections) {
            if (conn.cGate) {
                // Get the world position of this hotspot
                auto hotspot = conn.cGate->getHotspot(conn.connection);
                if (hotspot) {
                    GLPoint2f hotspotPos = hotspot->getLocation();
                    svgFile << "      <circle cx=\"" << floatToStr(hotspotPos.x)
                            << "\" cy=\"" << floatToStr(flipY(hotspotPos.y, viewY, viewHeight))
                            << "\" r=\"0.15\" class=\"wire-dot\"/>\n";
                }
            }
        }
    }
    svgFile << "    </g>\n";

    // Write SVG footer
    svgFile << getSVGFooter();
    svgFile.close();

    return true;
}
