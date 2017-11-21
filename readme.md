[![Build Status](https://travis-ci.org/BearishSun/BansheeSBGen.svg?branch=master)](https://travis-ci.org/BearishSun/BansheeSBGen)
[![Build status](https://ci.appveyor.com/api/projects/status/lfpbyfy08jvuh0kt?svg=true)](https://ci.appveyor.com/project/BearishSun/bansheesbgen)


Tool used for automatic script binding generation for Banshee 3D game engine.

# Setting up dependencies
This tool depends on Clang & LLVM. 

Build Clang:
- Download LLVM & Clang (Version 5.0.0) source code: http://releases.llvm.org/download.html
- Place the Clang source code under llvm/tools
- Build Clang using the release configuration
 - Make sure to execute the 'install' target in your build tool
 
Build SBGen:
- In CMake set clang_INSTALL_DIR variable pointing to the LLVM install folder
- In CMake set CMAKE_INSTALL_PREFIX variable to the SBGen dependencies folder of your Banshee install (i.e. BansheeRoot/Dependencies/tools/BansheeSBGen/)
- Build SBGen using the release configuration
 - Execute the 'install' target