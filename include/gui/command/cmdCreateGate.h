
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

	// Send the library's per-pin settings (INVERTED, E_INPUT) to the core. Every path
	// that creates a core gate must call this after the gate's params, which create the pins.
	static void sendHotspotParams(GUICircuit *gCircuit, unsigned long gid,
		const std::string &gateType);

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