DIR_INC = . ../../../deps/include
DIR_SRC = .
DIR_OBJ = .

# CROSS_PLATFORM = arm-hisiv500-linux-
CC = $(CROSS_PLATFORM)gcc

SRC = $(wildcard ${DIR_SRC}/*.c)
OBJ = $(patsubst %.c,${DIR_OBJ}/%.o,$(notdir ${SRC})) 

TARGET = librtmp.so.0
DEBUG = -g

CFLAGS = -Wall $(addprefix -I,$(DIR_INC)) -shared -fPIC
# LDFLAGS := -L$(DIR_LIB)
# LIBS = -lmbedtls -lmbedx509 -lmbedcrypto -lpthread

$(TARGET):${OBJ}
	$(CC) $(OBJ) $(DEBUG) -shared -fPIC -o $@ $(LDFLAGS) $(LIBS)
	-@ln -s librtmp.so.0 librtmp.so
    
${DIR_OBJ}/%.o:${DIR_SRC}/%.c
	$(CC) $(DEBUG) $(CFLAGS) -c  $< -o $@

.PHONY:clean
clean:
	-@rm ./*.o $(TARGET) librtmp.so
