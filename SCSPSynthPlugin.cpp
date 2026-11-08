// SCSP Synth - DPF plugin. GPL-2.0-or-later (contains Mednafen-derived SCSP code).
#include "DistrhoPlugin.hpp"
#include "hotd_presets.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <random>

START_NAMESPACE_DISTRHO

namespace {

std::vector<uint8_t> readFile(const std::string& fn)
{
    std::ifstream f(fn, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string dirOf(const std::string& p)
{
    const size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string(".") : p.substr(0, s);
}

// Finds the three HOTD ROM files next to the chosen file (any of the usual file names).
bool loadHotdFromFolder(const std::string& anyFile, hotd::Loaded& out, std::string& err)
{
    std::string dir = dirOf(anyFile);
    // the user may also have picked the folder itself
    auto tryNames = [&](std::initializer_list<const char*> names) {
        for (const char* n : names) { auto d = readFile(dir + "/" + n); if (!d.empty()) return d; }
        return std::vector<uint8_t>();
    };
    auto epr = tryNames({"epr-19720_1.31", "epr-19720.31", "epr-19720"});
    auto m32 = tryNames({"mpr-19721_1.32", "mpr-19721.32", "mpr-19721"});
    auto m34 = tryNames({"mpr-19722_1.34", "mpr-19722.34", "mpr-19722"});
    if (epr.empty() || m32.empty() || m34.empty()) { err = "need epr-19720, mpr-19721 and mpr-19722 in " + dir; return false; }
    hotd::Rom rom;
    if (!rom.fromStockDumps(std::move(epr), std::move(m32), std::move(m34), &err)) return false;
    return hotd::loadRomSet(out, std::move(rom), &err);
}

std::string configPath()
{
#ifdef _WIN32
    const char* a = std::getenv("APPDATA"); return std::string(a ? a : ".") + "\\SCSPSynth\\config.txt";
#else
    const char* x = std::getenv("XDG_CONFIG_HOME"); const char* h = std::getenv("HOME");
    return (x ? std::string(x) : std::string(h ? h : ".") + "/.config") + "/SCSPSynth/config.txt";
#endif
}

} // namespace

class SCSPSynthPlugin : public Plugin
{
public:
    SCSPSynthPlugin() : Plugin(kParamCount, kProgramCount, kStateCount)
    {
        defaults();
        engine.reset(new scsp_synth::Engine());
        buildInit();
        // optional config file: rom=<path to any HOTD ROM file>, ram=<raw 512 KB SCSP RAM image>, program=<n>
        std::ifstream cfg(configPath());
        std::string ln; int startProgram = 0;
        while (std::getline(cfg, ln)) {
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
            if (ln.rfind("rom=", 0) == 0) romPath = ln.substr(4);
            else if (ln.rfind("ram=", 0) == 0) ramPath = ln.substr(4);
            else if (ln.rfind("program=", 0) == 0) startProgram = std::atoi(ln.c_str() + 8);
        }
        if (romPath.empty()) autoFindRoms();
        reloadSources();
        loadProgram((uint32_t) startProgram);
    }

protected:
    const char* getLabel() const override { return "SCSPSynth"; }
    const char* getDescription() const override { return "Sega SCSP (Saturn/Model 2) sound chip synthesizer built on a bit-exact SCSP core, with House of the Dead instrument loading from your own ROMs."; }
    const char* getMaker() const override { return "SCSPSynth"; }
    const char* getHomePage() const override { return "https://example.invalid/scspsynth"; }
    const char* getLicense() const override { return "GPL-2.0-or-later"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('S', 'c', 'S', '1'); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t i, Parameter& p) override
    {
        p.hints = kParameterIsAutomatable | kParameterIsInteger;
        auto set = [&](const char* name, const char* sym, float def, float lo, float hi, const char* unit = "", bool integer = true) {
            p.name = name; p.symbol = sym; p.unit = unit; p.ranges.def = def; p.ranges.min = lo; p.ranges.max = hi;
            if (!integer) p.hints &= ~kParameterIsInteger;
        };
        switch (i) {
        case kParamGain:      set("Gain", "gain", 0.8f, 0.f, 1.f, "", false); break;
        case kParamWave:      set("Waveform (0 Sine 1 Saw 2 Square 3 Tri 4 Noise 5 RAM window)", "wave", 1, 0, 5); break;
        case kParamLoop:      set("Loop (0 off 1 fwd 2 rev 3 alt)", "loop", 1, 0, 3); break;
        case kParamBend:      set("Bend Range", "bend", 2, 0, 24, "st"); break;
        case kParamTune:      set("Tune", "tune", 0, -100.f, 100.f, "ct", false); break;
        case kParamAR:        set("AR", "ar", 31, 0, 31); break;
        case kParamD1R:       set("D1R", "d1r", 0, 0, 31); break;
        case kParamDL:        set("DL", "dl", 0, 0, 31); break;
        case kParamD2R:       set("D2R", "d2r", 0, 0, 31); break;
        case kParamRR:        set("RR", "rr", 14, 0, 31); break;
        case kParamKRS:       set("KRS", "krs", 15, 0, 15); break;
        case kParamTL:        set("TL", "tl", 0, 0, 255); break;
        case kParamVelSens:   set("Vel Sens", "vel", 1.f, 0.f, 1.f, "", false); break;
        case kParamDISDL:     set("Direct Level", "disdl", 5, 0, 7); break;
        case kParamPan:       set("Pan", "pan", 0, -15, 15); break;
        case kParamMDL:       set("Mod Level (MDL)", "mdl", 0, 0, 15); break;
        case kParamMDXSL:     set("Mod X Select", "mdxsl", 0, 0, 63); break;
        case kParamMDYSL:     set("Mod Y Select", "mdysl", 0, 0, 63); break;
        case kParamSeedA:     set("Stack Seed A", "seeda", 0, 0, 65535); break;
        case kParamSeedB:     set("Stack Seed B", "seedb", 0, 0, 65535); break;
        case kParamStWInh:    set("Stack Write Inhibit", "stwinh", 0, 0, 1); p.hints |= kParameterIsBoolean; break;
        case kParamSwTicks:   set("Sweep Ticks", "swticks", 0, 0, 255); break;
        case kParamSwStep:    set("Sweep Step", "swstep", 0, -128, 127); break;
        case kParamLFOF:      set("LFO Freq", "lfof", 0, 0, 31); break;
        case kParamPLFOWS:    set("Pitch LFO Wave (0 saw 1 sq 2 tri 3 noise)", "plfows", 0, 0, 3); break;
        case kParamPLFOS:     set("Pitch LFO Depth", "plfos", 0, 0, 7); break;
        case kParamALFOWS:    set("Amp LFO Wave (0 saw 1 sq 2 tri 3 noise)", "alfows", 0, 0, 3); break;
        case kParamALFOS:     set("Amp LFO Depth", "alfos", 0, 0, 7); break;
        case kParamRamAddr:   set("RAM Window Address (bytes)", "ramaddr", 0x28C0, 0, 0x7FFFE, "", true); break;
        case kParamRamLen:    set("RAM Window Length (samples)", "ramlen", 64, 1, 65535); break;
        case kParamRamLoopStart: set("RAM Window Loop Start", "ramls", 0, 0, 65534); break;
        case kParamRam8Bit:   set("RAM Window 8-bit", "ram8", 0, 0, 1); p.hints |= kParameterIsBoolean; break;
        case kParamRamRoot:   set("RAM Window Root Note", "ramroot", 65, 0, 127); break;
        case kParamRomLoaded: set("ROM Loaded", "romloaded", 0, 0, 1); p.hints = kParameterIsOutput | kParameterIsInteger | kParameterIsBoolean; break;
        }
    }

    void initProgramName(uint32_t i, String& name) override
    {
        if (i == 0) { name = "Init"; return; }
        char b[48]; std::snprintf(b, sizeof b, "HOTD Instrument %02X", i - 1); name = b;
    }

    void initState(uint32_t i, State& s) override
    {
        s.hints = kStateIsFilenamePath;
        if (i == kStateRom) { s.key = "rom"; s.label = "HOTD ROM (pick any one of the three files)"; }
        else                { s.key = "ram"; s.label = "SCSP RAM image (raw 512 KB)"; }
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, "rom") == 0) return String(romPath.c_str());
        if (std::strcmp(key, "ram") == 0) return String(ramPath.c_str());
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "rom") == 0) romPath = value;
        else if (std::strcmp(key, "ram") == 0) ramPath = value;
        else return;
        reloadSources();
        loadProgram((uint32_t) program);
    }

    float getParameterValue(uint32_t i) const override
    {
        if (i == kParamRomLoaded) return hotdOk ? 1.f : 0.f;
        return param[i];
    }

    void setParameterValue(uint32_t i, float v) override
    {
        if (i < kParamCount && i != kParamRomLoaded) { param[i] = v; dirty = true; }
    }

    void loadProgram(uint32_t i) override
    {
        std::lock_guard<std::mutex> g(mtx);
        program = (int) i;
        if (i >= 1 && hotdOk && (int) (i - 1) < (int) hotd.bank.instruments.size()) {
            installBaseImage(false);
            auto pr = hotd::makePreset(*engine, hotd, (int) i - 1);
            for (int ch = 0; ch < 16; ch++) engine->setPreset(ch, pr);
            hotdActive = true;
        } else {
            buildInit();
            hotdActive = false;
        }
        dirty = true;
    }

    void sampleRateChanged(double sr) override { (void) sr; }

    void run(const float**, float** out, uint32_t frames, const MidiEvent* ev, uint32_t evCount) override
    {
        std::unique_lock<std::mutex> lk(mtx, std::try_to_lock);
        if (!lk.owns_lock()) { std::memset(out[0], 0, frames * sizeof(float)); std::memset(out[1], 0, frames * sizeof(float)); return; }
        if (dirty.exchange(false)) applyParams();

        const double sr = getSampleRate();
        l.resize(frames); r.resize(frames);
        uint32_t pos = 0;
        auto renderTo = [&](uint32_t end) {
            if (end > frames) end = frames;
            if (end > pos) { engine->render(l.data() + pos, r.data() + pos, (int) (end - pos), sr); pos = end; }
        };
        for (uint32_t k = 0; k < evCount; k++) {
            renderTo(ev[k].frame);
            const uint8_t st = ev[k].data[0] & 0xF0, ch = ev[k].data[0] & 0x0F;
            if (ev[k].size < 2) continue;
            const int d1 = ev[k].data[1], d2 = ev[k].size > 2 ? ev[k].data[2] : 0;
            if (st == 0x90 && d2 > 0) engine->noteOn(ch, d1, d2);
            else if (st == 0x80 || (st == 0x90 && d2 == 0)) engine->noteOff(ch, d1);
            else if (st == 0xE0) engine->pitchBend(ch, (((d2 << 7) | d1) - 8192) / 8192.0);
            else if (st == 0xB0) {
                if (d1 == 64) engine->sustain(ch, d2 >= 64);
                else if (d1 == 123 || d1 == 120) engine->allNotesOff();
            }
        }
        renderTo(frames);
        const float g = param[kParamGain];
        for (uint32_t i = 0; i < frames; i++) { out[0][i] = l[i] * g; out[1][i] = r[i] * g; }
    }

