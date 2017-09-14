TOP=..
include $(TOP)/configure/CONFIG

ifeq ($(BUILD_PCRE), YES)
    LIBRARY = pcre
    LIB_SRCS = $(notdir $(wildcard ../pcre_*.c))
    
    INC += pcre.h
endif

include $(TOP)/configure/RULES

#for pcre version 8 and higher: have to create pcre.h and define some variables
USR_CPPFLAGS += -DHAVE_MEMMOVE=1 -DNEWLINE=10 -DINT64_MAX="(0x7FFFFFFFFFFFLL)"
USR_CPPFLAGS += -DLINK_SIZE=2 -DMAX_NAME_COUNT=10000 -DMAX_NAME_SIZE=32
USR_CPPFLAGS += -DMATCH_LIMIT=10000000 -DMATCH_LIMIT_RECURSION=MATCH_LIMIT
USR_CPPFLAGS += -DPOSIX_MALLOC_THRESHOLD=10 -DPARENS_NEST_LIMIT=250
USR_CPPFLAGS += -DSUPPORT_PCRE8

../O.Common/%.h: ../%.h.generic
	$(CP) $< $@
