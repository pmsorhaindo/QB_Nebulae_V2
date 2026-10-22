// CosmicFlux DSP core: a C++ port of Instruments/CosmicFlux/cosmicflux.instr.
//
// Design notes
//  - No Csound at runtime. Every opcode used by the instrument (portk, tone,
//    atone, dcblock2, follow2, flanger, alpass, phaser2, deltap/deltap3, lfo,
//    oscili, clip) is reimplemented here with the same coefficient formulas,
//    verified against Csound 6.18 micro-tests.
//  - Control-rate behaviour is kept: parameters are read and smoothed on a
//    fixed 64-sample grid (ksmps = 64), independent of the host buffer size,
//    so any frame count (down to 1) produces identical output.
//  - Csound evaluates opcodes block by block, which puts one extra 64-sample
//    block of latency in both feedback loops (Flux Chain and Cosmos). That is
//    reproduced so the port matches the Csound renders.
//  - prepare() is the only place that allocates. process() is real-time safe:
//    no allocation, no locks, no exceptions. Parameters are atomics.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "CosmicFluxParams.h"

namespace cosmicflux {

namespace detail {

struct DelayLine {
    void init(uint32_t maxDelaySamples);
    void clear();
    inline void write(float x) { buf[w] = x; w = (w + 1) & mask; }
    // Delay in whole samples (1 .. maxDelay()).
    inline float readInt(uint32_t d) const { return buf[(w - d) & mask]; }
    // Delay in fractional samples, 4-point cubic (2 .. maxDelay()-2).
    float readCubic(float d) const;
    uint32_t maxDelay() const { return mask > 4 ? mask - 3 : 1; }

    std::vector<float> buf;
    uint32_t mask = 0;
    uint32_t w = 0;
};

// portk: one-pole with half-time. Coefficient is supplied per control block.
struct Smoother {
    inline float process(float x, float coef) { y = x + coef * (y - x); return y; }
    float y = 0.0f;
};

// tone / atone (Csound first-order filters, half-power cutoff).
struct OnePole {
    void setCutoff(double hz, double sr);
    inline float lp(float x) { y1 = c1 * x + c2 * y1; return static_cast<float>(y1); }
    inline float hp(float x) {
        double y = c2 * (s + x);
        s = y - x;
        return static_cast<float>(y);
    }
    void clear() { y1 = 0.0; s = 0.0; }
    double c1 = 1.0, c2 = 0.0;
    double y1 = 0.0;  // lp state
    double s = 0.0;   // hp state
};

// dcblock2: input delayed by N-1 minus two cascaded N-point moving averages.
struct DcBlock2 {
    void init(uint32_t n);
    void clear();
    float process(float x);
    std::vector<float> d1, d2, d3;
    uint32_t n = 1, pos = 0, pos3 = 0;
    double acc1 = 0.0, acc2 = 0.0, inv = 1.0;
};

// follow2: envelope follower with attack/release defined as 60 dB times.
struct Follower {
    void set(double attackSec, double releaseSec, double sr);
    inline float process(float x) {
        double v = x < 0 ? -x : x;
        env = (v > env) ? v + ga * (env - v) : v + gr * (env - v);
        return static_cast<float>(env);
    }
    double ga = 0.0, gr = 0.0, env = 0.0;
};

// flanger: single delay line with feedback, output is the delayed signal.
struct Flanger {
    void init(uint32_t maxDelaySamples) { dl.init(maxDelaySamples); }
    void clear() { dl.clear(); }
    inline float process(float x, float delaySamples, float fb) {
        float maxd = static_cast<float>(dl.maxDelay() - 2);
        if (delaySamples < 2.0f) delaySamples = 2.0f;
        if (delaySamples > maxd) delaySamples = maxd;
        float y = dl.readCubic(delaySamples);
        dl.write(x + y * fb);
        return y;
    }
    DelayLine dl;
};

// alpass: Schroeder allpass, gain from reverb time.
struct SchroederAllpass {
    void init(double loopTimeSec, double reverbTimeSec, double sr);
    void clear() { dl.clear(); }
    inline float process(float x) {
        float y = dl.readInt(d);
        float w = x + g * y;
        dl.write(w);
        return y - g * w;
    }
    DelayLine dl;
    uint32_t d = 1;
    float g = 0.0f;
};

// phaser2: series of second-order allpass notches, mode 1 (harmonic spacing).
struct Phaser2 {
    static constexpr int kStages = 4;
    void setFrequency(double baseHz, double q, double sep, double sr);
    void clear();
    inline float process(float x) {
        double in = x + last * fbGain;
        for (int j = 0; j < kStages; ++j) {
            double y = a2[j] * in + a1[j] * x1[j] + x2[j] - a1[j] * y1[j] - a2[j] * y2[j];
            x2[j] = x1[j]; x1[j] = in;
            y2[j] = y1[j]; y1[j] = y;
            in = y;
        }
        last = in;
        return static_cast<float>(in);
    }
    double a1[kStages] = {0, 0, 0, 0}, a2[kStages] = {0, 0, 0, 0};
    double x1[kStages] = {0, 0, 0, 0}, x2[kStages] = {0, 0, 0, 0};
    double y1[kStages] = {0, 0, 0, 0}, y2[kStages] = {0, 0, 0, 0};
    double last = 0.0;
    double fbGain = 0.7;
};

// Fixed 64-sample FIFO used to emulate the one-block feedback latency of the
// Csound orchestra.
struct BlockDelay {
    static constexpr int kLen = 64;
    void clear() { buf.fill(0.0f); pos = 0; }
    std::array<float, kLen> buf{};
    int pos = 0;
};

}  // namespace detail

class CosmicFluxDSP {
public:
    static constexpr int kControlBlock = 64;  // ksmps in the Nebulae instrument

