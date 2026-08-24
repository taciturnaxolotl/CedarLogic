/*****************************************************************************
   Project: CEDAR Logic Simulator

   CircuitObserver: what the circuit tells its views as the simulation runs.
*****************************************************************************/

// GUICircuit used to reach straight for a wxGLCanvas and an OscopeFrame to say
// "repaint" and "there is new data". That made the document depend on the very
// windows it is supposed to be independent of. It says the same three things
// through this interface now, and the shell decides what a view is: MainFrame
// implements it on the desktop, the browser shell implements it with a
// requestAnimationFrame.

#ifndef CIRCUITOBSERVER_H_
#define CIRCUITOBSERVER_H_

class CircuitObserver {
public:
	virtual ~CircuitObserver() {}

	// Wire states changed; whatever is showing the circuit should repaint.
	virtual void circuitRedrawNeeded() {}

	// The oscilloscope has a new sample.
	virtual void oscopeDataAdded() {}

	// The set of signals the oscilloscope can show has changed (a gate carrying
	// a probe was added or removed).
	virtual void oscopeSignalsChanged() {}
};

#endif /*CIRCUITOBSERVER_H_*/
