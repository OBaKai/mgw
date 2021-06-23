SRC_DIR = .

INC_DIR = . \
		  ..

OBJ_DIR = .

LIB_DIR = . \
		  ../../deps/lib

INSTALL_DIR = ../../install

TARGET = libmgw-buffer.so

SRC = $(wildcard ${SRC_DIR}/*.c)
SRC += $(wildcard ${SRC_DIR}/*.cpp)
SRC += $(wildcard ${SRC_DIR}/*.cc)
OBJ = $(patsubst %.c,${OBJ_DIR}/%.o,$(notdir ${SRC}))

LIBS = #-lcurl

CFLAGS = -std=gnu99
#CXXFLAGS = -mcpu=cortex-a9 -mfloat-abi=softfp -mfpu=neon -mno-unaligned-access -fno-aggressive-loop-optimizations
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

$(TARGET):${OBJ}
	$(CC) $(OBJ) $(DEBUG) $(CFLAGS) -shared -fPIC -o $@ $(LDFLAGS) $(LIBS)
#	@cp -rvf $(TARGET) $(LIB_DIR)
#	@cp -rvf $(TARGET) $(INS_DIR)
ifneq ($(RELEASE), 1)
#	$(CXX) dlm-test.c -lyb_dlm -lcurl $(DEBUG) $(CXXFLAGS) $(LDFLAGS) $(LIBS) $(addprefix -I, $(INC_DIR))
endif
    
${OBJ_DIR}/%.o:${SRC_DIR}/%.c
	$(CC) $(DEBUG) $(CXXFLAGS) $(CFLAGS) $(addprefix -I, $(INC_DIR)) -c -fPIC $< -o $@ $(PLATFORM_MACRO)

.PHONY:install
install:
	@cp $(TARGET) $(INSTALL_DIR) -rvf

.PHONY:clean
clean:
	-@rm  -rf ${OBJ_DIR}/*.o $(TARGET) >> /dev/null
#	-@rm ./a.out >> /dev/null
