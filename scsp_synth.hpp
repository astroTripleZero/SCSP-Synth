// scsp_synth.hpp - polyphonic synth engine on top of scsp_core.hpp (GPL-2.0-or-later, derived from Mednafen).
//
// Everything audible is produced by the bit-exact SCSP core.  This layer only
//   * allocates SCSP RAM for waveforms,
//   * turns a Patch (a readable view of one SCSP slot register image) into slot register writes,
//   * allocates the 32 slots as voices, maps MIDI notes to OCT/FNS, applies bend/velocity,
//   * emulates the HOTD driver's "sound stack sweep" (timer B tick) used for the FM-style patches,
//   * resamples the native 44.1 kHz output to the host rate.
#pragma once
#include "scsp_core.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <cstdint>

namespace scsp_synth {

static constexpr double kNativeRate = 44100.0;
static constexpr int    kSlots      = 32;
static constexpr int    kTickSamples = 464;   // SCSP timer B as programmed by the HOTD driver (0x03C6)

struct Waveform {
    std::string name;
    std::vector<int16_t> pcm;     // 16-bit signed samples (used when pcm8 == false)
    std::vector<int8_t>  pcm8data;// 8-bit signed samples (used when pcm8 == true; uploaded as SCSP PCM8B data)
    bool     pcm8 = false;
    uint32_t loopStart = 0;       // in samples
    uint32_t loopEnd   = 0;       // in samples (SCSP LEA; loop is [LSA, LEA))
    double   rootKey   = 69.0;    // MIDI note (fractional) that sounds when the SCSP pitch increment is exactly 1.0
    // helper: a single-cycle wave of N samples sounds at 44100/N Hz at increment 1.0
    void setSingleCycle(uint32_t n) {
        loopStart = 0; loopEnd = n;
        rootKey = 69.0 + 12.0 * std::log2((kNativeRate / n) / 440.0);
    }
    // filled by the engine
    uint32_t ramByteAddr = 0;
    bool     autoAlloc = true;    // engine chooses the RAM address (false: ramByteAddr is fixed by the caller)
    bool     loaded = false;      // engine: data currently resident in sound RAM
    uint64_t lastUse = 0;         // engine: LRU stamp
};

struct Sweep {                     // HOTD driver sweep (ISR at 0x60064C); all-zero = none
    uint8_t ticks = 0;             // ticks per leg (entry +B)
    int8_t  step  = 0;             // added to stack[slot] every tick (entry +10)
    int8_t  alt   = 0;             // 0 = ping-pong (negate every leg), else switch to this step once and stop (entry +11)
};

struct Patch {
    bool     fixedPitch = false;   // drum key (informational; pitch is already fixed by root/note)
    std::string name = "Init";
    int      wave = -1;            // index into the engine's waveform bank
    // word 0
    uint8_t  sbctl = 0, ssctl = 0, lpctl = 1;     // source control, loop mode (0 off, 1 normal, 2 reverse, 3 alternate)
    bool     pcm8 = false;
    // envelope
    uint8_t  ar = 31, d1r = 0, d2r = 0, rr = 31, dl = 0, krs = 0xF;
    bool     egHold = false, lpslnk = false, egBypass = false;
    // level
    uint8_t  tl = 0; bool sdir = false, stwinh = false;
    // modulation (sound stack based FM)
    uint8_t  mdl = 0, mdxsl = 0, mdysl = 0;
    // LFO
    bool     lfore = false; uint8_t lfof = 0, plfows = 0, plfos = 0, alfows = 0, alfos = 0;
    // mixer
    uint8_t  isel = 0, imxl = 0;                  // DSP send
    uint8_t  disdl = 7, dipan = 0, efsdl = 0, efpan = 0;
    // sound stack seeds and sweep (HOTD style FM voices)
    bool     useSeed = false; uint16_t seed0 = 0, seed1 = 0; Sweep sweep;
    // performance
    double   tuneCents = 0.0;
    double   velSens = 1.0;                        // 0 = fixed level, 1 = dB-proportional
    // 1 = derive nothing, voice plays the waveform as is
    uint16_t word(int i, uint32_t sa, uint32_t lsa, uint32_t lea, uint16_t tlExtra, int oct, int fns) const {
        switch (i) {
        case 0: return (uint16_t)((sbctl & 3) << 9 | (ssctl & 3) << 7 | (lpctl & 3) << 5 | (pcm8 ? 0x10 : 0) | ((sa >> 16) & 0xF));
        case 1: return (uint16_t)(sa & 0xFFFF);
        case 2: return (uint16_t)lsa;
        case 3: return (uint16_t)lea;
        case 4: return (uint16_t)((d2r & 31) << 11 | (d1r & 31) << 6 | (egHold ? 0x20 : 0) | (ar & 31));
        case 5: return (uint16_t)((egBypass ? 0x8000 : 0) | (lpslnk ? 0x4000 : 0) | (krs & 15) << 10 | (dl & 31) << 5 | (rr & 31));
        case 6: return (uint16_t)((stwinh ? 0x200 : 0) | (sdir ? 0x100 : 0) | (tlExtra & 0xFF));
        case 7: return (uint16_t)((mdl & 15) << 12 | (mdxsl & 63) << 6 | (mdysl & 63));
        case 8: return (uint16_t)((oct & 15) << 11 | (fns & 0x3FF));
        case 9: return (uint16_t)((lfore ? 0x8000 : 0) | (lfof & 31) << 10 | (plfows & 3) << 8 | (plfos & 7) << 5 | (alfows & 3) << 3 | (alfos & 7));
        case 10: return (uint16_t)((isel & 15) << 3 | (imxl & 7));
        default: return (uint16_t)((disdl & 7) << 13 | (dipan & 31) << 8 | (efsdl & 7) << 5 | (efpan & 31));
        }
    }
};

struct ZonePatch { int minNote = 0, maxNote = 127; Patch patch; };
struct Preset { std::string name; std::vector<ZonePatch> zones; };

class Engine {
public:
    Engine() { reset(); }

