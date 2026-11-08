// minimal CLAP host for testing: clap_host plugin.clap seconds out.wav note...
#include "entry.h"
#include "plugin-factory.h"
#include "plugin.h"
#include "events.h"
#include "process.h"
#include "audio-buffer.h"
#include "host.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cstdint>
#include <cmath>
static const void* host_get_extension(const clap_host_t*, const char*) { return nullptr; }
static void host_nop(const clap_host_t*) {}
static clap_host_t host = { CLAP_VERSION, nullptr, "testhost", "x", "", "1", host_get_extension, host_nop, host_nop, host_nop };
struct Ev { uint32_t t; int type; int key; double v; };
struct InEvents { std::vector<Ev> evs; std::vector<clap_event_note_t> notes; };
static uint32_t in_size(const clap_input_events_t* l) { return (uint32_t)((InEvents*)l->ctx)->notes.size(); }
static const clap_event_header_t* in_get(const clap_input_events_t* l, uint32_t i) { return &((InEvents*)l->ctx)->notes[i].header; }
static bool out_push(const clap_output_events_t*, const clap_event_header_t*) { return true; }
static void wav(const char* fn, const std::vector<int16_t>& d) { FILE* f = fopen(fn, "wb"); uint32_t b = d.size() * 2, r = 44100, br = r * 4, s16 = 16, rf = 36 + b; uint16_t ch = 2, ba = 4, bt = 16, fm = 1; fwrite("RIFF", 1, 4, f); fwrite(&rf, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&s16, 4, 1, f); fwrite(&fm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&r, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bt, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&b, 4, 1, f); fwrite(d.data(), 2, d.size(), f); fclose(f); }
int main(int argc, char** argv) {
  if (argc < 4) { puts("usage: clap_host plugin.clap seconds out.wav [key:startSec:lenSec ...]"); return 1; }
  void* h = dlopen(argv[1], RTLD_NOW); if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
  auto* entry = (const clap_plugin_entry_t*)dlsym(h, "clap_entry"); if (!entry || !entry->init(argv[1])) { puts("entry failed"); return 1; }
  auto* fac = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
  auto* desc = fac->get_plugin_descriptor(fac, 0); printf("plugin: %s (%s)\n", desc->name, desc->id);
  auto* pl = fac->create_plugin(fac, &host, desc->id); if (!pl || !pl->init(pl)) { puts("plugin init failed"); return 1; }
  const double sr = 44100; const int blk = 256; pl->activate(pl, sr, 32, 4096); pl->start_processing(pl);
  const double secs = atof(argv[2]); const int total = (int)(secs * sr);
  struct N { int key; int on, off; }; std::vector<N> ns;
  for (int i = 4; i < argc; i++) { int k; double s, l; if (sscanf(argv[i], "%d:%lf:%lf", &k, &s, &l) == 3) ns.push_back({k, (int)(s * sr), (int)((s + l) * sr)}); }
  std::vector<int16_t> out; std::vector<float> L(blk), R(blk); float* chans[2] = {L.data(), R.data()};
  for (int pos = 0; pos < total; pos += blk) {
    InEvents ie;
    for (auto& n : ns) {
      if (n.on >= pos && n.on < pos + blk) { clap_event_note_t e = {}; e.header = {sizeof e, (uint32_t)(n.on - pos), CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, 0}; e.note_id = n.key; e.port_index = 0; e.channel = 0; e.key = n.key; e.velocity = 0.8; ie.notes.push_back(e); }
      if (n.off >= pos && n.off < pos + blk) { clap_event_note_t e = {}; e.header = {sizeof e, (uint32_t)(n.off - pos), CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_OFF, 0}; e.note_id = n.key; e.channel = 0; e.key = n.key; e.velocity = 0; ie.notes.push_back(e); }
    }
    clap_input_events_t in = {&ie, in_size, in_get}; clap_output_events_t oe = {nullptr, out_push};
    clap_audio_buffer_t ab = {}; ab.channel_count = 2; ab.data32 = chans;
    clap_process_t p = {}; p.frames_count = blk; p.in_events = &in; p.out_events = &oe; p.audio_outputs = &ab; p.audio_outputs_count = 1;
    pl->process(pl, &p);
    for (int i = 0; i < blk; i++) for (int c = 0; c < 2; c++) { float v = chans[c][i] * 32768.f; out.push_back((int16_t)fmaxf(-32768.f, fminf(32767.f, v))); }
  }
  pl->stop_processing(pl); pl->deactivate(pl); pl->destroy(pl);
  wav(argv[3], out); int pk = 0; for (auto s : out) pk = std::max(pk, abs((int)s)); printf("rendered %zu frames, peak %d\n", out.size() / 2, pk);
}
