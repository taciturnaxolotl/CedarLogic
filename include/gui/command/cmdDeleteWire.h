
#pragma once
#include "klsCommand.h"
#include <stack>
#include <memory>
#include <vector>

// cmdDeleteWire - Deletes a wire
class cmdDeleteWire : public klsCommand {
public:
	cmdDeleteWire(GUICircuit* gCircuit, GUICanvas* gCanvas, IDType wireId);

	virtual ~cmdDeleteWire();

	bool Do();

	bool Undo();

private:
	std::vector<IDType> wireIds;
	std::stack<std::unique_ptr<klsCommand>> cmdList;
};