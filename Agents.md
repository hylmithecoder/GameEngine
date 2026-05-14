# Rule Of this project if your a llm
1. Read all the docs in the docs folder
2. always build with debug mode, use "make VERBOSE=1" to see the build process
3. make sure all the libraries are loaded correctly, use "pkg-config --cflags --libs glfw3 vulkan gl" to see the build process
4. on nixos build the project with 
```nix
nix-shell --run "cmake --build build --target TargetProject"
```