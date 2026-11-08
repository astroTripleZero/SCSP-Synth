// hotd_bank.hpp - reads the House of the Dead (Sega Model 2, sound driver "v3") sound ROMs into a plain data structure.
// Layout knowledge comes from disassembling the driver (see FM_FINDINGS.md / M2MidiDec v3 patch).
// Input is the program ROM and the two sample ROMs as the 68000 sees them (use Rom::fromStockDumps for the usual word-swapped files).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace hotd {

struct Rom {
    std::vector<uint8_t> prg;   // 0x80000 bytes, 68k order, mapped at 0x600000
    std::vector<uint8_t> smp;   // 8 MB, 68k order, mapped at 0x800000

    // stock MAME-style dumps are byte swapped per 16-bit word
    static void swap16(std::vector<uint8_t>& v) { for (size_t i = 0; i + 1 < v.size(); i += 2) std::swap(v[i], v[i + 1]); }
    bool fromStockDumps(std::vector<uint8_t> epr, std::vector<uint8_t> mpr32, std::vector<uint8_t> mpr34, std::string* err = nullptr) {
        if (epr.size() < 0x10000) { if (err) *err = "program ROM too small"; return false; }
        swap16(epr); swap16(mpr32); swap16(mpr34);
        prg = std::move(epr); prg.resize(0x80000, 0);
        smp = std::move(mpr32); smp.resize(0x400000, 0);
        smp.insert(smp.end(), mpr34.begin(), mpr34.end()); smp.resize(0x800000, 0);
        return true;
    }
};

struct Sample {
    bool     valid = false;
    bool     resident = false;      // 16-bit single-cycle wave copied from the program ROM to RAM at boot
    uint32_t romOffset = 0;         // offset into Rom::smp (PCM) or Rom::prg (resident)
    uint32_t length = 0;            // in samples
    bool     loops = false;
    uint32_t loopStart = 0;         // in samples
};

struct Zone {
    int      minNote = 0, maxNote = 127;
    int      sample = -1;           // index into Bank::samples (PCM first, then resident waves)
    int      rootNote = 60;         // MIDI note at which the sample plays at its native pitch
    int      fineTune = 0;          // 1/16 semitone, signed
    uint16_t adsr[2] = {0, 0};      // SCSP slot registers 0x08 / 0x0A as written by the driver (AR/D1R/D2R/hold, RR/DL/KRS/LPSLNK)
    bool     fixed = false;         // drum key: pitch ignores the note
    uint8_t  disdl = 7, pan = 0, dspSend = 0;   // drums only (melody levels come from the track record)
    // FM entry (18-byte melody entries)
    bool     hasFM = false;
    uint8_t  mdl = 0, mdxsl = 0;    // MDXSL here holds bits 5:2 only (what the driver stores)
    uint8_t  sweepTicks = 0; int8_t sweepStep = 0, sweepAlt = 0;
    uint16_t seed0 = 0, seed1 = 0;
    uint8_t  raw[18] = {0};         // original entry bytes
};

struct Instrument {
    bool drum = false;
    bool empty = true;
    std::vector<Zone> zones;
};

struct Bank {
    std::vector<Sample> samples;
    int pcmCount = 0, waveCount = 0;
    uint32_t waveRomPos = 0, waveRamPos = 0, waveSize = 0, waveTblPos = 0;
    std::vector<Instrument> instruments;
    std::string error;
};

namespace detail {
inline uint16_t be16(const std::vector<uint8_t>& v, size_t p) { return p + 2 <= v.size() ? (uint16_t)(v[p] << 8 | v[p + 1]) : 0; }
inline uint32_t be32(const std::vector<uint8_t>& v, size_t p) { return (uint32_t)be16(v, p) << 16 | be16(v, p + 2); }
inline uint32_t globalPtr(const Rom& r, int id) { return be32(r.prg, 0x8000 + id * 4); }
}

