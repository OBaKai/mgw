SRC_DIR = .

INC_DIR = . \
		  ../mgw-core \
		  ../mgw-core/include \
		  ../plugins \
		  ../plugins/outputs \
		  ../plugins/sources \

LIB_DIR = . \
		  ../mgw-core/core-api/debug \
		  ../mgw-core/buffer/debug \
		  ../mgw-core/util/debug \
		  ../plugins/outputs/debug \
		  ../plugins/sources/debug \
		  ../plugins/formats/debug \
		  ../plugins/thirdparty/debug \

ifeq ($(strip $(PLATFORM)),)
LIB_DIR += ../deps/lib/x86

LIBS = -lmgw-core -lmgw-outputs -lmgw-sources -lmgw-formats -lmgw-thirdparty \
-lmgw-buffer -lmgw-util -lavformat -lavcodec -lavfilter -lavutil -lswscale -lpostproc \
-lswresample -lavdevice -lrtmp -lsrt -lx264 -lx265 -lfdk-aac \
-lssl -lcrypto -lz -ljansson -lrt \
-lpthread -ldl -lmbedtls -lmbedx509 -lmbedcrypto

else
LIB_DIR += /home/young/workDir/R8_switcher/common/lib \
		  /home/young/workDir/R8_switcher/yb_rootfs/home/lib \
		  /home/young/workDir/R8_switcher/media/gw_media/lib

LIBS = -lmgw-core -lmgw-outputs -lmgw-sources -lmgw-formats -lmgw-thirdparty \
-lmgw-buffer -lmgw-util -lavformat -lavcodec -lavfilter \
-lswresample -lpostproc -lswscale -lavutil -lx264 \
-lfdk-aac -lsrt \
-lssl -lcrypto -lz -ljansson -lrt -lm \
-lpthread -ldl -lmbedtls -lmbedx509 -lmbedcrypto

endif

TARGET = mgw.out
TARGET_TYPE = "app"

include $(PROJECT_ROOT_PATH)/compile_rules.mk
