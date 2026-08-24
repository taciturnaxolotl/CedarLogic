#pragma once
#include "wx/wxheadless.h"

// An in-process clipboard.
//
// The browser's real clipboard is asynchronous and gated on a user gesture, so
// it cannot be read from inside a synchronous copy/paste the way wxTheClipboard
// is. This holds the text instead, which makes copy and paste work within the
// page immediately. The shell reads and writes it (see clipboardText/
// setClipboardText in the bindings) to keep it in step with the system
// clipboard around a real gesture.

#define wxDF_UNICODETEXT 13

class wxDataObject {
public:
	virtual ~wxDataObject() {}
	virtual wxString AsText() const { return wxString(); }
	virtual void SetText(const wxString &) {}
};

class wxTextDataObject : public wxDataObject {
public:
	wxTextDataObject() {}
	explicit wxTextDataObject(const wxString &text) : fText(text) {}
	wxString GetText() const { return fText; }
	wxString AsText() const override { return fText; }
	void SetText(const wxString &text) override { fText = text; }
private:
	wxString fText;
};

class wxClipboard {
public:
	// The store outlives any Open/Close pair, so it is not cleared here.
	bool Open() { return true; }
	void Close() {}
	bool IsSupported(int) const { return !fText.empty(); }

	// Takes ownership, as wxWidgets does.
	bool AddData(wxDataObject *data) {
		if (!data) return false;
		fText = data->AsText();
		delete data;
		return true;
	}

	bool GetData(wxDataObject &out) {
		if (fText.empty()) return false;
		out.SetText(fText);
		return true;
	}

	const wxString &text() const { return fText; }
	void setText(const wxString &t) { fText = t; }

	static wxClipboard *Get() {
		static wxClipboard instance;
		return &instance;
	}

private:
	wxString fText;
};

#define wxTheClipboard (wxClipboard::Get())
