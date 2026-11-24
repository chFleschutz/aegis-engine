// aegis.audio: sound through miniaudio. Clips are decoded into memory, an AudioEngine mixes them, and an AudioSystem
// per world plays the world's AudioSource components and one-shots. miniaudio's header is included only in this
// module's .cpp files.
//
// Partitions:
//   :error         Error, ErrorCode, makeError and describe.
//   :decoder       decodeClip: a `.wav` or `.ogg` file's bytes into an assets::AudioClip.
//   :audio_engine  AudioEngine: the device (or none, mixed by frame time), its sounds, listener and counters.
//   :audio_source  The AudioSource component and registerComponents.
//   :audio_system  AudioSystem, and play, stop and playOneShot for scripts.
export module aegis.audio;

export import :audio_engine;
export import :audio_source;
export import :audio_system;
export import :decoder;
export import :error;
