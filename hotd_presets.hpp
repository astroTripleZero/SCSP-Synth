// hotd_presets.hpp - turns a hotd::Bank instrument into an scsp_synth::Preset and registers its waveforms with the engine.
#pragma once
#include "hotd_bank.hpp"
#include "scsp_synth.hpp"

namespace hotd {

struct Loaded {                      // what the plugin needs to keep per loaded ROM set
    Rom rom; Bank bank;
    int sampleSet = 0;
};

inline bool loadRomSet(Loaded& L, Rom rom, std::string* err = nullptr)
{
    L.rom = std::move(rom);
    if (!parse(L.rom, L.bank)) { if (err) *err = L.bank.error; return false; }
    return true;
}

// Installs the driver RAM image for a sample set into the engine (call before makePreset; invalidates previously made presets).
inline void installRamImage(scsp_synth::Engine& e, const Loaded& L, int sampleSet)
{
    e.clearWaves();
    // sampleSet < 0: only the static parts (resident waves, tables); the engine then owns all RAM above 0x5500 for on-demand sample loading
    e.loadRamImage(buildRamImage(L.rom, L.bank, sampleSet), sampleSet < 0 ? 0x5500 : 0x31000);
}

inline scsp_synth::Preset makePreset(scsp_synth::Engine& e, const Loaded& L, int insIndex)
{
    using namespace scsp_synth;
    Preset pr; char nm[32]; snprintf(nm, sizeof nm, "HOTD %02X%s", insIndex, L.bank.instruments[insIndex].drum ? " (drums)" : "");
    pr.name = nm;
    if (insIndex < 0 || insIndex >= (int)L.bank.instruments.size()) return pr;
    std::vector<int> waveOf(L.bank.samples.size(), -1);
    for (const Zone& z : L.bank.instruments[insIndex].zones) {
        const Sample& s = L.bank.samples[z.sample];
        if (!s.valid) continue;
        if (waveOf[z.sample] < 0) {
            Waveform w; w.name = "smp " + std::to_string(z.sample);
            if (s.resident) {
                w.autoAlloc = false; w.ramByteAddr = L.bank.waveRamPos + (s.romOffset - L.bank.waveRomPos);
                w.pcm8 = false;
            } else {
                w.pcm8 = true; w.pcm8data.assign((const int8_t*)&L.rom.smp[s.romOffset], (const int8_t*)&L.rom.smp[s.romOffset] + s.length);
            }
            w.loopStart = std::min<uint32_t>(s.loops ? s.loopStart : 0, 0xFFFF);
            w.loopEnd = std::min<uint32_t>(s.length, 0xFFFF);
            w.rootKey = z.rootNote;                         // played at increment 1.0 when note == root
            waveOf[z.sample] = e.addWave(w);
        }
        Patch p; p.name = pr.name; p.wave = waveOf[z.sample]; p.fixedPitch = z.fixed;
        p.lpctl = s.loops ? 1 : 0;
        p.tuneCents = z.fineTune * 100.0 / 16.0;
        p.ar = z.adsr[0] & 31; p.egHold = (z.adsr[0] >> 5) & 1; p.d1r = (z.adsr[0] >> 6) & 31; p.d2r = (z.adsr[0] >> 11) & 31;
        p.rr = z.adsr[1] & 31; p.dl = (z.adsr[1] >> 5) & 31; p.krs = (z.adsr[1] >> 10) & 15; p.lpslnk = (z.adsr[1] >> 14) & 1;
        p.disdl = 5;                                    // melody level normally comes from the track record; 5 leaves headroom
        if (z.fixed) { p.disdl = z.disdl; p.dipan = z.pan; p.imxl = z.dspSend & 7; p.isel = 0; }
        if (z.hasFM) {
            p.mdl = z.mdl; p.mdxsl = (uint8_t)(z.mdxsl << 2); p.stwinh = true;
            p.useSeed = true; p.seed0 = z.seed0; p.seed1 = z.seed1; p.sweep = {z.sweepTicks, z.sweepStep, z.sweepAlt};
            // LFO word as observed from the driver: 0x4E10 | (entry byte +1 & 7) << 5
            p.lfore = true; p.lfof = 0x13; p.plfows = 2; p.alfows = 2; p.plfos = z.raw[1] & 7;
        }
        ZonePatch zp; zp.minNote = z.minNote; zp.maxNote = z.maxNote; zp.patch = p;
        pr.zones.push_back(zp);
    }
    return pr;
}

} // namespace hotd