inline bool parse(const Rom& rom, Bank& b)
{
    using namespace detail;
    b = Bank();
    const auto& P = rom.prg;
    if (P.size() < 0x10000) { b.error = "program ROM missing"; return false; }

    // ---- sample table (global pointer 0): word 0, word count-1, then 12-byte records
    const uint32_t base = globalPtr(rom, 0) & 0x7FFFF;
    b.pcmCount = be16(P, base + 2) + 1;
    if (base + 4 + (size_t)b.pcmCount * 12 > P.size()) b.pcmCount = (int)((P.size() - base - 4) / 12);

    // ---- resident waveforms: located through the driver's boot code
    const size_t scanEnd = std::min<size_t>(P.size(), 0x4000);
    for (size_t pos = 0; pos + 0x10 <= scanEnd; pos += 2) {
        if (!b.waveSize && be16(P, pos) == 0x41F9 && be16(P, pos + 2) == 0x0060 && be16(P, pos + 6) == 0x227C &&
            be16(P, pos + 8) == 0x0000 && be16(P, pos + 12) == 0x3E3C) {
            b.waveRomPos = be16(P, pos + 4); b.waveRamPos = be16(P, pos + 10); b.waveSize = ((uint32_t)be16(P, pos + 14) + 1) * 8;
        }
        if (!b.waveTblPos && be16(P, pos) == 0x43F9 && be16(P, pos + 2) == 0x0060 && be16(P, pos + 6) == 0x0241 && be16(P, pos + 8) == 0x03FF)
            b.waveTblPos = be16(P, pos + 4);
    }
    if (b.waveSize && b.waveTblPos && b.waveRomPos + b.waveSize <= P.size()) {
        uint32_t tblEnd = 0x7FFFF;
        for (int id = 0; id < 0x20; id++) {
            uint32_t p = globalPtr(rom, id);
            if ((p & 0xFF0000) != 0x600000) continue;
            p &= 0x7FFFF;
            if (p > b.waveTblPos && p < tblEnd) tblEnd = p;
        }
        if (tblEnd > P.size()) tblEnd = (uint32_t)P.size();
        b.waveCount = std::min<int>((tblEnd - b.waveTblPos) / 12, 0x400);
    } else { b.waveSize = 0; }

    b.samples.resize((size_t)b.pcmCount + b.waveCount);
    for (int i = 0; i < b.pcmCount; i++) {
        Sample& s = b.samples[i]; const size_t rp = base + 4 + (size_t)i * 12;
        uint32_t start = be32(P, rp), len = be32(P, rp + 4), loopSt = be32(P, rp + 8);
        s.loops = (len & 0x80000000u) != 0; len &= 0x7FFFFFFF;
        if ((start & 0x80000000u) || !len) continue;
        start &= 0xFFFFFF; loopSt &= 0xFFFFFF;
        if ((start & 0x7FFFFF) + len > rom.smp.size()) continue;
        s.valid = true; s.romOffset = start & 0x7FFFFF; s.length = len;
        if (s.loops) { if (loopSt < start || loopSt >= start + len) s.loops = false; else s.loopStart = loopSt - start; }
    }
    for (int i = 0; i < b.waveCount; i++) {
        Sample& s = b.samples[(size_t)b.pcmCount + i]; const size_t rp = b.waveTblPos + (size_t)i * 12;
        const uint32_t start = be32(P, rp) & 0xFFFFFF, len = be32(P, rp + 4) & 0x7FFFFFFF;
        if (!len || start < b.waveRamPos || start + len * 2 > b.waveRamPos + b.waveSize) continue;
        s.valid = true; s.resident = true; s.romOffset = b.waveRomPos + (start - b.waveRamPos); s.length = len; s.loops = true;
    }

    auto sampleIndex = [&](uint16_t id) -> int {
        if (id & 0x8000) { id &= 0x3FF; return id < b.waveCount ? b.pcmCount + id : -1; }
        return id < b.pcmCount ? id : -1;
    };

    // ---- instrument table (global pointer 0x0B)
    const uint32_t ib = globalPtr(rom, 0x0B) & 0x7FFFF;
    const int insCnt = be16(P, ib) + 1;
    b.instruments.resize(insCnt);
    for (int n = 0; n < insCnt; n++) {
        Instrument& ins = b.instruments[n];
        const uint16_t ofs = be16(P, ib + 2 + n * 2);
        size_t pos = ib + ofs;
        if ((ofs & 0x8000) || pos + 0x10 > P.size()) continue;
        if (P[pos] == 0xFF) {
            ins.drum = true;
            int minKey, cnt;
            if (P[pos + 1] == 0) { minKey = P[pos + 2]; cnt = P[pos + 3] > minKey ? P[pos + 3] - minKey : 0; }
            else { minKey = 0; cnt = std::min<int>(be16(P, pos + 2) + 1, 0x80); }
            pos += 4;
            for (int k = 0; k < cnt && minKey + k <= 0x7F && pos + 12 <= P.size(); k++, pos += 12) {
                uint16_t sid = be16(P, pos);
                if (sid & 0x8000) continue;
                int si = sampleIndex(sid); if (si < 0) continue;
                Zone z; z.fixed = true; z.sample = si; z.minNote = z.maxNote = minKey + k;
                int8_t on = (int8_t)P[pos + 2]; int diff = (on >> 4) * 12 + (P[pos + 2] & 0x0F);
                z.rootNote = std::max(0, std::min(127, (minKey + k) - diff));
                z.fineTune = (int8_t)P[pos + 3];
                z.disdl = P[pos + 4] >> 5; z.pan = P[pos + 4] & 0x1F; z.dspSend = P[pos + 5];
                z.adsr[0] = be16(P, pos + 6); z.adsr[1] = be16(P, pos + 8);
                for (int i = 0; i < 12; i++) z.raw[i] = P[pos + i];
                ins.zones.push_back(z);
            }
        } else {
            const bool big = (P[pos] & 0x80) != 0;
            const size_t es = big ? 18 : 10;
            if (big) pos += 2;
            int last = 0;
            for (int e = 0; e < 0x80 && pos + es <= P.size(); e++, pos += es) {
                int cur = std::min<int>(P[pos], 0x7F);
                if (cur < last) break;
                int si = sampleIndex(be16(P, pos + 4));
                if (si >= 0) {
                    Zone z; z.sample = si; z.minNote = last; z.maxNote = cur;
                    z.rootNote = std::max(0, std::min(127, 96 - (int8_t)P[pos + 3]));
                    z.fineTune = (int8_t)P[pos + 2];
                    z.adsr[0] = be16(P, pos + 6); z.adsr[1] = be16(P, pos + 8);
                    for (size_t i = 0; i < es; i++) z.raw[i] = P[pos + i];
                    if (big) {
                        z.hasFM = true;
                        z.mdl = P[pos + 0x0A] >> 4; z.mdxsl = P[pos + 0x0A] & 0x0F;
                        z.sweepTicks = P[pos + 0x0B]; z.seed0 = be16(P, pos + 0x0C); z.seed1 = be16(P, pos + 0x0E);
                        z.sweepStep = (int8_t)P[pos + 0x10]; z.sweepAlt = (int8_t)P[pos + 0x11];
                    }
                    ins.zones.push_back(z);
                }
                last = cur + 1;
                if (cur >= 0x7F) break;
            }
        }
        ins.empty = ins.zones.empty();
    }
    return true;
}


