Tool used for automatic script binding generation for Banshee 3D game engine.

# Setting up dependencies
This tool depends on Clang & LLVM. 

- Download LLVM & Clang source code: http://releases.llvm.org/download.html
- Place the Clang source code under llvm/tools.
- Build using the release configuration.


- Build BansheeSBGen with llvm_BUILD_DIR, llvm_LIB_DIR and llvm_SOURCE_DIR variables pointing to your LLVM install. 
- For example if your LLVM root folder is C:\llvm, your build folder is C:\llvm\Build, and you've built LLVM + Clang with Visual Studio (MSVC), then those values should be:
   - llvm_SOURCE_DIR: C:\llvm
   - llvm_BUILD_DIR: C:\llvm\Build
   - llvm_LIB_DIR: C:\llvm\Build\Release\lib

