# RobCo-Core's examples/chain1 with the SD and framebuffer variants, handing
# over boot_flags bit 1. Not shipped.
#   make BUILD=<dir>

ROBCO_CORE ?= ../../external/robco-core
IMAGE      := chain1
LINK_ADDR  := 0x8000
SRCS       := main.c
CONFIG_H   := $(ROBCO_CORE)/examples/chain1/robco_config.h
EXTRA_CFLAGS += -DCHAIN1_SD=1 -DCHAIN1_FB=1

include $(ROBCO_CORE)/mk/robco.mk
