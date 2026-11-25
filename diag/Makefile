# DIAG.SYS, the diagnostics image of the terminal chain. Linked in the OS
# slot. The top-level Makefile calls this one:
#   make -C diag BUILD=<dir> [EXTRA_CFLAGS=-DDG_TEST=1]

ROBCO_CORE ?= ../external/robco-core
IMAGE      := diag
LINK_ADDR  := 0x00400000
# ../src/portreq.c: the port request rule, shared with the boot agent.
SRCS       := $(sort $(wildcard *.c)) ../src/portreq.c
CONFIG_H   := robco_config.h

include $(ROBCO_CORE)/mk/robco.mk
