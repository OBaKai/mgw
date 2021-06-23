SRC_DIR = .

INC_DIR = . \
		  ../mgw-core \
		  ../mgw-core/include \
		  ../plugins \
		  ../plugins/outputs \
		  ../plugins/sources \

LIB_DIR = . \
		  ../deps/lib/x86 \
		  ../install \
		  ../mgw-core/source \
		  ../mgw-core/buffer \
		  ../mgw-core/util \
		  ../plugins/outputs \
		  ../plugins/sources \
		  ../plugins/formats \

TARGET = mgw.out
TARGET_TYPE = "app"
LIBS = -lmgw-core -lmgw-outputs -lmgw-sources -lmgw-formats \
-lmgw-buffer -lmgw-util -lavcodec -lavformat -lavfilter -lavutil -lpostproc -lswscale \
-lx264 -lfdk-aac -lrtmp -lsrt -lx265 -lswresample -lavdevice \
-lssl -lcrypto -lz -ljansson -lrt \
-lpthread -ldl -lmbedtls -lmbedx509 -lmbedcrypto 

include $(PROJECT_ROOT_PATH)/compile_rules.mk
