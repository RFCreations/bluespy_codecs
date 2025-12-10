# blueSPY Audio Codecs

> [!WARNING]
> This repository is unreleased, experimental and subject to change.

The blueSPY protocol analyser software from RFCreations ([download here](https://www.rfcreations.com/bluespy-software)) includes in-built decoders for the SBC, CSVD, G.722, and LC3 codecs.

This repository serves two purposes:

   1) It contains the source for some extra codecs that are included as plugins to blueSPY when you download the software. These are currently versions of **[AAC](https://github.com/RFCreations/bluespy_codecs/blob/aptx_v2/AAC.c), [aptX](https://github.com/RFCreations/bluespy_codecs/blob/aptx_v2/APTX.c), and [LDAC](https://github.com/RFCreations/bluespy_codecs/blob/aptx_v2/LDAC.c)** that do not contain any patented technology.

   2) To allow users to implement their own audio codecs and add them to blueSPY as plugins. This could be, for example, a decoder for the patented HE-AAC, or something completely new. See instructions for doing this below in [Adding New and/or Custom Codecs](#adding-new-andor-custom-codecs).

## AAC, aptX, and LDAC

As explained above, plugins for decoding AAC Low Complexity ([aac-stripped](https://github.com/RFCreations/fdk-aac-stripped/tree/529b87452cd33d45e1d0a5066d20b64f10b38845)), aptX and aptX HD ([libfreeaptx](https://github.com/regularhunter/libfreeaptx/tree/c176b7de9c2017d0fc1877659cea3bb6c330aafa)), and LDAC ([libldac](https://github.com/hegdi/libldacdec/tree/35bed54275a66197d05b505b3e1b7c514529cac2)) are included when you download the blueSPY software, so decoding streams in blueSPY that use these codecs will work out of the box. 

If you wish to install the dynamic libraries in a location other than the default (which is "<Installation_Directory>\RFcreations\blueSPY\audio_codecs"), they can be downloaded [here](https://github.com/RFCreations/bluespy_codecs/actions). Click on the latest workflow run, scroll down to **Artifacts** and download the libraries for your OS of choice. Then move the files to an appropriate directory - listed [below](#directories-that-bluespy-will-look-in-for-audio-codec-dynamic-libraries) are the locations that blueSPY will look for audio codec plugins in Windows, MacOS, and Linux environments.

### AAC Higher Quality Modes

The AAC codec plugin included in the blueSPY download is a cut down version with all patented technology removed. If you wish to use higher quality modes like
HE or ELD then you can fork this repository, clone https://github.com/mstorsjo/fdk-aac, adjust CMakeLists.txt to use that instead of
fdk-aac-stripped, and recompile the AAC binary. No other source changes are required.

## Adding New and/or Custom Codecs

0. Ensure you have CMake and a suitable compiler/toolchain installed (MSVC/LLVM/GCC).
1. Open a terminal (on Windows you may need to use the Visual Studio developer prompt).
2. Run: `git clone --recurse-submodules https://github.com/RFCreations/bluespy_codecs.git && cd bluespy_codecs`
3. Add your decoder file MYCODEC.c. You may wish to copy the structure of one of the provided examples. There is also a template called [TEMPLATE_CODEC.c](TEMPLATE_CODEC.c) if you would like to copy that as a starting point. 
4. Implement the four functions in [bluespy_codec_interface.h](https://github.com/RFCreations/bluespy_codecs/blob/aptx_v2/include/bluespy_codec_interface.h) (init, new_codec_stream, codec_deinit, and codec_decode). See also [codec_structures.h](https://github.com/RFCreations/bluespy_codecs/blob/aptx_v2/include/codec_structures.h) for definitions of the data structures provided by blueSPY for use in codecs plugins.
5. Add a new secion at the bottom of CMakeLists.txt for MYCODEC, using the provided ones as examples.
6. Run: `cmake --preset release && cmake --build build/release`
7. Copy build/release/mycodec.{dll,so,dylib} to a directory as specified [below](#directories-that-bluespy-will-look-in-for-audio-codec-dynamic-libraries).

Contact [RFCreations support](https://www.rfcreations.com/contact-us) if you require help with this, or your codec needs some extra information.

Please consider releasing your custom codec back to RFCreations so we can include it for all of our mutual customers.
We can accept pull requests via GitHub, or private source or binary versions sent to RFCreations support.

## Directories that blueSPY Will Look in for Audio Codec Dynamic Libraries
   - Any directory whose path is the value of an environment variable on your machine called **BLUESPY_AUDIO_CODEC_DIR**.
   - If you do not wish to set an environment variable, the default directories that blueSPY will check are as follows:
      - Windows User: C:/Users/\<USER\>/AppData/Roaming/RFcreations/blueSPY/audio_codecs/
      - Windows System: C:/Program Files/RFcreations/blueSPY/audio_codecs/
      - Mac User: ~/Library/Application Support/RFcreations/blueSPY/audio_codecs/
      - Mac System: /Applications/blueSPY.app/Contents/Frameworks/audio_codecs/
      - Linux User: ~/.local/share/RFcreations/blueSPY/audio_codecs/
      - Linux System: \<INSTALLATION_DIRECTORY\>/audio_codecs/

## Licensing 

All code in this repository, excluding the submodules, is released under the Boost Software License which imposes
practically no requirements on your use of the code. However, the underlying codecs have their own licenses which can
be found in their respective directories, and the final binaries will be subject to the repective licenses.