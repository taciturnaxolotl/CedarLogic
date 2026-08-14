/*****************************************************************************
   Project: CEDAR Logic Simulator

   SimBridge: the state the GUI thread and the background threads (threadLogic,
   the logic thread) coordinate through. Extracted from the MainApp God-singleton
   (Workstream C); reach it via simBridge() instead of wxGetApp().

   ---- Threading model ----------------------------------------------------
   Two locks nest in a fixed order: take m_critsect first, then mexMessages --
   both message-drain sites (threadLogic::checkMessages and MainFrame::OnIdle)
   do exactly that. Preserve that order; the reverse risks deadlock.

   Drain protocol: each drain site takes mexMessages only long enough to swap
   the pending deque into a local, then processes the local with the lock
   released (so parseMessage may re-take mexMessages to send a reply). Anything
   that clears the queues (New/Open) must also hold mexMessages -- the logic
   thread drains dGUItoLOGIC on its own loop, independent of the GUI timers.
*****************************************************************************/

#pragma once

#include "wx/thread.h"
#include "wx/stopwatch.h"
#include <deque>
#include <unordered_map>
#include "klsMessage.h"
#include "logic_values.h"

class threadLogic;

class SimBridge {
public:
	// Coarse lock for the background-thread lifecycle: guards the thread
	// pointers below and wraps the message-drain sections.
	wxCriticalSection m_critsect;

	// Posted by a background thread as it exits so OnQuit can block on teardown.
	wxSemaphore m_semAllDone;
	// GUI <-> logic run/step signaling semaphores.
	wxSemaphore simulate;
	wxSemaphore readyToSend;

	// Guards the two message queues (both directions of GUI <-> logic).
	wxMutex mexMessages;
	std::deque< klsMessage::Message > dGUItoLOGIC;
	std::deque< klsMessage::Message > dLOGICtoGUI;

	// Signaled (under mexMessages) whenever a message is queued for the logic
	// thread, so it can block until there is work instead of polling. Bound to
	// mexMessages; wait only with that mutex held.
	wxCondition msgForLogic{ mexMessages };

	// Guards wireStateBuffer, the batch of wire-state updates the logic thread
	// produces for the GUI to drain.
	wxMutex wireStateMutex;
	std::unordered_map< IDType, StateType > wireStateBuffer;

	// Stopwatch for timing between step calls.
	wxStopWatch appSystemTime;

	// The last exiting thread should post to m_semAllDone if this is true
	// (guarded by m_critsect).
	bool m_waitingUntilAllDone = false;

	threadLogic* logicThread = nullptr;
};

// The process-wide GUI<->logic bridge. Lives for the whole run; constructed on
// first use.
SimBridge& simBridge();
