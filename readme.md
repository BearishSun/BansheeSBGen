Tool used for automatic script binding generation for Banshee 3D game engine.

# Setting up dependencies
This tool depends on Clang & LLVM. 

- Download LLVM & Clang (Version 5.0.0) source code: http://releases.llvm.org/download.html
- Place the Clang source code under llvm/tools.
- Build Clang using the release configuration.
 - Make sure to execute the 'install' target in your build tool.
- Set clang_INSTALL_DIR variable pointing to your LLVM install folder.

