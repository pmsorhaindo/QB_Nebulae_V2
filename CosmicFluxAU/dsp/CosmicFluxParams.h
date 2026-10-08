// CosmicFlux parameter definitions shared by the DSP core, the offline
// renderer and the AUv3 wrapper.
//
// Every parameter is stored internally as a normalised 0..1 float, which is
// exactly the value the Nebulae firmware feeds the Csound instrument on the
// corresponding channel. Indexed parameters (modes) and switches are exposed to
// the host with friendlier units and converted with the helpers below.
#pragma once

namespace cosmicflux {

enum ParamId : int {
    pTime = 0,        // start        - Flux Chain delay time (10 ms .. 1.2 s, exponential)
    pFeedback,        // size         - Flux Chain global feedback
    pDimension,       // overlap      - Flux Chain per-stage smear
    pMultiply,        // speed        - number of taps (1..6)
    pCosmosDelay,     // pitch        - Cosmos longest line (V/oct style)
    pCosmosDensity,   // density      - Cosmos mixing density
    pCosmosFeedback,  // window       - Cosmos decay
    pMix,             // blend        - dry/wet, equal power
    pEarlyMod,        // start_alt    - Flux Chain early modulation
    pLateMod,         // overlap_alt  - Flux Chain late modulation
    pFeedbackFilter,  // size_alt     - Flux Chain feedback tilt
    pCosmosWarp,      // density_alt  - Cosmos line spread
    pCosmosMod,       // window_alt   - Cosmos modulation
    pCosmosMode,      // pitch_alt    - Cosmos mode (4 modes)
    pFlangerDepth,    // blend_alt    - dynamic flanger depth
    pTap,             // reset        - tap / clock input (momentary)
    pFreeze,          // freeze       - infinite hold
    pPhaser,          // record       - barberpole phaser on/off
    pHalfSpeed,       // file         - half speed delay time
    pMuteInput,       // source       - mute input
    pDottedEighth,    // reset_alt    - dotted eighth multiplier
    pFlangerNegFB,    // freeze_alt   - flanger negative feedback
    pPhaserSync,      // record_alt   - phaser rate synced to delay time
    pFlangerMode,     // file_alt     - flanger mode (EnvDown / EnvUp / LFO)
    kNumParams
};

enum class ParamKind { Continuous, Toggle, Momentary, Indexed };

struct ParamInfo {
    const char* identifier;      // stable identifier for host state
    const char* displayName;     // shown in AUM / generic views
    const char* nebulaeChannel;  // channel name used by the Nebulae firmware and dev scenarios
    ParamKind kind;
    int numSteps;                // for Indexed parameters
    float defaultValue;          // normalised 0..1
    const char* const* valueStrings;  // for Indexed parameters, numSteps entries
};

const ParamInfo& paramInfo(int id);

// Returns -1 when the channel is not a CosmicFlux control.
int paramIdForNebulaeChannel(const char* channel);

// Indexed parameters: convert between the host-facing step index and the
// normalised value the instrument expects (int(value * (steps - 0.01))).
float normalizedFromIndex(int id, int index);
int indexFromNormalized(int id, float normalized);

}  // namespace cosmicflux
