SRC_DIR = .

INC_DIR = . \
		  ../mgw-core \
		  ../mgw-core/include \
		  ../plugins \
		  ../plugins/outputs \
		  ../plugins/sources \


OBJ_DIR = .

LIB_DIR = . \
		  ../deps/lib/x86 \
		  ../install \
		  ../mgw-core/source \
		  ../mgw-core/buffer \
		  ../mgw-core/util \
		  ../plugins/outputs \
		  ../plugins/sources \
		  ../plugins/formats \


INSTALL_DIR = ../install

TARGET = app.out

SOURCE_FILES = $(foreach dir,$(SRC_DIR),$(wildcard $(dir)/*.cc))
SOURCE_FILES += $(foreach dir,$(SRC_DIR),$(wildcard $(dir)/*.c))

#SRC = $(wildcard ${SRC_DIR}/*.c)
#SRC += $(wildcard ${SRC_DIR}/*.cpp)
#SRC += $(wildcard ${SRC_DIR}/*.cc)
#OBJ = $(patsubst %.c,${OBJ_DIR}/%.o,$(notdir ${SRC}))
#OBJ += $(patsubst %.cc,${OBJ_DIR}/%.o,$(notdir ${SRC}))

OBJECT_FILES := $(patsubst %.c,%.o,$(patsubst %.cc,%.o,$(SOURCE_FILES)))

LIBS = -lmgw-outputs -lmgw-sources -lmgw-formats -lmgw-core \
-lmgw-buffer -lmgw-util -lavcodec -lavformat -lavfilter -lavutil -lpostproc -lswscale \
-lx264 -lfdk-aac -lrtmp -lsrt -lx265 -lswresample -lavdevice \
-lssl -lcrypto -lz -ljansson -lrt \
-lpthread -ldl -lmbedtls -lmbedx509 -lmbedcrypto 

CFLAGS = -std=gnu99
#CXXFLAGS = -mcpu=cortex-a9 -mfloat-abi=softfp -mfpu=neon -mno-unaligned-access -fno-aggressive-loop-optimizations
CXXFLAGS = -std=c++11
LDFLAGS := $(addprefix -L, $(LIB_DIR))

#PLATFORM = arm-hisiv600-linux-
DEBUG ?= -g
CXX = $(PLATFORM)g++
CC	= $(PLATFORM)gcc

PLATFORM_MACRO =
ifeq ($(PLATFORM), arm-hisiv600-linux-)
PLATFORM_MACRO = -DCROSS_PLATFORM
LIBS += #-lssl -lcrypto -lz
endif

$(TARGET):${OBJECT_FILES}
	$(CC) $(DEBUG) $(CXXFLAGS) -o $@ $^ $(addprefix -I, $(INC_DIR)) $(LDFLAGS) $(LIBS)

ifneq ($(RELEASE), 1)
#	$(CXX) dlm-test.c -lyb_dlm -lcurl $(DEBUG) $(CXXFLAGS) $(LDFLAGS) $(LIBS) $(addprefix -I, $(INC_DIR))
endif
    
%.o: %.c
	$(CC) $(DEBUG) $(CFLAGS) $(addprefix -I, $(INC_DIR)) -c $< -o $@ $(PLATFORM_MACRO)

%.o: %.cc
	$(CXX) $(DEBUG) $(CXXFLAGS) $(addprefix -I, $(INC_DIR)) -c $< -o $@ $(PLATFORM_MACRO)

.PHONY:install
install:
	@cp $(TARGET) $(INSTALL_DIR) -rvf

.PHONY:clean
clean:
	-@rm  -rf ${OBJ_DIR}/*.o $(TARGET) >> /dev/null
#	-@rm ./a.out >> /dev/null
