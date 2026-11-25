# MAINT.SYS, the maintenance image of the terminal chain. Linked in the OS
# slot. The top-level Makefile calls this one:
#   make -C maint BUILD=<dir> [EXTRA_CFLAGS=-DMT_TEST=1]

ROBCO_CORE ?= ../external/robco-core
IMAGE      := maint
LINK_ADDR  := 0x00400000
# Shared with the boot agent: the port request rule. Shared with the
# diagnostics image: the image check, the write guard and the progress rate.
SRCS       := $(sort $(wildcard *.c)) ../src/portreq.c \
              ../diag/dg_sd.c ../diag/dg_sdimg.c ../diag/dg_fmt.c
CONFIG_H   := robco_config.h

include $(ROBCO_CORE)/mk/robco.mk
