#Makefile at top of application tree
TOP = .
include $(TOP)/configure/CONFIG
DIRS += configure pcre StreamDevice
StreamDevice_DEPEND_DIRS  = configure pcre

include $(TOP)/configure/RULES_TOP
