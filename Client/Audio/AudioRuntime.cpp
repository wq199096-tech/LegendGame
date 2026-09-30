#include "Client/Audio/AudioRuntime.h"

#include "Engine/Debug/Logger.h"

#include <SDL3/SDL_audio.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace legend::client {

namespace {

constexpr int kSampleRate = 44100;
constexpr int kChannels = 2;
constexpr float kPi = 3.14159265358979323846f;

// 立体声短音合成助手。
struct ToneSpec {
    float freqStart = 440.0f;
    float freqEnd = 440.0f;
    float duration = 0.15f;
    float attack = 0.01f;
    float release = 0.6f; // 相对时长尾部淡出比例
    float gain = 0.5f;
    float noise = 0.0f; // 0=纯音 1=纯噪
};

std::vector<float> RenderTone(const ToneSpec& spec) {
    const int frames = static_cast<int>(spec.duration * kSampleRate);
    std::vector<float> out(static_cast<std::size_t>(frames) * kChannels, 0.0f);
    float phase = 0.0f;
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(frames);
        const float freq = spec.freqStart + (spec.freqEnd - spec.freqStart) * t;
        phase += 2.0f * kPi * freq / static_cast<float>(kSampleRate);
        float env = 1.0f;
        if (t < spec.attack) {
            env = t / spec.attack;
        } else if (t > 1.0f - spec.release) {
            const float r = (1.0f - t) / spec.release;
            env = r * r;
        }
        const float tone = std::sin(phase);
        const float noise = (std::rand() % 2000 - 1000) / 1000.0f;
        const float sample = spec.gain * env * (tone * (1.0f - spec.noise) + noise * spec.noise);
        out[static_cast<std::size_t>(i) * kChannels] = sample;
        out[static_cast<std::size_t>(i) * kChannels + 1] = sample;
    }
    return out;
}

// 简单琶音（LevelUp/QuestComplete）。
std::vector<float> RenderArpeggio(const float* freqs, int count, float noteDuration,
                                  float gain) {
    std::vector<float> out;
    for (int i = 0; i < count; ++i) {
        ToneSpec spec;
        spec.freqStart = freqs[i];
        spec.freqEnd = freqs[i];
        spec.duration = noteDuration;
        spec.release = 0.5f;
        spec.gain = gain;
        auto note = RenderTone(spec);
        out.insert(out.end(), note.begin(), note.end());
    }
    return out;
}

} // namespace

bool AudioRuntime::Initialize() {
    if (m_stream != nullptr) {
        return true;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, kChannels, kSampleRate};
    m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                         &AudioRuntime::SdlCallback, this);
    if (m_stream == nullptr) {
        LOG_WARN("[Audio] SDL audio stream open failed: " + std::string(SDL_GetError()));
        return false;
    }
    SDL_ResumeAudioStreamDevice(m_stream);
    LOG_INFO("[Audio] audio runtime ready (procedural synth, 44.1kHz stereo).");
    return true;
}

void AudioRuntime::Shutdown() {
    if (m_stream != nullptr) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
    m_voices.clear();
}

void AudioRuntime::SetVolumes(float master, float music, float sfx) {
    m_master = std::clamp(master, 0.0f, 1.0f);
    m_music = std::clamp(music, 0.0f, 1.0f);
    m_sfx = std::clamp(sfx, 0.0f, 1.0f);
}

void AudioRuntime::PlaySfx(SfxId id) {
    if (m_stream == nullptr) {
        return;
    }
    Voice voice;
    voice.buffer = MakeSfx(id);
    voice.gain = m_sfx * m_master;
    voice.music = false;
    voice.looping = false;
    if (voice.buffer.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_voiceMutex);
    // SFX 最多 8 并发（防刷屏爆音；最旧丢弃）。
    int sfxCount = 0;
    for (const auto& v : m_voices) {
        if (!v.music) {
            ++sfxCount;
        }
    }
    if (sfxCount >= 8) {
        for (auto it = m_voices.begin(); it != m_voices.end(); ++it) {
            if (!it->music) {
                m_voices.erase(it);
                break;
            }
        }
    }
    m_voices.push_back(std::move(voice));
}

