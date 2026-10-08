// Self-contained checks for the CosmicFlux C++ core (no external test framework).
//   1. buffer-size independence: identical output for any host block size (1..4096)
//   2. real-time safety: no heap allocation inside process()
//   3. 44.1 kHz and 48 kHz both run, finite, and land at similar levels
//   4. freeze stays bounded and finite over 60 s
//   5. silence in gives silence out
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#include "../dsp/CosmicFluxDSP.h"

namespace {

std::atomic<long> gAllocations{0};

}  // namespace

void* operator new(std::size_t sz) {
    gAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(sz ? sz : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

int gFailures = 0;

void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++gFailures;
}

// Deterministic noise bursts (so delays/feedback have something to chew on)
void makeTestSignal(std::vector<float>& l, std::vector<float>& r, int sr, double seconds) {
    size_t n = size_t(sr * seconds);
    l.assign(n, 0.0f);
    r.assign(n, 0.0f);
    uint32_t s = 0x12345678u;
    for (size_t i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
        float noise = (float(s >> 8) / 8388608.0f - 1.0f) * 0.5f;
        double t = double(i) / sr;
        float gate = std::fmod(t, 0.5) < 0.1 ? 1.0f : 0.0f;  // 100 ms bursts every 500 ms
        float tone = 0.3f * std::sin(2.0 * 3.14159265 * 220.0 * t);
        l[i] = gate * (noise + tone);
        r[i] = gate * (noise * 0.7f - tone);
    }
}

void setBusyParams(cosmicflux::CosmicFluxDSP& d) {
    using namespace cosmicflux;
    d.setParam(pTime, 0.35f); d.setParam(pFeedback, 0.6f); d.setParam(pDimension, 0.5f);
    d.setParam(pMultiply, 0.9f); d.setParam(pCosmosDelay, 0.55f); d.setParam(pCosmosDensity, 0.6f);
    d.setParam(pCosmosFeedback, 0.5f); d.setParam(pMix, 0.6f); d.setParam(pEarlyMod, 0.4f);
    d.setParam(pLateMod, 0.4f); d.setParam(pFeedbackFilter, 0.3f); d.setParam(pCosmosWarp, 0.7f);
    d.setParam(pCosmosMod, 0.5f); d.setParam(pCosmosMode, 0.4f); d.setParam(pFlangerDepth, 0.5f);
    d.setParam(pPhaser, 1.0f); d.setParam(pPhaserSync, 1.0f); d.setParam(pFlangerMode, 0.9f);
}

void renderWithBlocks(cosmicflux::CosmicFluxDSP& d, const std::vector<float>& l, const std::vector<float>& r,
                      std::vector<float>& ol, std::vector<float>& orr, const std::vector<int>& blocks) {
    size_t pos = 0, bi = 0;
    while (pos < l.size()) {
        int n = int(std::min<size_t>(size_t(blocks[bi % blocks.size()]), l.size() - pos));
        d.process(l.data() + pos, r.data() + pos, ol.data() + pos, orr.data() + pos, n);
        pos += size_t(n);
        ++bi;
    }
}

double rmsDb(const std::vector<float>& a, const std::vector<float>& b) {
    double acc = 0;
    for (size_t i = 0; i < a.size(); ++i) acc += double(a[i]) * a[i] + double(b[i]) * b[i];
    return 10.0 * std::log10(acc / double(2 * a.size()) + 1e-30);
}

bool allFinite(const std::vector<float>& a) {
    for (float v : a) if (!std::isfinite(v)) return false;
    return true;
}

}  // namespace

