// WASM bindings for the CedarLogic simulation engine.
// Exposes the Circuit class to JavaScript via Emscripten embind.

#include <emscripten/bind.h>
#include <emscripten/val.h>
#include "logic_circuit.h"
#include "logic_values.h"

using namespace emscripten;

// Wrapper that provides a JS-friendly interface over Circuit.
// The raw Circuit::step() uses output parameters and C++ sets,
// so we wrap it to return JavaScript-friendly types.
class CircuitWrapper {
public:
	CircuitWrapper() : circuit() {}

	IDType newGate(const std::string &type, IDType gateID) {
		return circuit.newGate(type, gateID);
	}

	IDType newGateAuto(const std::string &type) {
		return circuit.newGate(type);
	}

	IDType newWire(IDType wireID) {
		return circuit.newWire(wireID);
	}

	IDType newWireAuto() {
		return circuit.newWire();
	}

	void deleteGate(IDType gateID) {
		circuit.deleteGate(gateID);
	}

	void deleteWire(IDType wireID) {
		circuit.deleteWire(wireID);
	}

	IDType connectGateInput(IDType gateID, const std::string &inputID, IDType wireID) {
		return circuit.connectGateInput(gateID, inputID, wireID);
	}

	IDType connectGateOutput(IDType gateID, const std::string &outputID, IDType wireID) {
		return circuit.connectGateOutput(gateID, outputID, wireID);
	}

	void disconnectGateInput(IDType gateID, const std::string &inputID) {
		circuit.disconnectGateInput(gateID, inputID);
	}

	void disconnectGateOutput(IDType gateID, const std::string &outputID) {
		circuit.disconnectGateOutput(gateID, outputID);
	}

	void setGateParameter(IDType gateID, const std::string &paramName, const std::string &value) {
		circuit.setGateParameter(gateID, paramName, value);
	}

	std::string getGateParameter(IDType gateID, const std::string &paramName) {
		return circuit.getGateParameter(gateID, paramName);
	}

	void setGateInputParameter(IDType gateID, const std::string &inputID, const std::string &paramName, const std::string &value) {
		circuit.setGateInputParameter(gateID, inputID, paramName, value);
	}

	void setGateOutputParameter(IDType gateID, const std::string &outputID, const std::string &paramName, const std::string &value) {
		circuit.setGateOutputParameter(gateID, outputID, paramName, value);
	}

	StateType getWireState(IDType wireID) {
		return circuit.getWireState(wireID);
	}

	// Step the simulation and return an object with changed wire IDs and their new states.
	// Returns a JS object: { changedWires: [{id, state}, ...], time: number }
	val step() {
		ID_SET<IDType> changedWires;
		circuit.step(&changedWires);

		val result = val::object();
		val wireChanges = val::array();

		unsigned idx = 0;
		for (IDType wireID : changedWires) {
			val entry = val::object();
			entry.set("id", wireID);
			entry.set("state", static_cast<int>(circuit.getWireState(wireID)));
			wireChanges.set(idx++, entry);
		}

		result.set("changedWires", wireChanges);
		result.set("time", static_cast<double>(circuit.getSystemTime()));
		return result;
	}

	// Step multiple times, returning only the final wire states for changed wires.
	val stepN(int n) {
		ID_SET<IDType> allChanged;
		for (int i = 0; i < n; i++) {
			ID_SET<IDType> changed;
			circuit.step(&changed);
			allChanged.insert(changed.begin(), changed.end());
		}

		val result = val::object();
		val wireChanges = val::array();

		unsigned idx = 0;
		for (IDType wireID : allChanged) {
			val entry = val::object();
			entry.set("id", wireID);
			entry.set("state", static_cast<int>(circuit.getWireState(wireID)));
			wireChanges.set(idx++, entry);
		}

		result.set("changedWires", wireChanges);
		result.set("time", static_cast<double>(circuit.getSystemTime()));
		return result;
	}

	void stepOnlyGates() {
		circuit.stepOnlyGates();
	}

	double getSystemTime() {
		return static_cast<double>(circuit.getSystemTime());
	}

	void destroyAllEvents() {
		circuit.destroyAllEvents();
	}

	// Get all wire states as a flat array: [wireID, state, wireID, state, ...]
	// Useful for full state sync.
	val getAllWireStates() {
		val result = val::array();
		ID_SET<IDType> gateIDs = circuit.getGateIDs();
		// We don't have direct wire enumeration, so we use getWireState
		// on known wire IDs. For full sync, the JS side should track wire IDs.
		return result;
	}

private:
	Circuit circuit;
};

EMSCRIPTEN_BINDINGS(cedarlogic) {
	// Wire state constants
	constant("ZERO", static_cast<int>(ZERO));
	constant("ONE", static_cast<int>(ONE));
	constant("HI_Z", static_cast<int>(HI_Z));
	constant("CONFLICT", static_cast<int>(CONFLICT));
	constant("UNKNOWN", static_cast<int>(UNKNOWN));

	class_<CircuitWrapper>("Circuit")
		.constructor<>()
		.function("newGate", &CircuitWrapper::newGate)
		.function("newGateAuto", &CircuitWrapper::newGateAuto)
		.function("newWire", &CircuitWrapper::newWire)
		.function("newWireAuto", &CircuitWrapper::newWireAuto)
		.function("deleteGate", &CircuitWrapper::deleteGate)
		.function("deleteWire", &CircuitWrapper::deleteWire)
		.function("connectGateInput", &CircuitWrapper::connectGateInput)
		.function("connectGateOutput", &CircuitWrapper::connectGateOutput)
		.function("disconnectGateInput", &CircuitWrapper::disconnectGateInput)
		.function("disconnectGateOutput", &CircuitWrapper::disconnectGateOutput)
		.function("setGateParameter", &CircuitWrapper::setGateParameter)
		.function("getGateParameter", &CircuitWrapper::getGateParameter)
		.function("setGateInputParameter", &CircuitWrapper::setGateInputParameter)
		.function("setGateOutputParameter", &CircuitWrapper::setGateOutputParameter)
		.function("getWireState", &CircuitWrapper::getWireState)
		.function("step", &CircuitWrapper::step)
		.function("stepN", &CircuitWrapper::stepN)
		.function("stepOnlyGates", &CircuitWrapper::stepOnlyGates)
		.function("getSystemTime", &CircuitWrapper::getSystemTime)
		.function("destroyAllEvents", &CircuitWrapper::destroyAllEvents);
}