void AudioRuntime::PlayBgmForMap(std::uint16_t mapId) {
    if (m_stream == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_voiceMutex);
    // 当前已有同图 BGM 则不重启。
    for (const auto& v : m_voices) {
        if (v.music && v.looping) {
            return;
        }
    }
    Voice voice;
    voice.buffer = MakeBgm(static_cast<int>(mapId));
    voice.gain = m_music * m_master;
    voice.music = true;
    voice.looping = true;
    if (!voice.buffer.empty()) {
        m_voices.push_back(std::move(voice));
    }
}

void AudioRuntime::StopBgm() {
    std::lock_guard<std::mutex> lock(m_voiceMutex);
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(),
                                  [](const Voice& v) { return v.music; }),
                   m_voices.end());
}

void AudioRuntime::SdlCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount,
                               int totalAmount) {
    (void)totalAmount;
    auto* self = static_cast<AudioRuntime*>(userdata);
    if (additionalAmount <= 0) {
        return;
    }
    const int bytesPerFrame = sizeof(float) * kChannels;
    const int frames = additionalAmount / bytesPerFrame;
    std::vector<float> mix(static_cast<std::size_t>(frames) * kChannels, 0.0f);
    self->Mix(mix.data(), frames);
    SDL_PutAudioStreamData(stream, mix.data(), additionalAmount);
}

void AudioRuntime::Mix(float* out, int frames) {
    // 音频线程回调：m_voiceMutex 与主线程 PlaySfx/StopBgm/PlayBgmForMap 互斥。
    std::lock_guard<std::mutex> lock(m_voiceMutex);
    for (auto& voice : m_voices) {
        const float gain = voice.gain;
        const std::size_t total = voice.buffer.size();
        for (int i = 0; i < frames; ++i) {
            std::size_t idx = voice.pos;
            if (voice.looping) {
                idx %= total;
            } else if (voice.pos >= total) {
                break;
            }
            const std::size_t base = idx;
            out[static_cast<std::size_t>(i) * kChannels] +=
                voice.buffer[base] * gain;
            out[static_cast<std::size_t>(i) * kChannels + 1] +=
                voice.buffer[base + 1] * gain;
            voice.pos += kChannels;
            if (voice.looping && voice.pos >= total) {
                voice.pos = 0;
            }
        }
    }
    // 移除播完的 SFX。
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(),
                                  [](const Voice& v) {
                                      return !v.music && v.pos >= v.buffer.size();
                                  }),
                   m_voices.end());
    // 简单软限幅（防叠加爆音）。
    for (int i = 0; i < frames * kChannels; ++i) {
        out[i] = std::clamp(out[i], -0.95f, 0.95f);
    }
}

