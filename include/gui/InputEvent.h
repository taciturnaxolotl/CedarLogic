/*****************************************************************************
   Project: CEDAR Logic Simulator

   InputEvent: pointer and keyboard input, with no toolkit attached.
*****************************************************************************/

// How a circuit responds to the mouse -- what counts as a drag, where a wire
// snaps, how far a pin's hover bulb reaches, how much one notch of the wheel
// zooms -- is the feel of the application, and it is the part users notice when
// a port gets it wrong. So it should be written once and compiled everywhere,
// not described in a spec and reimplemented per platform.
//
// That means the handlers cannot take wxMouseEvent. They take these instead,
// and each shell translates: MainFrame's canvas from wx, the browser shell from
// DOM events. The vocabulary is deliberately small -- it is exactly what the
// existing handlers read off a wx event and nothing more.

#ifndef INPUTEVENT_H_
#define INPUTEVENT_H_

#include "gl_defs.h"

namespace input {

enum class Button { None, Left, Right, Middle };

// Named keys. Anything printable travels as Key::Character with `ch` set,
// which is what both wxWidgets and the DOM settle on once you stop counting
// keypad duplicates -- the translators fold those in (numpad Left is Left,
// numpad Add is Plus).
enum class Key {
	None,
	Character,
	Backspace,
	Delete,
	Escape,
	Space,
	Left,
	Right,
	Up,
	Down,
	Plus,
	Minus,
	Equals,
};

// The modifier keys held during an event. Cmd is macOS's command key, which is
// distinct from ctrl there and absent elsewhere.
struct Modifiers {
	bool shift = false;
	bool ctrl = false;
	bool alt = false;
	bool cmd = false;
};

struct PointerEvent {
	// Where the pointer is, in world coordinates. The shell has already mapped
	// it through the camera, because the shell is what owns the widget's pixel
	// space.
	GLPoint2f pos;

	// Which button this event is about. None for a plain move.
	Button button = Button::None;

	// Whether the left button is held, which a move needs to know and a press
	// does not.
	bool leftIsDown = false;

	// Set on the second click of a double-click on `button`, in addition to the
	// ordinary press and release that surround it. Always test it together with
	// `button`: a middle double-click sets this too.
	bool doubleClick = false;

	// The second click of a left double-click, which is the only one the
	// circuit reacts to.
	bool leftDoubleClick() const { return doubleClick && button == Button::Left; }

	Modifiers mods;

	// The two are always tested together at the call sites: either one extends
	// a selection rather than replacing it.
	bool extendSelection() const { return mods.shift || mods.ctrl; }
};

struct KeyEvent {
	Key key = Key::None;
	// The character, when key is Key::Character. Compare case-insensitively:
	// shells differ on whether shift has already been applied.
	char32_t ch = 0;

	Modifiers mods;

	// True for a bare letter -- no modifier held. The shortcuts that live on
	// unmodified letters ('a' to quick-add, 'r' to rotate) must not fire while
	// the user is reaching for a menu accelerator.
	bool unmodified() const { return !mods.ctrl && !mods.alt && !mods.cmd; }
};

}  // namespace input

#endif /*INPUTEVENT_H_*/
