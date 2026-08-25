// The gate parameter editor.
//
// Double-clicking a gate on the desktop opens a dialog built from the library's
// dlgParams: a labelled field per parameter, typed, and one cmdSetParams
// submitted when you accept. This is that, built from the same list in the same
// order and submitting the same command -- so an edit made here and an edit
// made there are the same edit, and undo treats them alike.
//
// It is a panel rather than a modal because a browser cannot block, and because
// seeing the circuit change as you type is better than not.

import type { Document } from "./engine.ts";

interface ParamSpec {
  label: string;
  name: string;
  gui: boolean;
  type: string;
  min?: number;
  max?: number;
  value: string;
}

export class ParamEditor {
  private gateId = -1;

  constructor(
    private readonly doc: Document,
    private readonly root: HTMLElement,
    private readonly onApplied: () => void,
  ) {
    root.hidden = true;
  }

  /** Poll for a double-click asking to edit a gate. */
  poll(): void {
    const id = this.doc.takeParamRequest();
    if (id >= 0) this.open(id);
  }

  close(): void {
    this.gateId = -1;
    this.root.hidden = true;
    this.root.replaceChildren();
  }

  private open(gateId: number): void {
    const specs = this.doc.gateParams(gateId) as ParamSpec[];
    this.gateId = gateId;
    this.root.replaceChildren();
    this.root.hidden = false;

    const head = document.createElement("header");
    const title = document.createElement("strong");
    // Nothing to edit is worth saying, rather than an empty panel that looks
    // broken: plenty of gates have no parameters at all.
    title.textContent = specs.length > 0 ? "Parameters" : "No parameters";
    const close = document.createElement("button");
    close.type = "button";
    close.textContent = "Close";
    close.addEventListener("click", () => this.close());
    head.append(title, close);
    this.root.append(head);

    for (const spec of specs) this.root.append(this.field(spec));
  }

  private field(spec: ParamSpec): HTMLElement {
    const row = document.createElement("label");
    row.className = "param";

    const label = document.createElement("span");
    label.textContent = spec.label || spec.name;
    label.title = `${spec.name} (${spec.gui ? "display" : "logic"})`;

    const input = document.createElement("input");
    switch (spec.type) {
      case "BOOL":
        input.type = "checkbox";
        input.checked = spec.value === "true";
        break;
      case "INT":
      case "FLOAT":
        input.type = "number";
        input.value = spec.value;
        if (spec.min !== undefined) input.min = String(spec.min);
        if (spec.max !== undefined) input.max = String(spec.max);
        if (spec.type === "FLOAT") input.step = "any";
        break;
      default:
        // STRING, and the two file names, which have no file to browse for in a
        // browser -- the RAM contents load from a dropped file instead.
        input.type = "text";
        input.value = spec.value;
        break;
    }

    const apply = (): void => {
      const value = spec.type === "BOOL" ? String(input.checked) : input.value;
      this.doc.beginParamEdit();
      this.doc.setParam(spec.name, spec.gui, value);
      if (this.doc.commitParamEdit(this.gateId)) this.onApplied();
    };

    // Commit on change rather than per keystroke: one command per edit, so undo
    // steps back a whole value and not a letter of it.
    input.addEventListener("change", apply);

    row.append(label, input);
    return row;
  }
}
