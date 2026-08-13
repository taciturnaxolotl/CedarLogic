<!-- footer -->

---

<details>
<summary>Which download do I want?</summary>

| Platform | File |
| --- | --- |
| Windows | `CedarLogic-__VERSION__-win32.exe` (installer) |
| macOS | `CedarLogic-__VERSION__-Darwin.dmg` (signed and notarized) |
| Linux | `CedarLogic-__VERSION__-Linux.tar.gz` |
| Web / embedding | `CedarLogic-__VERSION__-wasm.tar.gz`, also on [npm](https://www.npmjs.com/package/@cedarville/cedarlogic-engine) |

CedarLogic updates itself automatically on Windows and macOS so you only need
to manually update on Linux.

</details>

<details>
<summary>Verifying your download</summary>

Download [`checksums.txt`](https://github.com/taciturnaxolotl/CedarLogic/releases/download/__TAG__/checksums.txt) next to the file you grabbed, then:

```bash
sha256sum --ignore-missing -c checksums.txt
```

`OK` means the file is intact.

</details>
