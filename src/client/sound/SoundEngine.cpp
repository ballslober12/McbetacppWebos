#include "client/sound/SoundEngine.h"

#include "world/entity/Mob.h"
#include "util/Mth.h"
#include "java/String.h"

#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cfloat>
#include <algorithm>

// stb_vorbis is compiled separately (src/pc/external/stb_vorbis.cpp); we just
// need the decode function. We decode fully to PCM ourselves and hand the
// result to miniaudio as a raw buffer (see loadDecodedAudio/bindChannel)
// rather than routing files through miniaudio's own decoders.
extern "C" {
	typedef unsigned char uint8;
	int stb_vorbis_decode_memory(const uint8 *mem, int len, int *channels, int *sample_rate, short **output);
}

bool SoundEngine::loaded = false;

SoundEngine::SoundEngine()
{
	noMusicDelay = random.nextInt(12000);
}

SoundEngine::~SoundEngine()
{
	destroy();
}

// SoundManager.java:26-32
void SoundEngine::init(Options *options)
{
	this->options = options;
	this->streamingSounds.setTrimDigits(false);

	if (!loaded && (options == nullptr || options->sound != 0.0f || options->music != 0.0f))
	{
		if (initAudioDevice())
			loaded = true;
	}
}

bool SoundEngine::initAudioDevice()
{
	ma_engine_config engineConfig = ma_engine_config_init();
	// Tuned the same way as the Butterscotch webOS port: a short, explicit
	// period size avoids the device defaulting to something that underruns
	// on TV audio hardware while still keeping latency low.
	engineConfig.periodSizeInMilliseconds = 10;
	engineConfig.channels = 2;

	if (ma_engine_init(&engineConfig, &engine) != MA_SUCCESS)
	{
		std::cerr << "Failed to initialize miniaudio engine" << std::endl;
		return false;
	}
	engineInitialized = true;

	channels.assign(MAX_SOURCES, Channel());
	channelIds.assign(MAX_SOURCES, std::string());
	nextNormalChannel = 0;

	ma_engine_listener_set_position(&engine, 0, 0.0f, 0.0f, 0.0f);
	ma_engine_listener_set_direction(&engine, 0, 0.0f, 0.0f, -1.0f);
	ma_engine_listener_set_world_up(&engine, 0, 0.0f, 1.0f, 0.0f);

	return true;
}

void SoundEngine::cleanupAudioDevice()
{
	for (Channel &channel : channels)
	{
		if (channel.soundInitialized)
		{
			ma_sound_uninit(&channel.sound);
			channel.soundInitialized = false;
		}
		channel.audio.reset();
	}
	channels.clear();
	channelIds.clear();
	sourceInfoMap.clear();

	if (musicChannelActive)
	{
		ma_sound_uninit(&musicChannel.sound);
		musicChannelActive = false;
	}
	musicChannel.audio.reset();

	if (streamingChannelActive)
	{
		ma_sound_uninit(&streamingChannel.sound);
		streamingChannelActive = false;
	}
	streamingChannel.audio.reset();

	decodedCache.clear();

	if (engineInitialized)
	{
		ma_engine_uninit(&engine);
		engineInitialized = false;
	}
}

// SoundManager.java:54-67
void SoundEngine::updateOptions()
{
	if (!loaded && options && (options->sound != 0.0f || options->music != 0.0f))
	{
		if (initAudioDevice())
			loaded = true;
	}

	if (loaded)
	{
		if (options->music == 0.0f)
		{
			if (musicChannelActive)
				ma_sound_stop(&musicChannel.sound);
		}
		else if (musicChannelActive && ma_sound_is_playing(&musicChannel.sound))
		{
			ma_sound_set_volume(&musicChannel.sound, options->music);
		}
	}
}

// SoundManager.java:69-74
void SoundEngine::destroy()
{
	if (loaded)
	{
		cleanupAudioDevice();
		loaded = false;
	}
}

