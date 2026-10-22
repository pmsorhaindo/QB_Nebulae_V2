// Offline renderer for the CosmicFlux C++ core.
//
// Mirrors the Csound dev harness (Instruments/CosmicFlux/dev/render.py):
//   --set   channel=value          static control (Nebulae channel names)
//   --gate  channel=t1[,t2,...]    first time sets the control to 1, later times toggle
//   --ramp  channel=start,end[,t0,t1]  linear ramp (t0/t1 default 0 and duration)
//   --duration SEC                 output length (input is zero padded / truncated)
//   --block N                      host buffer size to emulate (default 64)
//   --csound-init                  start smoothers from zero like Csound portk
//   --pcm16                        write 16-bit PCM instead of float32
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../dsp/CosmicFluxDSP.h"
#include "../dsp/CosmicFluxParams.h"
#include "WavIO.h"

namespace {

struct GateEvent { int param; double time; bool first; };
struct Ramp { int param; float start, end; double t0, t1; };

int paramFromName(const std::string& name) {
    int id = cosmicflux::paramIdForNebulaeChannel(name.c_str());
    if (id < 0) {
        for (int i = 0; i < cosmicflux::kNumParams; ++i) {
            if (name == cosmicflux::paramInfo(i).identifier) return i;
        }
    }
    return id;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t p = s.find(sep, start);
        out.push_back(s.substr(start, p == std::string::npos ? std::string::npos : p - start));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return out;
}

void usage() {
    std::fprintf(stderr,
                 "usage: cosmicflux_render --in in.wav --out out.wav [--duration S] [--block N]\n"
                 "       [--csound-init] [--pcm16] [--set ch=v]... [--gate ch=t,...]... [--ramp ch=a,b[,t0,t1]]...\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::string inPath, outPath;
    double duration = -1.0;
    int block = 64;
    bool csoundInit = false, pcm16 = false;
    std::vector<std::pair<int, float>> statics;
    std::vector<GateEvent> gates;
    std::vector<Ramp> ramps;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string& dst) {
            if (i + 1 >= argc) { usage(); std::exit(2); }
            dst = argv[++i];
        };
        std::string v;
        if (a == "--in") next(inPath);
        else if (a == "--out") next(outPath);
        else if (a == "--duration") { next(v); duration = std::atof(v.c_str()); }
        else if (a == "--block") { next(v); block = std::atoi(v.c_str()); }
        else if (a == "--csound-init") csoundInit = true;
        else if (a == "--pcm16") pcm16 = true;
        else if (a == "--set" || a == "--gate" || a == "--ramp") {
            next(v);
            size_t eq = v.find('=');
            if (eq == std::string::npos) { usage(); return 2; }
            std::string name = v.substr(0, eq), rest = v.substr(eq + 1);
            int id = paramFromName(name);
            if (id < 0) { std::fprintf(stderr, "unknown control: %s\n", name.c_str()); return 2; }
            if (a == "--set") {
                statics.emplace_back(id, float(std::atof(rest.c_str())));
            } else if (a == "--gate") {
                bool first = true;
                for (const auto& t : split(rest, ',')) {
                    gates.push_back({id, std::atof(t.c_str()), first});
                    first = false;
                }
            } else {
                auto parts = split(rest, ',');
                if (parts.size() < 2) { usage(); return 2; }
                Ramp r{id, float(std::atof(parts[0].c_str())), float(std::atof(parts[1].c_str())), 0.0, -1.0};
                if (parts.size() >= 4) { r.t0 = std::atof(parts[2].c_str()); r.t1 = std::atof(parts[3].c_str()); }
                ramps.push_back(r);
            }
        } else { usage(); return 2; }
    }
    if (inPath.empty() || outPath.empty() || block < 1) { usage(); return 2; }

    wavio::Audio in;
    std::string err;
    if (!wavio::read(inPath, in, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    const int sr = in.sampleRate;
    size_t frames = in.frames();
    if (duration > 0) frames = size_t(duration * sr + 0.5);
    std::vector<float> inL(frames, 0.0f), inR(frames, 0.0f);
    for (size_t i = 0; i < frames && i < in.frames(); ++i) {
        inL[i] = in.channels[0][i];
        inR[i] = in.channels.size() > 1 ? in.channels[1][i] : in.channels[0][i];
    }
    std::vector<float> outL(frames, 0.0f), outR(frames, 0.0f);

    cosmicflux::CosmicFluxDSP dsp;
    dsp.prepare(double(sr));
    for (auto& s : statics) dsp.setParam(s.first, s.second);
    for (auto& r : ramps) {
        if (r.t1 < 0) r.t1 = double(frames) / sr;
        dsp.setParam(r.param, r.start);
    }
    dsp.reset(!csoundInit);

    size_t pos = 0;
    while (pos < frames) {
        double t = double(pos) / sr;
        for (auto& g : gates) {
            if (g.time >= t && g.time < t + double(block) / sr) {
                dsp.setParam(g.param, g.first ? 1.0f : (dsp.getParam(g.param) > 0.5f ? 0.0f : 1.0f));
            }
        }
        for (auto& r : ramps) {
            double len = r.t1 - r.t0;
            float v = len > 0 ? float(r.start + (r.end - r.start) * std::min(1.0, t / len)) : r.end;
            dsp.setParam(r.param, v);
        }
        int n = int(std::min<size_t>(size_t(block), frames - pos));
        dsp.process(inL.data() + pos, inR.data() + pos, outL.data() + pos, outR.data() + pos, n);
        pos += size_t(n);
    }

    wavio::Audio out;
    out.sampleRate = sr;
    out.channels = {outL, outR};
    if (!wavio::write(outPath, out, pcm16, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    std::printf("rendered %zu frames at %d Hz -> %s\n", frames, sr, outPath.c_str());
    return 0;
}