// Sound RAM (512 KB, 16-bit words holding the 68000-visible big-endian values) as the driver leaves it after boot / after
// a song's sample set was loaded.  Only the parts that are derived from the ROMs are filled (resident waves, sample table copy,
// instrument table copy, preloaded PCM samples); the 68000 work variables at the bottom of RAM stay zero.
//   sampleSet: index into the driver's per-song preload list (global pointer 8), or -1 for none.
inline std::vector<uint16_t> buildRamImage(const Rom& rom, const Bank& b, int sampleSet, std::vector<int>* loaded = nullptr)
{
    using namespace detail;
    std::vector<uint16_t> ram(0x40000, 0);
    auto putBytes = [&](uint32_t addr, const uint8_t* src, size_t n) {          // copy bytes in 68k order
        for (size_t i = 0; i < n && addr + i < 0x80000; i++) {
            uint16_t& w = ram[(addr + i) >> 1];
            if ((addr + i) & 1) w = (uint16_t)((w & 0xFF00) | src[i]); else w = (uint16_t)((w & 0x00FF) | (src[i] << 8));
        }
    };
    const auto& P = rom.prg;
    if (b.waveSize) putBytes(b.waveRamPos, &P[b.waveRomPos], b.waveSize);
    // sample table: ROM copy lives at RAM 0x3000 (length up to the next table, global pointer 6)
    {
        const uint32_t src = globalPtr(rom, 0) & 0x7FFFF, end = globalPtr(rom, 6) & 0x7FFFF;
        if (end > src && end - src < 0x4000) putBytes(0x3000, &P[src], end - src);
    }
    // instrument table: ROM global 0B (size in global 0C) -> RAM address in global 0E
    {
        const uint32_t src = globalPtr(rom, 0x0B) & 0x7FFFF, len = globalPtr(rom, 0x0C), dst = globalPtr(rom, 0x0E);
        if (len < 0x4000 && dst < 0x80000) putBytes(dst, &P[src], len);
    }
    // preloaded samples, allocated upwards from the free pointer (global 0D)
    if (sampleSet >= 0) {
        const uint32_t tb = globalPtr(rom, 8) & 0x7FFFF;
        const int n = be16(P, tb) + 1;
        if (sampleSet < n) {
            size_t p = tb + be16(P, tb + 2 + sampleSet * 2);
            int cnt = be16(P, p) + 1; p += 2;
            uint32_t dst = globalPtr(rom, 0x0D);
            for (int i = 0; i < cnt; i++, p += 2) {
                const int id = be16(P, p);
                if (id >= b.pcmCount || !b.samples[id].valid) continue;
                const Sample& s = b.samples[id];
                putBytes(dst, &rom.smp[s.romOffset], s.length);
                if (loaded) loaded->push_back(id);
                dst += (s.length + 1) & ~1u;
            }
        }
    }
    return ram;
}

} // namespace hotd
