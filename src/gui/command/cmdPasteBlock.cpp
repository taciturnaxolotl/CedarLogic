
#include "cmdPasteBlock.h"

cmdPasteBlock::cmdPasteBlock(std::vector<klsCommand *> &cmdList) :
		klsCommand(true, "Paste") {

	// Adopt each sub-command. This command exclusively owns them (unique_ptr), so
	// a paste and its slot in the undo history free cleanly with no manual delete.
	for (unsigned int i = 0; i < cmdList.size(); i++)
		this->cmdList.push_back(std::unique_ptr<klsCommand>(cmdList[i]));

	m_init = false;
}

bool cmdPasteBlock::Do() {

	if (!m_init) {
		m_init = true;
		return true;
	}

	for (unsigned int i = 0; i < cmdList.size(); i++) cmdList[i]->Do();

	return true;
}

bool cmdPasteBlock::Undo() {

	for (int i = cmdList.size() - 1; i >= 0; i--) {
		cmdList[i]->Undo();
	}

	return true;
}