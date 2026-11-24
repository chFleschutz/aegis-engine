// The miniaudio implementation, compiled once (see ../CMakeLists.txt), with stb_vorbis for Ogg Vorbis: miniaudio
// decodes Vorbis when stb_vorbis's declarations come before its implementation.
#define STB_VORBIS_HEADER_ONLY
#include "../stb/stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "../miniaudio/miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "../stb/stb_vorbis.c"
