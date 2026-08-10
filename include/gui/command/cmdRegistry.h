#pragma once

// Keyword-dispatched command factory.
//
// The clipboard paste path reconstructs commands from their serialized text
// form, one line per command. It used to pick the concrete type with a
// hand-written if/else chain over the leading keyword, so adding a pasteable
// command meant editing that chain in lockstep with the command class. This
// registry inverts that: each command self-registers its keyword and a factory
// beside its own definition, and the dispatcher just asks the registry to build
// whatever the line names.

#include <functional>
#include <string>

class klsCommand;

namespace cmd {

// Builds a command from a full serialized line (keyword included). The returned
// command is owned by the caller, which still wires it up (setPointers) and runs
// it (Do) as before.
using Factory = std::function<klsCommand *(const std::string &line)>;

// Register a factory under its leading keyword. Returns true so it can seed a
// namespace-scope `static const bool` for self-registration at load time. A
// later registration for the same keyword replaces the earlier one.
bool registerFactory(const std::string &keyword, Factory factory);

// Construct the command whose keyword leads `line`, or nullptr if no factory is
// registered for that keyword.
klsCommand *fromLine(const std::string &line);

// Whether a factory is registered for the leading keyword of `line`.
bool canBuild(const std::string &line);

} // namespace cmd