    void reset() {
        scsp.Reset(true);
        std::fill(vox, vox + kSlots, Voice());
        counter = 0; tickCnt = 0; serial = 0;
        cw(0x400, 0x030F);        // MVOL max, DAC18 + 4Mbit like the HOTD driver (0x0300) with volume up
        for (auto& w : waves) { if (w.autoAlloc) w.loaded = false; else uploadWave(w); }
    }

    // ---- waveform bank (stored in SCSP RAM from 0x1000 upwards) -------------------------------------------------
    int addWave(Waveform w) {
        waves.push_back(std::move(w));
        if (!waves.back().autoAlloc) { uploadWave(waves.back()); waves.back().loaded = true; }   // fixed address: data goes in now
        return (int)waves.size() - 1;
    }
    std::vector<Waveform>& bank() { return waves; }
    // Replace sound RAM with a driver RAM image (512 KB as 0x40000 big-endian-valued words). Own waveforms are placed after 'freeFrom'.
    void loadRamImage(const std::vector<uint16_t>& img, uint32_t freeFrom = 0x31000) {
        allNotesOff();
        uint16_t* ram = scsp.GetRAMPtr();
        for (size_t i = 0; i < 0x40000; i++) ram[i] = i < img.size() ? img[i] : 0;
        nextRam = freeFrom; imageFree = freeFrom;
        for (auto& w : waves) if (w.autoAlloc) { w.ramByteAddr = 0; w.loaded = false; }
    }
    // Drop uploaded (non-fixed) waveforms and reuse the free area (call when no notes sound; done automatically when RAM is full).
    void clearWaves() { allNotesOff(); waves.clear(); }

    // ---- performance --------------------------------------------------------------------------------------------
    void setPatch(int ch, const Patch& p) { if (ch >= 0 && ch < 16) { chPreset[ch].zones.assign(1, ZonePatch{0, 127, p}); chPreset[ch].name = p.name; } }
    void setPreset(int ch, const Preset& p) { if (ch >= 0 && ch < 16) chPreset[ch] = p; }
    void setBendRange(double semis) { bendRange = semis; }
    void pitchBend(int ch, double norm /* -1..1 */) { bend[ch & 15] = norm; refreshPitch(ch & 15); }
    void sustain(int ch, bool on) {
        sus[ch & 15] = on;
        if (!on) for (auto& v : vox) if (v.active && v.ch == (ch & 15) && v.held && !v.down) { release(v); }
    }

