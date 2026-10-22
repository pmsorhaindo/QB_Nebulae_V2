// Minimal WAV reader/writer for the offline tools (PCM 16/24/32 and float32).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wavio {

struct Audio {
    int sampleRate = 0;
    std::vector<std::vector<float>> channels;  // deinterleaved
    size_t frames() const { return channels.empty() ? 0 : channels[0].size(); }
};

inline uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
inline uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

inline bool read(const std::string& path, Audio& out, std::string& err) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "cannot open " + path; return false; }
    std::vector<uint8_t> data;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data() + 8, "WAVE", 4) != 0) {
        err = "not a RIFF/WAVE file: " + path;
        return false;
    }
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* pcm = nullptr;
    uint32_t pcmBytes = 0;
    size_t pos = 12;
    while (pos + 8 <= data.size()) {
        const uint8_t* ck = data.data() + pos;
        uint32_t sz = rd32(ck + 4);
        const uint8_t* body = ck + 8;
        if (std::memcmp(ck, "fmt ", 4) == 0 && sz >= 16) {
            format = rd16(body);
            channels = rd16(body + 2);
            rate = rd32(body + 4);
            bits = rd16(body + 14);
            if (format == 0xFFFE && sz >= 40) format = rd16(body + 24);  // WAVE_FORMAT_EXTENSIBLE sub-format
        } else if (std::memcmp(ck, "data", 4) == 0) {
            pcm = body;
            pcmBytes = sz;
            if (pos + 8 + pcmBytes > data.size()) pcmBytes = uint32_t(data.size() - pos - 8);
            break;
        }
        pos += 8 + sz + (sz & 1);
    }
    if (!pcm || channels == 0 || rate == 0) { err = "missing fmt/data chunk: " + path; return false; }
    if (!((format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (format == 3 && bits == 32))) {
        err = "unsupported WAV format (need PCM 16/24/32 or float32): " + path;
        return false;
    }
    uint32_t bytesPer = bits / 8;
    size_t frames = pcmBytes / (bytesPer * channels);
    out.sampleRate = int(rate);
    out.channels.assign(channels, std::vector<float>(frames, 0.0f));
    const uint8_t* p = pcm;
    for (size_t i = 0; i < frames; ++i) {
        for (uint16_t c = 0; c < channels; ++c) {
            float v = 0.0f;
            if (format == 3) {
                float fv; std::memcpy(&fv, p, 4); v = fv;
            } else if (bits == 16) {
                v = float(int16_t(rd16(p))) / 32768.0f;
            } else if (bits == 24) {
                int32_t s = int32_t((uint32_t(p[0]) << 8) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 24)) >> 8;
                v = float(s) / 8388608.0f;
            } else {
                v = float(int32_t(rd32(p))) / 2147483648.0f;
            }
            out.channels[c][i] = v;
            p += bytesPer;
        }
    }
    return true;
}

inline void wr32(std::vector<uint8_t>& d, uint32_t v) { d.push_back(v & 0xFF); d.push_back((v >> 8) & 0xFF); d.push_back((v >> 16) & 0xFF); d.push_back((v >> 24) & 0xFF); }
inline void wr16(std::vector<uint8_t>& d, uint16_t v) { d.push_back(v & 0xFF); d.push_back((v >> 8) & 0xFF); }

// Writes 32-bit float WAV when `pcm16` is false, otherwise 16-bit PCM.
inline bool write(const std::string& path, const Audio& audio, bool pcm16, std::string& err) {
    uint16_t channels = uint16_t(audio.channels.size());
    size_t frames = audio.frames();
    uint16_t bits = pcm16 ? 16 : 32;
    uint32_t dataBytes = uint32_t(frames * channels * (bits / 8));
    std::vector<uint8_t> d;
    d.reserve(44 + dataBytes);
    d.insert(d.end(), {'R', 'I', 'F', 'F'});
    wr32(d, 36 + dataBytes);
    d.insert(d.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    wr32(d, 16);
    wr16(d, pcm16 ? 1 : 3);
    wr16(d, channels);
    wr32(d, uint32_t(audio.sampleRate));
    wr32(d, uint32_t(audio.sampleRate) * channels * (bits / 8));
    wr16(d, uint16_t(channels * (bits / 8)));
    wr16(d, bits);
    d.insert(d.end(), {'d', 'a', 't', 'a'});
    wr32(d, dataBytes);
    for (size_t i = 0; i < frames; ++i) {
        for (uint16_t c = 0; c < channels; ++c) {
            float v = audio.channels[c][i];
            if (pcm16) {
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;
                wr16(d, uint16_t(int16_t(v * 32767.0f)));
            } else {
                uint32_t u; std::memcpy(&u, &v, 4); wr32(d, u);
            }
        }
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { err = "cannot write " + path; return false; }
    std::fwrite(d.data(), 1, d.size(), f);
    std::fclose(f);
    return true;
}

}  // namespace wavio