    CosmicFluxDSP();

    // Allocates all delay memory for the given sample rate. Not real-time safe.
    void prepare(double sampleRate);

    // Clears all state. With snapSmoothers = true the control smoothers start
    // at their targets (no start-up sweep); false mimics Csound's portk, which
    // starts from zero.
    void reset(bool snapSmoothers = true);

    double sampleRate() const { return sr_; }

    // Parameter access, normalised 0..1. Safe to call from any thread.
    void setParam(int id, float normalized);
    float getParam(int id) const;

    // Process numFrames of stereo audio. Buffers may alias (in-place is fine).
    // Real-time safe once prepare() has been called.
    void process(const float* inL, const float* inR, float* outL, float* outR, int numFrames);

private:
    void controlTick();
    void renderSamples(const float* inL, const float* inR, float* outL, float* outR, int n);
    void snapAllSmoothers();

    double sr_ = 48000.0;
    bool prepared_ = false;
    std::array<std::atomic<float>, kNumParams> params_;
    std::array<float, kNumParams> p_{};  // snapshot read at each control tick

    // ---- control-rate state ----
    int samplesUntilTick_ = 0;
    uint64_t samplesElapsed_ = 0;
    double coef002_ = 0, coef005_ = 0, coef01_ = 0, coef02_ = 0;  // portk coefficients

    // Flanger
    detail::Follower follower_;
    float followDepth_ = 0.0f;
    detail::Smoother flangerDepthPort_;
    float flangerDepth_ = 0.0f;
    float flangerMix_ = 0.0f;
    float flangerFeedback_ = 0.7f;
    int flangerMode_ = 1;
    double flangerLfoPhase_ = 0.0, flangerLfoInc_ = 0.0;
    detail::Flanger flangerL_, flangerR_;

    // Clock
    bool tapLast_ = false;
    double clockLast_ = 0.0, clockTime_ = 0.4;

    // Flux chain
    detail::Smoother moonTimePort_;
    float moonDelayTime_ = 0.4f;
    float stageTimeSamples_ = 0.0f;
    std::array<double, 6> fluxLfoPhase_{}, fluxLfoInc_{};
    std::array<float, 6> fluxLfoAmpSamples_{};
    detail::Smoother dimensionPort_;
    float stageGain_ = 1.0f;
    detail::Smoother feedbackFilterPort_;
    float lpAmount_ = 0.0f, hpAmount_ = 0.0f;
    detail::OnePole fluxLpL_, fluxLpR_, fluxHpL_, fluxHpR_;
    detail::DcBlock2 fluxDcL_, fluxDcR_;
    detail::Smoother moonFeedbackPort_;
    float moonFeedback_ = 0.0f;
    std::array<detail::DelayLine, 6> fluxL_, fluxR_;
    detail::BlockDelay fluxFbDelayL_, fluxFbDelayR_;
    detail::Smoother multiplyPort_;
    float multiply_ = 1.0f, tapGain_ = 1.0f;

    // Phaser
    detail::Smoother phaserOnPort_;
    float phaserDepth_ = 0.0f;
    double phaserLfoPhase1_ = 0.0, phaserLfoPhase2_ = 0.5, phaserLfoInc_ = 0.0;
    float phaserXfade_ = 0.0f;
    detail::Phaser2 phaserL1_, phaserL2_, phaserR1_, phaserR2_;

    // Cosmos
    detail::Smoother cosmosDelayPort_, warpPort_, cosmosFeedbackPort_, densityPort_;
    detail::Smoother modDepthPort_, modRatePort_;
    bool freeze_ = false;
    int cosmosMode_ = 0;
    float cosmosFeedback_ = 0.0f, density_ = 0.0f;
    std::array<float, 8> fdnDelaySamples_{};
    std::array<uint32_t, 8> fdnDelayInt_{};
    std::array<double, 8> fdnLfoPhase_{}, fdnLfoIncPerSample_{};
    float fdnModAmpSamples_ = 0.0f;
    std::array<detail::DelayLine, 8> fdn_;
    std::array<float, 8> fdnState_{};
    std::array<detail::OnePole, 8> fdnModeFilter_;
    std::array<detail::DcBlock2, 8> fdnDc_;
    detail::SchroederAllpass diffM1L_[2], diffM1R_[2], diffM2L_[3], diffM2R_[3];
    detail::OnePole outLpL_, outLpR_, outHpL_, outHpR_;

    // Output
    detail::Smoother mixPort_;
    float mixWet_ = 0.0f, mixDry_ = 1.0f;
    float blockPeak_ = 0.0f;
    detail::Smoother limiterPort_;
    float limiterGain_ = 1.0f;
};

}  // namespace cosmicflux