// SoundManager.java:76-78
void SoundEngine::add(const jstring &name, const std::string &filePath)
{
	sounds.add(name, filePath);
}

// SoundManager.java:80-82
void SoundEngine::addStreaming(const jstring &name, const std::string &filePath)
{
	streamingSounds.add(name, filePath);
}

// SoundManager.java:84-86
void SoundEngine::addMusic(const jstring &name, const std::string &filePath)
{
	songs.add(name, filePath);
}

// SoundManager.java:88-106
void SoundEngine::playMusicTick()
{
	if (!loaded || !options || options->music == 0.0f)
		return;

	bool musicPlaying = musicChannelActive && ma_sound_is_playing(&musicChannel.sound);
	bool streamPlaying = streamingChannelActive && ma_sound_is_playing(&streamingChannel.sound);

	if (musicPlaying || streamPlaying)
	{
		if (noMusicDelay > 0)
			noMusicDelay--;
		return;
	}

	if (noMusicDelay > 0)
	{
		noMusicDelay--;
		return;
	}

	Sound *song = songs.any();
	if (song != nullptr)
	{
		noMusicDelay = random.nextInt(12000) + 12000;

		bool isMus = song->filePath.size() >= 4 && song->filePath.compare(song->filePath.size() - 4, 4, ".mus") == 0;
		std::shared_ptr<DecodedAudio> audio = loadDecodedAudio(song->filePath, isMus);
		if (audio != nullptr && bindChannel(musicChannel, audio, true))
		{
			musicChannelActive = true;
			ma_sound_set_attenuation_model(&musicChannel.sound, ma_attenuation_model_none);
			ma_sound_set_volume(&musicChannel.sound, options->music);
			ma_sound_set_position(&musicChannel.sound, 0.0f, 0.0f, 0.0f);
			ma_sound_start(&musicChannel.sound);
		}
	}
}

// SoundManager.java:108-127 (func_338_a - listener update)
void SoundEngine::update(Mob *player, float a)
{
	if (!loaded || !options || options->sound == 0.0f || !player)
		return;

	float yRot = player->yRotO + (player->yRot - player->yRotO) * a;
	double x = player->xo + (player->x - player->xo) * (double)a;
	double y = player->yo + (player->y - player->yo) * (double)a;
	double z = player->zo + (player->z - player->zo) * (double)a;

	float yCos = Mth::cos(-yRot * Mth::DEGRAD - Mth::PI);
	float ySin = Mth::sin(-yRot * Mth::DEGRAD - Mth::PI);
	float forwardX = -ySin;
	float forwardY = 0.0f;
	float forwardZ = -yCos;

	ma_engine_listener_set_position(&engine, 0, (float)x, (float)y, (float)z);
	ma_engine_listener_set_direction(&engine, 0, forwardX, forwardY, forwardZ);

	listenerX = (float)x;
	listenerY = (float)y;
	listenerZ = (float)z;

	updateSourceGains();
	checkAndReleaseFinishedSources();
}

void SoundEngine::checkAndReleaseFinishedSources()
{
	for (size_t n = 0; n < channels.size(); ++n)
	{
		if (channelIds[n].empty())
			continue;

		Channel &channel = channels[n];
		bool finished = !channel.soundInitialized || !ma_sound_is_playing(&channel.sound);

		if (finished)
		{
			if (channel.soundInitialized)
			{
				ma_sound_uninit(&channel.sound);
				channel.soundInitialized = false;
			}
			channel.audio.reset();

			sourceInfoMap.erase(channelIds[n]);
			channelIds[n].clear();
		}
	}
}

