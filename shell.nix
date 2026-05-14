{ pkgs ? import <nixpkgs> {
  config.allowUnfreePredicate = pkg: builtins.elem (pkgs.lib.getName pkg) [
    "discord-gamesdk"
  ];
} }:

pkgs.mkShell {
  stdenv = pkgs.gcc15Stdenv;
  
  nativeBuildInputs = with pkgs; [
    pkg-config
    cmake
    ninja
    gdb
  ];
  
  buildInputs = with pkgs; [
    # Core Libraries
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
    discord-gamesdk.dev
    curl
  ];

  # Essential for Vulkan and OpenGL detection on NixOS
  shellHook = ''
    export VK_LAYER_PATH="${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d"
    export LD_LIBRARY_PATH="${pkgs.vulkan-loader}/lib:${pkgs.lib.makeLibraryPath [ pkgs.libGL pkgs.libGLU pkgs.libnotify pkgs.gtk3 pkgs.discord-gamesdk ]}:$LD_LIBRARY_PATH"
    export DISCORD_SDK_PATH="${pkgs.discord-gamesdk}"
    
    echo "=== Game Engine Development Environment ==="
    echo "GCC version: $(gcc --version | head -n1)"
    echo "CMake version: $(cmake --version | head -n1)"
    echo "Dependencies loaded: SDL2, SDL3, GLFW, Vulkan, GTK3, FFmpeg, etc."
  '';
}
