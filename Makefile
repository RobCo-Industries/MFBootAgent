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
              src/menu.c \
              src/menulines.c \
              src/screen.c \

SRCS       := $(sort $(wildcard src/*.c))

# Host unit tests: tests/host/test_<name>.c with the pure files. The
# screen reader needs the console font and the colour schemes.
HOST_TEST_DIR := tests/host
HOST_SRCS     := $(PURE_SRCS) \
                 $(ROBCO_CORE)/src/gfx/font8x16_fixedsys.c \
                 $(ROBCO_CORE)/src/gfx/schemes.c
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
OS_BUILD = $(abspath $(BUILD))/os

.PHONY: testos
all: testos

testos:
	$(MAKE) --no-print-directory -C tests/os BUILD=$(OS_BUILD)/uos KIND=uos SUBTYPE=0
	$(MAKE) --no-print-directory -C tests/os BUILD=$(OS_BUILD)/maint KIND=maint SUBTYPE=2
	$(MAKE) --no-print-directory -C tests/os BUILD=$(OS_BUILD)/diag KIND=diag SUBTYPE=3

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
