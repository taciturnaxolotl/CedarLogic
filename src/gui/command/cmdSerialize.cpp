#include "cmdSerialize.h"

#include <sstream>

namespace cmdser {

std::string emit(const CreateGate &c) {
	std::ostringstream oss;
	oss << "creategate " << c.gid << " " << c.gateType << " " << c.x << " " << c.y;
	return oss.str();
}

std::string emit(const ConnectWire &c) {
	std::ostringstream oss;
	oss << "connectwire " << c.wireId << ' ' << c.gateId << ' ' << c.hotspot;
	return oss.str();
}

std::string emit(const MoveGate &c) {
	std::ostringstream oss;
	oss << "movegate " << c.gid << " " << c.startX << " " << c.startY << " "
	    << c.endX << " " << c.endY;
	return oss.str();
}

std::string keyword(const std::string &line) {
	std::istringstream iss(line);
	std::string kw;
	iss >> kw;
	return kw;
}

bool parse(const std::string &line, CreateGate &out) {
	std::istringstream iss(line);
	std::string kw;
	iss >> kw;
	if (kw != "creategate") return false;
	iss >> out.gid >> out.gateType >> out.x >> out.y;
	return true;
}

bool parse(const std::string &line, ConnectWire &out) {
	std::istringstream iss(line);
	std::string kw;
	iss >> kw;
	if (kw != "connectwire") return false;
	iss >> out.wireId >> out.gateId >> out.hotspot;
	return true;
}

} // namespace cmdser