    void noteOn(int ch, int note, int vel) { noteOnF(ch, note, vel, (double)note); }
    // 'exact' = fractional MIDI note actually used for pitch (note stays the key id for noteOff)
    void noteOnF(int ch, int note, int vel, double exact) {
        if (vel == 0) { noteOff(ch, note); return; }
        ch &= 15;
        const ZonePatch* zp = nullptr;
        for (const auto& z : chPreset[ch].zones) if (note >= z.minNote && note <= z.maxNote) { zp = &z; break; }
        if (!zp) return;
        Patch p = zp->patch;
        if (p.wave < 0 || p.wave >= (int)waves.size()) return;
        int s = allocSlot();
        Voice& v = vox[s];
        v = Voice(); v.active = true; v.down = true; v.ch = ch; v.note = note; v.exact = exact; v.vel = vel; v.serial = ++serial;
        const Waveform& w = waves[p.wave];
        p.pcm8 = w.pcm8; v.patch = p;

        // level: TL carries the patch level plus velocity attenuation (0.375 dB / step)
        double att = 0.0;
        if (p.velSens > 0.0) att = -20.0 * std::log10(std::max(1, vel) / 127.0) * p.velSens;
        int tl = std::min(255, p.tl + (int)std::lround(att / 0.375));
        v.tlTotal = tl;

        calcPitch(v, v.oct, v.fns);
        ensureLoaded(p.wave);
        const uint32_t sa = waves[p.wave].ramByteAddr; v.sa = sa;
        // words 1..11 first, word 0 (with key bits) last, as the driver does
        for (int i = 1; i < 12; i++) cw(slotReg(s, i), p.word(i, sa, w.loopStart, w.loopEnd, (uint16_t)tl, v.oct, v.fns));
        if (p.useSeed) {
            cw(0x600 + 2 * s, p.seed0);
            cw(0x600 + 2 * (s + 32), p.seed1);
            v.sweepOn = p.sweep.ticks != 0;
            v.cnt = p.sweep.ticks; v.ticks = p.sweep.ticks; v.step = p.sweep.step; v.alt = p.sweep.alt;
        }
        if (p.lfore) cw(slotReg(s, 9), (uint16_t)(p.word(9, 0, 0, 0, 0, 0, 0) & 0x7FFF));   // LFO runs from key-on (driver: reset held, then released)
        cw(slotReg(s, 0), (uint16_t)(p.word(0, sa, 0, 0, 0, 0, 0) | 0x1800));   // KYONEX | KYONB
    }

    void noteOff(int ch, int note) {
        ch &= 15;
        for (auto& v : vox)
            if (v.active && v.down && v.ch == ch && v.note == note) {
                v.down = false;
                if (sus[ch]) v.held = true; else release(v);
            }
    }
    int forceSlot = -1;                                    // test helper: always use this slot
    bool autoTick = true;                                  // false: caller drives sweepTick() (used by the validation test)
    void forceTick() { sweepTick(); }
    void setTickPhase(int samplesUntilNextTick) { tickCnt = kTickSamples - samplesUntilNextTick; }   // test/sync helper
    void allNotesOff() { for (auto& v : vox) if (v.active) { v.down = false; release(v); } }

    // ---- audio --------------------------------------------------------------------------------------------------
    // one native frame (44.1 kHz), int16 range
    void nativeFrame(float& l, float& r) {
        if (autoTick && ++tickCnt >= kTickSamples) { tickCnt = 0; sweepTick(); }
        int16_t o[2];
        scsp.RunSample(o);
        l = o[0] / 32768.0f; r = o[1] / 32768.0f;
        // retire finished voices (EG reached silence is not visible from here; release timeout is by slot monitor)
        if ((++counter & 0xFF) == 0) retire();
    }
    // host-rate rendering with cubic Hermite resampling
    void render(float* L, float* R, int n, double hostRate) {
        const double step = kNativeRate / hostRate;
        for (int i = 0; i < n; i++) {
            while (phase >= 1.0) { shift(); phase -= 1.0; }
            double t = phase;
            L[i] = (float)herm(hl, t); R[i] = (float)herm(hr, t);
            phase += step;
        }
    }

