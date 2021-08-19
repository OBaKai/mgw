SRC_DIR = .

INC_DIR = ./ ../ \
		  ../../mgw-core \
		  ../../mgw-core/include \
		  ../outputs

LIB_DIR = . \
		  ../../deps/lib/x86

ifeq ($(strip $(PLATFORM)),)
INC_DIR += ../../deps/include
else
INC_DIR += /home/young/workDir/R8_switcher/media/mp4/include/ffmpeg \
		   /home/young/workDir/R8_switcher/yb_rootfs/home/include
endif

TARGET = libmgw-formats.so
TARGET_TYPE = "shared"
LIBS =

include $(PROJECT_ROOT_PATH)/compile_rules.mk