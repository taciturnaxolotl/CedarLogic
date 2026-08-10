#include "cmdRegistry.h"
#include "cmdSerialize.h"

#include <map>

namespace {

// Meyers singleton: initialized on first use, so it is already live no matter
// which translation unit's self-registration static runs first at load time.
std::map<std::string, cmd::Factory> &table() {
	static std::map<std::string, cmd::Factory> t;
	return t;
}

} // namespace

namespace cmd {

bool registerFactory(const std::string &keyword, Factory factory) {
	table()[keyword] = std::move(factory);
	return true;
}

klsCommand *fromLine(const std::string &line) {
	auto it = table().find(cmdser::keyword(line));
	return it == table().end() ? nullptr : it->second(line);
}

bool canBuild(const std::string &line) {
	return table().count(cmdser::keyword(line)) != 0;
}

} // namespace cmd
