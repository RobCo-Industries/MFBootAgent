# MFBootAgent, the second stage of the terminal chain. Built with the
# RobCo-Core library in external/robco-core.
#
#   make                      build/mfboot.img, build/diag.sys, build/maint.sys and the
#                             test images
#   make test                 host tests, then target tests on every board
#   make test BOARD=raspi0    the same, target tests on one board
#   make test-host            host unit tests and the pin check
#   make test-target          QEMU target tests (BOARD=all by default).
#                             TIMING_SKIPS_MAX=<n> allows more host timing skips per board
#                             RETROS_IMG=<RETROS.IMG> adds the whole-chain tests.
#   make sdimage              build/sd.img: MFBOOT.SYS, BOOT.CFG and the test
#                             operating images. FIRST=<RETROS.IMG> adds a first
#                             stage and its config.txt
#   make run BOARD=raspi2b    boot build/sd.img in QEMU on this terminal
#                             (WINDOW=1 also opens the display window)
#   make run-diag             the diagnostics image alone, with the card
#                             (DEVICES=1 adds the I2C chips)
#   make run-maint            the maintenance image alone, with the card
#                             (DEVICES=1 adds the I2C chips)
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
              src/maintscr.c \
              src/menu.c \
              src/menulines.c \
              src/portreq.c \

