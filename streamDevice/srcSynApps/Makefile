TOP=../..

include $(TOP)/configure/CONFIG

-include ../src/CONFIG_STREAM
-include ../../src/CONFIG_STREAM

LIBRARY_IOC = streamSynApps

DBD += $(LIBRARY_IOC).dbd

ifeq ($(LOADABLE_MODULE),YES)
SRCS += $(LIBRARY_IOC)_registerRecordDeviceDriver.cpp
LIB_LIBS += $(EPICS_BASE_IOC_LIBS)
endif 
SRCS += $(SYNAPPS_RECORDS:%=dev%Stream.c)

#LIB_LIBS += stream dbIoc registryIoc iocsh
# In 3.14.10, libiocsh is no longer built
LIB_LIBS += stream dbIoc registryIoc

include $(TOP)/configure/RULES

# create streamSynApps.dbd from all SYNAPPS_RECORDS
$(COMMON_DIR)/$(LIBRARY_IOC).dbd: ../../src/CONFIG_STREAM
	@for r in $(SYNAPPS_RECORDS); \
	do echo "device($$r,INST_IO,dev$${r}Stream,\"stream\")"; \
	done > $@

$(LIBRARY_IOC).dbd$(DEP):
	@echo "$@: ../../src/CONFIG_STREAM" >> $@


