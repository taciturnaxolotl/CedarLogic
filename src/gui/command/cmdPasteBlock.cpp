
#include "cmdPasteBlock.h"

cmdPasteBlock::cmdPasteBlock(std::vector<klsCommand *> &cmdList) :
		klsCommand(true, "Paste") {

	for (unsigned int i = 0; i < cmdList.size(); i++) this->cmdList.push_back(cmdList[i]);

	m_init = false;
}

cmdPasteBlock::~cmdPasteBlock() {
	// This command exclusively owns its sub-commands; free them so a paste
	// (and its slot in the undo history) doesn't leak.
	for (klsCommand *cmd : cmdList) delete cmd;
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