std::vector<float> AudioRuntime::MakeSfx(SfxId id) const {
    switch (id) {
        case SfxId::SwordAttack: {
            ToneSpec spec;
            spec.freqStart = 900.0f;
            spec.freqEnd = 2400.0f;
            spec.duration = 0.10f;
            spec.gain = 0.30f;
            spec.noise = 0.85f; // 挥砍白噪
            return RenderTone(spec);
        }
        case SfxId::FireBolt: {
            ToneSpec spec;
            spec.freqStart = 220.0f;
            spec.freqEnd = 660.0f;
            spec.duration = 0.22f;
            spec.gain = 0.32f;
            spec.noise = 0.25f;
            return RenderTone(spec);
        }
        case SfxId::Whirlwind: {
            ToneSpec spec;
            spec.freqStart = 300.0f;
            spec.freqEnd = 520.0f;
            spec.duration = 0.55f;
            spec.gain = 0.26f;
            spec.noise = 0.55f;
            return RenderTone(spec);
        }
        case SfxId::MonsterHit: {
            ToneSpec spec;
            spec.freqStart = 180.0f;
            spec.freqEnd = 90.0f;
            spec.duration = 0.09f;
            spec.gain = 0.34f;
            spec.noise = 0.35f;
            return RenderTone(spec);
        }
        case SfxId::ItemPickup: {
            const float notes[2] = {660.0f, 990.0f};
            return RenderArpeggio(notes, 2, 0.07f, 0.30f);
        }
        case SfxId::QuestComplete: {
            const float notes[4] = {523.0f, 659.0f, 784.0f, 1047.0f}; // C5 E5 G5 C6
            return RenderArpeggio(notes, 4, 0.11f, 0.28f);
        }
        case SfxId::LevelUp: {
            const float notes[5] = {392.0f, 523.0f, 659.0f, 784.0f, 1047.0f};
            return RenderArpeggio(notes, 5, 0.12f, 0.30f);
        }
        case SfxId::UiClick: {
            ToneSpec spec;
            spec.freqStart = 880.0f;
            spec.freqEnd = 880.0f;
            spec.duration = 0.045f;
            spec.gain = 0.22f;
            return RenderTone(spec);
        }
        case SfxId::Gold: {
            const float notes[2] = {1319.0f, 1760.0f};
            return RenderArpeggio(notes, 2, 0.06f, 0.24f);
        }
        case SfxId::Error: {
            ToneSpec spec;
            spec.freqStart = 220.0f;
            spec.freqEnd = 180.0f;
            spec.duration = 0.14f;
            spec.gain = 0.28f;
            return RenderTone(spec);
        }
        default:
            return {};
    }
}

std::vector<float> AudioRuntime::MakeBgm(int mapId) const {
    // 每图 8 音符循环乐句（不同根音/速度；程序化占位，替换正式音乐不动接口）。
    float notes[8];
    float noteDuration = 0.28f;
    if (mapId == 1) {
        // Greenfield Village：明亮 C 大调分解和弦。
        const float n[8] = {523.0f, 659.0f, 784.0f, 659.0f, 523.0f, 587.0f, 659.0f, 494.0f};
        std::copy(n, n + 8, notes);
        noteDuration = 0.30f;
    } else if (mapId == 2) {
        // Slime Meadow：轻快 G 大调跳跃。
        const float n[8] = {392.0f, 494.0f, 587.0f, 494.0f, 440.0f, 494.0f, 587.0f, 659.0f};
        std::copy(n, n + 8, notes);
        noteDuration = 0.22f;
    } else {
        // Ancient Ruins：低沉 A 小调氛围。
        const float n[8] = {220.0f, 261.6f, 246.9f, 220.0f, 196.0f, 220.0f, 261.6f, 246.9f};
        std::copy(n, n + 8, notes);
        noteDuration = 0.42f;
    }
    std::vector<float> out;
    for (int i = 0; i < 8; ++i) {
        ToneSpec spec;
        spec.freqStart = notes[i] * 0.5f;   // 低八度垫底
        spec.freqEnd = notes[i] * 0.5f;
        spec.duration = noteDuration * 2.0f; // 持续垫音（重叠感）
        spec.attack = 0.15f;
        spec.release = 0.4f;
        spec.gain = 0.16f;
        auto bass = RenderTone(spec);
        // 与旋律长度对齐（截断）。
        spec.freqStart = notes[i];
        spec.freqEnd = notes[i];
        spec.duration = noteDuration;
        spec.attack = 0.03f;
        spec.gain = 0.20f;
        auto lead = RenderTone(spec);
        out.insert(out.end(), lead.begin(), lead.end());
        // bass 混入（简单相加到前段）。
        const std::size_t mixLen = std::min(bass.size(), lead.size());
        for (std::size_t k = 0; k < mixLen; ++k) {
            out[out.size() - lead.size() + k] += bass[k];
        }
    }
    // 循环尾部交叉淡出由 RenderTone release 自然处理。
    return out;
}

} // namespace legend::client
