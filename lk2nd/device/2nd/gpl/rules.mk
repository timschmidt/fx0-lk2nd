# SPDX-License-Identifier: GPL-2.0-only
LOCAL_DIR := $(GET_LOCAL_DIR)

OBJS += \
	$(LOCAL_DIR)/motorola-unit-info.o \
	$(LOCAL_DIR)/lg-madai-battery.o \
	$(LOCAL_DIR)/lg-madai-charge-gate.o \

# Space for the runtime-selected battery's five OCV tables and DT references.
DEFINES += DTB_PAD_SIZE=8192