int main() {
    using namespace cosmicflux;

    // ---- 1. block-size independence ----
    {
        const int sr = 48000;
        std::vector<float> l, r;
        makeTestSignal(l, r, sr, 4.0);
        std::vector<std::vector<int>> patterns = {{64}, {1}, {7}, {480}, {4096}, {64, 32, 100, 3, 512, 1}};
        std::vector<std::vector<float>> outs;
        for (auto& pat : patterns) {
            CosmicFluxDSP d;
            d.prepare(sr);
            setBusyParams(d);
            d.reset(true);
            std::vector<float> ol(l.size()), orr(r.size());
            renderWithBlocks(d, l, r, ol, orr, pat);
            ol.insert(ol.end(), orr.begin(), orr.end());
            outs.push_back(std::move(ol));
        }
        double maxDiff = 0;
        for (size_t k = 1; k < outs.size(); ++k)
            for (size_t i = 0; i < outs[0].size(); ++i)
                maxDiff = std::max(maxDiff, double(std::fabs(outs[0][i] - outs[k][i])));
        std::printf("  max difference across block sizes: %.3g\n", maxDiff);
        check(maxDiff == 0.0, "output is bit-identical for block sizes 1, 7, 64, 480, 4096 and mixed");
        check(rmsDb(outs[0], outs[0]) > -60.0, "busy settings produce signal (RMS above -60 dBFS)");
    }

    // ---- 2. no allocation on the audio thread ----
    {
        CosmicFluxDSP d;
        d.prepare(44100.0);
        setBusyParams(d);
        d.reset(true);
        std::vector<float> l, r;
        makeTestSignal(l, r, 44100, 2.0);
        std::vector<float> ol(l.size()), orr(r.size());
        const std::vector<int> mixedBlocks = {64, 128, 256, 1024};
        const std::vector<int> smallBlocks = {64};
        long before = gAllocations.load();
        renderWithBlocks(d, l, r, ol, orr, mixedBlocks);
        for (int i = 0; i < kNumParams; ++i) d.setParam(i, 0.5f);
        d.setParam(pFreeze, 1.0f);
        renderWithBlocks(d, l, r, ol, orr, smallBlocks);
        long after = gAllocations.load();
        std::printf("  heap allocations during process(): %ld\n", after - before);
        check(after == before, "process() and setParam() do not allocate");
    }

    // ---- 3. sample rates ----
    {
        double levels[2];
        int rates[2] = {44100, 48000};
        bool finite = true;
        for (int k = 0; k < 2; ++k) {
            CosmicFluxDSP d;
            d.prepare(rates[k]);
            setBusyParams(d);
            d.reset(true);
            std::vector<float> l, r;
            makeTestSignal(l, r, rates[k], 6.0);
            std::vector<float> ol(l.size()), orr(r.size());
            renderWithBlocks(d, l, r, ol, orr, {256});
            finite = finite && allFinite(ol) && allFinite(orr);
            levels[k] = rmsDb(ol, orr);
        }
        std::printf("  RMS at 44.1 kHz: %.2f dBFS, at 48 kHz: %.2f dBFS\n", levels[0], levels[1]);
        check(finite, "44.1 kHz and 48 kHz renders are finite");
        check(std::fabs(levels[0] - levels[1]) < 1.5, "44.1 kHz and 48 kHz land within 1.5 dB of each other");
    }

    // ---- 4. freeze bounded over 60 s ----
    {
        const int sr = 48000;
        CosmicFluxDSP d;
        d.prepare(sr);
        setBusyParams(d);
        d.setParam(pMix, 1.0f);
        d.reset(true);
        std::vector<float> l, r;
        makeTestSignal(l, r, sr, 3.0);
        std::vector<float> ol(l.size()), orr(r.size());
        renderWithBlocks(d, l, r, ol, orr, {256});
        d.setParam(pFreeze, 1.0f);
        std::vector<float> zl(sr, 0.0f), zr(sr, 0.0f), fl(sr), fr(sr);
        double first = 0, last = 0, peak = 0;
        bool finite = true;
        for (int s = 0; s < 60; ++s) {
            renderWithBlocks(d, zl, zr, fl, fr, {256});
            finite = finite && allFinite(fl) && allFinite(fr);
            double lv = rmsDb(fl, fr);
            if (s == 2) first = lv;
            if (s == 59) last = lv;
            for (float v : fl) peak = std::max(peak, double(std::fabs(v)));
        }
        std::printf("  freeze RMS at 2 s: %.2f dBFS, at 59 s: %.2f dBFS, peak %.3f\n", first, last, peak);
        check(finite && peak <= 1.2, "freeze stays finite and bounded for 60 s");
        check(std::fabs(first - last) < 2.0, "freeze RMS drifts less than 2 dB between 2 s and 59 s");
    }

    // ---- 5. silence ----
    {
        CosmicFluxDSP d;
        d.prepare(48000.0);
        d.reset(true);
        std::vector<float> zl(48000, 0.0f), zr(48000, 0.0f), ol(48000), orr(48000);
        renderWithBlocks(d, zl, zr, ol, orr, {64});
        double peak = 0;
        for (size_t i = 0; i < ol.size(); ++i) peak = std::max(peak, double(std::max(std::fabs(ol[i]), std::fabs(orr[i]))));
        std::printf("  silence peak: %.3g\n", peak);
        check(peak < 1e-6, "silence in gives silence out");
    }

    std::printf("%s (%d failure%s)\n", gFailures == 0 ? "ALL PASSED" : "FAILED", gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