    scsp_core::SS_SCSP scsp;
    // direct register access for advanced users / UI
    void writeWord(uint32_t byteAddr, uint16_t v) { cw(byteAddr, v); }

private:
    struct Voice {
        bool active = false, down = false, held = false, sweepOn = false, released = false;
        double exact = 0; uint32_t sa = 0; int ch = 0, note = 0, vel = 0, oct = 0, fns = 0, tlTotal = 0; uint32_t serial = 0;
        uint8_t cnt = 0, ticks = 0; int8_t step = 0, alt = 0;
        Patch patch;
        uint32_t relCount = 0;
    };

    void cw(uint32_t byteAddr, uint16_t v) { scsp.RW<uint16_t, true>(0x100000 | byteAddr, v); }
    static uint32_t slotReg(int slot, int word) { return (uint32_t)slot * 0x20 + (uint32_t)word * 2; }

    uint32_t waveBytes(const Waveform& w) const { return w.pcm8 ? (uint32_t)w.pcm8data.size() : (uint32_t)w.pcm.size() * 2; }
    void uploadWave(Waveform& w) {
        if (w.ramByteAddr + waveBytes(w) > 0x80000) return;
        uint16_t* ram = scsp.GetRAMPtr();
        if (w.pcm8) {
            for (size_t i = 0; i < w.pcm8data.size(); i++) {
                uint16_t& wd = ram[(w.ramByteAddr + i) >> 1]; const uint8_t b = (uint8_t)w.pcm8data[i];
                if ((w.ramByteAddr + i) & 1) wd = (uint16_t)((wd & 0xFF00) | b); else wd = (uint16_t)((wd & 0x00FF) | (b << 8));
            }
        } else {
            const uint32_t base = w.ramByteAddr >> 1;
            for (size_t i = 0; i < w.pcm.size(); i++) ram[base + i] = (uint16_t)w.pcm[i];
        }
    }
    bool waveInUse(int idx) const { for (const auto& v : vox) if (v.active && v.patch.wave == idx) return true; return false; }
    // first-fit gap search among resident auto-allocated waves
    bool findGap(uint32_t need, uint32_t& addr) const {
        std::vector<std::pair<uint32_t, uint32_t>> used;
        for (const auto& w : waves) if (w.autoAlloc && w.loaded) used.push_back({w.ramByteAddr, w.ramByteAddr + waveBytes(w) + 64});
        std::sort(used.begin(), used.end());
        uint32_t cur = (imageFree + 1) & ~1u;
        for (auto& r : used) { if (r.first >= cur + need) break; cur = std::max(cur, (r.second + 1) & ~1u); }
        if (cur + need > 0x80000) return false;
        addr = cur; return true;
    }
    void ensureLoaded(int idx) {
        Waveform& w = waves[idx];
        w.lastUse = ++useStamp;
        if (w.loaded || !w.autoAlloc) return;
        const uint32_t need = waveBytes(w) + 64; uint32_t addr = 0;
        while (!findGap(need, addr)) {
            int victim = -1; uint64_t best = ~0ull;
            for (int i = 0; i < (int)waves.size(); i++) if (waves[i].autoAlloc && waves[i].loaded && i != idx && !waveInUse(i) && waves[i].lastUse < best) { best = waves[i].lastUse; victim = i; }
            if (victim < 0) { w.ramByteAddr = 0x7FFFE; return; }   // cannot fit: plays silence
            waves[victim].loaded = false;
        }
        w.ramByteAddr = addr; uploadWave(w); w.loaded = true;
    }

