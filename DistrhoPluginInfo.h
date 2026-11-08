#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "SCSPSynth"
#define DISTRHO_PLUGIN_NAME    "SCSP Synth"
#define DISTRHO_PLUGIN_URI     "https://example.invalid/scspsynth"
#define DISTRHO_PLUGIN_CLAP_ID "invalid.scspsynth.scsp"

#define DISTRHO_PLUGIN_BRAND_ID  ScSp
#define DISTRHO_PLUGIN_UNIQUE_ID ScS1

#define DISTRHO_PLUGIN_HAS_UI          0
#define DISTRHO_PLUGIN_IS_RT_SAFE      0
#define DISTRHO_PLUGIN_IS_SYNTH        1
#define DISTRHO_PLUGIN_NUM_INPUTS      0
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1
#define DISTRHO_PLUGIN_WANT_PROGRAMS   1
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1
#define DISTRHO_PLUGIN_WANT_STATEFILES 0
#define DISTRHO_PLUGIN_USES_MODGUI     0

enum Parameters {
    kParamGain = 0, kParamWave, kParamLoop, kParamBend, kParamTune,
    kParamAR, kParamD1R, kParamDL, kParamD2R, kParamRR, kParamKRS,
    kParamTL, kParamVelSens, kParamDISDL, kParamPan,
    kParamMDL, kParamMDXSL, kParamMDYSL, kParamSeedA, kParamSeedB, kParamStWInh, kParamSwTicks, kParamSwStep,
    kParamLFOF, kParamPLFOWS, kParamPLFOS, kParamALFOWS, kParamALFOS,
    kParamRamAddr, kParamRamLen, kParamRamLoopStart, kParamRam8Bit, kParamRamRoot,
    kParamRomLoaded,                     // output: 1 when a HOTD ROM set is loaded
    kParamCount
};

enum States { kStateRom = 0, kStateRam, kStateCount };

#define kProgramCount 45   // 0 = Init, 1..44 = HOTD instruments 00..2B

#endif
