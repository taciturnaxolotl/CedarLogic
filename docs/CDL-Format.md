# The CedarLogic Circuit File Format (`.cdl`)

This document specifies the three versions of the CedarLogic circuit file: v1, v2, and v3.

All three versions use the extension `.cdl`, so the version must be identified from the content / header.
 v1 and v2 use a hand-written format that resembles XML. v3 uses S-expressions. The model of a circuit is
 mostly the same between the formats however.

## How to read this document

Most of this document is description of the `.cdl` format. 

A smaller number of statements are requirements. They use MUST, SHOULD, and MAY in capitals, with the
 meanings given in RFC 2119 and RFC 8174. We try to restrict these to only the places where there are
 multiple possibly correct ways of parsing a circuit.

**Contents**

- [How to read this document](#how-to-read-this-document)
1. [Terminology](#1-terminology)
2. [The circuit model](#2-the-circuit-model)
3. [Version 1](#3-version-1)
4. [Version 2](#4-version-2)
5. [Version 3](#5-version-3)
6. [Detection](#6-detection)
7. [Migration](#7-migration)
8. [How the reference application loads a document](#8-how-the-reference-application-loads-a-document)
9. [Writing](#9-writing)
10. [Conformance](#10-conformance)
11. [Round-tripping and numeric precision](#11-round-tripping-and-numeric-precision)
12. [Error handling](#12-error-handling)
13. [Security considerations](#13-security-considerations)
14. [Compatibility](#14-compatibility)
15. [Worked example](#15-worked-example)
- [Appendix: version history](#appendix-version-history)

---

## 1. Terminology

**Gate library.** A separate XML catalog that defines each gate type: its `libName`, the
pins it has, its default params, and its shape. A `.cdl` document names library gates and
their params, but it never defines them. You can parse a document and check
it against this specification without a library.

The catalog is not part of this specification and carries no version of its own. CedarLogic
currently vendors it as `res/cl_gatedefs.xml`.

**Pin.** A named connection point on a gate, declared by the library. Names are strings such
as `IN_0`, `OUT_3`, `clock`, and `count_enable`. The CedarLogic source calls these hotspots.

**Param.** A name and value pair on a gate. GUI params (`gparam`) affect appearance and
editor state. Logic params (`lparam`) go to the simulation core. Both are strings in the
file. The library's `paramSchema` gives them types when the circuit loads.

**World coordinates.** Doubles in the canvas coordinate system. x increases to the right and
y increases upward. The editor snaps to a 0.5-unit grid, so coordinates from CedarLogic are
typically multiples of 0.5. The format itself does not requires that however.

**Angle.** Degrees, counter-clockwise, about the point that `position` defines. That point is
the gate's local origin, which isn't necessarily the center of its shape. Cedarlogic only exports 
files with `0.0`, `90`, `180`, and `270` but again the format doesn't require this,
Some gate types add a transform of their own at some angles. Bus ends, for example, flip across their
long axis at 180 and 270 degrees. This is purely a drawing decision however and is not needed to parse
the file.

**Page.** One canvas, which the editor shows as one tab. Pages count from 0. CedarLogic 1.x
always wrote pages 0 through 9 empty by default. Later versions write one page per open tab.

**Wire ids.** A wire carries one id for each logical line. A single wire has one id. A bus
has several. The first id is the primary id. It is the id the editor keys the wire by, and
it is the only one that is exported to v1.

**uuid.** The identifier of one gate instance. It is unique across the document.

## 2. The circuit model

Every version encodes the same rough shape: pages of gate instances and wires. The in-memory form
in the reference implementation is `cl::CircuitFile` (`format/circuit_file.hpp`), which
both readers produce and the v3 writer consumes.

The defaults shown are what a v1 or v2 document means when it omits the field, and a reader
MUST use these defaults. v3 requires these fields explicitly (§5.3) however older versions
should also treat these as required.

```
CircuitFile
  formatVersion : int  = 3     // always 3 in memory; see §6
  generator     : string       // free text; may be empty
  pages         : [Page]

Page
  index : int          = 0     // 0-based
  gates : [GateInstance]
  wires : [WireInstance]

GateInstance
  uuid    : string     = ""
  libName : string     = ""    // library gate name, e.g. "AA_AND2"
  at      : (x, y)     = (0,0) // world coordinates
  angle   : double     = 0     // degrees CCW
  params  : [Param]

Param
  name  : string
  value : string
  gui   : bool                 // true = gparam, false = lparam

WireInstance
  ids      : [string]          // one per line; several = bus
  segments : [WireSegment]

WireSegment
  id            : string = ""  // unique within the wire
  vertical      : bool = false // authored orientation
  begin, end    : (x, y) = (0,0)
  connects      : [WireConn]   // gate pins landing on this segment
  intersections : [Intersection]

WireConn
  gateUuid : string
  pin      : string

Intersection
  at      : double             // coordinate along this segment (x if horizontal, y if vertical)
  segment : string             // id of the other segment of the same wire met there
```

### 2.1 Identifier domains

Ids are strings in the model and decimal integers in Cedarlogic. They are converted on load and use
different conversions per area:

| Field | Conversion | Failure |
|---|---|---|
| `GateInstance.uuid` | `istringstream >> long` (signed) | non-numeric → 0 |
| `WireConn.gateUuid` (a reference to a gate uuid) | `strtoul` (unsigned) | non-numeric → 0 |
| `WireInstance.ids[]` | `strtoull` | non-numeric → 0 |
| `WireSegment.id` | `strtol` (signed) | non-numeric → 0 |
| `Intersection.segment` | `strtol` (signed) | non-numeric → 0 |

A gates id MUST never be negative or it does funky things with the simulation.

These conversions are lossy and do not report a failure, so a reader MUST reject an id
that is not a decimal integer before it writes it. Two such ids both convert to 0, and the second
object silently replaces the first. An empty id is refused where the id is an identity that
other things point at, which means a gate uuid or a wire id. A segment label may be absent,
because legacy documents leave it out and it means nothing outside its own wire.

`-1` is a sentinel rather than an id. It makes the editor assign a fresh id, which renumbers
the gate and orphans `connect` nodes that reference it. A writer MUST emit ids as non-negative
decimal integers, and SHOULD keep them within 2^32 - 1, which is respected in all conversion
cases above on all current platforms CedarLogic supports.

Coordinates lose precision in two stages. The format library parses `<position>` as a double
(section 12). The editor then reads that value again as a float, so the editor as a whole
keeps only single precision (section 11).

Gate uuids are unique per document (the app keys gates globally, not per page). Wire ids are
unique the same way. Segment ids are unique within their wire.

### 2.2 Notes on the model

Params have no type in the `.cdl` file. Every value is a string, and the library's `paramSchema`
gives it a type when the circuit loads. A reader MUST NOT reject a param it does not
recognize, and MUST pass it through a load and a save unchanged.

Wire routing is user data. The segments and intersections are the polyline tree that a user
defined. A reader MUST NOT reroute or normalize them.

Connectivity lives on the wire rather than on the gate. A gate's connections come from the
`connect` entries of the wires on its page. v1 and v2 also store a second copy on the gate
itself (section 3.4) however CedarLogic ignores that copy.

Angle is a field of its own in v3. In v1 and v2 it is the gparam named `angle`. A reader of
those versions lifts it out of the params and into the angle field. A writer targeting them
MUST put it back into a `<gparam>`.

RAM contents travel in ordinary lparams, not in an element of their own. See section 3.4.

View state is not circuit content. v1 and v2 write the pan and zoom of each page
(`<PageViewport>`) and the selected page (`<CurrentPage>`). Readers discard both. v3 does
not encode them at all and it is recommended to avoid writing them in the first place.

### 2.3 Document validity

A `.cdl` document is valid, independent of any application, when all of the following are true.

1. Every gate `uuid` is unique across the document, and every wire id is unique.
2. Every `connect` names a gate `uuid` that exists on the same page.
3. Every `cross` names a segment id that exists in the same wire, and the reference is
   symmetric (if A crosses B, B crosses A).
4. Every wire has at least one id and at least one `connect` somewhere in its segments.
5. For each segment, `begin <= end` component-wise, and the orientation flag agrees with
   the endpoints (`h` ⇒ `begin.y == end.y`, `v` ⇒ `begin.x == end.x`).
6. Each `cross.at` lies within the segment's extent along its axis.
7. Page indices are non-negative, unique, and no greater than 255. Gaps are allowed. A
   document with pages 0 and 5 loads with the empty pages between them. Writers emit page
   indices contiguously from 0. A reader skips an index outside the range (section 8.2).

A writer MUST produce valid documents. A reader MAY reject an invalid one. CedarLogic does
not currently which causes these failures to be silent.

---

## 3. Version 1

The original format. CedarLogic 1.x wrote it. CedarLogic v2.3.8-1 and higher can export this via
the File > Export v1.x Compatible menu item.

### 3.1 Lexical rules

The v1 format resembles XML but is not XML, and an proper XML parser mostly likely can't read it.

A tag runs from `<` to `>`. The tag name is everything in between and can contain spaces, as
in `<page 0>`. A closing tag is `</name>`.

There are no attributes, namespaces, self-closing tags, prologues, DTDs, or entity
references. The text `&amp;` means exactly those five characters.

An element holds child elements and text, mixed together. A reader joins every run of text
in the element into one string and trims that string once at each end. The trim set is
exactly space, tab, carriage return, and newline. The result is not the text after the last
child. In `<input><ID>IN_0</ID>30 </input>` the element has a child `ID` and the text `30`.

A literal `<` inside a value is written as the byte 0x07, and a reader MUST map it back. This
applies to element text and never to tag names. It is the only escape method the format has,
so a value cannot contain a real 0x07 byte. The `>` character is never escaped.

The `#` character opens a comment that runs to the end of the line in CedarLogic 1.x and
2.x. This applies between tags and inside a value and a writer is not required to escape it.
A `LABEL_TEXT` value containing `#` therefore loses everything after the `#` in those versions,
while 3.x it keeps the whole value.

Newlines inside a value are ambiguous in the same way. CedarLogic 1.x and 2.x drop them.
3.x keeps interior newlines and trims only the ends. A multi-line value therefore does not
survive a round trip through both, so a writer SHOULD avoid multi-line values.

Both LF and CRLF line endings appear in existing files and readers SHOULD accept both. The byte
encoding is unspecified and readers pass bytes through, so treat a file as UTF-8. A UTF-8
byte order mark at the start of a file is skipped by detection and by both readers. A mark
anywhere else is ordinary text.

### 3.2 Grammar

The original implementation never wrote a grammar for the legacy encoding. This is what its
reader accepts however.

```
document  ::= junk* element (junk* element)* junk*
element   ::= "<" name ">" content "</" anyname ">"
content   ::= (text | element)*
name      ::= any bytes except ">"          ; may contain spaces: "page 0"
anyname   ::= any bytes except ">"          ; NOT checked against the opening name
text      ::= any bytes except "<"          ; 0x07 stands for a literal "<"
junk      ::= any byte that does not open an element, including a stray closing tag
```

Text runs inside one element are concatenated across its children and trimmed once, at the
ends. There are no attributes, no self-closing tags, no entity references, and no prologue.

### 3.3 Document structure

The element names carry all the structure. A document is a `<circuit>` holding pages,
each page holding gates and wires:

```xml
<circuit>
<CurrentPage>0</CurrentPage>
<page 0>
<PageViewport>left,top,right,bottom</PageViewport>
<gate>…</gate>
<wire>…</wire>
</page 0>
<page 1>…</page 1>
</circuit>
```

Page elements are matched by the prefix `page` on the tag name, and the index is read as a
word followed by an integer. This has three consequences. `<pageant>` counts as a page.
`<page>` and `<page0>` come out as index 0, because the read fails and leaves the field at
its default. `<CurrentPage>` escapes the rule only because of its capital C. A writer MUST
emit exactly `page N`.

Structurally the parser is permissive: top-level bytes outside any tag are skipped, and
stray closing tags are ignored. That, not any special knowledge, is what lets it walk past
the v2 preamble (§4).

Where two children share a name, the reader takes the first. This covers `<ID>`, `<type>`,
`<position>`, and `<points>`. At top level the rule reverses: the last `<circuit>` and the
last `<version>` win.

### 3.4 `<gate>`

```xml
<gate>
<ID>2</ID>
<type>AE_DFF_LOW</type>
<position>-50.5,-9</position>
<input><ID>IN_0</ID>30 </input>
<output><ID>OUT_0</ID>34 </output>
<gparam>angle 0.0</gparam>
<lparam>INPUT_BITS 1</lparam>
</gate>
```

`ID` gives the uuid, `type` gives the library gate name, and `position` gives the
coordinates as `x,y`.

An `<input>` or `<output>` element names a pin in its `<ID>` child. Its text is the list of
ids of the wire attached to that pin, and every id is followed by a space, including the
last. There is one element for each connected pin, and nothing for an unconnected pin. v1
writes one id, v2 writes all of them. No reader uses the input or output distinction, and
CedarLogic ignores these elements completely, because the wires carry the same information
and are authoritative. Write them if the file is for CedarLogic 1.x, which needs them. Two
details matter if you do. The text lists the ids of the whole wire, so every pin of a bus
repeats the same list. The `<ID>` child inside these elements does not collide with the
gate's own `<ID>`. A reader looks only at direct children, so the order of the elements does
not matter.

A `<gparam>` or `<lparam>` holds a name and a value separated by the first space. The reader
trims the whole text first, so a value cannot start or end with a space, and a param name
cannot contain a space at all. A value can contain spaces. `<gparam>` marks a GUI param and
`<lparam>` a logic param.

An empty value is common, and it arrives by a route that is easy to get wrong.
`<gparam>LABEL_TEXT </gparam>` gives an empty value not because the text after the space is
empty, but because trimming removes the trailing space and leaves no space at all. With no
space, the name is the whole text and the value is empty. An `angle` param with no value
means angle 0.

The `angle` gparam becomes the gate's angle field and leaves the param list.

RAM contents are ordinary `<lparam>` entries. Each is named `Address:<n>` and holds the cell
value. They follow the params from the schema, in ascending address order. Only non-zero
cells appear, so a missing cell reads as 0. RAM is the only gate type with storage of its
own, so this convention covers all of it.

A reader MUST carry through any child or param it does not recognize.

An lparam whose schema type is `FILE_IN` or `FILE_OUT` names a file on the machine that
wrote it. A ROM image to load and a log to write are both examples. Writers leave these out,
because a path from one machine means nothing on another. The rule covers lparams only.
Nothing filters gparams.

### 3.5 `<wire>`

```xml
<wire>
<ID>12</ID>
<shape>
<hsegment>
<ID>0</ID>
<points>-3,4,7,4</points>
<connection><GID>2</GID><name>OUT_0</name></connection>
<intersection>5.5 1</intersection>
</hsegment>
<vsegment>…</vsegment>
</shape>
</wire>
```

- `<ID>` is whitespace-separated. A v1 wire has exactly one id, so v1 cannot represent a
  bus. Exporting a bus to v1 keeps only the primary id.
- Segments MUST be direct children of `<shape>`, and `<connection>` and `<intersection>`
  direct children of a segment. A reader MUST NOT look deeper, so a nested one is invisible
  rather than an error. A `<wire>` with no `<shape>` is a wire with no segments.
- `<hsegment>` / `<vsegment>` set `vertical`. `<points>` is `beginX,beginY,endX,endY`.
- `<connection>` gives `GID` (a gate uuid) and `name` (a pin).
- `<intersection>` text is `<coordinate><SP><otherSegmentId>`. One element per
  pair of coordinate and other segment. Repeated coordinates are normal.

### 3.6 Ordering

This section applies to all three versions. Two different things go by the name "order", and
they behave in opposite ways.

A parse followed by a rewrite preserves document order exactly. The reader keeps pages,
gates, wires, segments, connects, and intersections in the order it met them. The v3 writer
emits them in that same order, with two regroupings: all gates before all wires within a
page, and all connects before all crosses within a segment (section 5.5). This is what makes
a read and a rewrite reproduce the same bytes, and an independent implementation depends on
it more than on anything else here.

The CedarLogic editor normalizes order instead of preserving it.

- Writing v3, gates and wires are sorted by id, so two saves of an unchanged circuit are
  byte-identical and a `.cdl` in version control diffs to just what changed.
- Writing v1 or v2, they are not. Those writers emit in hash-table order, which is
  unspecified and varies between runs, so byte-comparing two v1/v2 saves of one circuit is
  meaningless.
- Either way, a document's original gate and wire order does not survive a load-and-save.
  Only its content does.
- gparams come before lparams. Each group is sorted by the byte value of the name, so
  `angle` sits after every uppercase name such as `VALUE_BOX` and `TEXT_HEIGHT`.
- Segments are ordered by segment id. Intersections are ordered by coordinate, then by the
  order they were added.
- `<connection>` elements follow the order the segment recorded them in.
- RAM `Address:` lparams come after the ordinary lparams, in ascending address order, and
  are never filtered.

### 3.7 Malformed input

A field that is present but malformed is an error, not something to guess at. All of the
following are refused:

- A `<position>` with fewer than two numbers, or a `<points>` with fewer than four. Earlier
  versions loaded these at the origin, which put the gate or segment somewhere the author
  never placed it.
- An `<intersection>` whose text is not a coordinate followed by a segment id.
- A `<gate>` with no `<type>`, or with no `<ID>` (section 2.1).

A reader still accepts three things quietly.

- Extra numbers beyond the two or four expected are ignored, and each comma-separated token
  is trimmed before conversion.
- An empty numeric token is skipped. A token that is not wholly a number is an error
  (section 12).
- Where two children share a name, the first one wins. That includes `<shape>`, which the
  list in section 3.3 does not mention.

---

## 4. Version 2

v2 is v1 with two additions and no change to the gate/wire grammar. Written by CedarLogic
2.x through 2.4.3, and today by 3.x under the File ▸ Export v2 menu item.

### 4.1 The decoy

A v2 file starts with a complete v1 circuit that is not the user's circuit. It holds two
`AA_LABEL` gates. Gate 2 reads "Go to https://cedar.to/vjyQw7 to download the latest
version!" and gate 3 reads "Error: This file was made with a newer version of Cedar Logic!".
The real document follows it.

```xml
</circuit>
<throw_away></throw_away>

	<version>2.4.3</version><circuit>
…the real circuit…
</circuit>
```

The reason is compatibility in the wrong direction. CedarLogic 1.x stops at the first
`<circuit>`. It therefore opens the decoy and shows the user a message, instead of mangling
a file it cannot represent.

The decoy is a fixed byte sequence. It is reproduced in full here because a v2 writer cannot
be built without it. It runs from the leading newline through the tab that follows
`<throw_away>`.

```xml
\n
<circuit>\n
<CurrentPage>0</CurrentPage>\n
<page 0>\n
<PageViewport>-32.95,39.6893,61.95,-63.2229</PageViewport>\n
<gate>\n
<ID>2</ID>\n
<type>AA_LABEL</type>\n
<position>14.5,-13</position>\n
<gparam>LABEL_TEXT Go to https://cedar.to/vjyQw7 to download the latest version!</gparam>\n
<gparam>TEXT_HEIGHT 2</gparam>\n
<gparam>angle 0.0</gparam></gate>\n
<gate>\n
<ID>3</ID>\n
<type>AA_LABEL</type>\n
<position>14.5,-9.5</position>\n
<gparam>LABEL_TEXT Error: This file was made with a newer version of Cedar Logic!</gparam>\n
<gparam>TEXT_HEIGHT 2</gparam>\n
<gparam>angle 0.0</gparam></gate></page 0>\n
</circuit>\n
<throw_away></throw_away>\n
\n
\t
```

(`\n` and `\t` mark the literal newline and tab. Everything else is verbatim.) A v2 writer
SHOULD reproduce it exactly. Readers care only about the shape, which is a complete
`<circuit>` before `<throw_away>`. A writer that emits a different decoy still produces a
file every reader accepts. It only stops being useful to CedarLogic 1.x, and 1.x is the
whole reason the decoy exists.

A v2 reader MUST use the last top-level `<circuit>` element in the document. Note that
no reader knows what `<throw_away>` means beyond detecting it. Taking the last `<circuit>`
is the whole mechanism.

### 4.2 `<version>`

Free text. Files written by CedarLogic 2.x carry `MAJOR.MINOR.PATCH | timestamp`, e.g.
`2.4.1 | 2026-03- 6 19:09:56`, where the day is space-padded. A v2 export from 3.x writes
the bare version `2.4.3` with no timestamp. Only the part before `" | "`, and within that
only the major component, has ever affected a reader. It records the format's lineage, not a
format version number. Two consumers:

- The reader turns it into the model's `generator`, as `"imported from CedarLogic " +
  <version>`, taking the last *top-level* `<version>`. A v1 file has none, so a v1
  import yields an empty generator, and a v1 file rewritten as v3 emits `(generator "")`.
- CedarLogic's newer-version guard (§8.1) reads the first `<version>` anywhere in the
  raw bytes and refuses the file if its major component exceeds its own.

A writer targeting v2 SHOULD emit `2.4.3`, the last release whose Save default was v2,
instead of its own version. A reader refuses any file whose major version is above its own.
A writer that stamps its own 3.x version therefore produces a v2 file that no CedarLogic 2.x
can open. 2.x is the audience a v2 file is for.

The tag therefore has three different scopes in three places. The detector matches it
anywhere (section 6). The version guard takes the first one anywhere. The generator takes
the last one at top level. A `<version>` nested inside `<circuit>` satisfies the first two
and is invisible to the third.

### 4.3 Buses

v2 lifts the v1 single-id restriction: a `<wire><ID>` MAY hold several whitespace-separated
ids, and a gate's `<input>`/`<output>` payload MAY list several. This is the only difference
in what the *writers* produce. There is no difference at all on the read side beyond
last-`<circuit>` and `<version>`: the same code parses both.

---

## 5. Version 3

The current format, and what CedarLogic 3.x writes by default. Rationale: v1/v2 have no real
grammar, one ad-hoc escape that collides with a comment character, no way to distinguish
malformed from truncated, and diffs hostile to review. v3 is a small, fully specified
grammar with quoting that works.

### 5.1 Grammar

```
node    ::= list | symbol | string
list    ::= "(" node* ")"
symbol  ::= (any byte except space, tab, CR, LF, "(", ")", '"')+
string  ::= '"' ( "\" any-byte | [^"\] )* '"'
ws      ::= " " | "\t" | "\n" | "\r"
```

- A document is one top-level node. A reader MUST reject anything after it other than
  whitespace. Earlier versions ignored trailing bytes, so a pair of concatenated documents
  loaded as the first one alone.
- A backslash escapes the next byte, which is taken literally: `"a\nb"` is `a`, `n`, `b`
  and not a newline. A string cannot express a newline with an escape. It can contain a raw
  newline byte, which the parser accepts. The writer escapes only `\` and `"`.
- Whitespace is exactly those four bytes. Vertical tab, form feed, and NUL are symbol
  characters, so a stray one silently corrupts a symbol rather than erroring.
- There are no comments and no quote/quasiquote syntax. `;` and `'` are ordinary symbol
  characters, so a Lisp-style comment parses as data.
- `()` is a legal empty list.

### 5.2 Numbers

Numbers are written as bare symbols:

- Integral values with `|v| < 1e15` are written as integers with no decimal point.
- Everything else is written with 10 significant digits in the default
  `%g` style, which emits scientific notation when the exponent warrants: `1e15` →
  `1e+15`, `123456789012.3456` → `1.23456789e+11`, `1e-5` → `1e-05`. `+` and `e` are legal
  symbol characters, so these read back fine.
- 10 significant digits does not round-trip a double (which needs 17). `1/3` becomes
  `0.3333333333`, and `1234567890.5` becomes `1234567890`. See section 11.
- `nan` and `inf` are written bare and read back. `inf` round-trips. `nan` breaks equality.
  Nothing validates coordinates on the way in.

A reader MUST reject a number with trailing junk. `3abc` is an error, not 3. The reference reader
parses with `strtod` and then requires the remainder to be whitespace, so it accepts hex
such as `0x10` and the word `infinity` as whole tokens. A stricter reader MAY reject those.

One field departs from this: the `(page N)` index is read as an integer, not a double, so
`(page 1e3)` is malformed rather than page 1000. A writer MUST emit page indices as plain
decimal integers.

### 5.3 Document

```scheme
(cedarlogic
  (version 3)
  (generator "CedarLogic 3.0.1 | 2026-08-15 12:34:56")
  (page 0
    (gate "AE_DFF_LOW"
      (uuid "2")
      (at -50.5 -9)
      (angle 0)
      (lparam "INPUT_BITS" "1")
      (gparam "VALUE_BOX" "-0.8,-0.8,0.8,1.5"))
    (wire
      (ids "12" "13")
      (seg "0" h
        (pts -3 4 7 4)
        (connect "2" "OUT_0")
        (cross 5.5 "1")))))
```

The root is a list whose head is the bare symbol `cedarlogic`. Head position is the one
place where a symbol and a string are not interchangeable. A reader recognizes a head only
when it is a symbol. `("cedarlogic" …)` is therefore not a v3 document, and a quoted
`("gate" …)` or `("uuid" …)` is invisible. A reader skips it where the child is optional,
and fails where the child is required. A writer MUST emit every head as a bare symbol.

`(version N)` MUST be present. CedarLogic reads it as a number and truncates it to an integer.
It accepts `3.7` as 3 and rejects any value whose integer part is not 3. A writer MUST emit
exactly `3`.

`(generator "…")` MUST be present, and a reader MUST reject a v3 document without it. Its
content is free text with no defined meaning and MAY be the empty string, which is what a v1
file rewritten as v3 produces (section 4.2).

`(page N …)` holds the index first, then any number of `gate` and `wire` children in any
order.

`(gate "libName" (uuid "…") (at x y) (angle deg) …)` requires all four of those fields, and
a reader MUST reject a gate missing any of them. Any number of `(gparam "name" "value")` and
`(lparam "name" "value")` children follow. The defaults in section 2 cover the v1 and v2
encodings, which leave fields out silently. v3 does not.

`(wire (ids "a" "b" …) (seg …) …)` holds the ids and then the segments. A reader MUST accept
a wire with no `(ids …)` at all, and a writer MUST NOT emit one (section 2.3, item 4). The
reference writer always emits the list, empty if it has to, and a wire that reaches the
editor with no ids is dropped (section 8.2).

`(seg "id" h|v (pts bx by ex ey) (connect "gateUuid" "pin")… (cross at "segId")…)` takes the
id and the orientation by position and then requires `pts`. The connects and crosses are
optional.

- Unknown nodes at any level are ignored.

Structural rules the grammar does not show:

- Nesting is fixed, exactly as in v1/v2 (§3.5): a reader MUST NOT search deeper than the
  positions given here, and a writer MUST NOT nest these constructs. `gate` and `wire` are
  recognized only as direct children of a `page`. `gparam` and `lparam` are recognized only
  as direct children of a `gate`, and `connect` and `cross` only as direct children of a
  `seg`. A nested `(gate …)` is invisible, not an error.
- Minimum arity, below which the reader errors: `(at x y)` 3 items, `(pts …)` 5,
  `(connect …)` 3, `(cross …)` 3, `(seg …)` 3 plus a `pts` child. A `(page N)` index MUST be
  present and MUST parse as an int.
- A reader cannot tell an empty `(ids)` from an absent one. Both give zero ids. Every element of `(ids …)` MUST be an atom: a nested list there is an error, not
  an ignorable unknown node. The same holds inside `(at …)`, `(pts …)`, `(connect …)`, and
  `(cross …)`. The rule that unknown nodes are ignored covers unknown children. It does not
  cover a list sitting where a value belongs.
- The parser neither dedupes nor sorts page indices. Pages arrive in document order
  carrying whatever index they claim. Item 7 in section 2.3 is a rule about documents, not
  something the reader enforces.
- Encoding is unspecified, as in v1/v2: symbols and strings are raw byte strings with no
  normalization. A leading byte order mark is skipped before parsing (§6).

Reference-reader lenience. None of it is a guarantee, and a writer MUST NOT rely on it:

- Atoms are not typed in value positions. Values are read by index, so a symbol and a
  string are interchangeable there: `(uuid 7)`, `(at "1" "2")`, and `(version "3")` all
  parse. This does not extend to head position (above). Such a file round-trips through
  the model but not the bytes, because the writer re-quotes. The symbols-for-keywords,
  strings-for-user-data split is a writer convention introduced in this section, not a rule
  §5.1's grammar expresses.
- The orientation token is exactly `h` or `v`. Any other token is an error, as is a list in
  that position or nothing at all.
- Duplicate keys resolve inconsistently: a keyed lookup takes the first match, so a second
  `(uuid …)` is ignored, while gparams and lparams are collected by full scan, so duplicate
  params are all kept.

### 5.4 What v3 drops on purpose

- The decoy circuit. A v1/v2 reader given a v3 file finds no `<circuit>` and errors, which
  is honest enough.
- `<CurrentPage>` and `<PageViewport>`. View state does not belong in the document.
- The gate-side `<input>`/`<output>` copy. Wires are the single source of truth, so the
  duplicate cannot go stale.
- The application version as a gate on loading. `(version 3)` is a format version.
  `(generator …)` records who wrote the file and no reader parses it.

### 5.5 Formatting

The writer pretty-prints: each nested list starts on its own line, indented two spaces per
level. Atoms are written inline, separated by one space, on whatever line they land on, so a
leading run of atoms stays on the head's line and an atom after a nested list follows that
list's closing paren. The file ends with a newline. It also regroups each page as all gates
then all wires, even though readers accept them interleaved. This is writer convention, not
grammar: a reader MUST accept any amount of the whitespace §5.1 defines wherever the grammar
allows it, and a rewriting tool SHOULD follow the pretty-printing convention so diffs stay
small.

The app sorts gates and wires by id before writing (§3.6), so two saves of an unchanged
circuit produce identical bytes and a `.cdl` in version control diffs to just what changed.

---

## 6. Detection

Detection is looser than it looks. Only the `(` test inspects the first non-whitespace
character. The rest are unanchored substring searches over the whole file, in this order.

| Test | Result |
|---|---|
| a leading UTF-8 byte order mark | skipped, then the tests below apply |
| empty or all whitespace | `Unknown` |
| first non-space char is `(` and the text contains `cedarlogic` anywhere | `SexprV3` |
| first non-space char is `(`, no `cedarlogic` | `Unknown` |
| text contains `<throw_away>` or `<version>` anywhere | `XmlV2` |
| text contains `<circuit>` anywhere | `XmlV1` |
| otherwise | `Unknown` |

So `junk<circuit>…` detects as v1. A leading UTF-8 byte order mark is skipped before any of
these tests run, so it does not hide the format. An unrecognized file is an error. Nothing
guesses.

This sniff is safe in practice only because legacy values BEL-escape `<`, so a marker does
not appear inside user data in a v1/v2 file CedarLogic wrote. A hand-written file is under
no such constraint, and nothing rejects a raw `<` in a value on the read side. The v3
escape rules do not offer the same protection: a v3 param value may legally contain the
literal text `<version>`, which matters for the guard in §8.1.

One reader parses both v1 and v2, and both produce a model marked as version 3.
There is no v1 or v2 model, only a v1 or v2 file. A reader that wants to offer
"save in the format I opened" MUST record the detected version next to the model. The model
itself carries no trace of where it came from.

---

## 7. Migration

The reference loader sniffs, parses, and for legacy input runs the migration handlers,
returning the model, the detected source format, and notices for the UI. v3 files are not
migrated.

Handlers run in order over the parsed model, and migration finishes before the caller sees
the document. A caller that counts gate types sees the renamed `libName`, never the
original. A notice carries severity, the affected gate, a one-line summary, a longer detail, and
whether it was already fixed.

**Gate renames.** A table of `from → to` with an optional note. A null note
is a silent alias. A note means the behavior changed and the user should hear about it.

| From | To | Note |
|---|---|---|
| `AM_RAM_16x16_Single_Port` | `AM_RAM_16x16` | none |
| `AA_DFF` | `AE_DFF_LOW` | reset polarity changed |
| `BA_JKFF` | `BE_JKFF_LOW` | reset polarity changed |
| `BA_JKFF_NT` | `BE_JKFF_LOW_NT` | reset polarity changed |

A reader that skips these renames will produce gate types CedarLogic no longer has. They are
part of reading v1/v2 correctly, not an optional courtesy.

**Decoder output width.** Decoders historically sized their output bus as `inBits²` instead
of `2^inBits`. This migration changes nothing in the document. The correct width comes from
the gate library when the gate is built. The migration exists only to warn when that
correction costs a saved circuit something. The rules are these.

- A gate counts as a decoder by case-insensitive substring match on `decoder` in `libName`,
  not by a name list.
- If `INPUT_BITS` appears more than once, the last one wins. A missing or non-positive
  value skips the gate.
- It returns early unless `2^inBits < inBits²`, which for any sane `INPUT_BITS` is true only
  of `inBits == 3` (9 outputs where there should be 8). 1 is skipped, 2 and 4 agree, 5 and
  up gain outputs. `INPUT_BITS` above 30 is skipped outright, so the shift cannot reach the
  undefined range. A decoder that wide would want a billion outputs and is not a real gate.
- It warns only if some wire actually connects to one of the vanishing `OUT_n` pins, naming
  them. Otherwise silent, because real files carry many such decoders and a notice each
  would be noise. The doomed `connect` entries are not removed, and they do not politely
  fail either: they are registered against a pin the gate does not have, and are written back
  out on save. A reader MAY drop a `connect` that names a pin its library does not have.
  CedarLogic keeps it.

A migration MUST be idempotent on the document: running it twice MUST produce the same
result. Both of the above are, since no rename target appears as a rename source and the
decoder rule does not change the document at all. Notices are not idempotent. A second run
reports the same ones again, so run the set once per load.

---

## 8. How the reference application loads a document

Nothing in this section is part of the format. It is here because a document can be
perfectly conforming, parse without error, and still lose content on the way to the screen.
A writer needs to know what CedarLogic does with what it is given.

### 8.1 The newer-version guard

Before detection or parsing, CedarLogic scans the raw bytes for the first
`<version>…</version>` and compares it to its own version:

- Both strings are split on the exact separator `" | "`, and the part before it is read as
  `int . int . int`. Components that do not parse read as 0, so a malformed version never
  blocks.
- Only a greater major version blocks. Minor and patch are read but never change the
  outcome.
- A blocked file is never parsed. The user is told to get a newer version.

Two consequences for a writer. A file claiming a major version above the reader's is refused
outright, which is what the tag is for (§4.2). And because the scan is a raw substring
search that predates format detection, a v3 file is refused too if any param value contains
the literal text `<version>9.0</version>`. v3 has no version tag of its own, so this is the
only way a v3 document can trip the guard.

### 8.2 What a document can lose on the way to the canvas

- A gate whose `libName` is not in the loaded gate library becomes a phantom. It keeps its
  name and params, and is written back out on save, but it has no shape, no pins, and no
  presence in the simulation, so it is invisible and unclickable. The load reports it.
  Connections aimed at it are still recorded, including ones naming pins it does not have.
- A wire is dropped entirely if any of these hold: it has no ids, it has no `connect`
  anywhere in its segments, or a `connect` names a gate uuid that does not exist on that
  page. The load reports each one and names the reason. This is the most common way a
  hand-written file loses content, so the report is the only sign the user gets.
- Duplicate endpoints collapse. Two `connect` entries naming the same (gate, pin) on one
  wire produce one connection, though both survive in the routing.
- Page indices are honored, within bounds. A file with pages {0, 5} loads with six
  pages and its content on page 5. An index that is negative or above 255 is skipped.
- Unknown params are kept, forwarded to the simulation core, and written back out
  unchanged.

Everything above that costs the document something is reported. The migrations and the load
share one list, which the editor shows in a single dialog after the circuit appears, and
which a headless render prints to stderr.

### 8.3 What a failed load leaves behind

A document is read and validated before anything is cleared, so a file refused by the
version guard or rejected by the parser changes nothing: the circuit already open stays on
screen, still bound to its own filename, and the user sees an error. Nothing is written, and
no partially-applied circuit is left on the canvas.

---

## 9. Writing

Which version to write is a policy question, not a format one. This section records the
policy CedarLogic follows. It is worth knowing for two reasons. It is a worked answer to the
question, and a tool that edits the same files should not surprise a user who moves between
it and CedarLogic. The "Written when" column names CedarLogic menu items and behavior.

| Version | Written when |
|---|---|
| v1 | File ▸ Export v1.x Compatible; Save/Save As on a v1 file whose format the user keeps |
| v2 | File ▸ Export v2; Save/Save As on a v2 file whose format the user keeps |
| v3 | Save/Save As for new or v3 files; "Convert to V3" on open or on save; autosave, always |

v3 is the default for anything new. The exports exist for sharing with classrooms still
running older builds.

Format is sticky per open document. Opening a v1/v2 file records its format and offers
conversion right away. Declining leaves the question open, and the first save asks again
with Convert, Keep, or Cancel. Choosing Convert pins the document to v3 for the rest of the
session. Choosing Keep pins the original format and stops asking. Cancelling decides
nothing, so the next save asks again, and a failed on-open conversion likewise leaves the
question open. Autosave snapshots are always v3 regardless, and recovering from one resets
the document to v3, quietly ending v1/v2 stickiness.

Lossiness.

| Path | Loses |
|---|---|
| → v1 | bus wires collapse to the primary id; coordinates rounded to 6 significant digits |
| → v2 | coordinates rounded to 6 significant digits |
| → v3 | non-integral coordinates rounded to 10 significant digits; integral ones exact below 1e15 (§11) |
| any | view state (which was never in the model), param order, and the document's original gate/wire order (§3.6) |
| any, via the app | everything below float precision, ~7 digits, before the writer even runs |

The 6-digit legacy rounding is easy to miss, since nothing announces it. It also sets a
practical ceiling: CedarLogic holds coordinates in single precision throughout, so no path
through the editor preserves more than about 7 significant digits, and a writer gains
nothing by writing more. The v3 format itself carries more than that (section 5.2). The
application is the limit, not the format.

CedarLogic writes `<CurrentPage>` as 0 in every v1/v2 file it produces, whatever page is
selected, so a reader gains nothing by honoring it.

Note that the v1 bus warning does not mean the export was refused: the file is written, and
the warning tells the user what the older format could not carry.

---

## 10. Conformance

This section restates the requirements that define each conformance class. It is a summary,
not the whole of the normative text: every MUST, MUST NOT, SHOULD, and MAY elsewhere in this
document binds the class it names, and this section does not weaken any of them.

Three classes are defined. A conforming document satisfies §2.3. A conforming reader accepts
every conforming document, except where a requirement below explicitly permits it to be
stricter, and satisfies those requirements. A conforming writer emits only conforming
documents and satisfies the writer requirements below. An implementation MAY be a reader, a
writer, or both.

### 10.1 Reader requirements

A conforming reader MUST:

1. Determine the version from content, not from the filename or extension, and reject input
   it cannot classify instead of guessing. It MAY be stricter than the test in section 6, for
   example by anchoring the markers. It SHOULD document any such difference.
2. Take the last top-level `<circuit>` in a v1 or v2 document.
3. Map `0x07` to `<` in v1/v2 element text, and MUST NOT do so in tag names.
4. Split `<gparam>`/`<lparam>` on the first space only, and accept an empty value and a
   value containing spaces.
5. Ignore unknown elements, children, nodes, and params at every level, in every version.
6. Reject a v3 document whose `(version …)`, after any truncation the reader performs, is
   not 3. A reader MAY reject a version with a fraction, such as `3.7`. It MUST NOT accept
   a version whose integer part is not 3.
7. Preserve every segment id, orientation, endpoint, connect, and intersection it reads,
   without dropping, merging, or synthesizing any. A reader that also writes text back MUST
   preserve their file order too (section 3.6). A reader that feeds an editor MAY reorder
   them, as CedarLogic does.
8. Expose params it does not recognize to its caller unchanged, values included, so that a
   writer downstream can round-trip them.
9. Accept a v3 wire with no `(ids …)`, and a `(page N)` whose gates and wires are
   interleaved in any order.
10. Bound its nesting depth, and bound any value it takes from the document before using it
    as a size, a count, or an index (§13).
11. Recognize a v3 list head only when it is a bare symbol, and reject a document whose root
    head is not the symbol `cedarlogic`.

A conforming reader SHOULD:

12. Accept both symbols and strings in v3 *value* positions, since the reference reader does
    and files in the wild may exercise it. This does not apply to head position, which
    item 11 governs.
13. Refuse a document rather than repair it. Where a field is present but malformed, the
    reference reader now errors (§3.7), and a reader that instead substitutes a default
    SHOULD say so, because a circuit that loads wrong is harder to notice than one that
    does not load.
14. Impose a maximum input size, for the reasons in §13.

### 10.2 Writer requirements

A conforming writer MUST:

1. Emit `(version 3)` and a `(generator …)` for v3, and quote all user data as strings
   rather than relying on the reader's tolerance of bare symbols.
2. Escape `\` and `"` in v3 strings. Symbols are emitted raw and are not escaped, so a
   symbol MUST NOT contain whitespace, parens, or a quote.
3. Emit `angle` as a `<gparam>` for v1/v2 and as `(angle …)` for v3.
4. Emit a v3 segment's orientation as exactly `h` or `v`, matching its endpoints, and use
   `<hsegment>` / `<vsegment>` in v1/v2. A reader treats any other v3 token as horizontal
   without complaint, so a typo here is silently wrong rather than an error.
5. Emit every id as a non-negative decimal integer within the range of the conversion §2.1
   gives for that field, and MUST NOT emit `-1` as a gate uuid.
6. Emit page elements in v1/v2 as the tag name `page`, one U+0020 space, then the decimal
   index with no leading zeros.
7. Emit only conforming documents (§2.3), including `begin <= end` on every segment and at
   least one id on every wire.

A conforming writer SHOULD:

8. Avoid `#` in v1/v2 values, which CedarLogic 1.x and 2.x read as a comment (§3.1).
9. Tell the user when an export drops data that the target version cannot hold. A bus
   written to v1 keeps only its primary id (section 3.5). Write the file and report the
   loss. Refusing to write it is not the established behavior.
10. Follow the v3 pretty-printing convention of §5.5, so diffs stay small.

---

## 11. Round-tripping and numeric precision

Let `read` parse a document into the model of §2 and `write` serialize that model as v3.
Model equality is structural and includes `generator`, so a legacy import round-trips only
if the synthesized `"imported from CedarLogic …"` string (§4.2) is preserved verbatim.

The properties an implementation should hold to are:

```
read(write(read(D))) == read(D)          // model equality
write(read(write(read(D)))) == write(read(D))   // byte equality of the text
```

Both hold for documents whose numbers survive 10 significant digits. They are not universal:
coordinates are compared with exact `double ==`, so any value needing more than 10
significant digits changes on the first write and the first equality fails. What is
unconditional is stability from the second write onward. `nan` is the one exception to the
model line even there: it writes and reads back as `nan`, so the bytes stay stable while
`NaN != NaN` keeps the models unequal forever.

In practice the 10-digit ceiling is only reachable by a hand-written file: CedarLogic keeps
coordinates in single precision throughout (§9), so anything it produces carries at most ~7
significant digits and survives the writer untouched.

Reading a legacy file and writing it back as v3 is therefore stable in practice but never
byte-preserving for v1/v2: view state, the decoy, and the gate-side connection copy are
dropped by design, and coordinates were already rounded to 6 digits by the legacy writer.

An independent implementation SHOULD be checked against, at minimum: a BEL-escaped `<` in a
value, an empty param value, a value containing spaces, a bus wire, a v2 decoy skip, a `page
N` tag with a space in the name, a gate rename migration, and a 3-bit decoder with a wire on
`OUT_8`.

Real circuits are available in `format/tests/fixtures/` in the CedarLogic repository. They
are useful for checking a parser against files a real user produced, but they are not a
conformance suite and do not cover the list above: none of them contains a BEL escape, a bus
wire, or a gate type that migrates. They do exercise the v2 decoy, `page N` tags, values
containing spaces, and 3-bit decoders whose surplus outputs carry no wire. Reporting zero
migration notices on all of them is the correct result, not a sign of a broken migrator.

---

## 12. Error handling

Both readers throw on malformed input. A caller that loads untrusted files has to be ready
for that.

Every error is a `std::runtime_error` carrying a message fit to show a user. The complete
set:

- `cdl: unterminated tag`, `cdl: unclosed <name>`, `cdl: no <circuit> element`
- `cdl: <position> needs two numbers`, `cdl: <points> needs four numbers`,
  `cdl: <intersection> needs a coordinate and a segment id`, `cdl: <gate> has no <type>`
- `sexpr: unterminated string`, `sexpr: unbalanced '('`, `sexpr: unexpected ')'`,
  `sexpr: unexpected end of input`
- `circuit file: not a (cedarlogic ...) document`
- `circuit file: unsupported formatVersion N`
- `circuit file: malformed number "x" in (at ...)`, `circuit file: malformed integer ...`,
  `circuit file: number out of range ...`, `circuit file: integer out of range ...`
- `sexpr: nesting too deep`, `cdl: nesting too deep`
- `circuit file: trailing content after the document`
- `circuit file: segment orientation "X" is not h or v`
- `circuit file: identifier is not a decimal integer "x" in (uuid ...)`,
  `circuit file: empty identifier "" in (ids ...)`
- `circuit file: missing (name ...)` and `circuit file: malformed value in (name ...)`,
  where `name` is the head of the enclosing list. That head is empty when it is not a bare
  symbol, so `circuit file: malformed value in ( ...)` is a message you can actually see
- `loadCircuit: unrecognized .cdl format`

Numeric fields are part of that set: a bad coordinate reports `circuit file: malformed
number "foo" in (at ...)`, naming the field it failed in. Not every legacy numeric throws.
Wire ids, intersection coordinates, and the page index are read by stream extraction that
fails quietly, which is why section 3.7 describes them as degrading silently.

A partially-parsed document is never returned from the parse. That guarantee stops at the
parse: applying a parsed document to the canvas drops content silently in the cases listed
in §8.2.

---

## 13. Security considerations

Circuit files circulate by email and shared drives, so a reader should assume the document
in front of it was written by someone else and may be hostile. These are the hazards the
format itself creates, and what a reader is expected to do about them.

- Unbounded nesting. Both encodings nest, and the natural implementation recurses per
  level, so a file of a few hundred thousand nested `(` or `<t>` exhausts the stack. A
  reader MUST bound nesting depth. 64 is a generous limit: a well-formed document reaches 5
  levels in v3 and 7 in v1/v2, and neither grows with the size of the circuit.
- Unbounded numbers used as sizes. A gate's `INPUT_BITS` is a free-form param that
  decides pin counts. A reader MUST bound it before it computes anything from it. A decoder
  claiming 31 input bits overflows a plain `1 << bits`. One claiming a million asks for a
  million pins.
- Indices used as offsets. A page index is an integer from the file. A reader MUST NOT
  use one to index an array without checking both ends: negative values are the obvious
  hazard, and a huge value invites an allocation per step to reach it. The same caution
  applies to ids (§2.1), where an unparseable value converts to 0 and can alias two distinct
  objects onto one.
- No size limit is expressed by the format. Nothing caps the document, the page count,
  the gate count, or the length of a param value. A reader that streams untrusted files
  should impose its own ceilings.
- Silent truncation is legal. v3 ignores everything after the first top-level node, and
  the legacy reader skips top-level bytes it cannot parse, so two conforming readers can
  disagree about what a file contains with neither reporting anything. A reader SHOULD warn
  rather than accept trailing content quietly.
- The legacy escapes are ambiguous. `#` opens a comment for CedarLogic 1.x and 2.x but
  not for readers that skip that rule, and interior newlines survive in one and not the
  other (section 3.1). The same bytes mean two things. A writer avoids the question by
  emitting neither.
- Params are opaque and are passed on. A reader MUST NOT treat a param value as trusted
  input to anything else. `FILE_IN` / `FILE_OUT` params name filesystem paths and are
  deliberately never written (§3.4), but nothing stops a file from carrying them.

None of this requires memory-unsafe parsing to hurt: the realistic outcomes are a crashed
process, an allocation blow-up, or two programs disagreeing about what a circuit is.

---

## 14. Compatibility

Two questions decide whether a program written against this document keeps working: what a
future version of the format promises to leave alone, and what it does not.

The promise is that unknown children are ignored, at every level, in every version. A later
writer can add child nodes or params, and every reader alive today will drop them without
complaint. That is the whole extension mechanism. It follows that anything a reader must not
be free to drop cannot be added this way and needs a new version instead.

Positional fields are outside the promise. `(gate <libName> …)`, `(page <index> …)`, and
`(seg <id> <h|v> …)` are read by position, so an atom added in front is taken as that field.
`(gate junk "AA_AND2" …)` gives a gate whose library name is `junk`, with no error. Nothing
can be added to those slots later.

A change visible in the bytes MUST raise `(version …)`. A reader that meets a version it does
not know SHOULD refuse the document rather than read the part it recognizes, because a
version you do not know is a document you cannot claim to understand.

The rest of this section is what the format does not promise. The behaviors below are
accidents of the reference implementation. They are described where they arise because they
decide whether real files load, but a later version MAY change any of them, and an
implementation MUST NOT depend on them.

This list covers the parser and the format. It is not a list of every quiet behavior in
CedarLogic: what a document can still lose between a clean parse and the screen is section
8.2, and that is application behavior rather than a property of the format.

- Atoms are untyped in value positions, so a v3 reader accepts `(uuid 7)` as readily as
  `(uuid "7")`.
- A duplicate key takes the first match instead of being an error.
- The page match in v1 and v2 tests a prefix, so `<pageant>` counts as a page (section 3.3).
- Detection searches the whole file rather than anchoring at the start, so bytes before
  `<circuit>` do not stop a v1 file from being recognized (section 6).
- The editor accepts page indices from 0 to 255 and skips the rest (section 8.2).

---

## 15. Worked example

The same circuit in all three versions: one page holding a 2-input AND rotated 90°, a 4-bit
counting register, an empty text label, and a two-line bus wire drawn as an L (a horizontal
segment meeting a vertical one at (1.5, 2)).

Pin names and params come from the gate library, not from the writer's imagination:
`AA_AND2`'s output is `OUT` (its inputs are `IN_0`/`IN_1`), and each gate carries the params
its library entry declares.

### v1

```xml
<circuit>
<CurrentPage>0</CurrentPage>
<page 0>
<PageViewport>-20,10,20,-10</PageViewport>
<gate>
<ID>1</ID>
<type>AA_AND2</type>
<position>-4,2</position>
<output><ID>OUT</ID>10 </output>
<gparam>angle 90.0</gparam>
<lparam>INPUT_BITS 2</lparam></gate>
<gate>
<ID>2</ID>
<type>AA_REGISTER4</type>
<position>6,0</position>
<input><ID>IN_0</ID>10 </input>
<gparam>VALUE_BOX -0.8,-0.8,0.8,1.8</gparam>
<gparam>angle 0.0</gparam>
<lparam>INPUT_BITS 4</lparam>
<lparam>MAX_COUNT 15</lparam>
<lparam>SYNC_CLEAR true</lparam>
<lparam>SYNC_LOAD true</lparam></gate>
<gate>
<ID>3</ID>
<type>AA_LABEL</type>
<position>0,6</position>
<gparam>LABEL_TEXT </gparam>
<gparam>TEXT_HEIGHT 2</gparam>
<gparam>angle 0.0</gparam></gate>
<wire>
<ID>10</ID>
<shape>
<hsegment>
<ID>0</ID>
<points>-3,2,5,2</points>
<connection><GID>1</GID><name>OUT</name></connection>
<connection><GID>2</GID><name>IN_0</name></connection>
<intersection>1.5 1</intersection></hsegment>
<vsegment>
<ID>1</ID>
<points>1.5,-2,1.5,2</points>
<intersection>2 0</intersection></vsegment></shape></wire></page 0>
</circuit>
```

Three details to read off this: the bus is gone (v1 keeps only the primary id, 10, and the
`<output>` payload carries that one id with a trailing space). `angle` sits in its
alphabetical place among the gparams, written as the string `0.0`. The two intersection
coordinates differ because each one names a coordinate along its own segment. 1.5 is the x
where the horizontal segment is met. 2 is the y where the vertical one is met.

### v2

Identical in structure, with three changes: the decoy circuit and `<throw_away>` precede it,
a `<version>` tag sits between them, and the wire keeps both bus ids.

```xml
<circuit>
…the two-label decoy circuit…
</circuit>
<throw_away></throw_away>

	<version>2.4.3</version><circuit>
<CurrentPage>0</CurrentPage>
<page 0>
<PageViewport>-20,10,20,-10</PageViewport>
<gate>
<ID>1</ID>
<type>AA_AND2</type>
<position>-4,2</position>
<output><ID>OUT</ID>10 11 </output>
<gparam>angle 90.0</gparam>
<lparam>INPUT_BITS 2</lparam></gate>
…
<wire>
<ID>10 11 </ID>
…
</circuit>
```

### v3

Byte-for-byte output of the reference writer for this circuit:

```scheme
(cedarlogic
  (version 3)
  (generator "CedarLogic 3.0.1 | 2026-08-15 12:34:56")
  (page 0
    (gate "AA_AND2"
      (uuid "1")
      (at -4 2)
      (angle 90)
      (lparam "INPUT_BITS" "2"))
    (gate "AA_REGISTER4"
      (uuid "2")
      (at 6 0)
      (angle 0)
      (gparam "VALUE_BOX" "-0.8,-0.8,0.8,1.8")
      (lparam "INPUT_BITS" "4")
      (lparam "MAX_COUNT" "15")
      (lparam "SYNC_CLEAR" "true")
      (lparam "SYNC_LOAD" "true"))
    (gate "AA_LABEL"
      (uuid "3")
      (at 0 6)
      (angle 0)
      (gparam "LABEL_TEXT" "")
      (gparam "TEXT_HEIGHT" "2"))
    (wire
      (ids "10" "11")
      (seg "0" h
        (pts -3 2 5 2)
        (connect "1" "OUT")
        (connect "2" "IN_0")
        (cross 1.5 "1"))
      (seg "1" v
        (pts 1.5 -2 1.5 2)
        (cross 2 "0")))))
```

Note what v3 does differently: `angle` is a field rather than a param, the empty
`LABEL_TEXT` needs no trailing-space trick to express, both bus ids survive, and no pin name
is repeated on the gate side.

---

## Appendix: version history

| Format | Written by | Notes |
|---|---|---|
| v1 | CedarLogic 1.x | No version tag, no buses. Still readable and exportable. |
| v2 | CedarLogic 2.0 – 2.4.3 | Adds the decoy, `<version>`, and buses. 2.4.3 is the last release whose Save default was v2. |
| v3 | CedarLogic 3.0.0 onward | S-expressions; the default for Save and the only format autosave writes. |

The app's own version string is `MAJOR.MINOR.PATCH | YYYY-MM-DD HH:MM:SS`, and the current
major is 3. That matters for §8.1: the app has crossed the major-version boundary it
enforces, so anything it stamps with its own version is refused by every 2.x install. This
is why a v2 export writes `2.4.3` instead of the running version (section 4.2). Without
that, the export is unreadable by the only versions that want it.

Old builds given a newer file behave differently. 1.x reads the v2 decoy and shows its two
labels. 2.x reads a v3 file, finds no `<circuit>`, and reports an error. The major-version
guard (§8.1) is a third, independent mechanism and applies only to files carrying a
`<version>` string.