private:
    void defaults()
    {
        for (uint32_t i = 0; i < kParamCount; i++) {
            Parameter p; initParameter(i, p); param[i] = p.ranges.def;
        }
    }

    // No rom= in the config file: look for a "roms" folder next to the plugin or beside the config file.
    void autoFindRoms()
    {
        std::vector<std::string> dirs;
        if (const char* b = getBundlePath()) { dirs.push_back(std::string(b) + "/roms"); dirs.push_back(dirOf(b) + "/roms"); }
        dirs.push_back(dirOf(configPath()) + "/roms");
        for (const auto& d : dirs) {
            hotd::Loaded L; std::string err;
            if (loadHotdFromFolder(d + "/x", L, err)) { romPath = d + "/epr-19720_1.31"; return; }
        }
    }

    // ---- engine setup ---------------------------------------------------------------------------------------
    void installBaseImage(bool addInitWaves)
    {
        engine->clearWaves();
        uint32_t freeFrom = 0x1000;
        std::vector<uint16_t> img;
        if (!rawRam.empty()) { img = rawRam; freeFrom = 0x74000; }
        else if (hotdOk)     { img = hotd::buildRamImage(hotd.rom, hotd.bank, -1); freeFrom = 0x5500; }
        engine->loadRamImage(img, freeFrom);
        if (addInitWaves) addInitWavesToEngine();
    }

    void addInitWavesToEngine()
    {
        using namespace scsp_synth;
        const int N = 1024; Waveform w; w.pcm.resize(N);
        w.name = "Sine";     for (int i = 0; i < N; i++) w.pcm[i] = (int16_t) std::lround(32767 * std::sin(2 * M_PI * i / N)); w.setSingleCycle(N); engine->addWave(w);
        w.name = "Saw";      for (int i = 0; i < N; i++) w.pcm[i] = (int16_t) (32767 - 65534 * i / N); w.setSingleCycle(N); engine->addWave(w);
        w.name = "Square";   for (int i = 0; i < N; i++) w.pcm[i] = (int16_t) (i < N / 2 ? 28000 : -28000); w.setSingleCycle(N); engine->addWave(w);
        w.name = "Triangle"; for (int i = 0; i < N; i++) w.pcm[i] = (int16_t) (32767 * (i < N / 2 ? (4.0 * i / N - 1) : (3 - 4.0 * i / N))); w.setSingleCycle(N); engine->addWave(w);
        std::mt19937 rng(1); w.name = "Noise"; for (int i = 0; i < N; i++) w.pcm[i] = (int16_t) ((int) (rng() & 0xFFFF) - 32768); w.setSingleCycle(N); engine->addWave(w);
        Waveform ramw; ramw.name = "RAM window"; ramw.autoAlloc = false; ramw.ramByteAddr = 0x28C0; ramw.loopStart = 0; ramw.loopEnd = 64; ramw.rootKey = 65;
        engine->addWave(ramw);      // index 5
    }

    void buildInit()
    {
        installBaseImage(true);
        applyParams();
    }

    void reloadSources()
    {
        std::lock_guard<std::mutex> g(mtx);
        hotdOk = false; rawRam.clear();
        if (!romPath.empty()) {
            std::string err; hotd::Loaded L;
            if (loadHotdFromFolder(romPath, L, err)) { hotd = std::move(L); hotdOk = true; }
            else d_stderr("SCSP Synth: %s", err.c_str());
        }
        if (!ramPath.empty()) {
            auto d = readFile(ramPath);
            if (d.size() >= 0x80000) {
                rawRam.resize(0x40000);
                for (size_t i = 0; i < 0x40000; i++) rawRam[i] = (uint16_t) (d[2 * i] | d[2 * i + 1] << 8);   // little-endian words (Mednafen / our dump format)
            } else d_stderr("SCSP Synth: RAM image must be 512 KB");
        }
    }

    // ---- parameter -> patch ---------------------------------------------------------------------------------
    void applyParams()
    {
        engine->setBendRange(param[kParamBend]);
        if (hotdActive) return;
        auto I = [&](int k) { return (int) std::lround(param[k]); };
        using namespace scsp_synth;
        Patch p; p.name = "Init";
        p.wave = I(kParamWave); p.lpctl = (uint8_t) I(kParamLoop); p.tuneCents = param[kParamTune];
        p.ar = (uint8_t) I(kParamAR); p.d1r = (uint8_t) I(kParamD1R); p.dl = (uint8_t) I(kParamDL); p.d2r = (uint8_t) I(kParamD2R);
        p.rr = (uint8_t) I(kParamRR); p.krs = (uint8_t) I(kParamKRS);
        p.tl = (uint8_t) I(kParamTL); p.velSens = param[kParamVelSens]; p.disdl = (uint8_t) I(kParamDISDL);
        const int pan = I(kParamPan); p.dipan = (uint8_t) (pan >= 0 ? pan : (0x10 | -pan));
        p.mdl = (uint8_t) I(kParamMDL); p.mdxsl = (uint8_t) I(kParamMDXSL); p.mdysl = (uint8_t) I(kParamMDYSL);
        p.seed0 = (uint16_t) I(kParamSeedA); p.seed1 = (uint16_t) I(kParamSeedB); p.stwinh = I(kParamStWInh) != 0;
        p.sweep.ticks = (uint8_t) I(kParamSwTicks); p.sweep.step = (int8_t) I(kParamSwStep);
        p.useSeed = p.stwinh || p.sweep.ticks || p.seed0 || p.seed1;
        p.lfore = true; p.lfof = (uint8_t) I(kParamLFOF);
        p.plfows = (uint8_t) I(kParamPLFOWS); p.plfos = (uint8_t) I(kParamPLFOS);
        p.alfows = (uint8_t) I(kParamALFOWS); p.alfos = (uint8_t) I(kParamALFOS);
        auto& bank = engine->bank();
        if (bank.size() > 5) {
            Waveform& w = bank[5];
            w.ramByteAddr = (uint32_t) I(kParamRamAddr) & ~1u; w.pcm8 = I(kParamRam8Bit) != 0;
            w.loopStart = (uint32_t) I(kParamRamLoopStart); w.loopEnd = (uint32_t) I(kParamRamLen); w.rootKey = I(kParamRamRoot);
        }
        for (int ch = 0; ch < 16; ch++) engine->setPatch(ch, p);
    }

    std::mutex mtx;
    std::unique_ptr<scsp_synth::Engine> engine;
    hotd::Loaded hotd; bool hotdOk = false, hotdActive = false;
    std::vector<uint16_t> rawRam;
    std::string romPath, ramPath;
    float param[kParamCount] = {0};
    std::atomic<bool> dirty{true};
    int program = 0;
    std::vector<float> l, r;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SCSPSynthPlugin)
};

Plugin* createPlugin() { return new SCSPSynthPlugin(); }

END_NAMESPACE_DISTRHO
