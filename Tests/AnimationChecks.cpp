// ---------------------------------------------------------------------------
// 阶段24：AnimationChecks —— animations.json + 统一 AnimationPlayer 检查
//（指令四十五/四十六：加入现有 LegendWorldTests）。
// 覆盖：animation frame validation / invalid fps / invalid frameCount /
// invalid direction / sheet 引用缺失 / 帧网格溢出 / Player 推进与 UV。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Client/Visuals/AnimationPlayer.h"
#include "Client/Visuals/VisualAssetData.h"

#include <string>

#ifndef LEGEND_SOURCE_DIR
#define LEGEND_SOURCE_DIR "."
#endif

namespace worldtest {

namespace {

using namespace legend::visual;

void RunAnimationLogicChecks() {
    // ---- 真实仓库 animations.json ----
    AnimationSet set;
    AssetManifest manifest;
    const std::string animPath =
        std::string(LEGEND_SOURCE_DIR) + "/Data/Assets/animations.json";
    const std::string manifestPath =
        std::string(LEGEND_SOURCE_DIR) + "/Data/Assets/asset_manifest.json";
    {
        std::string error;
        const bool loaded = LoadAnimations(animPath, set, error);
        const bool manifestLoaded = LoadAssetManifest(manifestPath, manifest, error);
        Check("Animation: repo animations.json + manifest load", loaded && manifestLoaded);
        if (loaded && manifestLoaded) {
            std::string validateError;
            Check("Animation: repo set validates clean",
                  ValidateAnimationSet(set, validateError));
            std::string resolveError;
            Check("Animation: resolve against repo manifest ok",
                  ResolveAnimations(set, manifest, resolveError));
            Check("Animation: clip count == 28 (3 职业 x6 + Slime x5 + NPC x4 + Portal x1)",
                  set.clips.size() == 28);
            // 帧网格：warrior_idle 48x64 帧，sheet 192x512 → 4 列 8 行。
            const AnimationClipDef* idle = FindClip(set, "warrior_idle");
            Check("Animation: warrior_idle resolved sheet size",
                  idle != nullptr && idle->sheetWidth == 192 && idle->sheetHeight == 512);
        }
    }

    // ---- invalid fps / frameCount / directionCount（指令四十六）----
    {
        AnimationSet bad = set;
        if (!bad.clips.empty()) {
            bad.clips.front().fps = 0.0f;
            std::string error;
            Check("Animation: invalid fps rejected",
                  !ValidateAnimationSet(bad, error) && error.find("fps") != std::string::npos);
        }
        AnimationSet badCount = set;
        if (!badCount.clips.empty()) {
            badCount.clips.front().frameCount = 0;
            std::string error;
            Check("Animation: invalid frameCount rejected",
                  !ValidateAnimationSet(badCount, error));
        }
        AnimationSet badDir = set;
        if (!badDir.clips.empty()) {
            badDir.clips.front().directionCount = 5;
            std::string error;
            Check("Animation: invalid directionCount (5) rejected",
                  !ValidateAnimationSet(badDir, error));
        }
        AnimationSet dup = set;
        if (!dup.clips.empty()) {
            dup.clips.push_back(dup.clips.front());
            std::string error;
            Check("Animation: duplicate animationId rejected",
                  !ValidateAnimationSet(dup, error));
        }
    }

    // ---- sheet 引用缺失 / 帧网格溢出（Resolve 交叉校验）----
    {
        AnimationSet missingRef = set;
        if (!missingRef.clips.empty()) {
            missingRef.clips.front().spriteSheetAssetId = "no_such_sheet";
            std::string error;
            Check("Animation: missing sheet asset rejected by resolve",
                  !ResolveAnimations(missingRef, manifest, error));
        }
        AnimationSet overflow = set;
        if (!overflow.clips.empty()) {
            overflow.clips.front().frameWidth = 9999; // sheetWidth 不再整除
            std::string error;
            Check("Animation: frame grid overflow rejected",
                  !ResolveAnimations(overflow, manifest, error) &&
                      error.find("divisible") != std::string::npos);
        }
        AnimationSet tooManyFrames = set;
        if (!tooManyFrames.clips.empty()) {
            tooManyFrames.clips.front().frameCount = 999; // 列数不足
            std::string error;
            Check("Animation: frameCount beyond columns rejected",
                  !ResolveAnimations(tooManyFrames, manifest, error));
        }
    }

    // ---- AnimationPlayer（统一播放器；指令十六）----
    {
        AnimationPlayer player;
        player.SetClips(set);
        Check("AnimationPlayer: play missing clip returns false",
              !player.Play("no_such_clip") && !player.IsPlaying());

        Check("AnimationPlayer: play idle ok", player.Play("warrior_idle"));
        player.SetDirection(0); // South = 行 0
        player.Update(0.05f);
        const auto frame0 = player.CurrentFrame();
        Check("AnimationPlayer: frame valid + first frame UV",
              frame0.valid && frame0.frameIndex == 0 && frame0.u0 == 0.0f &&
                  std::abs(frame0.u1 - 0.25f) < 0.0001f);

        player.SetDirection(4); // North = 行 4/8 → v0 = 0.5
        const auto frameNorth = player.CurrentFrame();
        Check("AnimationPlayer: direction row maps to V band",
              frameNorth.directionRow == 4 &&
                  std::abs(frameNorth.v0 - 0.5f) < 0.0001f &&
                  std::abs(frameNorth.v1 - 0.625f) < 0.0001f);

        // 同名 Play 保持进度；异名重置。
        player.Update(0.3f);
        const int ordinalBefore = player.CurrentFrame().frameIndex;
        player.Play("warrior_idle");
        Check("AnimationPlayer: same-clip Play keeps progress",
              player.CurrentFrame().frameIndex == ordinalBefore);
        player.Play("warrior_walk");
        Check("AnimationPlayer: different clip resets progress",
              player.CurrentFrame().frameIndex == 0);

        // NonLoop 播完持帧（warrior_hit：2 帧 8fps → 0.25s）。
        player.Stop();
        player.Play("warrior_hit");
        player.Update(1.0f);
        Check("AnimationPlayer: non-loop clip finishes and holds last frame",
              player.IsFinished() &&
                  player.CurrentFrame().frameIndex ==
                      FindClip(set, "warrior_hit")->frameCount - 1);

        // 未 Resolve 的 clip（无 sheet 尺寸）→ invalid frame，不崩溃。
        AnimationSet unresolved = set;
        if (!unresolved.clips.empty()) {
            unresolved.clips.front().sheetWidth = 0;
            unresolved.clips.front().sheetHeight = 0;
            AnimationPlayer fallbackPlayer;
            fallbackPlayer.SetClips(unresolved);
            fallbackPlayer.Play(unresolved.clips.front().animationId);
            fallbackPlayer.Update(0.1f);
            Check("AnimationPlayer: unresolved clip yields invalid frame (no crash)",
                  !fallbackPlayer.CurrentFrame().valid);
        }
    }
}

} // namespace

// 由 WorldChecks.cpp 调用（阶段24）。
void RunAnimationChecks() {
    std::printf("[Animation] checks begin\n");
    RunAnimationLogicChecks();
    std::printf("[Animation] checks complete (failures so far = %d)\n", g_failures);
}

} // namespace worldtest
