#include "CosmicFluxDSP.h"

#include <algorithm>
#include <cmath>

namespace cosmicflux {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float csoundScale(float k, float kmax, float kmin) { return kmin + k * (kmax - kmin); }

// Csound lfo type 1 (triangle), phase 0..1
inline double triLfo(double ph) {
    if (ph < 0.25) return ph * 4.0;
    if (ph < 0.75) return 2.0 - ph * 4.0;
    return ph * 4.0 - 4.0;
}

inline double wrapPhase(double ph) {
    if (ph >= 1.0) ph -= std::floor(ph);
    return ph;
}

inline float tanhf_fast(float x) { return std::tanh(x); }

// clip method 2 (tanh), limit 0.95
inline float softClip095(float x) {
    constexpr float kLimit = 0.95f;
    static const float kNorm = 1.0f / std::tanh(1.0f);
    if (x >= kLimit) return kLimit;
    if (x <= -kLimit) return -kLimit;
    return kLimit * std::tanh(x / kLimit) * kNorm;
}

uint32_t nextPow2(uint32_t v) {
    uint32_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

}  // namespace

// ---------------------------------------------------------------------------
// detail
// ---------------------------------------------------------------------------
namespace detail {

void DelayLine::init(uint32_t maxDelaySamples) {
    uint32_t size = nextPow2(maxDelaySamples + 8);
    buf.assign(size, 0.0f);
    mask = size - 1;
    w = 0;
}

void DelayLine::clear() {
    std::fill(buf.begin(), buf.end(), 0.0f);
    w = 0;
}

float DelayLine::readCubic(float d) const {
    float maxd = static_cast<float>(maxDelay() - 2);
    if (d < 2.0f) d = 2.0f;
    if (d > maxd) d = maxd;
    uint32_t D = static_cast<uint32_t>(d);
    float f = d - static_cast<float>(D);
    float xm1 = buf[(w - (D - 1)) & mask];
    float x0 = buf[(w - D) & mask];
    float x1 = buf[(w - (D + 1)) & mask];
    float x2 = buf[(w - (D + 2)) & mask];
    float c1 = 0.5f * (x1 - xm1);
    float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

void OnePole::setCutoff(double hz, double sr) {
    double b = 2.0 - std::cos(kTwoPi * hz / sr);
    c2 = b - std::sqrt(b * b - 1.0);
    c1 = 1.0 - c2;
}

void DcBlock2::init(uint32_t order) {
    n = order < 2 ? 2 : order;
    inv = 1.0 / static_cast<double>(n);
    d1.assign(n, 0.0f);
    d2.assign(n, 0.0f);
    d3.assign(n - 1, 0.0f);
    clear();
}

void DcBlock2::clear() {
    std::fill(d1.begin(), d1.end(), 0.0f);
    std::fill(d2.begin(), d2.end(), 0.0f);
    std::fill(d3.begin(), d3.end(), 0.0f);
    pos = 0;
    pos3 = 0;
    acc1 = 0.0;
    acc2 = 0.0;
}

float DcBlock2::process(float x) {
    acc1 += static_cast<double>(x) - d1[pos];
    d1[pos] = x;
    float m1 = static_cast<float>(acc1 * inv);
    acc2 += static_cast<double>(m1) - d2[pos];
    d2[pos] = m1;
    float m2 = static_cast<float>(acc2 * inv);
    float xd = d3[pos3];
    d3[pos3] = x;
    if (++pos >= n) pos = 0;
    if (++pos3 >= n - 1) pos3 = 0;
    return xd - m2;
}

void Follower::set(double attackSec, double releaseSec, double sr) {
    ga = std::pow(0.001, 1.0 / (attackSec * sr));
    gr = std::pow(0.001, 1.0 / (releaseSec * sr));
}

void SchroederAllpass::init(double loopTimeSec, double reverbTimeSec, double sr) {
    d = static_cast<uint32_t>(std::lround(loopTimeSec * sr));
    if (d < 1) d = 1;
    dl.init(d + 4);
    g = static_cast<float>(std::pow(0.001, loopTimeSec / reverbTimeSec));
}

void Phaser2::setFrequency(double baseHz, double q, double sep, double sr) {
    double nyq = sr * 0.49;
    for (int j = 0; j < kStages; ++j) {
        double f = baseHz * (1.0 + sep * j);  // mode 1: harmonic spacing
        if (f > nyq) f = nyq;
        if (f < 1.0) f = 1.0;
        double r = std::exp(-kPi * f / (sr * q));
        a1[j] = -2.0 * r * std::cos(kTwoPi * f / sr);
        a2[j] = r * r;
    }
}

void Phaser2::clear() {
    for (int j = 0; j < kStages; ++j) x1[j] = x2[j] = y1[j] = y2[j] = 0.0;
    last = 0.0;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// CosmicFluxDSP
// ---------------------------------------------------------------------------
CosmicFluxDSP::CosmicFluxDSP() {
    for (int i = 0; i < kNumParams; ++i) {
        params_[i].store(paramInfo(i).defaultValue, std::memory_order_relaxed);
        p_[i] = paramInfo(i).defaultValue;
    }
}

void CosmicFluxDSP::prepare(double sampleRate) {
    sr_ = sampleRate > 1000.0 ? sampleRate : 48000.0;

    // Flux chain: stage delay <= 2.0 s / 6 plus modulation (< 2 ms)
    uint32_t fluxMax = static_cast<uint32_t>(0.345 * sr_) + 8;
    for (int i = 0; i < 6; ++i) {
        fluxL_[i].init(fluxMax);
        fluxR_[i].init(fluxMax);
    }
    // Cosmos: line <= 2.0 s plus one Csound block plus modulation (< 8 ms)
    uint32_t fdnMax = static_cast<uint32_t>(2.02 * sr_) + kControlBlock + 8;
    for (int i = 0; i < 8; ++i) fdn_[i].init(fdnMax);

    // Flanger: depth <= 10 ms
    uint32_t flMax = static_cast<uint32_t>(0.02 * sr_) + 8;
    flangerL_.init(flMax);
    flangerR_.init(flMax);

    // dcblock2 default order behaves like N ~ 179 samples at 48 kHz
    uint32_t dcN = static_cast<uint32_t>(std::lround(179.0 * sr_ / 48000.0));
    fluxDcL_.init(dcN);
    fluxDcR_.init(dcN);
    for (int i = 0; i < 8; ++i) fdnDc_[i].init(dcN);

    // Cosmos input diffusers
    const double m1L[2] = {0.005, 0.011}, m1R[2] = {0.007, 0.013};
    const double m2L[3] = {0.031, 0.043, 0.053}, m2R[3] = {0.037, 0.047, 0.059};
    for (int i = 0; i < 2; ++i) {
        diffM1L_[i].init(m1L[i], 0.5, sr_);
        diffM1R_[i].init(m1R[i], 0.5, sr_);
    }
    for (int i = 0; i < 3; ++i) {
        diffM2L_[i].init(m2L[i], 0.5, sr_);
        diffM2R_[i].init(m2R[i], 0.5, sr_);
    }

    // Fixed filters
    for (int i = 0; i < 8; ++i) fdnModeFilter_[i].setCutoff((i % 2 == 0) ? 3000.0 : 500.0, sr_);
    outLpL_.setCutoff(8000.0, sr_);
    outLpR_.setCutoff(8000.0, sr_);
    outHpL_.setCutoff(80.0, sr_);
    outHpR_.setCutoff(80.0, sr_);

    follower_.set(0.005, 0.1, sr_);

    double blockSec = static_cast<double>(kControlBlock) / sr_;
    coef002_ = std::pow(0.5, blockSec / 0.02);
    coef005_ = std::pow(0.5, blockSec / 0.05);
    coef01_ = std::pow(0.5, blockSec / 0.1);
    coef02_ = std::pow(0.5, blockSec / 0.2);

    prepared_ = true;
    reset(true);
}

void CosmicFluxDSP::reset(bool snapSmoothers) {
    for (auto& d : fluxL_) d.clear();
    for (auto& d : fluxR_) d.clear();
    for (auto& d : fdn_) d.clear();
    fdnState_.fill(0.0f);
    flangerL_.clear();
    flangerR_.clear();
    fluxDcL_.clear();
    fluxDcR_.clear();
    for (auto& d : fdnDc_) d.clear();
    for (auto& f : fdnModeFilter_) f.clear();
    fluxLpL_.clear(); fluxLpR_.clear(); fluxHpL_.clear(); fluxHpR_.clear();
    outLpL_.clear(); outLpR_.clear(); outHpL_.clear(); outHpR_.clear();
    for (auto& a : diffM1L_) a.clear();
    for (auto& a : diffM1R_) a.clear();
    for (auto& a : diffM2L_) a.clear();
    for (auto& a : diffM2R_) a.clear();
    fluxFbDelayL_.clear();
    fluxFbDelayR_.clear();
    phaserL1_.clear(); phaserL2_.clear(); phaserR1_.clear(); phaserR2_.clear();
    follower_.env = 0.0;
    followDepth_ = 0.0f;

    flangerLfoPhase_ = 0.0;
    fluxLfoPhase_.fill(0.0);
    phaserLfoPhase1_ = 0.0;
    phaserLfoPhase2_ = 0.5;
    fdnLfoPhase_ = {0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875};

    tapLast_ = false;
    clockLast_ = 0.0;
    clockTime_ = 0.4;
    samplesElapsed_ = 0;
    samplesUntilTick_ = 0;
    blockPeak_ = 0.0f;

    // Csound portk state starts at zero
    flangerDepthPort_.y = 0; moonTimePort_.y = 0; dimensionPort_.y = 0; feedbackFilterPort_.y = 0;
    moonFeedbackPort_.y = 0; multiplyPort_.y = 0; phaserOnPort_.y = 0; cosmosDelayPort_.y = 0;
    warpPort_.y = 0; cosmosFeedbackPort_.y = 0; densityPort_.y = 0; modDepthPort_.y = 0;
    modRatePort_.y = 0; mixPort_.y = 0; limiterPort_.y = 0;

    if (snapSmoothers) snapAllSmoothers();
    controlTick();
    samplesUntilTick_ = kControlBlock;
}

void CosmicFluxDSP::setParam(int id, float normalized) {
    if (id < 0 || id >= kNumParams) return;
    params_[id].store(clampf(normalized, 0.0f, 1.0f), std::memory_order_relaxed);
}

float CosmicFluxDSP::getParam(int id) const {
    if (id < 0 || id >= kNumParams) return 0.0f;
    return params_[id].load(std::memory_order_relaxed);
}

void CosmicFluxDSP::snapAllSmoothers() {
    for (int i = 0; i < kNumParams; ++i) p_[i] = params_[i].load(std::memory_order_relaxed);
    const bool freeze = p_[pFreeze] > 0.5f;

    flangerDepthPort_.y = p_[pFlangerDepth] * 0.01f;
    float raw = csoundScale(p_[pTime], 1.19f, 0.01f);
    moonTimePort_.y = raw * raw;
    dimensionPort_.y = p_[pDimension] * 0.8f;
    feedbackFilterPort_.y = p_[pFeedbackFilter];
    moonFeedbackPort_.y = p_[pFeedback] * 0.98f;
    multiplyPort_.y = static_cast<float>(static_cast<int>(p_[pMultiply] * 5.99f) + 1);
    phaserOnPort_.y = p_[pPhaser] > 0.5f ? 1.0f : 0.0f;
    float vnorm = (p_[pCosmosDelay] - 0.6f) * 5.0f;
    cosmosDelayPort_.y = clampf(0.25f * std::pow(2.0f, -vnorm), 0.06f, 2.0f);
    warpPort_.y = p_[pCosmosWarp];
    cosmosFeedbackPort_.y = freeze ? 1.0f : p_[pCosmosFeedback] * 0.995f;
    densityPort_.y = freeze ? 1.0f : p_[pCosmosDensity];
    float modDepth = freeze ? 0.0f : csoundScale(p_[pCosmosMod], 0.008f, 0.0f);
    modDepthPort_.y = modDepth;
    modRatePort_.y = (freeze || modDepth <= 0.0f) ? 0.0f : std::sqrt(modDepth) * 2.0f;
    mixPort_.y = p_[pMix];
    limiterPort_.y = 1.0f;
}

// ---------------------------------------------------------------------------
// Control rate (every 64 samples)
// ---------------------------------------------------------------------------
void CosmicFluxDSP::controlTick() {
    for (int i = 0; i < kNumParams; ++i) p_[i] = params_[i].load(std::memory_order_relaxed);
    const float sr = static_cast<float>(sr_);
    const float c002 = static_cast<float>(coef002_), c005 = static_cast<float>(coef005_);
    const float c01 = static_cast<float>(coef01_), c02 = static_cast<float>(coef02_);

    // ---- [A] flanger ----
    flangerMode_ = static_cast<int>(p_[pFlangerMode] * 2.99f);
    flangerDepth_ = flangerDepthPort_.process(p_[pFlangerDepth] * 0.01f, c01);
    flangerFeedback_ = p_[pFlangerNegFB] > 0.5f ? -0.7f : 0.7f;
    flangerMix_ = flangerDepth_ > 0.0f ? 0.5f : 0.0f;
    flangerLfoInc_ = 0.5 / sr_;

    // ---- clock / tap ----
    bool tapNow = p_[pTap] > 0.5f;
    bool clockTrig = tapNow && !tapLast_;
    tapLast_ = tapNow;
    if (clockTrig) {
        double now = static_cast<double>(samplesElapsed_) / sr_;
        double interval = now - clockLast_;
        if (interval > 0.05 && interval < 3.0) clockTime_ = interval;
        clockLast_ = now;
    }

    // ---- Flux Chain timing ----
    float moonRaw = csoundScale(p_[pTime], 1.19f, 0.01f);
    float moonExp = moonRaw * moonRaw;
    float moonPort = moonTimePort_.process(moonExp, c02);
    float mult = p_[pHalfSpeed] > 0.5f ? 0.5f : 1.0f;
    if (p_[pDottedEighth] > 0.5f) mult *= 1.5f;
    moonDelayTime_ = clampf(moonPort * mult, 0.01f, 2.0f);
    float stageTime = moonDelayTime_ / 6.0f;
    stageTimeSamples_ = stageTime * sr;

    float earlyRate = csoundScale(p_[pEarlyMod], 0.5f, 0.01f);
    float earlyDepth = earlyRate * 0.002f;
    float lateRate = csoundScale(p_[pLateMod], 1.0f, 0.01f);
    float lateDepth = lateRate * 0.002f;
    const float amps[6] = {earlyDepth, earlyDepth * 0.8f, earlyDepth * 0.6f,
                           lateDepth * 0.6f, lateDepth * 0.8f, lateDepth};
    const float rates[6] = {earlyRate, earlyRate * 1.1f, earlyRate * 1.2f,
                            lateRate * 0.9f, lateRate * 1.0f, lateRate * 1.1f};
    for (int i = 0; i < 6; ++i) {
        fluxLfoAmpSamples_[i] = amps[i] * sr;
        fluxLfoInc_[i] = rates[i] / sr_;
    }

    float dim = dimensionPort_.process(p_[pDimension] * 0.8f, c01);
    stageGain_ = (1.0f - dim) / (1.0f + std::fabs(dim));

    float fbf = feedbackFilterPort_.process(p_[pFeedbackFilter], c01);
    lpAmount_ = fbf < 0.5f ? (0.5f - fbf) * 2.0f : 0.0f;
    hpAmount_ = fbf > 0.5f ? (fbf - 0.5f) * 2.0f : 0.0f;
    if (lpAmount_ > 0.0f) {
        double hz = 2000.0 + lpAmount_ * 8000.0;
        fluxLpL_.setCutoff(hz, sr_);
        fluxLpR_.setCutoff(hz, sr_);
    }
    if (hpAmount_ > 0.0f) {
        double hz = 200.0 + hpAmount_ * 1000.0;
        fluxHpL_.setCutoff(hz, sr_);
        fluxHpR_.setCutoff(hz, sr_);
    }
    moonFeedback_ = moonFeedbackPort_.process(p_[pFeedback] * 0.98f, c01);

    float multInt = static_cast<float>(static_cast<int>(p_[pMultiply] * 5.99f) + 1);
    multiply_ = multiplyPort_.process(multInt, c005);
    tapGain_ = 1.0f / std::sqrt(std::max(multiply_, 1.0e-3f));

    // ---- [C] phaser ----
    float phaserOn = phaserOnPort_.process(p_[pPhaser] > 0.5f ? 1.0f : 0.0f, c005);
    phaserDepth_ = phaserOn * 0.8f;
    double phaserRate = p_[pPhaserSync] > 0.5f ? 1.0 / moonDelayTime_ : 0.1;
    phaserLfoInc_ = phaserRate * static_cast<double>(kControlBlock) / sr_;
    double lfo1 = std::sin(kTwoPi * phaserLfoPhase1_);
    double lfo2 = std::sin(kTwoPi * phaserLfoPhase2_);
    phaserLfoPhase1_ = wrapPhase(phaserLfoPhase1_ + phaserLfoInc_);
    phaserLfoPhase2_ = wrapPhase(phaserLfoPhase2_ + phaserLfoInc_);
    double f1 = 200.0 + (lfo1 + 1.0) * 1900.0;
    double f2 = 200.0 + (lfo2 + 1.0) * 1900.0;
    phaserL1_.setFrequency(f1, 0.7, 1.0, sr_);
    phaserL2_.setFrequency(f2, 0.7, 1.0, sr_);
    phaserR1_.setFrequency(4200.0 - f1, 0.7, 1.0, sr_);
    phaserR2_.setFrequency(4200.0 - f2, 0.7, 1.0, sr_);
    double xf = (lfo1 + 1.0) * 0.5;
    phaserXfade_ = static_cast<float>((1.0 - std::cos(xf * kPi)) * 0.5);

    // ---- [D] Cosmos ----
    freeze_ = p_[pFreeze] > 0.5f;
    float vnorm = (p_[pCosmosDelay] - 0.6f) * 5.0f;
    float delaySec = clampf(0.25f * std::pow(2.0f, -vnorm), 0.06f, 2.0f);
    float delayPort = cosmosDelayPort_.process(delaySec, c02);
    float warp = warpPort_.process(p_[pCosmosWarp], c01);
    static const float kRatios[8] = {1.0f, 0.887f, 0.781f, 0.693f, 0.613f, 0.541f, 0.479f, 0.421f};
    for (int i = 0; i < 8; ++i) {
        float lenSec = delayPort * (1.0f - warp * (1.0f - kRatios[i]));
        // Csound writes the previous block's feedback vector, adding ksmps of loop latency.
        fdnDelaySamples_[i] = lenSec * sr + static_cast<float>(kControlBlock);
        fdnDelayInt_[i] = static_cast<uint32_t>(lenSec * sr + 0.5f) + kControlBlock;
    }

    float fbRaw = freeze_ ? 1.0f : p_[pCosmosFeedback] * 0.995f;
    float fbPort = cosmosFeedbackPort_.process(fbRaw, c01);
    cosmosFeedback_ = freeze_ ? 1.0f : fbPort;
    density_ = densityPort_.process(freeze_ ? 1.0f : p_[pCosmosDensity], c01);
    cosmosMode_ = static_cast<int>(p_[pCosmosMode] * 3.99f);

    float modDepthRaw = csoundScale(p_[pCosmosMod], 0.008f, 0.0f);
    float modDepth = freeze_ ? 0.0f : modDepthRaw;
    float modRate = freeze_ ? 0.0f : (modDepth > 0.0f ? std::sqrt(modDepth) * 2.0f : 0.0f);
    float modDepthP = modDepthPort_.process(modDepth, c01);
    float modRateP = modRatePort_.process(modRate, c01);
    fdnModAmpSamples_ = modDepthP * sr;
    for (int i = 0; i < 8; ++i) fdnLfoIncPerSample_[i] = modRateP / sr_;

    // ---- output ----
    float mix = mixPort_.process(p_[pMix], c01);
    mixWet_ = std::sin(mix * 1.5708f);
    mixDry_ = std::cos(mix * 1.5708f);

    float gainTarget = blockPeak_ > 0.85f ? 0.85f / blockPeak_ : 1.0f;
    limiterGain_ = limiterPort_.process(gainTarget, c002);
    blockPeak_ = 0.0f;
}

// ---------------------------------------------------------------------------
// Audio rate
// ---------------------------------------------------------------------------
void CosmicFluxDSP::process(const float* inL, const float* inR, float* outL, float* outR, int numFrames) {
    if (!prepared_) {
        for (int i = 0; i < numFrames; ++i) { outL[i] = inL[i]; outR[i] = inR[i]; }
        return;
    }
    int done = 0;
    while (done < numFrames) {
        if (samplesUntilTick_ <= 0) {
            controlTick();
            samplesUntilTick_ = kControlBlock;
        }
        int n = std::min(numFrames - done, samplesUntilTick_);
        renderSamples(inL + done, inR + done, outL + done, outR + done, n);
        samplesUntilTick_ -= n;
        samplesElapsed_ += static_cast<uint64_t>(n);
        done += n;
    }
}

void CosmicFluxDSP::renderSamples(const float* inL, const float* inR, float* outL, float* outR, int n) {
    const float sr = static_cast<float>(sr_);
    const bool mute = p_[pMuteInput] > 0.5f;
    const bool freeze = freeze_;
    const int mode = cosmosMode_;
    const float density2over8 = density_ * 0.25f;
    const float fbGain2 = cosmosFeedback_ * 2.0f;
    const float massiveInputGain = freeze ? 0.0f : 1.0f;
    const float xf = phaserXfade_;
    const float xfInv = 1.0f - xf;
    const float phDepth = phaserDepth_;
    const float phDryGain = 1.0f - phDepth * 0.5f;
    constexpr float kAntiDenorm = 1.0e-18f;

    for (int i = 0; i < n; ++i) {
        float l = mute ? 0.0f : inL[i] * 0.95f;
        float r = mute ? 0.0f : inR[i] * 0.95f;

        // ---- [A] dynamic flanger ----
        float env = follower_.process((l + r) * 0.5f);
        if (samplesUntilTick_ == kControlBlock && i == 0) followDepth_ = env;  // k(aFollow)
        double flLfo = std::sin(kTwoPi * flangerLfoPhase_);
        flangerLfoPhase_ = wrapPhase(flangerLfoPhase_ + flangerLfoInc_);
        float modSec;
        if (flangerMode_ == 0) modSec = (1.0f - followDepth_) * flangerDepth_;
        else if (flangerMode_ == 1) modSec = followDepth_ * flangerDepth_;
        else modSec = static_cast<float>(flLfo) * flangerDepth_;
        float modSamples = modSec * sr;
        float flL = flangerL_.process(l, modSamples, flangerFeedback_);
        float flR = flangerR_.process(r, modSamples, flangerFeedback_);
        float sigL = flL * flangerMix_ + l * (1.0f - flangerMix_);
        float sigR = flR * flangerMix_ + r * (1.0f - flangerMix_);

        // ---- [B] Flux Chain: 6 series stages ----
        // The feedback written into stage 1 is the value computed one Csound
        // block (64 samples) earlier; read it now, the new value is stored in
        // the same slot at the end of this sample.
        const int fbPos = fluxFbDelayL_.pos;
        const float fbL = fluxFbDelayL_.buf[fbPos];
        const float fbR = fluxFbDelayR_.buf[fbPos];

        float stagesL[6], stagesR[6];
        float xL = sigL + fbL;
        float xR = sigR + fbR;
        for (int s = 0; s < 6; ++s) {
            float lfo = static_cast<float>(triLfo(fluxLfoPhase_[s])) * fluxLfoAmpSamples_[s];
            fluxLfoPhase_[s] = wrapPhase(fluxLfoPhase_[s] + fluxLfoInc_[s]);
            float d = stageTimeSamples_ + lfo;
            float tL = fluxL_[s].readCubic(d);
            float tR = fluxR_[s].readCubic(d);
            fluxL_[s].write(xL + kAntiDenorm);
            fluxR_[s].write(xR + kAntiDenorm);
            stagesL[s] = tL * stageGain_;
            stagesR[s] = tR * stageGain_;
            xL = stagesL[s];
            xR = stagesR[s];
        }

        // feedback path: tilt filter, saturation, dc block, gain
        float f6L = stagesL[5], f6R = stagesR[5];
        if (lpAmount_ > 0.0f) { f6L = fluxLpL_.lp(f6L); f6R = fluxLpR_.lp(f6R); }
        if (hpAmount_ > 0.0f) { f6L = fluxHpL_.hp(f6L); f6R = fluxHpR_.hp(f6R); }
        f6L = std::tanh(f6L * 2.0f) * 0.5f;
        f6R = std::tanh(f6R * 2.0f) * 0.5f;
        f6L = fluxDcL_.process(f6L);
        f6R = fluxDcR_.process(f6R);
        fluxFbDelayL_.buf[fbPos] = f6L * moonFeedback_;
        fluxFbDelayR_.buf[fbPos] = f6R * moonFeedback_;

        // multiply: tap mix with alternating pans
        static const float panL[6] = {0.7f, 0.3f, 0.6f, 0.4f, 0.5f, 0.5f};
        static const float panR[6] = {0.3f, 0.7f, 0.4f, 0.6f, 0.5f, 0.5f};
        float moonL = 0.0f, moonR = 0.0f;
        for (int s = 0; s < 6; ++s) {
            if (multiply_ >= static_cast<float>(s + 1)) {
                moonL += stagesL[s] * panL[s];
                moonR += stagesR[s] * panR[s];
            }
        }
        moonL *= tapGain_;
        moonR *= tapGain_;

        // ---- [C] dual barberpole phaser ----
        float pL1 = phaserL1_.process(moonL);
        float pL2 = phaserL2_.process(moonL);
        float pR1 = phaserR1_.process(moonR);
        float pR2 = phaserR2_.process(moonR);
        float phL = pL1 * xf + pL2 * xfInv;
        float phR = pR1 * xf + pR2 * xfInv;
        moonL = phL * phDepth + moonL * phDryGain;
        moonR = phR * phDepth + moonR * phDryGain;

        // ---- [D] Cosmos FDN ----
        float mInL = moonL * massiveInputGain;
        float mInR = moonR * massiveInputGain;
        if (mode == 1) {
            mInL = diffM1L_[1].process(diffM1L_[0].process(mInL)) * 0.7f;
            mInR = diffM1R_[1].process(diffM1R_[0].process(mInR)) * 0.7f;
        } else if (mode == 2) {
            mInL = diffM2L_[2].process(diffM2L_[1].process(diffM2L_[0].process(mInL))) * 0.5f;
            mInR = diffM2R_[2].process(diffM2R_[1].process(diffM2R_[0].process(mInR))) * 0.5f;
        } else {
            mInL *= 0.7f;
            mInR *= 0.7f;
        }

        float taps[8];
        for (int k = 0; k < 8; ++k) {
            if (freeze) {
                taps[k] = fdn_[k].readInt(fdnDelayInt_[k]);
            } else {
                float lfo = static_cast<float>(std::sin(kTwoPi * fdnLfoPhase_[k])) * fdnModAmpSamples_;
                fdnLfoPhase_[k] = wrapPhase(fdnLfoPhase_[k] + fdnLfoIncPerSample_[k]);
                taps[k] = fdn_[k].readCubic(fdnDelaySamples_[k] + lfo);
            }
        }
        taps[0] += mInL; taps[1] += mInR; taps[2] += mInL; taps[3] += mInR;

        float mixed[8];
        if (freeze) {
            for (int k = 0; k < 8; ++k) mixed[k] = taps[k];
        } else {
            float sum = 0.0f;
            for (int k = 0; k < 8; ++k) sum += taps[k];
            float house = sum * density2over8;
            for (int k = 0; k < 8; ++k) mixed[k] = taps[k] - house;
        }
        if (mode == 3) {
            for (int k = 0; k < 8; k += 2) mixed[k] = fdnModeFilter_[k].lp(mixed[k]);
            for (int k = 1; k < 8; k += 2) mixed[k] = fdnModeFilter_[k].hp(mixed[k]);
        }
        for (int k = 0; k < 8; ++k) {
            float v = mixed[k];
            if (!freeze) {
                v = std::tanh(v * fbGain2) * 0.5f;
                v = fdnDc_[k].process(v);
            }
            fdnState_[k] = v;
            fdn_[k].write(v + kAntiDenorm);
        }
        float massiveL = (fdnState_[0] + fdnState_[2] + fdnState_[4] + fdnState_[6]) * 1.2f;
        float massiveR = (fdnState_[1] + fdnState_[3] + fdnState_[5] + fdnState_[7]) * 1.2f;
        massiveL = outHpL_.hp(outLpL_.lp(massiveL));
        massiveR = outHpR_.hp(outLpR_.lp(massiveR));

        // ---- output: mix, limiter, soft clip, makeup ----
        float oL = sigL * mixDry_ + massiveL * mixWet_;
        float oR = sigR * mixDry_ + massiveR * mixWet_;
        float pk = std::max(std::fabs(oL), std::fabs(oR));
        if (pk > blockPeak_) blockPeak_ = pk;
        oL *= limiterGain_;
        oR *= limiterGain_;
        oL = softClip095(oL) * 1.2f;
        oR = softClip095(oR) * 1.2f;
        outL[i] = oL;
        outR[i] = oR;

        // advance the feedback FIFO
        fluxFbDelayL_.pos = (fbPos + 1) % detail::BlockDelay::kLen;
        fluxFbDelayR_.pos = fluxFbDelayL_.pos;
    }
}

}  // namespace cosmicflux
