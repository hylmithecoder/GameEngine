{
  description = "GameEngineSDL Development Environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
          config.allowUnfreePredicate = pkg: builtins.elem (pkgs.lib.getName pkg) [
            "discord-gamesdk"
          ];
        };
      in
      {
        devShells.default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            cmake
            pkg-config
            ninja
            gdb
            gcc15
          ];

          buildInputs = with pkgs; [
            # Core
            SDL2
            SDL2_ttf
            sdl3
            glfw
            glew
            glm
            stb
            ktx-tools
            
            # Graphics & Media
            vulkan-loader
            vulkan-headers
            vulkan-validation-layers
            libGL
            libGLU
            ffmpeg
            
            # UI & System
            gtk3
            libnotify
            xorg.libxcb
            xorg.libX11
            xorg.libXdmcp
            xorg.libXau
            
            # Utilities
            nlohmann_json
            sqlite
            discord-gamesdk
            curl
          ];

          shellHook = ''
            export VK_LAYER_PATH="${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d"
            export LD_LIBRARY_PATH="${pkgs.vulkan-loader}/lib:${pkgs.lib.makeLibraryPath [ 
              pkgs.libGL 
              pkgs.libGLU 
              pkgs.libnotify 
              pkgs.gtk3 
              pkgs.discord-gamesdk 
            ]}:$LD_LIBRARY_PATH"
            
            # Set path to discord sdk for CMake
            export DISCORD_SDK_PATH="${pkgs.discord-gamesdk}"

            echo "=== Ilmee Engine Flake Environment ==="
            echo "GCC version: $(gcc --version | head -n1)"
            echo "Discord SDK: ${pkgs.discord-gamesdk.version}"
          '';
        };
      }
    );
}