SRCS       := $(sort $(wildcard src/*.c))

# The diagnostics image's pure files, in diag/: the host tests link them too.
DIAG_PURE_SRCS := \
              diag/dg_fmt.c \
              diag/dg_mem.c \
              diag/dg_menu.c \
              diag/dg_sd.c \

# The maintenance image's pure files, in maint/: the host tests link them too.
MAINT_PURE_SRCS := \
              maint/mt_cfg.c \
              maint/mt_ui.c \
              maint/mt_view.c \
              maint/mt_walk.c \


# `make sdimage`. Set before the include: robco.mk reads them. The card
# holds MFBOOT.SYS, the default BOOT.CFG and the test operating images.
OS_BUILD       = $(abspath $(BUILD))/os
SDIMAGE_MIB   ?= 64
SDIMAGE_DEPS  := $(BUILD)/$(IMAGE).img testos diag maint
SDIMAGE_FILES := boot/BOOT.CFG:ROBCO/BOOT.CFG $(BUILD)/$(IMAGE).img:ROBCO/MFBOOT.SYS \
                 $(OS_BUILD)/uos/testos.img:ROBCO/UOS.SYS \
                 $(BUILD)/maint.sys:ROBCO/MAINT.SYS \
                 $(BUILD)/diag.sys:ROBCO/DIAG.SYS

# Host unit tests: tests/host/test_<name>.c with the pure files.
HOST_TEST_DIR := tests/host
HOST_SRCS     := $(PURE_SRCS) $(DIAG_PURE_SRCS) $(MAINT_PURE_SRCS)
HOST_INCLUDES := src diag maint

# The library's commit for the maintenance screen, from the CI workflow
# pin, which check-pins holds equal to the submodule. No git needed.
MFB_CORE_PIN := $(or $(shell sed -n 's/.*robco-ci\.yml@\([0-9a-f]\{12\}\).*/\1/p' .github/workflows/ci.yml 2>/dev/null | head -n 1),unknown)
EXTRA_CFLAGS += -DMFB_CORE_PIN='"$(MFB_CORE_PIN)"'

ifeq ($(wildcard $(ROBCO_CORE)/mk/robco.mk),)
$(error $(ROBCO_CORE) is empty: run git submodule update --init)
endif

.DEFAULT_GOAL := all
include $(ROBCO_CORE)/mk/robco.mk
include $(ROBCO_CORE)/mk/host.mk

# Target tests boot the images built here; keep top-level targets serial.
.NOTPARALLEL:

# The pin is compiled into main.c: rebuild it when the pin moves.
$(BUILD)/obj/src/main.o: .github/workflows/ci.yml

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

# ---- The diagnostics image ------------------------------------------------

# build/diag.sys, DIAG.SYS on the card, from diag/. The test build
# build/diagtest/diag.img adds the maintenance port's fault command and
# caps the memory test at DIAG_TEST_MEM_BYTES. Not shipped.
DIAG_BUILD          = $(abspath $(BUILD))/diag
DIAG_TEST_BUILD     = $(abspath $(BUILD))/diagtest
DIAG_TEST_MEM_BYTES ?= 0x01000000
.PHONY: diag run-diag
all: diag

diag:
	$(MAKE) --no-print-directory -C diag BUILD=$(DIAG_BUILD)
	cmp -s $(DIAG_BUILD)/diag.img $(BUILD)/diag.sys || cp $(DIAG_BUILD)/diag.img $(BUILD)/diag.sys
	$(MAKE) --no-print-directory -C diag BUILD=$(DIAG_TEST_BUILD) \
	    EXTRA_CFLAGS="-DDG_TEST=1 -DDG_MEM_CAP_BYTES=$(DIAG_TEST_MEM_BYTES)u"

$(BUILD)/diag.sys: diag
	@test -f $@

# Alone, a two-word image at 0x8000 jumps to the diagnostics image in the OS slot.
$(BUILD)/tramp_diag.img:
	@mkdir -p $(dir $@)
	$(PYTHON) -c "import struct, sys; open(sys.argv[1], 'wb').write(struct.pack('<II', 0xE51FF004, 0x00400000))" $@

# ---- The maintenance image ----------------------------------------------

# build/maint.sys, MAINT.SYS on the card, from maint/. The test build
# build/mainttest/maint.img adds the maintenance port's write probe. Not
# shipped.
MAINT_BUILD      = $(abspath $(BUILD))/maint
MAINT_TEST_BUILD = $(abspath $(BUILD))/mainttest
.PHONY: maint run-maint
all: maint

maint:
	$(MAKE) --no-print-directory -C maint BUILD=$(MAINT_BUILD)
	cmp -s $(MAINT_BUILD)/maint.img $(BUILD)/maint.sys || cp $(MAINT_BUILD)/maint.img $(BUILD)/maint.sys
	$(MAKE) --no-print-directory -C maint BUILD=$(MAINT_TEST_BUILD) EXTRA_CFLAGS="-DMT_TEST=1"

$(BUILD)/maint.sys: maint
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
	$(VENV)/bin/pytest tests -v -rs --board $(BOARD) --build-dir $(BUILD) \
	    $(if $(RETROS_IMG),--retros-img $(abspath $(RETROS_IMG))) \
	    $(if $(TIMING_SKIPS_MAX),--timing-skips-max=$(TIMING_SKIPS_MAX))

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

# The diagnostics image alone, with the card. DEVICES=1 adds the clock chip
# and both expanders on I2C1.
RUN_DEVICES := --qemu-arg=-device --qemu-arg=ds1338$(comma)bus=i2c-bus.1$(comma)address=0x68 \
               --qemu-arg=-device --qemu-arg=pca9554$(comma)bus=i2c-bus.1$(comma)address=0x20 \
               --qemu-arg=-device --qemu-arg=pca9554$(comma)bus=i2c-bus.1$(comma)address=0x21
run-diag: sdimage $(BUILD)/tramp_diag.img
	PYTHONPATH=$(ROBCO_CORE)/tests $(PYTHON) -m harness.run $(BUILD)/tramp_diag.img \
	    --qemu-arg=-device \
	    --qemu-arg=loader$(comma)file=$(BUILD)/diag.sys$(comma)addr=0x00400000$(comma)force-raw=on \
	    $(if $(filter 1,$(DEVICES)),$(RUN_DEVICES)) \
	    --board $(RUN_BOARD) --sd $(BUILD)/sd.img --serial2 pty $(if $(filter 1,$(WINDOW)),--window)

# The maintenance image alone, with the card. The OS slot trampoline is the
# diagnostics image's: both link at 0x00400000.
run-maint: sdimage $(BUILD)/tramp_diag.img
	PYTHONPATH=$(ROBCO_CORE)/tests $(PYTHON) -m harness.run $(BUILD)/tramp_diag.img \
	    --qemu-arg=-device \
	    --qemu-arg=loader$(comma)file=$(BUILD)/maint.sys$(comma)addr=0x00400000$(comma)force-raw=on \
	    $(if $(filter 1,$(DEVICES)),$(RUN_DEVICES)) \
	    --board $(RUN_BOARD) --sd $(BUILD)/sd.img --serial2 pty $(if $(filter 1,$(WINDOW)),--window)
