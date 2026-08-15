# Releasing

Releases are cut from a tag. `task release` computes the next version from the
conventional commits since the last tag, stamps it into `CMakeLists.txt` and
`flake.nix`, tags, and pushes. Everything after the push is automated.

A published release is the announcement you wrote, followed by a footer with
the download table and checksum instructions. No generated commit list; if a
change is worth knowing about, write a sentence about it.

## Writing the announcement

```bash
task notes
```

This opens (creating it if needed) a **draft** release for the next version in
your browser. Write it there: a headline, a short paragraph per notable change,
screenshots dragged straight into the editor. Leave the footer alone, the
release workflow appends it.

Skipping this is fine. You get a release with just the footer, and `task
release` tells you which one you are about to cut.

## Cutting a release

```bash
task release
```

It refuses to run unless:

- you are on `master`,
- the working tree is clean,
- CI passed for the commit you are releasing.

Then it prints the version it intends to release and asks for confirmation.
`v2.4.3` becomes `v2.5.0` if there is a `feat:` in the range, `v2.4.4` if there
are only fixes, and a major bump on a `!` or `BREAKING CHANGE:` footer.

To pick the version yourself, pass it to either task:

```bash
task notes -- 3.0.0
task release -- 3.0.0
```

The `v` is optional, `3.0.0` and `v3.0.0` both land on the tag `v3.0.0`, and
`VERSION=3.0.0` works too if you would rather name it. Pass the same version to
both, or the announcement ends up drafted against a tag nobody releases.

## What the tag triggers

`.github/workflows/release.yml` runs on `v*.*.*` tags and does the rest:

1. builds the Windows installer, the Linux archive, the signed and notarized
   macOS DMG, and the WASM bundle,
2. keeps everything above the `<!-- footer -->` marker in the draft, appends a
   freshly stamped `.github/release-footer.md`, and publishes the release with
   the artifacts and `checksums.txt`,
3. regenerates `docs/appcast.xml` so Sparkle and WinSparkle offer the update,
   and commits it back to `master`,
4. publishes the WASM bundle to npm.

Because step 2 only ever regenerates what sits below the marker, re-running the
workflow on a tag is safe: your prose survives and the footer is not
duplicated. The Sparkle "what's new" pane gets the announcement without the
footer.

A `workflow_dispatch` run from a branch builds everything but publishes
nothing, which is the way to test a change to the release pipeline.

## Prereleases

Tags containing a hyphen (`v2.5.0-rc.1`) are marked as prereleases on GitHub.

## Tooling

The Nix devshell (`nix develop`, or direnv) provides `task`, `svu`, and `gh`.
Without Nix, install [Task](https://taskfile.dev); `task release` falls back to
`go run` for `svu` if it is not on your PATH.
