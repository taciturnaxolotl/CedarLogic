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

std::string emit(const DisconnectWire &c) {
	std::ostringstream oss;
	oss << "disconnectwire " << c.wireId << " " << c.gateId << " " << c.hotspot;
	return oss.str();
}

std::string emit(const CreateWire &c) {
	std::ostringstream oss;
	oss << "createwire ";
	// Each id is followed by a space; the two connectwire clauses follow.
	for (IDType id : c.wireIds) oss << id << ' ';
	oss << emit(c.conn1) << ' ' << emit(c.conn2);
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

bool parse(const std::string &line, CreateWire &out) {
	std::istringstream iss(line);
	std::string kw;
	iss >> kw;
	if (kw != "createwire") return false;
	// Read wire ids until the extraction fails on the first "connectwire" token,
	// then clear the failbit and read the two connectwire clauses. This mirrors
	// the original cmdCreateWire string constructor exactly.
	IDType id;
	while (iss >> id) out.wireIds.push_back(id);
	iss.clear();
	std::string cwkw;
	iss >> cwkw >> out.conn1.wireId >> out.conn1.gateId >> out.conn1.hotspot;
	iss >> cwkw >> out.conn2.wireId >> out.conn2.gateId >> out.conn2.hotspot;
	return true;
}

std::string emit(const SetParams &c) {
	std::ostringstream oss;
	oss << "setparams " << c.gid << " " << c.guiParams.size() << ","
	    << c.logicParams.size() << " ";
	for (const auto &p : c.guiParams) oss << p.first << " " << p.second << "\t";
	for (const auto &p : c.logicParams) oss << p.first << " " << p.second << "\t";
	return oss.str();
}

bool parse(const std::string &line, SetParams &out) {
	std::istringstream iss(line);
	std::string kw;
	char comma;
	unsigned long numGui, numLogic;
	iss >> kw;
	if (kw != "setparams") return false;
	iss >> out.gid >> numGui >> comma >> numLogic;
	// A value can contain spaces, so read the name with >> but the value up to
	// its tab; the value carries a leading space from the "name value" join,
	// which substr(1) drops -- exactly as the original inline parse did.
	for (unsigned long i = 0; i < numGui; i++) {
		std::string name, val;
		iss >> name;
		std::getline(iss, val, '\t');
		out.guiParams[name] = val.substr(1, val.size() - 1);
	}
	for (unsigned long i = 0; i < numLogic; i++) {
		std::string name, val;
		iss >> name;
		std::getline(iss, val, '\t');
		out.logicParams[name] = val.substr(1, val.size() - 1);
	}
	return true;
}

std::string emit(const MoveWire &c) {
	std::ostringstream oss;
	oss << "movewire " << c.wid << " ";
	for (const MoveWireSeg &s : c.segments) {
		oss << (s.vertical ? "vsegment " : "hsegment ");
		oss << s.id << " ";
		oss << s.bx << "," << s.by << "," << s.ex << "," << s.ey << " ";
		for (const auto &conn : s.connections)
			oss << "connection " << conn.first << " " << conn.second << " ";
		for (const auto &isect : s.intersects)
			oss << "isect " << isect.first << " " << isect.second << " ";
	}
	oss << " done ";
	return oss.str();
}

bool parse(const std::string &line, MoveWire &out) {
	std::istringstream iss(line);
	std::string temp;
	char dump;
	iss >> temp;
	if (temp != "movewire") return false;
	iss >> out.wid >> temp;
	while (temp == "vsegment" || temp == "hsegment") {
		MoveWireSeg s;
		s.vertical = (temp == "vsegment");
		iss >> s.id >> s.bx >> dump >> s.by >> dump >> s.ex >> dump >> s.ey;
		iss >> temp;
		while (temp == "connection") {
			unsigned long gid;
			std::string name;
			iss >> gid >> name;
			s.connections.push_back({ gid, name });
			iss >> temp;
		}
		while (temp == "isect") {
			float key;
			long sid;
			iss >> key >> sid;
			s.intersects.push_back({ key, sid });
			iss >> temp;
		}
		out.segments.push_back(s);
	}
	return true;
}

} // namespace cmdser
