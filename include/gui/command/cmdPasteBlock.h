
#pragma once
#include "klsCommand.h"
#include <vector>
#include <memory>

// cmdPasteBlock - Paste's a block of gates/wires
class cmdPasteBlock : public klsCommand {
public:
	// Adopts the sub-commands (raw pointers in, owned as unique_ptr).
	cmdPasteBlock(std::vector<klsCommand*> &cmdList);

	bool Do();

	bool Undo();

	// Adopt an additional sub-command.
	void addCommand(klsCommand* cmd) { cmdList.push_back(std::unique_ptr<klsCommand>(cmd)); };

private:
	std::vector<std::unique_ptr<klsCommand>> cmdList;
	bool m_init;
};