    int allocSlot() {
        int best = -1;
        if (forceSlot >= 0) return forceSlot;
        for (int i = 0; i < kSlots; i++) if (!vox[i].active) return i;
        uint32_t oldest = 0xFFFFFFFF;
        for (int i = 0; i < kSlots; i++) if (!vox[i].down && vox[i].released && vox[i].serial < oldest) { oldest = vox[i].serial; best = i; }
        if (best < 0) for (int i = 0; i < kSlots; i++) if (vox[i].serial < oldest) { oldest = vox[i].serial; best = i; }
        if (best >= 0) { cw(slotReg(best, 0), 0x1000); }   // key off, clean steal
        return best;
    }

    void release(Voice& v) {
        int s = (int)(&v - vox);
        v.released = true; v.held = false;
        cw(slotReg(s, 0), (uint16_t)((v.patch.word(0, v.sa, 0, 0, 0, 0, 0) & ~0x0800) | 0x1000));   // KYONEX, KYONB=0
        v.relCount = 0;
    }

    void retire() {   // free slots whose release finished: poll the slot monitor (EG level)
        for (int s = 0; s < kSlots; s++) {
            Voice& v = vox[s];
            if (!v.active || !v.released) continue;
            v.relCount += 256;
            if (v.relCount > 44100u * 12u) { v.active = false; v.sweepOn = false; }
        }
    }

    void calcPitch(const Voice& v, int& oct, int& fns) const {
        const Waveform& w = waves[v.patch.wave];
        double semis = (v.exact - w.rootKey) + v.patch.tuneCents / 100.0 + bend[v.ch] * bendRange;
        double lg = semis / 12.0;                  // log2(increment)
        int o = (int)std::floor(lg);
        o = std::max(-8, std::min(7, o));
        double frac = std::exp2(lg - o) - 1.0;       // 0..1 normally
        int f = (int)std::lround(frac * 1024.0);
        if (f > 1023) f = 1023; if (f < 0) f = 0;
        oct = o; fns = f;
    }
    void refreshPitch(int ch) {
        for (int s = 0; s < kSlots; s++) {
            Voice& v = vox[s];
            if (!v.active || v.ch != ch) continue;
            calcPitch(v, v.oct, v.fns);
            cw(slotReg(s, 8), v.patch.word(8, 0, 0, 0, 0, v.oct, v.fns));
        }
    }

    void sweepTick() {            // identical to the 68k routine at 0x60064C..0x6006CE
        for (int s = 0; s < kSlots; s++) {
            Voice& v = vox[s];
            if (!v.active || !v.sweepOn) continue;
            if (--v.cnt == 0) {
                if (v.ticks == 0) { v.sweepOn = false; continue; }
                v.cnt = v.ticks;
                if (v.alt) { v.step = v.alt; v.ticks = 0; }
                else v.step = (int8_t)(-(int)v.step);
            }
            uint16_t cur;
            { uint16_t t = 0; scsp.RW<uint16_t, false>(0x100000 | (0x600 + 2 * s), t); cur = t; }
            cw(0x600 + 2 * s, (uint16_t)(cur + (int16_t)v.step));
        }
    }

    // resampler state
    float hl[4] = {0, 0, 0, 0}, hr[4] = {0, 0, 0, 0};
    double phase = 1.0;
    void shift() {
        for (int i = 0; i < 3; i++) { hl[i] = hl[i + 1]; hr[i] = hr[i + 1]; }
        nativeFrame(hl[3], hr[3]);
    }
    static double herm(const float* y, double t) {
        const double c0 = y[1], c1 = 0.5 * (y[2] - y[0]);
        const double c2 = y[0] - 2.5 * y[1] + 2.0 * y[2] - 0.5 * y[3];
        const double c3 = 0.5 * (y[3] - y[0]) + 1.5 * (y[1] - y[2]);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    std::vector<Waveform> waves;
    uint32_t nextRam = 0x1000, imageFree = 0x1000; uint64_t useStamp = 0;
    Voice vox[kSlots];
    Preset chPreset[16];
    double bend[16] = {0}; double bendRange = 2.0; bool sus[16] = {false};
    uint32_t counter = 0, serial = 0; int tickCnt = 0;
};

} // namespace scsp_synth
