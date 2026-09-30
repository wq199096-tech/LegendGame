#pragma once

// ---------------------------------------------------------------------------
// 阶段25 指令四十八~五十一：Audio Runtime V1（SDL3 音频）。
// - 全部音效/音乐为【运行时程序化合成】（正弦/噪声包络）——零版权风险，
//   以后替换正式音频资源只需换实现（接口不变）。
// - BGM：三张地图各一段程序化循环乐句（Map1 明亮/Map2 轻快/Map3 低沉）。
// - SFX：SwordAttack/FireBolt/Whirlwind/MonsterHit/ItemPickup/QuestComplete/
//   LevelUp/UIClick/Gold/Error。
// - 音量：Master/Music/SFX（Settings V1 实时生效，本地持久化在 VisualRuntime）。
// ---------------------------------------------------------------------------

#include <cstdint>
#include <mutex>
#include <vector>

struct SDL_AudioStream;

namespace legend::client {

enum class SfxId : std::uint8_t {
    SwordAttack = 0,
    FireBolt = 1,
    Whirlwind = 2,
    MonsterHit = 3,
    ItemPickup = 4,
    QuestComplete = 5,
    LevelUp = 6,
    UiClick = 7,
    Gold = 8,
    Error = 9,
};

class AudioRuntime {
public:
    bool Initialize();
    void Shutdown();
    bool IsReady() const { return m_stream != nullptr; }

    // Settings V1 实时音量（0~1）。
    void SetVolumes(float master, float music, float sfx);

    void PlaySfx(SfxId id);
    // MapChanged -> 每图 BGM（同一地图重复调用不重启）。
    void PlayBgmForMap(std::uint16_t mapId);
    void StopBgm();

private:
    void Mix(float* out, int frames); // 音频线程回调
    static void SdlCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount,
                            int totalAmount);

    SDL_AudioStream* m_stream = nullptr;

    // 简易混音器（音频线程独占读写；主线程经 AddVoice 投递）。
    struct Voice {
        std::vector<float> buffer; // 交错立体声
        std::size_t pos = 0;
        float gain = 1.0f;
        bool music = false;
        bool looping = false;
    };
    std::vector<Voice> m_voices;
    std::mutex m_voiceMutex; // 主线程投递 / 音频线程混音 互斥
    float m_master = 0.8f;
    float m_music = 0.6f;
    float m_sfx = 0.8f;

    std::vector<float> MakeSfx(SfxId id) const;      // 单发音效（立体声）
    std::vector<float> MakeBgm(int mapId) const;     // 循环乐句（立体声）
};

} // namespace legend::client