// Paulscode SourceLWJGLOpenAL.java:calculateGain (lines 426-446)
// attModel 2: linear distance attenuation with manual gain
static float calculateLinearGain(float srcX, float srcY, float srcZ,
                                  float lisX, float lisY, float lisZ,
                                  float distOrRoll)
{
	float dX = srcX - lisX;
	float dY = srcY - lisY;
	float dZ = srcZ - lisZ;
	float dist = std::sqrt(dX * dX + dY * dY + dZ * dZ);

	float gain;
	if (dist <= 0.0f)
		gain = 1.0f;
	else if (dist >= distOrRoll)
		gain = 0.0f;
	else
		gain = 1.0f - dist / distOrRoll;

	if (gain > 1.0f) gain = 1.0f;
	if (gain < 0.0f) gain = 0.0f;
	return gain;
}

// SourceLWJGLOpenAL.java:positionChanged (lines 199-208)
void SoundEngine::updateSourceGains()
{
	for (size_t n = 0; n < channels.size(); ++n)
	{
		if (channelIds[n].empty())
			continue;

		Channel &channel = channels[n];
		if (!channel.soundInitialized || !ma_sound_is_playing(&channel.sound))
			continue;

		auto infoIt = sourceInfoMap.find(channelIds[n]);
		if (infoIt == sourceInfoMap.end())
			continue;

		const SourceInfo &info = infoIt->second;
		float gain = 1.0f;
		if (info.attModel == 2)
			gain = calculateLinearGain(info.x, info.y, info.z, listenerX, listenerY, listenerZ, info.distOrRoll);

		ma_sound_set_volume(&channel.sound, gain * info.sourceVolume);
	}

	// Streaming source (not in the channel pool)
	auto streamingInfoIt = sourceInfoMap.find("streaming");
	if (streamingInfoIt != sourceInfoMap.end() && streamingChannelActive && ma_sound_is_playing(&streamingChannel.sound))
	{
		const SourceInfo &info = streamingInfoIt->second;
		float gain = 1.0f;
		if (info.attModel == 2)
			gain = calculateLinearGain(info.x, info.y, info.z, listenerX, listenerY, listenerZ, info.distOrRoll);
		ma_sound_set_volume(&streamingChannel.sound, gain * info.sourceVolume);
	}
}

// SoundManager.java:129-151
void SoundEngine::playStreaming(const jstring &name, float x, float y, float z, float volume, float pitch)
{
	if (!loaded || !options || options->sound == 0.0f)
		return;

	std::string id = "streaming";

	// Stop current streaming
	if (streamingChannelActive)
	{
		ma_sound_stop(&streamingChannel.sound);
		ma_sound_uninit(&streamingChannel.sound);
		streamingChannel.soundInitialized = false;
		streamingChannelActive = false;
	}
	streamingChannel.audio.reset();

	if (name.empty())
	{
		sourceInfoMap.erase(id);
		return;
	}

	Sound *sound = streamingSounds.get(name);
	if (sound == nullptr || volume <= 0.0f)
		return;

	// Stop background music if streaming starts
	if (musicChannelActive && ma_sound_is_playing(&musicChannel.sound))
		ma_sound_stop(&musicChannel.sound);

	// dist * 4.0F = 64.0F for streaming
	float dist = 16.0f * 4.0f;

	// vanilla picks the codec by extension: only .mus files go through CodecMus
	bool isMus = sound->filePath.size() >= 4 && sound->filePath.compare(sound->filePath.size() - 4, 4, ".mus") == 0;
	std::shared_ptr<DecodedAudio> audio = loadDecodedAudio(sound->filePath, isMus);
	if (audio == nullptr || !bindChannel(streamingChannel, audio, false))
	{
		sourceInfoMap.erase(id);
		return;
	}
	streamingChannelActive = true;

	ma_sound_set_position(&streamingChannel.sound, x, y, z);

	// attModel 2: disable miniaudio's own distance attenuation, we calculate manually
	ma_sound_set_attenuation_model(&streamingChannel.sound, ma_attenuation_model_none);
	ma_sound_set_spatialization_enabled(&streamingChannel.sound, true);

	ma_sound_set_pitch(&streamingChannel.sound, pitch);

	// SoundManager.java:145 - soundSystem.setVolume(id, 0.5F * this.options.sound)
	float sourceVolume = 0.5f * options->sound;
	sourceInfoMap[id] = SourceInfo(x, y, z, dist, sourceVolume, 2);

	float gain = calculateLinearGain(x, y, z, listenerX, listenerY, listenerZ, dist);
	ma_sound_set_volume(&streamingChannel.sound, gain * sourceVolume);

	ma_sound_start(&streamingChannel.sound);
}

