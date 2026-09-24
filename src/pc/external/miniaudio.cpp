// Single-translation-unit implementation for miniaudio (vendor/miniaudio/miniaudio.h).
//
// SoundEngine uses miniaudio's high-level ma_engine/ma_sound API for real-time
// mixing and playback (replacing the previous unused OpenAL backend, which was
// never reliably available on webOS - see WEBOS_WAYLAND_PORT notes on the
// Butterscotch port, which hit the same problem and settled on miniaudio for
// the exact same reason).
//
// OGG decoding is handled separately by stb_vorbis (src/pc/external/stb_vorbis.cpp)
// and fed into miniaudio via ma_audio_buffer_ref, so we don't need miniaudio's
// own container/format decoders here.
#define MINIAUDIO_IMPLEMENTATION

// We only ever decode PCM ourselves and hand it to miniaudio as raw buffers, so
// none of the built-in file decoders are needed - cuts a fair bit of code size,
// which matters on a webOS TV's limited storage.
#define MA_NO_WAV
#define MA_NO_FLAC
#define MA_NO_MP3

#include "miniaudio.h"
