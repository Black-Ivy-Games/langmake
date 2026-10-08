// One miniaudio implementation shared by device playback and offline decoding.
// File decoding is only invoked while compiling immutable assets.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_MP3
#include <miniaudio.h>
