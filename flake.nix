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

        # withSkia builds against the Skia rendering engine (Workstream G).
        # The hermetic Nix build has no network for the CI Skia cache, so it
        # uses a system Skia (USE_SYSTEM_SKIA=ON) -- exactly the shape of the
        # existing USE_SYSTEM_WXWIDGETS path. This is gated behind the
        # cedarlogic-skia variant so the default build never depends on Skia,
        # and stays buildable even before nixpkgs' skia GL support is confirmed.
        mkCedarLogic =
          { withSkia ? false }:
          pkgs.stdenv.mkDerivation {
            pname = if withSkia then "cedarlogic-skia" else "cedarlogic";
            version = "2.3.8";

            src = ./.;

            nativeBuildInputs = with pkgs; [
              cmake
              pkg-config
            ];

            buildInputs =
              (with pkgs; [
                wxGTK32
                libGL
                libGLU
                mesa
                catch2_3
              ])
              ++ pkgs.lib.optional withSkia pkgs.skia;

            cmakeFlags = [
              "-DUSE_SYSTEM_WXWIDGETS=ON"
              "-DCMAKE_BUILD_TYPE=Release"
            ]
            ++ pkgs.lib.optionals withSkia [
              "-DWITH_SKIA=ON"
              "-DUSE_SYSTEM_SKIA=ON"
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
          cedarlogic-skia = mkCedarLogic { withSkia = true; };
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.cedarlogic ];

          buildInputs = with pkgs; [
            # Development tools
            gdb
            valgrind
            clang-tools

            # Documentation
            doxygen

            # Additional utilities
            git
            gnumake
          ];

          shellHook = ''
            echo "CedarLogic development environment"
            echo "Available commands:"
            echo "  cmake -B build -S ."
            echo "  cmake --build build"
            echo "  ./build/CedarLogic"
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

