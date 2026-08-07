#ifndef KLSMESSAGE_H_
#define KLSMESSAGE_H_

#include <string>
#include <sstream>
#include <memory>

// ALL inter-thread message structures defined here
namespace klsMessage {
	using std::string;
	using std::ostringstream;

	enum MessageType {
		// core -> GUI
		MT_SET_WIRE_STATE = 0, // SET WIRE id STATE TO state
		MT_DONESTEP, // DONESTEP
		MT_COMPLETE_INTERIM_STEP, // COMPLETE INTERIM STEP - UPDATE OSCOPE

		// GUI -> core
		MT_REINITIALIZE, // REINITIALIZE LOGIC CIRCUIT
		MT_CREATE_GATE, // CREATE GATE TYPE type ID id
		MT_CREATE_WIRE, // CREATE WIRE ID id
		MT_DELETE_GATE, // DELETE GATE id
		MT_DELETE_WIRE, // DELETE WIRE id
		MT_SET_GATE_INPUT, // SET GATE ID id INPUT ID id TO DISCONNECT/wid
		MT_SET_GATE_INPUT_PARAM, // SET GATE ID id INPUT ID id PARAM name value
		MT_SET_GATE_OUTPUT, // SET GATE ID id OUTPUT ID id TO DISCONNECT/wid
		MT_SET_GATE_OUTPUT_PARAM, // SET GATE ID id OUTPUT ID id PARAM name value
		MT_SET_GATE_PARAM, // SET GATE ID id PARAMETER paramname paramval
		MT_STEPSIM, // STEPSIM numsteps
		MT_UPDATE_GATES // UPDATE GATES
	};

	// Base for every message payload. A Message owns its payload through this
	// type, so the payload is freed automatically when the Message is destroyed
	// -- no hand-cast void*, no manual delete, and no leak if a message type
	// ever goes unhandled.
	struct Payload {
		virtual ~Payload() {}
	};

	class Message {
	public:
		MessageType mType;
		std::shared_ptr<Payload> data; // owned; may be null for payload-less messages

		Message( MessageType t ) : mType(t) {}
		template <class T>
		Message( MessageType t, T* s ) : mType(t), data(s) {}

		// Read the payload as its concrete type. The caller must ask for the
		// type that matches mType (same contract the old void* cast had).
		template <class T>
		const T& as() const { return *static_cast<const T*>(data.get()); }
	};

	struct Message_SET_WIRE_STATE : public Payload {
		int wireId;
		int state;
		Message_SET_WIRE_STATE( int wid, int s ) : wireId(wid), state(s) {};
	};

	struct Message_DONESTEP : public Payload {
		int logicTime;
		Message_DONESTEP( int lt ) : logicTime(lt) {};
	};

	// no parameters for COMPLETE_INTERIM_STEP

	// no parameters for REINITIALIZE

	struct Message_CREATE_GATE : public Payload {
		string gateType;
		int gateId;
		Message_CREATE_GATE( string gt, int gid ) : gateType(gt), gateId(gid) {};
	};

	struct Message_CREATE_WIRE : public Payload {
		int wireId;
		Message_CREATE_WIRE( int wid ) : wireId(wid) {};
	};

	struct Message_DELETE_GATE : public Payload {
		int gateId;
		Message_DELETE_GATE( int gid ) : gateId(gid) {};
	};

	struct Message_DELETE_WIRE : public Payload {
		int wireId;
		Message_DELETE_WIRE( int wid ) : wireId(wid) {};
	};

	struct Message_SET_GATE_INPUT : public Payload {
		int gateId;
		string inputId;
		int wireId;
		bool disconnect;
		Message_SET_GATE_INPUT( int gid, string iid, int wid, bool d = false ) : gateId(gid), inputId(iid), wireId(wid), disconnect(d) {};
	};

	struct Message_SET_GATE_INPUT_PARAM : public Payload {
		int gateId;
		string inputId;
		string paramName;
		string paramValue;
		Message_SET_GATE_INPUT_PARAM( int gid, string iid, string pN, string pV ) : gateId(gid), inputId(iid), paramName(pN), paramValue(pV) {};
	};

	struct Message_SET_GATE_OUTPUT : public Payload {
		int gateId;
		string outputId;
		int wireId;
		bool disconnect;
		Message_SET_GATE_OUTPUT( int gid, string oid, int wid, bool d = false ) : gateId(gid), outputId(oid), wireId(wid), disconnect(d) {};
	};

	struct Message_SET_GATE_OUTPUT_PARAM : public Payload {
		int gateId;
		string outputId;
		string paramName;
		string paramValue;
		Message_SET_GATE_OUTPUT_PARAM( int gid, string oid, string pN, string pV ) : gateId(gid), outputId(oid), paramName(pN), paramValue(pV) {};
	};

	struct Message_SET_GATE_PARAM : public Payload {
		int gateId;
		string paramName;
		string paramValue;
		Message_SET_GATE_PARAM( int gid, string pN, string pV ) : gateId(gid), paramName(pN), paramValue(pV) {};
		Message_SET_GATE_PARAM( int gid, string pN, long pV, bool useHex = false ) : gateId(gid), paramName(pN) {
			ostringstream oss; oss << (useHex ? std::hex : std::dec) << pV; paramValue = oss.str();
		};
	};

	struct Message_STEPSIM : public Payload {
		int numSteps;
		Message_STEPSIM( int n ) : numSteps(n) {};
	};

	// no parameters for UPDATE_GATES
}

#endif /*KLSMESSAGE_H_*/