// SoundManager.java:153-175
void SoundEngine::play(const jstring &name, float x, float y, float z, float volume, float pitch)
{
	if (!loaded || !options || options->sound == 0.0f)
		return;

	Sound *sound = sounds.get(name);
	if (sound == nullptr || volume <= 0.0f)
		return;

	// SoundManager.java:156-157
	idCounter = (idCounter + 1) % 256;
	std::string id = "sound_" + std::to_string(idCounter);

	// SoundManager.java:158-160
	float dist = 16.0f;
	if (volume > 1.0f)
		dist *= volume;

	// SoundManager.java:163 - sndSystem.newSource(var5 > 1.0F, ...) (priority flag)
	bool priority = volume > 1.0f;

	int_t n = getOrCreateChannel(id, priority);
	if (n < 0)
	{
		debugDrops++;
		return;
	}

	std::shared_ptr<DecodedAudio> audio = loadDecodedAudio(sound->filePath);
	if (audio == nullptr || !bindChannel(channels[n], audio, false))
	{
		sourceInfoMap.erase(id);
		channelIds[n].clear();
		return;
	}

	Channel &channel = channels[n];

	ma_sound_set_position(&channel.sound, x, y, z);

	// attModel 2: disable miniaudio's own distance attenuation
	ma_sound_set_attenuation_model(&channel.sound, ma_attenuation_model_none);
	ma_sound_set_spatialization_enabled(&channel.sound, true);

	// Paulscode clamps pitch between 0.5F and 2.0F (Library.java:401-416)
	float clampedPitch = pitch;
	if (clampedPitch < 0.5f)
		clampedPitch = 0.5f;
	else if (clampedPitch > 2.0f)
		clampedPitch = 2.0f;
	ma_sound_set_pitch(&channel.sound, clampedPitch);

	// SoundManager.java:165-167 - clamp volume after pitch
	float finalVolume = volume > 1.0f ? 1.0f : volume;
	float sourceVolume = finalVolume * options->sound;
	sourceInfoMap[id] = SourceInfo(x, y, z, dist, sourceVolume, 2, priority);

	float gain = calculateLinearGain(x, y, z, listenerX, listenerY, listenerZ, dist);
	ma_sound_set_volume(&channel.sound, gain * sourceVolume);

	ma_sound_start(&channel.sound);
}

// SoundManager.java:177-195
void SoundEngine::playUI(const jstring &name, float volume, float pitch)
{
	if (!loaded || !options || options->sound == 0.0f)
		return;

	Sound *sound = sounds.get(name);
	if (sound == nullptr)
		return;

	idCounter = (idCounter + 1) % 256;
	std::string id = "sound_" + std::to_string(idCounter);

	// SoundManager.java:184-186
	if (volume > 1.0f)
		volume = 1.0f;
	volume *= 0.25f;

	int_t n = getOrCreateChannel(id);
	if (n < 0)
		return;

	std::shared_ptr<DecodedAudio> audio = loadDecodedAudio(sound->filePath);
	if (audio == nullptr || !bindChannel(channels[n], audio, false))
	{
		sourceInfoMap.erase(id);
		channelIds[n].clear();
		return;
	}

	Channel &channel = channels[n];

	// Non-positional (attModel 0)
	ma_sound_set_position(&channel.sound, 0.0f, 0.0f, 0.0f);
	ma_sound_set_spatialization_enabled(&channel.sound, false);
	ma_sound_set_pitch(&channel.sound, pitch);
	ma_sound_set_volume(&channel.sound, volume * options->sound);

	ma_sound_start(&channel.sound);
}

