
#pragma once
#include "klsCommand.h"
#include <map>

class guiGate;
typedef std::map<std::string, std::string> ParameterMap;

// Holds pointers to all of a gate's parameters
struct paramSet {

	ParameterMap *gParams;
	ParameterMap *lParams;

	paramSet(ParameterMap *g, ParameterMap* l);
};

// cmdSetParams - Sets a gate's parameters
class cmdSetParams : public klsCommand {
public:
	cmdSetParams(GUICircuit* gCircuit, unsigned long gid, paramSet pSet, bool setMode = false);

	cmdSetParams(std::string def);

	bool Do();

	bool Undo();

	// Every logic param of the gate, including the ones a gate type keeps
	// outside its param map (a RAM's memory cells). This is the set to snapshot
	// when the gate is going to be rebuilt from nothing, as delete+undo and
	// copy+paste do; the map alone lost a ROM's program on the way.
	static ParameterMap fullLogicParams(guiGate *gate);

	virtual std::string toString() const override;

	virtual void setPointers(GUICircuit* gCircuit, GUICanvas* gCanvas,
		TranslationMap &gateids, TranslationMap &wireids) override;

protected:
	unsigned long gid;
	ParameterMap oldGUIParamList;
	ParameterMap newGUIParamList;
	ParameterMap oldLogicParamList;
	ParameterMap newLogicParamList;
};