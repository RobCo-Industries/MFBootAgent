# MFBootAgent, the second stage of the terminal chain. Built with the
# RobCo-Core library in external/robco-core.
#
#   make                      build/mfboot.img and the test operating images
#   make test                 host tests, then target tests on every board
#   make test BOARD=raspi0    the same, target tests on one board
#   make test-host            host unit tests and the pin check
#   make test-target          QEMU target tests (BOARD=all by default).
#                             RETROS_IMG=<RETROS.IMG> adds the whole-chain tests.
#   make sdimage              build/sd.img: MFBOOT.SYS, BOOT.CFG and the test
#                             operating images. FIRST=<RETROS.IMG> adds a first
#                             stage and its config.txt
#   make run BOARD=raspi2b    boot build/sd.img in QEMU on this terminal
#                             (WINDOW=1 also opens the display window)
#   make check-pins           CI workflow pins and doc links match the submodule
#   make clean

ROBCO_CORE := external/robco-core
IMAGE      := mfboot
LINK_ADDR  := 0x00200000
CONFIG_H   := src/robco_config.h
BUILD      ?= build
PYTHON     ?= python3

# Every src/*.c builds into the image. PURE_SRCS, one per line and sorted,
# have no hardware access: the host tests link them.
PURE_SRCS  := \
              src/boot.c \
              src/bootcfg.c \
              src/keymap.c \
              src/menu.c \
              src/menulines.c \

SRCS       := $(sort $(wildcard src/*.c))

# `make sdimage`. Set before the include: robco.mk reads them. The card
# holds MFBOOT.SYS, the default BOOT.CFG and the test operating images.
OS_BUILD       = $(abspath $(BUILD))/os
SDIMAGE_MIB   ?= 64
SDIMAGE_DEPS  := $(BUILD)/$(IMAGE).img testos
SDIMAGE_FILES := boot/BOOT.CFG:ROBCO/BOOT.CFG $(BUILD)/$(IMAGE).img:ROBCO/MFBOOT.SYS \
                 $(OS_BUILD)/uos/testos.img:ROBCO/UOS.SYS \
                 $(OS_BUILD)/maint/testos.img:ROBCO/MAINT.SYS \
                 $(OS_BUILD)/diag/testos.img:ROBCO/DIAG.SYS

# Host unit tests: tests/host/test_<name>.c with the pure files.
HOST_TEST_DIR := tests/host
HOST_SRCS     := $(PURE_SRCS)
HOST_INCLUDES := src

ifeq ($(wildcard $(ROBCO_CORE)/mk/robco.mk),)
$(error $(ROBCO_CORE) is empty: run git submodule update --init)
endif

.DEFAULT_GOAL := all
include $(ROBCO_CORE)/mk/robco.mk
include $(ROBCO_CORE)/mk/host.mk

# Target tests boot the images built here; keep top-level targets serial.
.NOTPARALLEL:

# ---- Test operating images ------------------------------------------------

# The test OS, built for tests only, once per operating subtype:
# build/os/<kind>/testos.img. Not shipped.
.PHONY: testos
all: testos

testos:
	$(MAKE) --no-print-directory -C tests/os BUILD=$(OS_BUILD)/uos KIND=uos SUBTYPE=0
	$(MAKE) --no-print-directory -C tests/os BUILD=$(OS_BUILD)/maint KIND=maint SUBTYPE=2
	$(MAKE) --no-print-directory -C tests/os BUILD=$(OS_BUILD)/diag KIND=diag SUBTYPE=3

$(OS_BUILD)/%/testos.img: testos
	@test -f $@

# ---- Python environment for the target tests --------------------------

VENV := .venv

.PHONY: venv
venv: $(VENV)/.installed

$(VENV)/.installed: $(ROBCO_CORE)/tests/requirements.txt
	$(PYTHON) -m venv $(VENV)
	$(VENV)/bin/pip install -q -r $<
	touch $@

# ---- Tests ------------------------------------------------------------

BOARD ?= all

.PHONY: test test-target check-pins check-doc-links
test: test-host
	$(MAKE) test-target

test-target: all venv
	$(VENV)/bin/pytest tests -v --board $(BOARD) --build-dir $(BUILD) \
	    $(if $(RETROS_IMG),--retros-img $(abspath $(RETROS_IMG)))

# DOC_ROOT: the tree whose Markdown links check-doc-links reads.
DOC_ROOT ?= .

check-pins: check-doc-links
	$(ROBCO_CORE)/tools/check_pins.sh .github/workflows/ci.yml

check-doc-links:
	$(ROBCO_CORE)/tools/check_doc_links.sh $(DOC_ROOT) external/robco-core

# Host unit tests come from mk/host.mk. The pin check runs first.
test-host: check-pins

# ---- Cards and QEMU -----------------------------------------------------

# FIRST=<RETROS.IMG> puts a first stage on the card as RETROS.IMG, with
# its config.txt, so the card boots the whole chain.
sdimage-stage::
	$(if $(FIRST),$(ROBCO_CORE)/tools/mkbootdir.sh $(FIRST) $(SDIMAGE_STAGE) \
	    --kernel-name RETROS.IMG --no-firmware)

# Alone, a two-word image at 0x8000 jumps to MFBootAgent at its link address.
$(BUILD)/tramp.img:
	@mkdir -p $(dir $@)
	$(PYTHON) -c "import struct, sys; open(sys.argv[1], 'wb').write(struct.pack('<II', 0xE51FF004, $(LINK_ADDR)))" $@

.PHONY: run
comma := ,
RUN_ALONE := $(BUILD)/tramp.img --qemu-arg=-device \
             --qemu-arg=loader$(comma)file=$(BUILD)/$(IMAGE).img$(comma)addr=$(LINK_ADDR)$(comma)force-raw=on
RUN_BOARD := $(if $(filter all,$(BOARD)),raspi2b,$(BOARD))
run: sdimage $(BUILD)/tramp.img
	PYTHONPATH=$(ROBCO_CORE)/tests $(PYTHON) -m harness.run $(if $(FIRST),$(FIRST),$(RUN_ALONE)) \
	    --board $(RUN_BOARD) --sd $(BUILD)/sd.img --serial2 pty $(if $(filter 1,$(WINDOW)),--window)