// Paulscode Library.getNextChannel (b1.2 ref: paulscode/sound/Library.java:514)
int_t SoundEngine::getOrCreateChannel(const std::string &id, bool priority)
{
	(void)priority;

	int_t count = static_cast<int_t>(channels.size());
	if (count == 0)
		return -1;

	auto takeChannel = [this](int_t n) -> int_t
	{
		Channel &channel = channels[n];
		if (channel.soundInitialized)
		{
			ma_sound_stop(&channel.sound);
			ma_sound_uninit(&channel.sound);
			channel.soundInitialized = false;
		}
		channel.audio.reset();

		if (!channelIds[n].empty())
			sourceInfoMap.erase(channelIds[n]);
		return n;
	};

	// pass 1: a source with the same name takes over its existing channel
	for (int_t n = 0; n < count; ++n)
	{
		if (channelIds[n] == id)
		{
			takeChannel(n);
			channelIds[n] = id;
			return n;
		}
	}

	// pass 2: round-robin from the cursor, take the first channel whose
	// current sound is not playing
	int_t n = nextNormalChannel;
	for (int_t x = 0; x < count; ++x)
	{
		bool playing = false;
		if (!channelIds[n].empty())
			playing = channels[n].soundInitialized && ma_sound_is_playing(&channels[n].sound);

		if (!playing)
		{
			nextNormalChannel = (n + 1) % count;
			takeChannel(n);
			channelIds[n] = id;
			return n;
		}

		n = (n + 1) % count;
	}

	// pass 3: ANY new source takes over a channel whose current sound is not
	// priority; only priority sounds are protected. Drop only when every
	// channel is playing a priority sound.
	n = nextNormalChannel;
	for (int_t x = 0; x < count; ++x)
	{
		auto info = sourceInfoMap.find(channelIds[n]);
		bool protectedChannel = info != sourceInfoMap.end() && info->second.priority;

		if (!protectedChannel)
		{
			nextNormalChannel = (n + 1) % count;
			takeChannel(n);
			channelIds[n] = id;
			return n;
		}

		n = (n + 1) % count;
	}

	return -1;
}

void SoundEngine::releaseSource(const std::string &id)
{
	for (size_t n = 0; n < channels.size(); ++n)
	{
		if (channelIds[n] == id)
		{
			Channel &channel = channels[n];
			if (channel.soundInitialized)
			{
				ma_sound_stop(&channel.sound);
				ma_sound_uninit(&channel.sound);
				channel.soundInitialized = false;
			}
			channel.audio.reset();
			channelIds[n].clear();
			sourceInfoMap.erase(id);
			return;
		}
	}
}

// Binds a channel's ma_sound to (a view of) the given decoded PCM buffer, so it
// can be started with ma_sound_start(). Any sound previously bound to this
// channel is torn down first. Each channel gets its own ma_audio_buffer_ref -
// a cheap, independent read cursor into the shared PCM - so the same decoded
// sound can play concurrently on several channels (e.g. a burst of pops)
// without the playback positions stepping on each other.
bool SoundEngine::bindChannel(Channel &channel, const std::shared_ptr<DecodedAudio> &audio, bool loop)
{
	if (!engineInitialized || audio == nullptr || audio->frameCount == 0)
		return false;

	if (channel.soundInitialized)
	{
		ma_sound_uninit(&channel.sound);
		channel.soundInitialized = false;
	}

	if (ma_audio_buffer_ref_init(ma_format_s16, audio->channels, audio->pcm.data(), audio->frameCount, &channel.bufferRef) != MA_SUCCESS)
	{
		channel.audio.reset();
		return false;
	}
	channel.bufferRef.sampleRate = audio->sampleRate;

	if (ma_sound_init_from_data_source(&engine, &channel.bufferRef, 0, nullptr, &channel.sound) != MA_SUCCESS)
	{
		ma_audio_buffer_ref_uninit(&channel.bufferRef);
		channel.audio.reset();
		return false;
	}

	channel.soundInitialized = true;
	channel.audio = audio; // keep the PCM alive for as long as this channel references it
	ma_sound_set_looping(&channel.sound, loop ? MA_TRUE : MA_FALSE);
	return true;
}

