#include "CosmicFluxParams.h"

#include <cstring>

namespace cosmicflux {

namespace {

const char* const kCosmosModeNames[] = {"Sparse Echo", "Smooth", "Slow Bloom", "Cross-Filtered"};
const char* const kFlangerModeNames[] = {"Env Down", "Env Up", "LFO"};

const ParamInfo kParams[kNumParams] = {
    {"time", "Time", "start", ParamKind::Continuous, 0, 0.30f, nullptr},
    {"feedback", "Feedback", "size", ParamKind::Continuous, 0, 0.45f, nullptr},
    {"dimension", "Dimension", "overlap", ParamKind::Continuous, 0, 0.40f, nullptr},
    {"multiply", "Multiply", "speed", ParamKind::Continuous, 0, 0.50f, nullptr},
    {"cosmos_delay", "Cosmos Delay", "pitch", ParamKind::Continuous, 0, 0.55f, nullptr},
    {"cosmos_density", "Cosmos Density", "density", ParamKind::Continuous, 0, 0.50f, nullptr},
    {"cosmos_feedback", "Cosmos Feedback", "window", ParamKind::Continuous, 0, 0.40f, nullptr},
    {"mix", "Mix", "blend", ParamKind::Continuous, 0, 0.50f, nullptr},
    {"early_mod", "Early Mod", "start_alt", ParamKind::Continuous, 0, 0.15f, nullptr},
    {"late_mod", "Late Mod", "overlap_alt", ParamKind::Continuous, 0, 0.15f, nullptr},
    {"feedback_filter", "Feedback Filter", "size_alt", ParamKind::Continuous, 0, 0.50f, nullptr},
    {"cosmos_warp", "Cosmos Warp", "density_alt", ParamKind::Continuous, 0, 0.60f, nullptr},
    {"cosmos_mod", "Cosmos Mod", "window_alt", ParamKind::Continuous, 0, 0.30f, nullptr},
    {"cosmos_mode", "Cosmos Mode", "pitch_alt", ParamKind::Indexed, 4, 0.0f, kCosmosModeNames},
    {"flanger_depth", "Flanger Depth", "blend_alt", ParamKind::Continuous, 0, 0.0f, nullptr},
    {"tap", "Tap", "reset", ParamKind::Momentary, 0, 0.0f, nullptr},
    {"freeze", "Freeze", "freeze", ParamKind::Toggle, 0, 0.0f, nullptr},
    {"phaser", "Phaser", "record", ParamKind::Toggle, 0, 0.0f, nullptr},
    {"half_speed", "Half Speed", "file", ParamKind::Toggle, 0, 0.0f, nullptr},
    {"mute_input", "Mute Input", "source", ParamKind::Toggle, 0, 0.0f, nullptr},
    {"dotted_eighth", "Dotted Eighth", "reset_alt", ParamKind::Toggle, 0, 0.0f, nullptr},
    {"flanger_neg_fb", "Flanger Neg FB", "freeze_alt", ParamKind::Toggle, 0, 0.0f, nullptr},
    {"phaser_sync", "Phaser Sync", "record_alt", ParamKind::Toggle, 0, 0.0f, nullptr},
    {"flanger_mode", "Flanger Mode", "file_alt", ParamKind::Indexed, 3, 0.5f, kFlangerModeNames},
};

}  // namespace

const ParamInfo& paramInfo(int id) {
    if (id < 0 || id >= kNumParams) id = 0;
    return kParams[id];
}

int paramIdForNebulaeChannel(const char* channel) {
    if (!channel) return -1;
    for (int i = 0; i < kNumParams; ++i) {
        if (std::strcmp(kParams[i].nebulaeChannel, channel) == 0) return i;
    }
    // Firmware aliases used by the Csound header
    if (std::strcmp(channel, "loopstart") == 0) return pTime;
    if (std::strcmp(channel, "loopsize") == 0) return pFeedback;
    if (std::strcmp(channel, "loopstart_alt") == 0) return pEarlyMod;
    if (std::strcmp(channel, "loopsize_alt") == 0) return pFeedbackFilter;
    if (std::strcmp(channel, "filesel") == 0) return pHalfSpeed;
    return -1;
}

float normalizedFromIndex(int id, int index) {
    const ParamInfo& info = paramInfo(id);
    if (info.kind != ParamKind::Indexed || info.numSteps <= 0) return static_cast<float>(index);
    if (index < 0) index = 0;
    if (index >= info.numSteps) index = info.numSteps - 1;
    // Centre of the bin so int(value * (steps - 0.01)) lands on `index`.
    return (static_cast<float>(index) + 0.5f) / static_cast<float>(info.numSteps);
}

int indexFromNormalized(int id, float normalized) {
    const ParamInfo& info = paramInfo(id);
    if (info.kind != ParamKind::Indexed || info.numSteps <= 0) return static_cast<int>(normalized);
    int idx = static_cast<int>(normalized * (static_cast<float>(info.numSteps) - 0.01f));
    if (idx < 0) idx = 0;
    if (idx >= info.numSteps) idx = info.numSteps - 1;
    return idx;
}

}  // namespace cosmicflux
