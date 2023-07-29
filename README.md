# blueSPY Extra Codecs

> [!WARNING]
> This repository is unreleased, experimental and subject to change.

This repository contains the source for some extra audio codecs for RFcreations' blueSPY software.
These serve as an example for adding new custom codecs. In order to add a new one:

0. Ensure you have cmake and a suitable compiler/toolchain installed (MSVC/LLVM/GCC).
1. Open a terminal (on windows you may need to use the Visual Studio developer prompt).
2. Run: `git clone --recurse-submodules https://github.com/RFCreations/bluespy_codecs.git && cd bluespy_codecs`
3. Add mycodec.cpp, you may wish to copy the structure of aptx.cpp or aac.cpp.
4. Implement the four functions in bluespy_codec_interface.h.
5. Add a new secion at the bottom of CMakeLists.txt for mycodec, using the aptx/acc ones as an example.
6. Run: `cmake --preset release && cmake --build build/release`
7. Copy build/release/mycodec.{dll,so,dylib} to a directory as specified below:
   - Windows User: C:\\Users\\\<USER\>\\AppData\\Local\\RFcreations\\blueSPY\\codecs\\
   - Windows System: C:\\Program Files\\RFcreations\\blueSPY\\codecs\\
   - Mac User: ~/Library/Application Support/RFcreations/blueSPY/codecs/
   - Mac System: /Applications/blueSPY.app/Contents/Frameworks/codecs/
   - Linux User: ~/.local/share/RFcreations/blueSPY/codecs/
   - Linux System: \<INSTALLATION_DIRECTORY\>/codecs/

Contact RFcreations support if you require help with this, or your codec needs some extra information.

Please consider releasing your custom codec back to RFcreations so we can include it for all our mutual customers.
We can accept pull requests via github, or private source or binary versions sent to RFcreations support.

All code in this repository, excluding the submodules, is released under the Boost Software License which imposes
practically no requirements on your use of the code. However, the underlying codecs have their own licenses which can
be found in their respective directories, and the final binaries will be subject to the repective licenses.

The AAC codec is a cut down version with all patented technology removed. If you wish to use higher quality modes like
HE or ELD then you can clone https://github.com/mstorsjo/fdk-aac, adjust CMakeLists.txt to use that instead of
fdk-aac-stripped, and recompile the aac binary. No other source changes are required.