
#pragma once
#include "klsCommand.h"
#include <stack>
#include <memory>

// cmdDeleteGate - Deletes a gate
class cmdDeleteGate : public klsCommand {
public:
	cmdDeleteGate(GUICircuit* gCircuit, GUICanvas* gCanvas, IDType gateId);

	virtual ~cmdDeleteGate();

	bool Do();

	bool Undo();

private:
		IDType gateId;
		std::stack<std::unique_ptr<klsCommand>> cmdList;
		std::string gateType;
};