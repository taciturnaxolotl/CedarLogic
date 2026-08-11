
#pragma once
#include "klsCommand.h"
#include <vector>
#include <memory>

// cmdCreateGate - creates a gate on a given canvas at position (x,y)
class cmdCreateGate : public klsCommand {
public:
	cmdCreateGate(GUICanvas* gCanvas, GUICircuit* gCircuit,
		unsigned long gid, std::string gateType, float x, float y);

	cmdCreateGate(std::string def);

	bool Do();

	bool Undo();

	virtual std::string toString() const override;

	virtual void setPointers(GUICircuit* gCircuit, GUICanvas* gCanvas,
		TranslationMap &gateids, TranslationMap &wireids) override;

	// The proximity-connection sub-commands this command owns; callers append to
	// it (adopting into unique_ptr) after construction.
	std::vector<std::unique_ptr<klsCommand>> * getConnections();

protected:
	float x;
	float y;
	std::string gateType;
	unsigned long gid;
	std::vector<std::unique_ptr<klsCommand>> proxconnects;
};