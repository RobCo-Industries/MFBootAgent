# RobCo-Core's examples/chain1 with the SD and framebuffer variants, handing
# over a console grid of a chosen size. Not shipped.
#   make GRID_COLS=80 GRID_ROWS=17 BUILD=<dir>

ROBCO_CORE ?= ../../external/robco-core
IMAGE      := chain1
LINK_ADDR  := 0x8000
SRCS       := main.c
CONFIG_H   := $(ROBCO_CORE)/examples/chain1/robco_config.h
GRID_COLS  ?= 80
GRID_ROWS  ?= 30
EXTRA_CFLAGS += -DCHAIN1_SD=1 -DCHAIN1_FB=1 -DGRID_COLS=$(GRID_COLS) -DGRID_ROWS=$(GRID_ROWS)

include $(ROBCO_CORE)/mk/robco.mk
