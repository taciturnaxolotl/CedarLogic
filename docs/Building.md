# Building

Building CedarLogic on Linux and Windows is possible. Doing so on MacOS should be possible.

## Windows

### Required Programs

- [ ] [Visual Studio](https://visualstudio.microsoft.com/downloads/) 2015 or newer. Used to build and edit code.
- [ ] [CMake](https://cmake.org/download/) used to generate Visual Studio build file from CMake cross-platform build definition.
- [ ] [NSIS](https://nsis.sourceforge.io/Download) used to build installer

### Build CedarLogic Installer

1. Go to the root of the CedarLogic git repo ([clone](https://www.git-scm.com/docs/git-clone) it if you haven't already).

2. Run `cmake -B build -A Win32`, note replacing `-A Win32` with `-G "Ninja Multi-Config"` makes building faster, but isn't officially supported

3. Run `cmake --build build --config Release --target package`

4. There is now a CedarLogic installer executable in the `build` directory. If you run the installer, you will have the latest semi-stable version of CedarLogic installed. If you open `build\CedarLogic.sln` with
Visual Studio you can work from there.

## Linux

### Required Programs

- [ ] Your choice of IDE/editor
- [ ] [CMake](https://cmake.org/download/) (probably use the version from your package manager)
- [ ] A C++ compiler.

### Dependencies

#### Debian based distributions

```bash
sudo apt install build-essential cmake git libwxgtk3.2-dev -y
```

Note, if you don't have at least libwxgtk3.2 or later, you can get around this by adding this flag to your cmake build command:
`-DUSE_SYSTEM_WXWIDGETS=0`.

#### Arch

```bash
sudo pacman -S --needed base-devel wxwidgets-gtk3 glu git cmake
```

### Build CedarLogic Executable

From within the root of the CedarLogic repo:

```bash
# If you struggled to get a late enough version of wxWidgets, this is the command you add `-DUSE_SYSTEM_WXWIDGETS=0` to.
cmake -B build -DCMAKE_BUILD_TYPE=Release # Assuming you want a release build, could be debug

cmake --build build -j8
```

There is now a CedarLogic executable in the `build` directory.

## macOS

### Required Programs

- [ ] Xcode Command Line Tools (`xcode-select --install`)
- [ ] [CMake](https://cmake.org/download/) (or `brew install cmake`)

### Build CedarLogic App Bundle

From within the root of the CedarLogic repo:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

There is now a `CedarLogic.app` in the `build` directory.

### Build DMG Installer

```bash
cd build
cpack -G DragNDrop
```

This creates `CedarLogic-<version>-Darwin.dmg` in the build directory.

### Code Signing (Optional)

For distribution outside the Mac App Store, you need an Apple Developer ID certificate.
To sign the app and DMG:

```bash
# Sign the app bundle (replace with your Developer ID)
codesign --deep --force --verify --verbose \
    --sign "Developer ID Application: Your Name (TEAMID)" \
    --options runtime \
    build/CedarLogic.app

# Create signed DMG
hdiutil create -volname "CedarLogic" -srcfolder build/CedarLogic.app \
    -ov -format UDZO build/CedarLogic-signed.dmg

# Sign the DMG
codesign --force --sign "Developer ID Application: Your Name (TEAMID)" \
    build/CedarLogic-signed.dmg
```

For notarization (required for macOS 10.15+):

```bash
# Submit for notarization
xcrun notarytool submit build/CedarLogic-signed.dmg \
    --apple-id "your@email.com" \
    --team-id "TEAMID" \
    --password "app-specific-password" \
    --wait

# Staple the notarization ticket
xcrun stapler staple build/CedarLogic-signed.dmg
```

For local development/testing without a Developer ID, you can use ad-hoc signing:

```bash
codesign --deep --force --sign - build/CedarLogic.app
```

## Developing Notes

It is time-intensive to re-install CedarLogic each time you wish to test a code change. There is also an executable in the `build/<whatever build type you picked, like Release>`
folder. That executable would run, except a library or two aren't in the correct relative paths for it to do so. You can tell CedarLogic where to find them by by setting the 
`CEDARLOGIC_RESOURCES_DIR` environment variable to your build directory. On linux this can be usually be done with `export CEDARLOGIC_RESOURCES_DIR="./build"`.

You can do the same thing for `Debug` builds and the like.

You can also build many other solutions, which come with cool things like the ability  to unit test portions of the code, and whatnot. We are trying to break the code apart
into multiple libraries that do not share memory to make reasoning about the code and updating it and testing it much easier.

Here is what each solution (in Visual Studio) within the CedarLogic solution does:

- **ALL_BUILD** Does what the name implies, probably builds everything. TODO: verify
- **Catch2** Builds the Catch2 unit test framework which is brought in via CMake's attempt at a module-ish build-time dependency pull system. It's a dependency for unit tests, not something we modify.
- **Catch2WithMain** Another piece of Catch2
- **CedarLogic** This creates a CedarLogic executable in `build/Release` or `/Debug`, whatever you've picked.
- **INSTALL** Attempts to install CedarLogic on your computer.
- **Logic** Builds the new logic core, which is not yet integrated with the rest of CedarLogic
- **PACKAGE** Builds the installer
- **test_logic** Builds the new logic core's unit tests, which run as an executable and report back the result. This uses Catch2. Note, you'll want to run the test_logic executable in a terminal or you'll lose the results in an instant.
- **XMLParser** Is a step towards breaking the XML parsing out into it's own library, with an identifiable interface so we can swap out serialization backends without worrying about what else we broke. There are good reasons to even hope for a day when we don't use XML at all and switch to JSON or YAML.
- **ZERO_CHECK** This may be a null operation. If memory serves rightly, CMake on Windows at least automatically creates all the all-caps build targets, and this may be a target that exists but is useless.

If you want to debug CedarLogic and do so prettily, do a `Debug` type of build, and then look up how to set the Visual Studio settings to run the CedarLogic.exe you built
from the debug button.
