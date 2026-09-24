#pragma once

#include "client/sound/SoundRepository.h"
#include "client/Options.h"
#include "java/String.h"
#include "java/Random.h"

#include "miniaudio.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

class Mob;

class SoundEngine
{
private:
	static bool loaded;

	ma_engine engine{};
	bool engineInitialized = false;
	bool audioSuspended = false;

	SoundRepository sounds;
	SoundRepository streamingSounds;
	SoundRepository songs;
	int_t idCounter = 0;
	Options *options = nullptr;
	Random random;
	int_t noMusicDelay = 0;

	// Per-source metadata for manual gain calculation (matching Paulscode SourceLWJGLOpenAL)
	struct SourceInfo
	{
		float x, y, z;
		float distOrRoll;    // cutoff distance for attModel 2
		float sourceVolume;  // volume multiplier (includes options->sound)
		int attModel;        // 0=none, 2=linear
		bool priority;       // paulscode priority flag (SoundManager: volume > 1.0F)

		SourceInfo() : x(0), y(0), z(0), distOrRoll(0), sourceVolume(1.0f), attModel(0), priority(false) {}
		SourceInfo(float x, float y, float z, float distOrRoll, float sourceVolume, int attModel, bool priority = false)
			: x(x), y(y), z(z), distOrRoll(distOrRoll), sourceVolume(sourceVolume), attModel(attModel), priority(priority) {}
	};

	std::unordered_map<std::string, SourceInfo> sourceInfoMap;

	// Fully-decoded PCM (s16) for one sound file, shared by every channel currently
	// playing it. Decoding happens once per file and is cached for the life of the
	// engine, matching the old AL buffer cache.
	struct DecodedAudio
	{
		std::vector<int16_t> pcm; // interleaved
		ma_uint32 channels = 0;
		ma_uint32 sampleRate = 0;
		ma_uint64 frameCount = 0;
	};
	std::unordered_map<std::string, std::shared_ptr<DecodedAudio>> decodedCache;

	// One playback slot: a lightweight reference into a shared DecodedAudio buffer
	// (independent read cursor) bound to a ma_sound. Rebound to a new buffer every
	// time the channel is reused for a different sound, mirroring how the old code
	// rebound an OpenAL buffer onto a reused source.
	struct Channel
	{
		ma_audio_buffer_ref bufferRef{};
		ma_sound sound{};
		bool soundInitialized = false;
		std::shared_ptr<DecodedAudio> audio; // keeps PCM alive while bound
	};

	// Paulscode Library: fixed channel array + rotating cursor
	// (channels[n] mirrors normalChannelSourceNames)
	std::vector<Channel> channels;
	std::vector<std::string> channelIds;
	int_t nextNormalChannel = 0;

	Channel musicChannel;
	bool musicChannelActive = false;
	Channel streamingChannel;
	bool streamingChannelActive = false;

	// Paulscode default: 28 normal channels (streaming channel is separate)
	static constexpr int_t MAX_SOURCES = 28;

	float listenerX = 0.0f, listenerY = 0.0f, listenerZ = 0.0f;

	bool initAudioDevice();
	void cleanupAudioDevice();
	int_t getOrCreateChannel(const std::string &id, bool priority = false);
	void releaseSource(const std::string &id);
	std::shared_ptr<DecodedAudio> loadDecodedAudio(const std::string &filePath, bool isMUS = false);
	bool bindChannel(Channel &channel, const std::shared_ptr<DecodedAudio> &audio, bool loop);
	void checkAndReleaseFinishedSources();
	void updateSourceGains();

public:
	SoundEngine();
	~SoundEngine();

	void init(Options *options);
	void updateOptions();
	void destroy();

	// Stops / restarts the audio device without losing the playback position of
	// running sounds (used while the app is in the background on webOS).
	void setSuspended(bool suspended);

	void add(const jstring &name, const std::string &filePath);
	void addStreaming(const jstring &name, const std::string &filePath);
	void addMusic(const jstring &name, const std::string &filePath);

	void playMusicTick();
	void update(Mob *player, float a);

	void playStreaming(const jstring &name, float x, float y, float z, float volume, float pitch);
	void play(const jstring &name, float x, float y, float z, float volume, float pitch);

	// --sound-smoke diagnostics
	bool debugHasSound(const jstring &name) { return sounds.get(name) != nullptr; }
	int_t debugActiveSources();
	int_t debugPlayingSources();
	int_t debugDrops = 0;
	void playUI(const jstring &name, float volume, float pitch);
};
