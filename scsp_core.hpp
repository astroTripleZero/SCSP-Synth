// scsp_core.hpp - standalone SEGA Saturn Custom Sound Processor (SCSP) core
// Derived from Mednafen's Saturn SCSP emulation (src/ss/scsp.h, scsp.inc),
// Copyright (C) 2015-2020 Mednafen Team, licensed under GPL v2 or later.
// Changes: removed save-state, debugger and dynarec glue; added a tiny shim for the
// few Mednafen helpers the code needs. Little-endian host assumed.
#pragma once
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstddef>
#include <type_traits>
#include <algorithm>

namespace scsp_core {

typedef uint8_t uint8; typedef uint16_t uint16; typedef uint32_t uint32; typedef uint64_t uint64;
typedef int8_t int8; typedef int16_t int16; typedef int32_t int32; typedef int64_t int64;

#pragma push_macro("INLINE")
#undef INLINE
#pragma push_macro("NO_INLINE")
#undef NO_INLINE
#pragma push_macro("MDFN_COLD")
#undef MDFN_COLD
#pragma push_macro("MDFN_LIKELY")
#undef MDFN_LIKELY
#pragma push_macro("MDFN_UNLIKELY")
#undef MDFN_UNLIKELY
#pragma push_macro("SS_DBG")
#undef SS_DBG
#pragma push_macro("SCSP_SoundIntChanged")
#undef SCSP_SoundIntChanged
#pragma push_macro("SCSP_MainIntChanged")
#undef SCSP_MainIntChanged
#pragma push_macro("sign_x_to_s32")
#undef sign_x_to_s32

#define INLINE inline
#define NO_INLINE
#define MDFN_COLD
#define MDFN_LIKELY(x) (x)
#define MDFN_UNLIKELY(x) (x)
#define SS_DBG(...) ((void)0)
#define SCSP_SoundIntChanged(s, l) ((void)0)
#define SCSP_MainIntChanged(s, l) ((void)0)
#define sign_x_to_s32(n, v) ((int32)((uint32)(v) << (32 - (n))) >> (32 - (n)))

static inline unsigned MDFN_tzcount16(uint16 v) { return !v ? 16 : __builtin_ctz(v); }
static inline unsigned MDFN_lzcount32(uint32 v) { return !v ? 32 : __builtin_clz(v); }

// 16-bit-word-organised big-endian memory view on a little-endian host
template<typename T, typename X>
static inline uintptr_t neX_ptr_be(uintptr_t base, size_t o)
{
 return base + (((o & ~(sizeof(T) - 1)) ^ (sizeof(X) - ((sizeof(T) < sizeof(X)) ? sizeof(T) : sizeof(X)))));
}
template<typename T, typename BT>
static inline T ne16_rbo_be(BT base, const size_t o)
{
 uintptr_t p = neX_ptr_be<T, uint16>((uintptr_t)base, o);
 if(sizeof(T) == 4) { const uint16* q = (const uint16*)p; return (T)(((uint32)q[0] << 16) | q[1]); }
 return *(T*)p;
}
template<typename T, typename BT>
static inline void ne16_wbo_be(BT base, const size_t o, const T v)
{
 uintptr_t p = neX_ptr_be<T, uint16>((uintptr_t)base, o);
 if(sizeof(T) == 4) { uint16* q = (uint16*)p; q[0] = (uint16)((uint32)v >> 16); q[1] = (uint16)v; return; }
 *(T*)p = v;
}
template<typename T, typename BT>
static inline T ne64_rbo_be(BT base, const size_t o)
{
 T r; memcpy(&r, (void*)neX_ptr_be<T, uint64>((uintptr_t)base, o), sizeof(T)); return r;
}
template<typename T, typename BT>
static inline void ne64_wbo_be(BT base, const size_t o, const T v)
{
 memcpy((void*)neX_ptr_be<T, uint64>((uintptr_t)base, o), &v, sizeof(T));
}
template<typename T, bool IsWrite, typename BT>
static inline void ne64_rwbo_be(BT base, const size_t o, T* value)
{
 if(IsWrite) ne64_wbo_be<T>(base, o, *value); else *value = ne64_rbo_be<T>(base, o);
}
template<typename T, bool IsWrite, typename BT>
static inline void ne16_rwbo_be(BT base, const size_t o, T* value)
{
 if(IsWrite) ne16_wbo_be<T>(base, o, *value); else *value = ne16_rbo_be<T>(base, o);
}

/******************************************************************************/
/* Mednafen Sega Saturn Emulation Module                                      */
/******************************************************************************/
/* scsp.h:
**  Copyright (C) 2015-2020 Mednafen Team
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

class SS_SCSP
{
 public:

 SS_SCSP() MDFN_COLD;
 ~SS_SCSP() MDFN_COLD;


 void Reset(bool powering_up) MDFN_COLD;

 // Use int16 if the SCSP is connected to a 16-bit DAC, int32 if an 18-bit DAC
 template<typename T_out = int16>
 void RunSample(T_out* outlr, void (*midi_out)(uint8) = nullptr);

 template<typename T, bool IsWrite>
 void RW(uint32 A, T& V); //, void (*time_sucker)();

 // Caller must ensure appropriate timing.
 INLINE void WriteMIDI(uint8 V)
 {
  MIDI_WriteInput(V);
 }

 INLINE uint16* GetEXTSPtr(void)
 {
  return EXTS;
 }

 INLINE uint16* GetRAMPtr(void)
 {
  return RAM;
 }

 INLINE uint64 PeekMPROG(uint32 A)	  { assert(A < 0x80); return DSP.MPROG[A]; }
 INLINE void PokeMPROG(uint32 A, uint64 V) { assert(A < 0x80); DSP.MPROG[A] = V; }
 INLINE uint32 PeekMEMS(uint32 A)	  { assert(A < 0x20); return DSP.MEMS[A]; }
 INLINE void PokeMEMS(uint32 A, uint32 V)  { assert(A < 0x20); DSP.MEMS[A] = V & 0x00FFFFFF; }
 INLINE uint32 PeekTEMPRel(uint32 A)	  { assert(A < 0x80); return DSP.TEMP[(DSP.MDEC_CT + A) & 0x7F]; }
 INLINE void PokeTEMPRel(uint32 A, uint32 V)  { assert(A < 0x80); DSP.TEMP[(DSP.MDEC_CT + A) & 0x7F] = V & 0x00FFFFFF; }

 enum
 {
  GSREG_MVOL = 0,
  GSREG_DAC18B,
  GSREG_MEM4MB,
  GSREG_RBC,
  GSREG_MSLC,

  GSREG_SCIEB,
  GSREG_SCIPD,
  GSREG_MCIEB,
  GSREG_MCIPD,

  GSREG_EFREG0, GSREG_EFREG1, GSREG_EFREG2, GSREG_EFREG3, GSREG_EFREG4, GSREG_EFREG5, GSREG_EFREG6, GSREG_EFREG7,
  GSREG_EFREG8, GSREG_EFREG9, GSREG_EFREGA, GSREG_EFREGB, GSREG_EFREGC, GSREG_EFREGD, GSREG_EFREGE, GSREG_EFREGF
 };

 uint32 GetRegister(const unsigned id, char* const special, const uint32 special_len) MDFN_COLD;
 void SetRegister(const unsigned id, const uint32 value) MDFN_COLD;

 private:

 void RecalcSoundInt(void);
 void RecalcMainInt(void);

 enum
 {
  ENV_PHASE_ATTACK = 0,
  ENV_PHASE_DECAY1 = 1,
  ENV_PHASE_DECAY2 = 2,
  ENV_PHASE_RELEASE = 3
 };

 uint16 SlotRegs[0x20][0x10];

 struct Slot
 {
  uint32 StartAddr;	// 20 bits, memory address.
  uint16 LoopStart;	// 16 bits, in samples.
  uint16 LoopEnd;	// 16 bits, in samples.
  //
  bool KeyBit;
  //
  bool WF8Bit;
  uint8 LoopMode;
  enum
  {
   LOOP_DISABLED = 0,
   LOOP_NORMAL = 1,
   LOOP_REVERSE = 2,
   LOOP_ALTERNATING = 3
  };

  uint8 SourceControl;
  enum
  {
   SOURCE_MEMORY = 0,
   SOURCE_NOISE = 1,
   SOURCE_ZERO = 2,
   SOURCE_UNDEFINED = 3
  };

  uint16 SBXOR;

  uint8 EnvRates[4];

  bool AttackHold;
  bool AttackLoopLink;
  uint8 DecayLevel;

  uint8 KRS;
  uint8 TotalLevel;
  bool EGBypass;	// When true, force EG output to 0(no attenuation), but TL and ALFO still have an effect
  bool SoundDirect;	// When true, bypass EG, TL, ALFO volume control

  bool StackWriteInhibit;

  uint8 ModLevel;
  uint8 ModInputX;
  uint8 ModInputY;

  uint8 Octave;
  uint16 FreqNum;

  uint8 ALFOModLevel;
  uint8 ALFOWaveform;

  uint8 PLFOModLevel;
  uint8 PLFOWaveform;
 
  uint8 LFOFreq;

  bool LFOReset;

  // DSP mix stack
  uint8 ToDSPSelect;
  uint8 ToDSPLevel;

  int16 DirectVolume[2];	// 1.14 fixed point, derived from DISDL and DIPAN
  int16 EffectVolume[2];	// 1.14 fixed point, derived from EFSDL and EFPAN
  //
  //
  uint32 ShortWaveMask;
  bool ShortWave;
  uint16 CurrentAddr;
  uint32 PhaseWhacker;
  bool InLoop;
  bool LoopSub;
  bool WFAllowAccess;
  uint8 EnvPhase;	// ENV_PHASE_ATTACK ... ENV_PHASE_RELEASE (0...3)
  uint32 EnvLevel;	// 0 ... 0x3FF

  uint8 LFOCounter;
  uint16 LFOTimeCounter;
 } Slots[32];

 uint16 EXTS[2];

 void RecalcShortWaveMask(Slot* s);

 void RunEG(Slot* s, const unsigned key_eg_scale, const uint32 sc, const uint32 scxc);

 uint8 GetALFO(Slot* s);
 int GetPLFO(Slot* s);
 void RunLFO(Slot* s);

 uint16 SoundStack[0x40];
 uint16 SoundStackDelayer[4];

 uint16 MasterVolume;	// 1.8 fixed point, derived from MVOL
 uint8 MVOL;
 bool DAC18bit;
 bool Mem4Mb;

 uint32 SlotMonitorWhich;
 uint16 SlotMonitorData;

 bool KeyExecute;
 uint32 LFSR;
 uint32 GlobalCounter;

 const uint16 SB_XOR_Table[4] = { 0x0000, 0x7FFF, 0x8000, 0xFFFF };

 //
 //
 enum
 {
  MIDIF_INPUT_EMPTY = 0x01,
  MIDIF_INPUT_FULL  = 0x02,
  MIDIF_INPUT_OFLOW = 0x04,
  MIDIF_OUTPUT_EMPTY= 0x08,
  MIDIF_OUTPUT_FULL = 0x10
 };
 struct MIDIS
 {
  uint8 InputFIFO[4];
  uint8 InputRP, InputWP, InputCount;

  uint8 OutputFIFO[4];
  uint8 OutputRP, OutputWP, OutputCount;

  uint8 Flags;
  //
  uint8 SimuClockDivider;
  uint8 TransmitBitCounter;
  uint16 TransmitBuffer;
 } MIDI;
 uint8 MIDI_ReadInput(void);
 void MIDI_WriteInput(uint8 V);
 void MIDI_WriteOutput(uint8 V);
 void MIDI_Reset(void);
 void MIDI_Run(void (*midi_out)(uint8));
 //
 //
 uint16 SCIEB;
 uint16 SCIPD;

 uint16 MCIEB;
 uint16 MCIPD;

 uint8 SCILV[3];
 //
 //
 struct
 {
  uint8 Control;
  uint8 Counter;
  int32 Reload;
 } Timers[3];
 //
 //
 // DMEA, DRGA, and DTLG are apparently not altered by executing DMA.
 //
 uint32 DMEA;
 uint16 DRGA;
 uint16 DTLG;

 bool DMA_Execute;
 bool DMA_Direction;
 bool DMA_Gate;

 void RunDMA(void);
 //
 //
 uint8 RBP;
 uint8 RBL;
 void RunDSP(void);

 struct DSPS
 {
  uint64 MPROG[0x80];
  uint32 TEMP[0x80];	// 24 bit
  uint32 MEMS[0x20];	// 24 bit
  uint16 COEF[64];	// 13 bit
  uint16 MADRS[32];	// 16 bit

  uint32 MIXS[0x10];	// 20 bit
  uint16 EFREG[0x10];

  uint32 INPUTS;	// 24 bit

  uint32 SFT_REG;	// 26 bit
  uint16 FRC_REG;	// 13 bit
  uint32 Y_REG;		// 24 bit, latches INPUTS
  uint16 ADRS_REG;	// 12 bit, latches output of A_SEL(which selects between shifter output and upper 8 bits of INPUTS

  uint16 MDEC_CT;

  uint32 RWAddr;

  bool WritePending;
  uint16 WriteValue;

  uint8 ReadPending;	// = 1 (NOFL=0), =2 (NOFL=1) at time or MRT
  uint32 ReadValue;

  bool MPROG_Dirty;
 } DSP;
 //
 //

 uint16 RAM[262144 * 2];	// *2 for dummy so we don't have to have so many conditionals in the playback code.

#ifdef MDFN_SS_SCSP_DSP_DYNAREC
 alignas(8) uint8 DynaRecPool[65536];
#endif
};


/******************************************************************************/
/* Mednafen Sega Saturn Emulation Module                                      */
/******************************************************************************/
/* scsp.inc - SCSP Emulation
**  Copyright (C) 2015-2021 Mednafen Team
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/*
 TODO:
	Test the relative timing between timers, LFO, EG, and the DSP ringbuffer position.

	Open bus or something seems to get into the audio when a slot's playback ends by loop end, but not by env level being max, under
	certain conditions when the 68K is running.  Test further, maybe emulate in the future?

	Open bus emulation and cycle timing granularity instead of sample(someday?); be careful with DSP input/output buffering
	with respect to dry/direct output path delay, and the mapping of the buffers to the address space.  Emulating the extra wait
	states on 68K accesses caused by PCM playback and DSP memory accesses may be required to fix the problem of game music playing back
	a very teensy bit too fast.

	Hook up MIDI interface emulation to the outside somehow.

	Proper reset/power-on state.

	Mem4Mb

	DSP: Handle instruction with MRT=1 and MWT=1 correctly.

	DSP: Handle IWT=1 when MRT=0 and MWT=0 for the instruction a couple instructions back(NOFL influences the value...)

	DSP: Test IWT=1 when MRT=0 and MWT=1 for the intruction a couple back.
*/

inline SS_SCSP::SS_SCSP()
{
 memset(&RAM[0x40000], 0x00, 0x40000 * sizeof(uint16));	// Zero out dummy part.

 Reset(true);
}

inline SS_SCSP::~SS_SCSP()
{


}

inline void SS_SCSP::RecalcSoundInt(void)	// Sound CPU interrupts
{
 unsigned mask_test;
 unsigned lvmasked[3];
 unsigned out_level = 0;

 mask_test = SCIPD & SCIEB;
 if(mask_test &~ 0xFF)
  mask_test = (mask_test & 0xFF) | 0x80;

 lvmasked[0] = SCILV[0] & mask_test;
 lvmasked[1] = SCILV[1] & mask_test;
 lvmasked[2] = SCILV[2] & mask_test;

 if(lvmasked[2])
 {
  out_level |= 0x4;
  lvmasked[1] &= lvmasked[2];
  lvmasked[0] &= lvmasked[2];
 }

 if(lvmasked[1])
 {
  out_level |= 0x2;
  lvmasked[0] &= lvmasked[1];
 }

 out_level |= (bool)lvmasked[0];
 //
 //
 SCSP_SoundIntChanged(this, out_level);
}

inline void SS_SCSP::RecalcMainInt(void)	// Main CPU interrupts
{
 SCSP_MainIntChanged(this, (bool)(MCIPD & MCIEB));
}

INLINE void SS_SCSP::RecalcShortWaveMask(Slot* s)
{
 const unsigned lev = s->LoopEnd & 0x780;

 s->ShortWaveMask = 0xFFFFFFFF;
 if(s->ShortWave && lev)
  s->ShortWaveMask = (1U << MDFN_tzcount16(lev)) - 1;
}

inline void SS_SCSP::Reset(bool powering_up)
{
 //
 // May need to add a DecodeSlotReg() function or something similar if we implement 
 // more aggressive slot register value optimizations on writes in the future.
 //
 memset(SlotRegs, 0, sizeof(SlotRegs));
 memset(Slots, 0, sizeof(Slots));

 for(unsigned i = 0; i < 32; i++)
 {
  // Some SSFs require this; TODO: test to see if this is correct for a reset,
  // and if not, move it into special SSF loading code.
  Slots[i].EnvLevel = 0x3FF;
  Slots[i].EnvPhase = ENV_PHASE_RELEASE;

  Slots[i].LFOTimeCounter = 1;
  //
  RecalcShortWaveMask(&Slots[i]);
 }

 for(unsigned i = 0; i < 2; i++)
  EXTS[i] = 0;

 memset(SoundStack, 0, sizeof(SoundStack));
 memset(SoundStackDelayer, 0, sizeof(SoundStackDelayer));

 if(powering_up)
 {
  for(size_t i = 0; i < 0x40000; i++)
   RAM[i] = 0x0000; // or some other value?
 }

 //
 //
 MVOL = 0;
 MasterVolume = 0;

 SlotMonitorWhich = 0;
 SlotMonitorData = 0;

 DAC18bit = false;
 Mem4Mb = false;

 KeyExecute = false;
 LFSR = 1;
 GlobalCounter = 0;

 MIDI_Reset();
 //
 //
 DMEA = 0;
 DRGA = 0;
 DTLG = 0;

 DMA_Execute = false;
 DMA_Direction = false;
 DMA_Gate = false;
 //
 //
 for(unsigned i = 0; i < 3; i++)
 {
  Timers[i].Control = 0;
  Timers[i].Counter = 0;
  Timers[i].Reload = -1;
 }

 //
 //
 RBP = 0;
 RBL = 0;

 memset(&DSP, 0, sizeof(DSP));
 DSP.MDEC_CT = 0;
 //
 //
 SCIEB = 0;
 SCIPD = 0;

 MCIEB = 0;
 MCIPD = 0;

 for(unsigned i = 0; i < 3; i++)
  SCILV[i] = 0;

 RecalcSoundInt();
 RecalcMainInt();
}

//
//
//
INLINE uint8 SS_SCSP::MIDI_ReadInput(void)
{
 uint8 ret = MIDI.InputFIFO[MIDI.InputRP]; // TODO: May not be correct for InputCount == 0; test.

 if(MIDI.InputCount)
 {
  //printf("MIDI Read: %02x\n", ret);

  MIDI.InputRP = (MIDI.InputRP + 1) & 0x3;
  MIDI.InputCount--;
  MIDI.Flags &= ~MIDIF_INPUT_FULL;
  MIDI.Flags &= ~MIDIF_INPUT_OFLOW;	// TODO: Test.

  if(!MIDI.InputCount)
  {
   MIDI.Flags |= MIDIF_INPUT_EMPTY;

   SCIPD &= ~0x008;
   MCIPD &= ~0x008;
   RecalcSoundInt();
   RecalcMainInt();
  }
 }

 return ret;
}

inline void SS_SCSP::MIDI_WriteInput(uint8 V)
{
 if(MIDI.InputCount == 4)
 {
  MIDI.Flags |= MIDIF_INPUT_OFLOW;
  return;
 }
 //printf("MIDI Write: %02x\n", V);

 MIDI.InputFIFO[MIDI.InputWP] = V;
 MIDI.InputWP = (MIDI.InputWP + 1) & 0x3;
 MIDI.InputCount++;

 MIDI.Flags &= ~MIDIF_INPUT_EMPTY;
 if(MIDI.InputCount == 4)
  MIDI.Flags |= MIDIF_INPUT_FULL;

 SCIPD |= 0x008;
 MCIPD |= 0x008;
 RecalcSoundInt();
 RecalcMainInt();
}

INLINE void SS_SCSP::MIDI_WriteOutput(uint8 V)
{
 if(MIDI.OutputCount == 4)	// TODO: See if write is completely discarded, or replaces last entry in FIFO.
  return;

 MIDI.OutputFIFO[MIDI.OutputWP] = V;
 MIDI.OutputWP = (MIDI.OutputWP + 1) & 0x3;
 MIDI.OutputCount++;

 MIDI.Flags &= ~MIDIF_OUTPUT_EMPTY;
 if(MIDI.OutputCount == 4)
  MIDI.Flags |= MIDIF_OUTPUT_FULL;

 SCIPD &= ~0x200;
 MCIPD &= ~0x200;
 RecalcSoundInt();
 RecalcMainInt();
}

INLINE void SS_SCSP::MIDI_Run(void (*midi_out)(uint8))
{
 MIDI.SimuClockDivider += 32;
 if(MIDI.SimuClockDivider >= 45)
 {
  MIDI.SimuClockDivider -= 45;

  if(!MIDI.TransmitBitCounter)
  {
   if(MIDI.OutputCount)
   {
    //printf("Transmit\n");
    MIDI.TransmitBitCounter = 10;
    MIDI.TransmitBuffer = (uint32)(MIDI.OutputFIFO[MIDI.OutputRP] | ~0xFF) << 1;
    MIDI.OutputRP = (MIDI.OutputRP + 1) & 0x3;
    MIDI.OutputCount--;

    MIDI.Flags &= ~MIDIF_OUTPUT_FULL;
    if(!MIDI.OutputCount)
    {
     MIDI.Flags |= MIDIF_OUTPUT_EMPTY;

     SCIPD |= 0x200;
     MCIPD |= 0x200;
     RecalcSoundInt();
     RecalcMainInt();
    }
   }
  }

  if(MIDI.TransmitBitCounter == 1 && midi_out)
   midi_out((uint8)(MIDI.TransmitBuffer >> 1));

  MIDI.TransmitBitCounter -= (bool)MIDI.TransmitBitCounter;
 }
}

INLINE void SS_SCSP::MIDI_Reset(void)
{
 memset(MIDI.InputFIFO, 0, sizeof(MIDI.InputFIFO));
 memset(MIDI.OutputFIFO, 0, sizeof(MIDI.OutputFIFO));

 MIDI.InputRP = MIDI.InputWP = MIDI.InputCount = 0;
 MIDI.OutputRP = MIDI.OutputWP = MIDI.OutputCount = 0;

 MIDI.Flags = MIDIF_INPUT_EMPTY | MIDIF_OUTPUT_EMPTY;

 MIDI.SimuClockDivider = 0;
 MIDI.TransmitBitCounter = 0;
 MIDI.TransmitBuffer = 0x00;

 //SCIPD &= ~0x200;
 //MCIPD &= ~0x200;
 //RecalcSoundInt();
 //RecalcMainInt();
}
//
//
//

static INLINE void SDL_PAN_ToVolume(int16* outvol, const unsigned level, const unsigned pan)
{
 const bool pan_which = (bool)(pan & 0x10);
 unsigned basev;
 unsigned panv;

 basev = (0x80 << level);

 if(!level)
  basev = 0;

 panv = basev >> ((pan & 0x0F) >> 1);
 if(pan & 0x01)
  panv -= (panv >> 2);

 if((pan & 0x0F) == 0x0F)
  panv = 0;

 outvol[ pan_which] = panv;
 outvol[!pan_which] = basev;
}

template<typename T, bool IsWrite>
INLINE void SS_SCSP::RW(uint32 A, T& DBV)
{
 if(A < 0x100000)
 {
  if(MDFN_UNLIKELY(A >= 0x80000))
  {
   if(IsWrite)
    SS_DBG(SS_DBG_WARNING | SS_DBG_SCSP, "[SCSP] %zu-byte write of value 0x%08x to unmapped SCSP RAM address 0x%06x\n", sizeof(T), DBV, A);
   else
   {
    SS_DBG(SS_DBG_WARNING | SS_DBG_SCSP, "[SCSP] %zu-byte read from unmapped SCSP RAM address 0x%06x\n", sizeof(T), A);
    DBV = 0;
   }
  }
  else
  {
   ne16_rwbo_be<T, IsWrite>(RAM, A, &DBV);
  }
  return;
 }

 A &= 0xFFF;

 if(A < 0x400)
 {
  //if(IsWrite)
  // SS_DBG(SS_DBG_SCSP_REGW, "[SCSP] %zu-byte write to slot 0x%02x register offset 0x%02x: 0x%0*x\n", sizeof(T), (A >> 5) & 0x1F, A & 0x1F, (int)(2 * sizeof(T)), DBV);
  //
  // Slot regs
  //
  const unsigned slotnum = (A >> 5) & 0x1F;

  ne16_rwbo_be<T, IsWrite>(SlotRegs[slotnum], A & 0x1F, &DBV);

  if(IsWrite)
  {
   auto* s = &Slots[slotnum];
   uint16& SRV = SlotRegs[slotnum][(A >> 1) & 0xF];

   switch((A >> 1) & 0xF)
   {
    case 0x00:
	KeyExecute |= (bool)(SRV & 0x1000);
	SRV &= 0x0FFF;

	s->KeyBit = (SRV >> 11) & 0x1;
	s->SBXOR = SB_XOR_Table[(SRV >> 9) & 0x3];
	s->SourceControl = (SRV >> 7) & 0x3;
	s->LoopMode = (SRV >> 5) & 0x3;
	s->WF8Bit = (SRV >> 4) & 0x1;
	s->StartAddr = (s->StartAddr & 0xFFFF) | ((SRV & 0xF) << 16);
	break;

    case 0x01:
	s->StartAddr = (s->StartAddr &~ 0xFFFF) | SRV;
	break;

    case 0x02:
	s->LoopStart = SRV;
	break;

    case 0x03:
	s->LoopEnd = SRV;
	//
	RecalcShortWaveMask(s);
	break;

    case 0x04:
	s->EnvRates[ENV_PHASE_ATTACK] = SRV & 0x1F;
	s->AttackHold = (SRV >> 5) & 0x1;
	s->EnvRates[ENV_PHASE_DECAY1] = (SRV >> 6) & 0x1F;
	s->EnvRates[ENV_PHASE_DECAY2] = (SRV >> 11) & 0x1F;
	break;

    case 0x05:
	s->EnvRates[ENV_PHASE_RELEASE] = SRV & 0x1F;
	s->DecayLevel = (SRV >> 5) & 0x1F;
	s->KRS = (SRV >> 10) & 0xF;
	s->AttackLoopLink = (SRV >> 14) & 0x1;
	s->EGBypass = (SRV >> 15) & 0x1;
	break;

    case 0x06:
	SRV &= 0x0FFF;
	if(SRV & 0x0C00)
	{
	 SS_DBG(SS_DBG_WARNING | SS_DBG_SCSP, "[SCSP] Unknown bits non-zero in slot %u, register %u\n", slotnum, (A >> 1) & 0xF);
	}

	s->TotalLevel = SRV & 0xFF;
	s->SoundDirect = (SRV >> 8) & 0x1;
	s->StackWriteInhibit = (SRV >> 9) & 0x1;
	break;

    case 0x07:
        s->ModInputY = SRV & 0x3F;
	s->ModInputX = (SRV >> 6) & 0x3F;
	s->ModLevel = (SRV >> 12) & 0xF;
	break;

    case 0x08:
	s->FreqNum = SRV & 0x7FF;
	s->Octave = (SRV >> 11) & 0xF;
	s->ShortWave = (SRV >> 15) & 0x1;
	//
	RecalcShortWaveMask(s);
	break;

    case 0x09:
	s->ALFOModLevel = SRV & 0x7;
	s->ALFOWaveform = (SRV >> 3) & 0x3;
	s->PLFOModLevel = (SRV >> 5) & 0x7;
	s->PLFOWaveform = (SRV >> 8) & 0x3;
	s->LFOFreq = (SRV >> 10) & 0x1F;
	s->LFOReset = (SRV >> 15) & 0x1;
	break;

    case 0x0A:
	SRV &= 0x00FF;
	if(SRV & 0x0080)
	{
	 SS_DBG(SS_DBG_WARNING | SS_DBG_SCSP, "[SCSP] Unknown bits non-zero in slot %u, register %u\n", slotnum, (A >> 1) & 0xF);
	}
	s->ToDSPLevel = SRV & 0x7;
	s->ToDSPSelect = (SRV >> 3) & 0xF;
	break;

    case 0x0B:
	SDL_PAN_ToVolume(s->DirectVolume, (SRV >> 13) & 0x7, (SRV >> 8) & 0x1F);
	SDL_PAN_ToVolume(s->EffectVolume, (SRV >>  5) & 0x7, (SRV >> 0) & 0x1F);
	break;

    case 0x0C: case 0x0D: case 0x0E: case 0x0F:
	SRV = 0;
	break;
   }
  }

  return;
 }

 if(A < 0x430)
 {
  // OldReg = (OldReg &~ mask) | ((DBV << shift) & mask & whatever);
  unsigned mask = 0xFFFF;
  unsigned shift = 0;

  if(sizeof(T) == 1)
  {
   shift = ((A & 1) ^ 1) << 3;
   mask = 0xFF << shift;
  }

  //
  // Common regs
  //
  switch((A >> 1) & 0x1F)
  {
   case 0x00:	// MVOL (W), DB (W), M4 (W)
	if(IsWrite)
	{
	 uint16 tmp = MVOL | (DAC18bit << 8) | (Mem4Mb << 9);

	 tmp = (tmp &~ mask) | ((DBV << shift) & mask);

	 MVOL = (tmp & 0xF);
	 DAC18bit = (tmp >> 8) & 1;
	 Mem4Mb = (tmp >> 9) & 1;

         //
	 {
	  unsigned mv;

	  mv = 0x2 << (MVOL >> 1);
	  if(!(MVOL & 1))
	   mv -= (mv >> 2);

	  if(!MVOL)
	   mv = 0;

	  MasterVolume = mv;
         }
	}
	else
	 DBV = 0;
	break;

   case 0x01:	// RBP (W), RBL (W)
	if(IsWrite)
	{
	 uint16 tmp = RBP | (RBL << 7);
	 tmp = (tmp &~ mask) | ((DBV << shift) & mask);
	 RBP = tmp & 0x7F;
	 RBL = (tmp >> 7) & 0x3;
	}
	else
	 DBV = 0;
	break;

   case 0x02:	// MIDI input buffer, Input Empty, Input Full, Input Overflow, Output Empty, Output Full (all R)
	if(!IsWrite)
	{
	 if(!shift)
	 {
	  // TODO: Test correct order of flags latching returning versus input fetching/latching(also maybe take into consideration
	  // 16-bit access from SCU being split into 2x 8-bit accesses on the real thing...).
	  unsigned tmp = MIDI.Flags << 8;
	  tmp |= MIDI_ReadInput();
	  DBV = tmp & mask;
	 }
	 else
	  DBV = MIDI.Flags;
	}
	break;

   case 0x03:	// MOBUF (W)
	if(IsWrite)
	{
	 if(!shift)
	 {
	  SS_DBG(SS_DBG_SCSP_MOBUF, "[SCSP] MOBUF write: 0x%02x\n", (uint8)DBV);

	  MIDI_WriteOutput(DBV);
	 }
	}
	else
	 DBV = 0;
	break;

   case 0x04:	// CA/SGC/EG (R), MSLC (W)
	if(IsWrite)
	{
	 uint16 tmp = (SlotMonitorWhich << 11);
	 tmp = (tmp &~ mask) | ((DBV << shift) & mask);
	 SlotMonitorWhich = (tmp >> 11) & 0x1F;
	}
	else
	{
	 DBV = (SlotMonitorData & mask) >> shift;
	}
	break;

   case 0x05:
   case 0x06:
   case 0x07:
	if(!IsWrite)
	 DBV = 0;
	break;

   case 0x08:	// TEST
	if(IsWrite)
	{
	 //
	 // 0x80: Mute? messes up sound cpu too?
	 //
	 // 0x20: forces LFO frequency to max? and blocks memory access to waveform playback? messes up sound cpu too?
	 // 0x40: blocks memory access to waveform playback? messes up sound cpu too?
	 // 0x60: force wfallowaccess = 0 ? messes up sound cpu too?
	 //
	 // 0x08: appears to block SCU access to SCSP RAM(causes hang)
	 // 0x04: kills sound cpu(blocks memory access?)
	 // 0x02: blocks slot waveform playback from reading memory?
	 // 0x01: messes up dsp or blocks dsp memory access? the ears are not happy!
	 //
	 if(DBV)
	  SS_DBG(SS_DBG_WARNING | SS_DBG_SCSP, "[SCSP] Non-zero value written to test register: 0x%04x\n", DBV);
	}
	else
	 DBV = 0;
	break;

   case 0x09:	// DMEA(low) (W)
	if(IsWrite)
	{
	 uint16 tmp = DMEA << 1;
	 tmp = (tmp &~ mask) | ((DBV << shift) & mask);
	 DMEA = tmp >> 1;
	}
	else
	 DBV = 0;
	break;

   case 0x0A:	// DRGA (W), DMEA(high) (W)
	if(IsWrite)
	{
	 uint16 tmp = ((DMEA >> 3) & 0xF000) | (DRGA << 1);
	 tmp = (tmp &~ mask) | ((DBV << shift) & mask);
	 DMEA = (DMEA & 0x7FFF) | ((tmp & 0xF000) << 3);
	 DRGA = (tmp >> 1) & 0x7FF;
	}
	else
	 DBV = 0;
	break;

   case 0x0B: 	// DTLG(W), EX(R/W), DI(R/W), GA(R/W)
	if(IsWrite)
	{
	 //const bool prev_execute = DMA_Execute;
	 uint16 tmp = (DTLG << 1) | (DMA_Execute << 12) | (DMA_Direction << 13) | (DMA_Gate << 14);

	 tmp = (tmp &~ mask) | ((DBV << shift) & mask);

	 DTLG = (tmp >> 1) & 0x7FF;
	 DMA_Execute |= (tmp >> 12) & 0x1;
	 DMA_Direction = (tmp >> 13) & 0x1;
	 DMA_Gate = (tmp >> 14) & 0x1;

#if 0
	 if(!prev_execute && DMA_Execute)
	 {
	  printf("[SCSP] DMA Started; Memory Address: 0x%06x, Register Address: 0x%03x, Length: 0x%03x Direction: %u, Gate: %u\n",
		DMEA << 1, DRGA << 1, DTLG << 1, DMA_Direction, DMA_Gate);
	 }
#endif
	 RunDMA();
	}
	else
	{
	 uint16 tmp = (DMA_Execute << 12) | (DMA_Direction << 13) | (DMA_Gate << 14);

	 DBV = (tmp & mask) >> shift;
	}
	break;

   case 0x0C:	// TIMA(W), TACTL(W)
   case 0x0D:	// TIMB(W), TBCTL(W)
   case 0x0E:	// TIMC(W), TCCTL(W)
	if(IsWrite)
	{
	 auto* t = &Timers[((A >> 1) & 0x1F) - 0x0C];
	 uint16 tmp = (t->Control << 8);
	 tmp = (tmp &~ mask) | ((DBV << shift) & mask);
	 t->Control = (tmp >> 8) & 0x7;

	 if(!shift)
	  t->Reload = DBV & 0xFF;

	 //printf("Timer(%zu-byte) %u: %04x --- %02x\n", sizeof(T), ((A >> 1) & 0x1F) - 0x0C, DBV, t->Counter);
	}
	else
	 DBV = 0;
	break;

   case 0x0F:	// SCIEB (R/W)
	if(IsWrite)
	{
	 SCIEB = (SCIEB &~ mask) | ((DBV << shift) & mask & 0x7FF);
	 RecalcSoundInt();
	}
	else
	 DBV = (SCIEB & mask) >> shift;
	break;

   case 0x10:	// SCIPD (R) (b5 can be written, like MCIPD)
	if(IsWrite)
	{
	 SCIPD |= ((DBV << shift) & mask & 0x020);
	 RecalcSoundInt();
	}
	else
	 DBV = (SCIPD & mask) >> shift;
	break;

   case 0x11: 	// SCIRE (W)
	if(IsWrite)
	{
	 SCIPD &= ~((DBV << shift) & mask);
	 RecalcSoundInt();
	}
	else
	 DBV = 0;
	break;

   case 0x12:	// SCILV0 (W)
   case 0x13:	// SCILV1 (W)
   case 0x14:	// SCILV2 (W)
	if(IsWrite)
	{
	 const unsigned index = ((A >> 1) & 0x1F) - 0x12;

	 SCILV[index] = (SCILV[index] &~ mask) | ((DBV << shift) & mask & 0x00FF);
	 RecalcSoundInt();
	}
	else
	 DBV = 0;
	break;

   case 0x15:	// MCIEB (W)
	if(IsWrite)
	{
	 MCIEB = (MCIEB &~ mask) | ((DBV << shift) & mask & 0x7FF);
	 RecalcMainInt();
	}
	else
	 DBV = 0;
	break;

   case 0x16:	// MCIPD (R) (when b5=1 is written, set corresponding bit to 1; writing 0 has no apparent effect)
	if(IsWrite)
	{
	 MCIPD |= ((DBV << shift) & mask & 0x020);
	 RecalcMainInt();
	}
	else
	 DBV = (MCIPD & mask) >> shift;
	break;

   case 0x17:	// MCIRE (W)
	if(IsWrite)
	{
	 MCIPD &= ~((DBV << shift) & mask);
	 RecalcMainInt();
	}
	else
	 DBV = 0;
	break;

   case 0x18:
   case 0x19:
   case 0x1A:
   case 0x1B:
   case 0x1C:
   case 0x1D:
   case 0x1E:
   case 0x1F:
	if(IsWrite)
	{
	}
	else
	 DBV = 0;
	break;
  }

  return;
 }

 if(A >= 0x600 && A <= 0x67F)
 {
  //
  // Sound stack data
  //
  ne16_rwbo_be<T, IsWrite>(SoundStack, A & 0x7F, &DBV);

  return;
 }

 if(A >= 0x700 && A <= 0x77F)
 {
  //
  // DSP coefficients
  //
  const unsigned index = (A & 0x7F) >> 1;
  unsigned mask = 0xFFFF;
  unsigned shift = 0;

  if(sizeof(T) == 1)
  {
   shift = ((A & 1) ^ 1) << 3;
   mask = 0xFF << shift;
  }

  if(IsWrite)
   DSP.COEF[index] = (((DSP.COEF[index] << 3) &~ mask) | ((DBV << shift) & mask)) >> 3;
  else
   DBV = ((DSP.COEF[index] << 3) & mask) >> shift;

  return;
 }

 if(A >= 0x780 && A <= 0x7BF)
 {
  //
  // DSP memory addresses
  //
  ne16_rwbo_be<T, IsWrite>(DSP.MADRS, A & 0x3F, &DBV);

  return;
 }

 if(A >= 0x800 && A <= 0xBFF)
 {
  //
  // DSP microprogram
  //
  ne64_rwbo_be<T, IsWrite>(DSP.MPROG, A & 0x3FF, &DBV);

  if(IsWrite)
   DSP.MPROG_Dirty = true;

  return;
 }

 //
 // DSP work buffer
 //
 if(A >= 0xC00 && A <= 0xDFF)
 {
  const unsigned index = (A & 0x1FF) >> 2;
  unsigned mask;
  unsigned shift = (A & 2) ? 8 : 0;

  if(sizeof(T) == 1)
  {
   shift += ((A & 1) ^ 1) << 3;
   mask = 0xFF << shift;
  }
  else
   mask = 0xFFFF << shift;

  if(!(A & 2))
   mask &= 0xFF;

  if(IsWrite)
   DSP.TEMP[index] = (DSP.TEMP[index] &~ mask) | ((DBV << shift) & mask & 0xFFFFFF);
  else
   DBV = (DSP.TEMP[index] & mask) >> shift;

  return;
 }

 //
 // DSP memory read stack
 //
 if(A >= 0xE00 && A <= 0xE7F)
 {
  const unsigned index = (A & 0x7F) >> 2;
  unsigned mask;
  unsigned shift = (A & 2) ? 8 : 0;

  if(sizeof(T) == 1)
  {
   shift += ((A & 1) ^ 1) << 3;
   mask = 0xFF << shift;
  }
  else
   mask = 0xFFFF << shift;

  if(!(A & 2))
   mask &= 0xFF;

  if(IsWrite)
   DSP.MEMS[index] = (DSP.MEMS[index] &~ mask) | ((DBV << shift) & mask & 0xFFFFFF);
  else
   DBV = (DSP.MEMS[index] & mask) >> shift;

  return;
 }

 //
 // DSP mix stack
 //
 if(A >= 0xE80 && A <= 0xEBF)
 {
  const unsigned index = (A & 0x3F) >> 2;
  unsigned mask;
  unsigned shift = (A & 2) ? 4 : 0;

  if(sizeof(T) == 1)
  {
   shift += ((A & 1) ^ 1) << 3;
   mask = 0xFF << shift;
  }
  else
   mask = 0xFFFF << shift;

  if(!(A & 2))
   mask &= 0x0F;

  if(IsWrite)
   DSP.MIXS[index] = (DSP.MIXS[index] &~ mask) | ((DBV << shift) & mask & 0xFFFFFF);
  else
   DBV = (DSP.MIXS[index] & mask) >> shift;

  return;
 }

 //
 //
 //
 if(A >= 0xEC0 && A <= 0xEDF)
 {
  ne16_rwbo_be<T, IsWrite>(DSP.EFREG, A & 0x1F, &DBV);

  return;
 }

 if(A >= 0xEE0 && A <= 0xEE3)
 {
  if(!IsWrite)
   DBV = ne16_rbo_be<T>(EXTS, A & 0x3);

  return;
 }

 if(IsWrite)
  SS_DBG(SS_DBG_WARNING | SS_DBG_SCSP, "[SCSP] Unknown %zu-byte write of value 0x%08x to register address 0x%03x\n", sizeof(T), DBV, A);
 else
 {
  SS_DBG(SS_DBG_WARNING | SS_DBG_SCSP, "[SCSP] Unknown %zu-byte read from register address 0x%03x\n", sizeof(T), A);
  DBV = 0;
 }
}


//
// Gate bit only forces the value to 0, the read still seems to occur(or at the very least timing side effects occur for the case of reg->mem).
//
inline void SS_SCSP::RunDMA(void)
{
 if(!DMA_Execute)
  return;

 uint32 length = DTLG;
 uint32 mem_addr = DMEA;
 uint32 reg_addr = DRGA;
 bool dir = DMA_Direction;
 bool gate = DMA_Gate;

 while(length)
 {
  if(dir)
  {
   uint16 tmp;

   RW<uint16, false>(0x100000 | (reg_addr << 1), tmp);

   if(gate)
    tmp = 0;

   if(MDFN_LIKELY(mem_addr < 0x40000))
    RAM[mem_addr] = tmp;
  }
  else
  {
   uint16 tmp = RAM[mem_addr];

   if(gate)
    tmp = 0;

   RW<uint16, true>(0x100000 | (reg_addr << 1), tmp);
  }

  reg_addr = (reg_addr + 1) & 0x000007FF;
  mem_addr = (mem_addr + 1) & 0x0007FFFF;
  length = length - 1;
 }

 DMA_Execute = false;
 SCIPD |= 0x10;
 MCIPD |= 0x10;
 RecalcSoundInt();
 RecalcMainInt();
}


INLINE void SS_SCSP::RunEG(Slot* s, const unsigned key_eg_scale, const uint32 sc, const uint32 scxc)
{
 const uint32 EnvLevelCache = s->EnvLevel;
 const unsigned ERateNoScale = s->EnvRates[s->EnvPhase];
 const unsigned ERate = std::min<unsigned>(0x1F, key_eg_scale + ERateNoScale);
 const unsigned ERateWBT = (0x18 - std::min<unsigned>(0x18, ERate)) >> 1;
 const bool ShiftAdj = (ERate < 0x18) & ERate;
 bool ClockEG;

 //
 // The equivalent using SampleCounter instead of SampleCounterXC would be like:
 //
 //	When ShiftAdj is 0, look for a 1 at bit position ERateWBT, and lower bits must be 0, unless ERateWBT is 0, in which case
 //	bit0 must be 0 instead of 1.
 //
 //	When ShiftAdj is 1, look for a 1 at bit position ERateWBT + 1 or(inclusive) bit position ERateWBT + 2, and lower bits
 //	must be 0.
 //
 ClockEG = ((scxc | (scxc >> ShiftAdj)) >> (ERateWBT + ShiftAdj)) & (bool)ERateNoScale;

 if(s->EnvPhase == ENV_PHASE_ATTACK && (ERateNoScale + key_eg_scale) >= 0x20)
 {
  ClockEG = false;

  if(EnvLevelCache)
  {
   SS_DBG(SS_DBG_WARNING | SS_DBG_SCSP, "[SCSP] Slot %u stuck in EG attack phase @ EGL=0x%03x: AR=0x%02x, KRS=0x%01x, OCT=0x%01x\n", (unsigned)(s - Slots), EnvLevelCache, ERateNoScale, s->KRS, s->Octave);
  }
 }

 if(ClockEG)
 {
  const int32 inc_base = (s->EnvPhase == ENV_PHASE_ATTACK) ? ~EnvLevelCache : 16;
  const unsigned ermaxo = std::max<unsigned>(0x18, std::min<unsigned>(0x1E, ERate));
  const uint32 srac = ((0x20 - ermaxo) >> 1) + (ermaxo & 1 & (sc >> (ERateWBT + 1)));
  int32 NewEnvLevel;

  NewEnvLevel = EnvLevelCache + (inc_base >> srac);

  if(NewEnvLevel > 0x3FF)
   NewEnvLevel = 0x3FF;

  if(NewEnvLevel < 0)
   NewEnvLevel = 0;

  s->EnvLevel = NewEnvLevel;
 }
 //
 // Using EnvLevelCache instead of s->EnvLevel here is intentional:
 //
 if(s->EnvPhase == ENV_PHASE_DECAY1)
 {
  if((EnvLevelCache >> 5) == s->DecayLevel)
   s->EnvPhase = ENV_PHASE_DECAY2;
 }
 else if(s->EnvPhase == ENV_PHASE_ATTACK)
 {
  if((s->AttackLoopLink && s->InLoop) || (!s->AttackLoopLink && EnvLevelCache == 0))
   s->EnvPhase = ENV_PHASE_DECAY1;
 }
}

//
// Take care in handling LFSR, or else the volume of noise-ALFO-modulated noise will
// be quite off, or have a DC bias.
//
INLINE uint8 SS_SCSP::GetALFO(Slot* s)
{
 uint8 ret;

 switch(s->ALFOWaveform)
 {
  default:
  case 0:	// Saw
	ret = s->LFOCounter &~ 1;
	break;

  case 1:	// Square
	ret = ((int8)s->LFOCounter >> 7) &~ 1;
	break;

  case 2:	// Triangle
	ret = (unsigned)(s->LFOCounter ^ ((int8)s->LFOCounter >> 7)) << 1;
	break;

  case 3:	// Noise
	ret = LFSR &~ 1;
	break;
 }

 ret >>= (7 - s->ALFOModLevel);

 if(!s->ALFOModLevel)
  ret = 0;

 return ret;
}

INLINE int SS_SCSP::GetPLFO(Slot* s)
{
 int ret;

 switch(s->PLFOWaveform)
 {
  default:
  case 0:	// Saw
	ret = (int8)(s->LFOCounter &~ 1);
	break;

  case 1:	// Square
	ret = (int8)((s->LFOCounter & 0x80) ? 0x80 : 0x7E);
	break;

  case 2:	// Triangle
	ret = (int8)(((s->LFOCounter & 0x3F) ^ ((s->LFOCounter & 0x40) ? 0x3F : 0x00) ^ ((s->LFOCounter & 0x80) ? 0x7F : 0x00)) << 1);
	break;

  case 3:	// Noise
	ret = (int8)(LFSR &~ 1);
	break;
 }

 ret >>= (7 - s->PLFOModLevel);

 if(!s->PLFOModLevel)
  ret = 0;

 ret = ((0x40 ^ (s->FreqNum >> 4)) * ret) >> 6;

 return ret;
}

INLINE void SS_SCSP::RunLFO(Slot* s)
{
 s->LFOTimeCounter--;
 if(!s->LFOTimeCounter)
 {
  s->LFOCounter++;
  s->LFOTimeCounter = (((8 - (s->LFOFreq & 0x3)) << 7) >> (s->LFOFreq >> 2)) - 4;
 }

 if(s->LFOReset)
  s->LFOCounter = 0;
}

//
//
//
#ifdef MDFN_SS_SCSP_DSP_DYNAREC
 #include "scsp_dsp_dynarec.inc"
#else

static INLINE uint32 dspfloat_to_int(const uint16 inv)
{
 const uint32 sign_xor = (int32)((inv & 0x8000) << 16) >> 1;
 const uint32 exp = (inv >> 11) & 0xF;
 uint32 ret;

 ret = inv & 0x7FF;
 if(exp < 12)
  ret |= 0x800;
 ret <<= 11 + 8;
 ret ^= sign_xor;
 ret = (int32)ret >> (8 + std::min<unsigned>(11, exp));

 return ret & 0xFFFFFF;
}

static INLINE uint32 int_to_dspfloat(const uint32 inv)
{
 const uint32 invsl8 = inv << 8;
 const uint32 sign_xor = (int32)invsl8 >> 31;
 uint32 exp, shift;
 uint32 ret;

 exp = MDFN_lzcount32(((invsl8 ^ sign_xor) << 1) | (1 << 19));
 shift = exp - (bool)(exp == 12); //std::min<uint32>(11, exp);

 ret = (int32)invsl8 >> (19 - shift);
 ret &= 0x87FF;
 ret |= exp << 11;

 return ret;
}

INLINE void SS_SCSP::RunDSP(void)
{
 //
 //
 // Instruction field order/width RE'ing notes:
 //
 // Bit     0: NXADDR
 // Bit     1: ADRGB
 // Bit   2-6: MASA
 // Bit     8: NOFL (disables floating-point conversion when =1, instead just shifting by 8); has effect with MRT=1 or MWT=1
 // Bit  9-14: CRA (Coefficient read address, input into Y_SEL)
 // Bit    16: BSEL
 // Bit    17: ZERO
 // Bit    18: NEGB (apparently no effect when ZERO=1)
 // Bit    19: YRL
 // Bit    20: SHFT0
 // Bit    21: SHFT1
 // Bit    22: FRCL
 // Bit    23: ADRL (latches A_SEL output into ADRS_REG)
 // Bit 24-27: EWA(EFREG write address)
 // Bit    28: EWT(EFREG write enable)
 // Bit    29: MRT  (Memory read trigger; to read: [MWR=1] [whatever instruction] [IWT=1]
 // Bit    30: MWT  (Memory write trigger)
 // Bit    31: TABLE
 // Bit 32-36: IWA (MEMS write address)
 // Bit    37: IWT (MEMS write trigger)
 // Bit 38-43: IRA (0x00-0x1F MEMS, 0x20-0x2F MIXS)
 // Bit 45-46: YSEL
 // Bit    47: XSEL
 // Bit 48-54: TWA(temp write address) Seems to be an offset added to a counter changed each sample.
 // Bit    55: TWT(temp write trigger)  WARNING: Setting this to 1 for all 128 steps apparently can cause a CPU to freeze up if it tries to read/write TEMP afterward.
 // Bit 56-62: TRA(temp read address) 
 for(unsigned step = 0; step < 128; step++)
 {
  const uint64 instr = DSP.MPROG[step];

/*
  assert(!(instr & (1ULL << 7)));
  assert(!(instr & (1ULL << 15)));
  assert(!(instr & (1ULL << 44)));
  assert(!(instr & (1ULL << 63)));
*/

  const bool NXADDR = (instr >> 0) & 1;
  const bool ADRGB = (instr >> 1) & 1;
  const unsigned MASA = (instr >> 2) & 0x1F;
  const bool NOFL = (instr >> 8) & 1;
  const unsigned CRA = (instr >> 9) & 0x3F;
  const bool BSEL = (instr >> 16) & 1;
  const bool ZERO = (instr >> 17) & 1;
  const bool NEGB = (instr >> 18) & 1;
  const bool YRL = (instr >> 19) & 1;
  const bool SHFT0 = (instr >> 20) & 1;
  const bool SHFT1 = (instr >> 21) & 1;
  const bool FRCL = (instr >> 22) & 1;  
  const bool ADRL = (instr >> 23) & 1;
  const unsigned EWA = (instr >> 24) & 0x0F;
  const bool EWT = (instr >> 28) & 1;
  const bool MRT = (instr >> 29) & 1;
  const bool MWT = (instr >> 30) & 1;
  const bool TABLE = (instr >> 31) & 1;
  const unsigned IWA = (instr >> 32) & 0x1F;
  const bool IWT = (instr >> 37) & 1;
  const unsigned IRA = (instr >> 38) & 0x3F;
  const unsigned YSEL = (instr >> 45) & 0x03;
  const bool XSEL = (instr >> 47) & 1;
  const unsigned TEMPWriteAddr = ((instr >> 48) + DSP.MDEC_CT) & 0x7F;
  const bool TWT = (instr >> 55) & 1;
  const unsigned TEMPReadAddr = ((instr >> 56) + DSP.MDEC_CT) & 0x7F;

#if 0
  if(!(step & 1) && (MWT || MRT))
   SS_DBG(SS_DBG_WARNING | SS_DBG_SCSP, "[SCSP] Memory access requested at even DSP step %u; 0x%016llx\n", step, instr);

  if(MWT & MRT)
   SS_DBG(SS_DBG_WARNING | SS_DBG_SCSP, "[SCSP] MWT and MRT both 1 at DSP step %u; 0x%016llx\n", step, instr);
#endif
  //
  //
  if(IRA & 0x20)
  {
   if(IRA & 0x10)
   {
    if(!(IRA & 0xE))
     DSP.INPUTS = EXTS[IRA & 0x1] << 8;
   }
   else
   {
    DSP.INPUTS = DSP.MIXS[IRA & 0xF] << 4;
   }
  }
  else
  {
   DSP.INPUTS = DSP.MEMS[IRA & 0x1F];
  }

  const int32 INPUTS = sign_x_to_s32(24, DSP.INPUTS);
  const uint16 Y_SEL_Inputs[4] = { DSP.FRC_REG, DSP.COEF[CRA], (uint16)((DSP.Y_REG >> 11) & 0x1FFF), (uint16)((DSP.Y_REG >> 4) & 0x0FFF) };
  //
  //
  //
  if(YRL)
  {
   DSP.Y_REG = INPUTS & 0xFFFFFF;
  }
  //
  //
  //
  int32 ShifterOutput = (uint32)sign_x_to_s32(26, DSP.SFT_REG) << (SHFT0 ^ SHFT1);

  if(!SHFT1)
  {
   if(ShifterOutput > 0x7FFFFF)
    ShifterOutput = 0x7FFFFF;
   else if(ShifterOutput < -0x800000)
    ShifterOutput = 0x800000;
  }
  ShifterOutput &= 0xFFFFFF;
  //
  //
  if(FRCL)
  {
   const unsigned F_SEL_Inputs[2] = { (unsigned)(ShifterOutput >> 11), (unsigned)(ShifterOutput & 0xFFF) };

   DSP.FRC_REG = F_SEL_Inputs[SHFT0 & SHFT1];
   //printf("FRCL: 0x%08x\n", DSP.FRC_REG);
  }
  //
  //
  {
   const int32 TEMP = sign_x_to_s32(24, DSP.TEMP[TEMPReadAddr]);
   const uint32 SGA_Inputs[2] = { (uint32)TEMP, DSP.SFT_REG };
   const int32 X_SEL_Inputs[2] = { TEMP, INPUTS };
   const uint32 Product = ((int64)sign_x_to_s32(13, Y_SEL_Inputs[YSEL]) * X_SEL_Inputs[XSEL]) >> 12;
   uint32 SGAOutput;

   SGAOutput = SGA_Inputs[BSEL];

   if(NEGB)
    SGAOutput = -SGAOutput;

   if(ZERO)
    SGAOutput = 0;

   DSP.SFT_REG = (Product + SGAOutput) & 0x3FFFFFF;
  }
  //
  //
  if(EWT)
   DSP.EFREG[EWA] = (ShifterOutput >> 8);

  if(TWT)
   DSP.TEMP[TEMPWriteAddr] = ShifterOutput;

  if(IWT)
  {
   DSP.MEMS[IWA] = DSP.ReadValue;
  }
  //
  //
  if(DSP.ReadPending)
  {
   uint16 tmp = RAM[DSP.RWAddr];
   DSP.ReadValue = (DSP.ReadPending == 2) ? (tmp << 8) : dspfloat_to_int(tmp);
   DSP.ReadPending = false;
  }
  else if(DSP.WritePending)
  {
   if(!(DSP.RWAddr & 0x40000))
    RAM[DSP.RWAddr] = DSP.WriteValue;

   DSP.WritePending = false;
  }

  {
   uint16 addr;

   addr = DSP.MADRS[MASA];
   addr += NXADDR;

   if(ADRGB)
   {
    addr += sign_x_to_s32(12, DSP.ADRS_REG);
   }

   if(!TABLE)
   {
    addr += DSP.MDEC_CT;
    addr &= (0x2000 << RBL) - 1;
   }

   DSP.RWAddr = (addr + (RBP << 12)) & 0x7FFFF;

   if(MRT)
   {
    DSP.ReadPending = 1 + NOFL;
   }
   if(MWT)
   {
    DSP.WritePending = true;
    DSP.WriteValue = NOFL ? (ShifterOutput >> 8) : int_to_dspfloat(ShifterOutput);
   }
  }
  //
  //
  if(ADRL)
  {
   const uint16 A_SEL_Inputs[2] = { /*INPUTS is sign-extended above */ (uint16)((INPUTS >> 16) & 0xFFF), (uint16)(ShifterOutput >> 12) };

   DSP.ADRS_REG = A_SEL_Inputs[SHFT0 & SHFT1];
  }
 }

 if(!DSP.MDEC_CT)
  DSP.MDEC_CT = (0x2000 << RBL);
 DSP.MDEC_CT--;
}
#endif
//
//
//
template<typename T_out>
INLINE void SS_SCSP::RunSample(T_out* outlr, void (*midi_out)(uint8))
{
 const uint32 SampleCounter = GlobalCounter >> 5;
 const uint32 SampleCounterXC = (SampleCounter ^ (SampleCounter - 1)) & (SampleCounter ^ 1);
 int32 out_accum[2] = { 0, 0 };

 MIDI_Run(midi_out);

 for(unsigned i = 0; i < 3; i++)
 {
  auto* t = &Timers[i];
  const bool DoClock = !(SampleCounter & ((1U << t->Control) - 1));

  if(DoClock)
  {
   if(t->Reload >= 0)
   {
    t->Counter = t->Reload;
    t->Reload = -1;
   }
   else
    t->Counter++;

   if(t->Counter == 0xFF)
   {
    SCIPD |= 0x40 << i;
    MCIPD |= 0x40 << i;
   }
  }
 }

 SCIPD |= 0x400;
 MCIPD |= 0x400;
 RecalcSoundInt();
 RecalcMainInt();

 //
 //
 //
 RunDSP();

 for(unsigned i = 0; i < 0x10; i++)
  DSP.MIXS[i] = 0;
 //
 //
 //
 for(unsigned slot = 0; slot < 32; slot++)
 {
  auto* s = &Slots[slot];
  unsigned key_eg_scale;

  s->WFAllowAccess &= (s->EnvLevel < 0x3C0 || s->EGBypass);
  //
  //
  if(s->KRS == 0xF)
   key_eg_scale = 0x00;
  else
   key_eg_scale = std::max<int>(0x00, std::min<int>(0x0F, s->KRS + (s->Octave ^ 0x8) - 0x8));

  RunEG(s, key_eg_scale, SampleCounter, SampleCounterXC);

  if(KeyExecute && (s->EnvPhase == ENV_PHASE_RELEASE) == s->KeyBit)
  {
   if(s->KeyBit)
   {
    s->PhaseWhacker = 0;
    s->CurrentAddr = 0;
    s->InLoop = false;
    s->LoopSub = false;
    s->WFAllowAccess = true;
    s->EnvPhase = ENV_PHASE_ATTACK;

    if((s->EnvRates[ENV_PHASE_ATTACK] + key_eg_scale) >= 0x20)
     s->EnvLevel = 0x000;
    else
     s->EnvLevel = 0x280;
   }
   else
    s->EnvPhase = ENV_PHASE_RELEASE;
  }
  //
  //
  if(!s->InLoop)
  {
   if((uint16)(s->CurrentAddr + 1) > s->LoopStart)
   {
    if(s->LoopMode == 2)
    {
     s->CurrentAddr += -(s->LoopStart + s->LoopEnd);
     s->LoopSub = true;
    }

    s->InLoop = true;
   }
  }
  else
  {
   const uint16 ca = 1 + (s->LoopSub ? ~s->CurrentAddr : s->CurrentAddr);
   const uint16 comp = (s->LoopSub && (s->LoopMode & 0x2)) ? s->LoopStart : s->LoopEnd;

   if(s->LoopSub ^ (ca > comp))
   {
    if(s->LoopMode == 0)
     s->WFAllowAccess = false;
    
    if(s->LoopMode == 3)
    {
     s->LoopSub = !s->LoopSub;
     if(s->LoopSub)
      s->CurrentAddr -= s->LoopEnd << 1;
     else
      s->CurrentAddr += s->LoopStart << 1;
    }
    else
    {
     if(s->LoopSub && !(s->LoopMode & 0x2))
      s->CurrentAddr += s->LoopEnd - s->LoopStart;
     else
      s->CurrentAddr += s->LoopStart - s->LoopEnd;
    }
   }
  }
 }

 for(unsigned slot = 0; slot < 32; slot++)
 {
  auto* s = &Slots[slot];
  uint32 mdata = 0;
  uint16 sample = 0;

  if(s->SourceControl == 1)
   sample = LFSR << 8;

  sample ^= s->SBXOR;	// For zero and noise case only; waveform playback needs it to occur before linear interpolation.
  //
  //
  if(s->WFAllowAccess)
  {
   uint32 modalizer_int[2];
   uint32 tmppw = s->PhaseWhacker;
   uint16 tmpa = s->CurrentAddr;
   //
   //
   if(s->LoopSub)
   {
    tmppw = ~tmppw;
    tmpa = ~tmpa;
   }

   mdata |= ((tmpa >> 12) << 7);
   //
   //
   uint32 sia;
   int16 s0, s1;

   {
    //
    // TODO/FIXME: Proper handling of the slot 31->0 buggy FM interpolation case with respect to reverse looping(ns->LoopSub)
    // requires sub-sample timing emulation.
    //
    auto* ns = &Slots[(slot + 1) & 0x1F];
    uint32 modalizer;
    uint32 ns_sia;

    modalizer  = (int16)SoundStack[(GlobalCounter + s->ModInputX) & 0x3F];
    modalizer += (int16)SoundStack[(GlobalCounter + s->ModInputY) & 0x3F];
    modalizer = ((modalizer << 6) >> (0x10 - s->ModLevel)) & ~1;

    if(s->ModLevel <= 0x04)
     modalizer = 0;

    sia = modalizer + ((tmppw >> (14 - 6)) & 0x3F);
    ns_sia = modalizer + (((ns->PhaseWhacker >> (14 - 6)) ^ (ns->LoopSub ? 0x3F : 0x00)) & 0x3F);

    modalizer_int[0] = sign_x_to_s32(11,    sia >> 6);
    modalizer_int[1] = sign_x_to_s32(11, ns_sia >> 6);
    //
    sia &= 0x3F;
   }

   if(s->WF8Bit)
   {
    const uint32 addr0 = (s->StartAddr + ((modalizer_int[0] + (uint16)(tmpa + 0)) & s->ShortWaveMask)) & 0xFFFFF;
    const uint32 addr1 = (s->StartAddr + ((modalizer_int[1] + (uint16)(tmpa + 1)) & s->ShortWaveMask)) & 0xFFFFF;

    s0 = ne16_rbo_be<uint8>(RAM, addr0) << 8;
    s1 = ne16_rbo_be<uint8>(RAM, addr1) << 8;
   }
   else
   {
    s0 = RAM[((s->StartAddr >> 1) + ((modalizer_int[0] + (uint16)(tmpa + 0)) & s->ShortWaveMask)) & 0x7FFFF];
    s1 = RAM[((s->StartAddr >> 1) + ((modalizer_int[1] + (uint16)(tmpa + 1)) & s->ShortWaveMask)) & 0x7FFFF];
   }

   s0 ^= s->SBXOR;
   s1 ^= s->SBXOR;

   if(s->SourceControl == 0)
   {
    sample = ((s0 * (0x40 - sia)) + (s1 * sia)) >> 6;
   }

   s->PhaseWhacker += (((0x400 ^ s->FreqNum) + GetPLFO(s)) << (s->Octave ^ 0x8)) >> 4;
   s->CurrentAddr += s->PhaseWhacker >> 14;
   s->PhaseWhacker &= (1U << 14) - 1;
  }
  //
  //

  RunLFO(s);	// Run between PLFO fetching and ALFO fetching.

  // Do LFSR clocking between sample fetching and ALFO fetching.
  LFSR = (LFSR >> 1) | (((LFSR >> 5) ^ LFSR) & 1) << 16;

  
  {
   int32 vlevel;

   vlevel = ((s->EnvPhase == ENV_PHASE_ATTACK && s->AttackHold) || s->EGBypass) ? 0 : s->EnvLevel;
   //
   mdata |= (s->EnvPhase << 5) | (vlevel >> 5);
   //
   if(!s->SoundDirect)
   {
    vlevel += s->TotalLevel << 2;
    vlevel += GetALFO(s);

    if(vlevel > 0x3FF)
     vlevel = 0x3FF;

    sample = ((int16)sample * ((vlevel & 0x3F) ^ 0x7F)) >> ((vlevel >> 6) + 7);
   }
  }

  if(!Slots[(GlobalCounter - 4) & 0x1F].StackWriteInhibit)
  {
   SoundStack[(GlobalCounter - 4) & 0x3F] = SoundStackDelayer[3];
  }

  SoundStackDelayer[3] = SoundStackDelayer[2];
  SoundStackDelayer[2] = SoundStackDelayer[1];
  SoundStackDelayer[1] = SoundStackDelayer[0];
  SoundStackDelayer[0] = sample;
  //
  //
  if(SlotMonitorWhich == slot)
   SlotMonitorData = mdata;
  //
  //
  if(s->ToDSPLevel)
   DSP.MIXS[s->ToDSPSelect] = (DSP.MIXS[s->ToDSPSelect] + (((uint32)(int16)sample << 4) >> (7 - s->ToDSPLevel))) & 0xFFFFF;
  //
  //
  out_accum[0] += ((int16)sample * s->DirectVolume[0]) >> 14;
  out_accum[1] += ((int16)sample * s->DirectVolume[1]) >> 14;

  {
   const uint16 eff_sample = (slot & 0x10) ? ((slot & 0xE) ? 0 : EXTS[slot & 0x1]) : DSP.EFREG[slot];

   out_accum[0] += ((int16)eff_sample * s->EffectVolume[0]) >> 14;
   out_accum[1] += ((int16)eff_sample * s->EffectVolume[1]) >> 14;
  }
  //
  //
  GlobalCounter++;
 }

 KeyExecute = false;

 //
 //
 //
 out_accum[0] = (out_accum[0] * MasterVolume) >> 8;
 out_accum[1] = (out_accum[1] * MasterVolume) >> 8;

 out_accum[0] = std::min<int32>(32767, std::max<int32>(-32768, out_accum[0]));
 out_accum[1] = std::min<int32>(32767, std::max<int32>(-32768, out_accum[1]));

 if(DAC18bit)
 {
  // Doesn't seem to improve precision.
  out_accum[0] = (uint32)out_accum[0] << 2;
  out_accum[1] = (uint32)out_accum[1] << 2;
 }

 outlr[0] = out_accum[0];
 outlr[1] = out_accum[1];
}

//
//
//
//
//
//
inline uint32 SS_SCSP::GetRegister(const unsigned id, char* const special, const uint32 special_len)
{
 uint32 ret = 0xDEADBEEF;

 switch(id)
 {
  case GSREG_MVOL:
	ret = MVOL;
	break;

  case GSREG_DAC18B:
	ret = DAC18bit;
	break;

  case GSREG_MEM4MB:
	ret = Mem4Mb;
	break;

  case GSREG_RBC:
	ret = RBP | (RBL << 7);
	break;

  case GSREG_MSLC:
	ret = SlotMonitorWhich;
	break;

  case GSREG_SCIEB:
	ret = SCIEB;
	break;

  case GSREG_SCIPD:
	ret = SCIPD;
	break;

  case GSREG_MCIEB:
	ret = MCIEB;
	break;

  case GSREG_MCIPD:
	ret = MCIPD;
	break;

  case GSREG_EFREG0 ... GSREG_EFREGF:
	ret = DSP.EFREG[id - GSREG_EFREG0];
	break;
 }

 return ret;
}

inline void SS_SCSP::SetRegister(const unsigned id, const uint32 value)
{
 switch(id)
 {
  //case GSREG_MVOL: MVOL = value & 0xF; // TODO cache

  case GSREG_DAC18B:
	DAC18bit = value & 1;
	break;

  case GSREG_MEM4MB:
	Mem4Mb = value & 1;
	break;

  case GSREG_RBC:
	RBP = value & 0x7F;
	RBL = (value >> 7) & 0x3;
	break;

  case GSREG_MSLC:
	SlotMonitorWhich = value & 0x1F;
	break;

  case GSREG_EFREG0 ... GSREG_EFREGF:
	DSP.EFREG[id - GSREG_EFREG0] = (uint16)value;
	break;
 }
}


#pragma pop_macro("INLINE")
#pragma pop_macro("NO_INLINE")
#pragma pop_macro("MDFN_COLD")
#pragma pop_macro("MDFN_LIKELY")
#pragma pop_macro("MDFN_UNLIKELY")
#pragma pop_macro("SS_DBG")
#pragma pop_macro("SCSP_SoundIntChanged")
#pragma pop_macro("SCSP_MainIntChanged")
#pragma pop_macro("sign_x_to_s32")
} // namespace scsp_core
