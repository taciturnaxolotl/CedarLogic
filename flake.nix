{
  description = "CedarLogic - A digital logic simulator for educational use";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        # Skia, cut to CedarLogic's feature set.
        #
        # A Nix build is sandboxed with no network, so it cannot use the CI dist
        # that skia.yml publishes -- Skia has to come from nixpkgs. nixpkgs'
        # `skia` is m144, twenty Chrome milestones past the pin, and does not
        # compile against this code (Ganesh's headers moved under gpu/ganesh/).
        # `skia-aseprite` IS the pin: the same aseprite/skia ref that
        # cmake/skia-args builds. It is packaged for Aseprite though, so re-cut
        # it with the args this app needs (SVG + PDF export, the custom font
        # managers) and install the headers Aseprite has no use for.
        #
        # This diverges from the stock derivation, so it is a cache miss and
        # builds Skia from source -- which is why .github/workflows/nix.yml runs
        # on flake changes and a weekly schedule rather than on every push.
        skia = pkgs.skia-aseprite.overrideAttrs (old: {
          pname = "skia-cedarlogic";
          configurePhase = ''
            runHook preConfigure
            gn gen lib --args="is_debug=false is_official_build=true skia_use_system_icu=false skia_enable_svg=true skia_enable_pdf=true skia_use_expat=true skia_enable_fontmgr_custom_empty=true skia_enable_fontmgr_custom_directory=true extra_cflags=[\"-I${pkgs.harfbuzzFull.dev}/include/harfbuzz\"]"
            runHook postConfigure
          '';
          # The stock install copies only what Aseprite includes. Add the export
          # headers (SkSVGCanvas, SkPDFDocument, SkPngEncoder) this app needs.
          installPhase = old.installPhase + ''
            shopt -s globstar
            cp -r --parents -t $out/ \
              include/svg/**/*.h \
              include/docs/**/*.h \
              include/encode/**/*.h
          '';
        });

        # Skia is the renderer, not an option: there is one package and it always
        # builds against Skia. No pkg-config file comes out of the override, so
        # this takes the SKIA_ROOT path (USE_SYSTEM_SKIA=OFF), the same one
        # Windows, macOS, and CI use with the dist.
        mkCedarLogic =
          { }:
          pkgs.stdenv.mkDerivation {
            pname = "cedarlogic";
            version = "3.0.2";

            src = ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              pkg-config
            ];

            buildInputs = [ skia ] ++ (with pkgs; [
              wxGTK32
              libGL
              libGLU
              mesa
              catch2_3
              # This Skia links system libraries rather than vendoring them, so
              # the app link needs them too: fontconfig for the font manager,
              # libwebp for the webp codec, harfbuzz-subset for PDF font
              # subsetting. See the tail of cmake/module/SetupSkia.cmake.
              fontconfig
              libwebp
              harfbuzzFull
            ]);

            cmakeFlags = [
              "-DUSE_SYSTEM_WXWIDGETS=ON"
              "-DUSE_SYSTEM_SKIA=OFF"
              "-DSKIA_ROOT=${skia}"
              "-DCMAKE_BUILD_TYPE=Release"
            ];

            # Patch CMakeLists.txt to use system Catch2 instead of FetchContent
            postPatch = ''
                            substituteInPlace logic/CMakeLists.txt \
                              --replace "# Bring in the Catch2 framework
              Include(FetchContent)

              FetchContent_Declare(
                Catch2
                GIT_REPOSITORY https://github.com/catchorg/Catch2.git
                GIT_TAG        v3.5.2
              )

              FetchContent_MakeAvailable(Catch2)" "# Use system Catch2
              find_package(Catch2 3 REQUIRED)"
            '';

            meta = with pkgs.lib; {
              description = "CedarLogic is a digital logic simulator for educational use";
              homepage = "https://github.com/Cedarville/CedarLogic";
              license = licenses.mit;
              maintainers = with maintainers; [ ];
              platforms = platforms.linux;
              mainProgram = "CedarLogic";
            };
          };
      in
      {
        packages = {
          default = self.packages.${system}.cedarlogic;
          cedarlogic = mkCedarLogic { };
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.cedarlogic ];

          buildInputs = with pkgs; [
            # Development tools
            gdb
            valgrind
            clang-tools
            gcovr

            # Documentation
            doxygen

            # Task runner and release tooling
            go-task
            svu
            gh

            # Additional utilities
            git
            gnumake
          ];

          shellHook = ''
            echo "CedarLogic development environment"
            echo "Run 'task' to see the available commands."
            echo ""
          '';
        };

        # Add apps for easy running
        apps.default = {
          type = "app";
          program = "${self.packages.${system}.cedarlogic}/bin/CedarLogic";
        };
      }
    );
}

