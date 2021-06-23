#generation target

SRCS_PATH	= ./
INCS_PATH	= ../mgw-core ../deps/include
LIBS_PATH	= ../mgw-core/util ../deps/lib/x86
OBJS_PATH	= ./

PLATFORM 	= 

CFLAGS		= -std=gnu99
CXXFLAGS	= -std=c++11

CC			= $(PLATFORM)gcc
CXX			= $(PLATFORM)g++

LIBS		+= mgw-util jansson rt pthread dl

SRCS 		:= $(wildcard ${SRCS_PATH}/*.c)
SRCS 		+= $(wildcard ${SRCS_PATH}/*.cpp)
SRCS 		+= $(wildcard ${SRCS_PATH}/*.cc)

OBJS 		= $(patsubst %.c,${OBJS_PATH}/%.o,$(notdir ${SRCS}))

LDFLAGS		= $(addprefix -L, $(LIBS_PATH))
INCFLAGS	= $(addprefix -I, $(INCS_PATH))
LIBFLAGS	= $(addprefix -l, $(LIBS))


data_test:
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBFLAGS) $(INCFLAGS)  data-test.cc -o data-test 

.PHONY:clean
clean:
	-@rm $(OBJS_PATH)/*.o -rf >> /dev/null