// OGG decoding via stb_vorbis, with optional MUS XOR decryption. Decodes the
// whole file to PCM once and caches it; every channel that plays this sound
// gets its own lightweight reference into the shared PCM (see bindChannel).
// MUS decryption: MusInputStream.java - XOR each byte with (hash >> 8), evolve hash
std::shared_ptr<SoundEngine::DecodedAudio> SoundEngine::loadDecodedAudio(const std::string &filePath, bool isMUS)
{
	std::string cacheKey = filePath + (isMUS ? "_mus" : "");
	auto it = decodedCache.find(cacheKey);
	if (it != decodedCache.end())
		return it->second;

	std::ifstream file(filePath, std::ios::binary | std::ios::ate);
	if (!file.is_open())
	{
		std::cerr << "Failed to open sound file: " << filePath << std::endl;
		return nullptr;
	}

	size_t fileSize = (size_t)file.tellg();
	if (fileSize == 0 || fileSize > 100 * 1024 * 1024)
	{
		file.close();
		return nullptr;
	}

	file.seekg(0, std::ios::beg);
	std::vector<unsigned char> fileData(fileSize);
	if (!file.read((char *)fileData.data(), fileSize))
	{
		file.close();
		return nullptr;
	}
	file.close();

	// MUS decryption (MusInputStream.java)
	if (isMUS)
	{
		// Extract filename for hash seed
		size_t lastSlash = filePath.find_last_of("/\\");
		std::string filename = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;

		// Java String.hashCode()
		int32_t seed = 0;
		for (size_t i = 0; i < filename.length(); i++)
			seed = (int32_t)((uint32_t)seed * 31u + (uint32_t)(unsigned char)filename[i]);

		// XOR decryption with evolving hash
		for (size_t i = 0; i < fileData.size(); i++)
		{
			uint8_t key = (uint8_t)((uint32_t)seed >> 8);
			uint8_t val = (uint8_t)(fileData[i] ^ key);
			fileData[i] = val;
			// hash = hash * 498729871 + 85731 * (int8_t)val
			seed = (int32_t)((uint32_t)seed * 498729871u +
			                  (uint32_t)(85731 * (int32_t)(int8_t)val));
		}
	}

	int channelCount, sampleRate;
	short *output = nullptr;
	int samples = stb_vorbis_decode_memory((const uint8 *)fileData.data(), (int)fileSize, &channelCount, &sampleRate, &output);

	if (samples <= 0 || output == nullptr || channelCount <= 0 || channelCount > 2)
	{
		if (output) free(output);
		return nullptr;
	}

	auto audio = std::make_shared<DecodedAudio>();
	audio->channels = (ma_uint32)channelCount;
	audio->sampleRate = (ma_uint32)sampleRate;
	audio->frameCount = (ma_uint64)samples;
	audio->pcm.assign(output, output + (size_t)samples * channelCount);
	free(output);

	decodedCache[cacheKey] = audio;
	return audio;
}

int_t SoundEngine::debugActiveSources()
{
	int_t active = 0;
	for (const std::string &id : channelIds)
	{
		if (!id.empty())
			active++;
	}
	return active;
}

int_t SoundEngine::debugPlayingSources()
{
	int_t playing = 0;
	for (size_t n = 0; n < channels.size(); ++n)
	{
		if (channelIds[n].empty())
			continue;
		if (channels[n].soundInitialized && ma_sound_is_playing(&channels[n].sound))
			playing++;
	}
	return playing;
}
