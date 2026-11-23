# The test operating image: stands in for UOS, MAINT and DIAG in the
# target tests. Linked in the OS slot. Built once per subtype:
#   make SUBTYPE=0 KIND=uos BUILD=<dir>
# Not shipped.

ROBCO_CORE ?= ../../external/robco-core
IMAGE      := testos
LINK_ADDR  := 0x00400000
SRCS       := main.c
CONFIG_H   := robco_config.h
SUBTYPE    ?= 0
KIND       ?= uos
EXTRA_CFLAGS += -DTESTOS_SUBTYPE=$(SUBTYPE) -DTESTOS_KIND='"$(KIND)"'

include $(ROBCO_CORE)/mk/robco.mk
