#!/usr/bin/make -f
# SCSP Synth - DPF plugin build. Set DPF_PATH to a DPF checkout (default: ./dpf).
DPF_PATH ?= dpf
DPF_BUILD_DIR ?= $(CURDIR)/build
DPF_TARGET_DIR ?= $(CURDIR)/bin
NAME = SCSPSynth
FILES_DSP = SCSPSynthPlugin.cpp
include $(DPF_PATH)/Makefile.plugins.mk
BUILD_CXX_FLAGS += -std=gnu++17 -w
TARGETS += clap
TARGETS += lv2_dsp
TARGETS += vst3
all: $(TARGETS)
