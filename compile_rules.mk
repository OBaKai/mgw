#------------------------ Output Settings ---------------------------
prefix = $(PROJECT_ROOT_PATH)/install
DEBUG = "yes"

#------------------------ Platform Settings -------------------------
MACRO :=
ifneq ($(strip $(PLATFORM)),)
	MACRO = CROSS_PLATFORM
	PLATFORM_FLAGS += -mcpu=cortex-a9 -mfloat-abi=softfp -mfpu=neon -mno-unaligned-access -fno-aggressive-loop-optimizations
endif

#------------------------ Compiler Options ---------------------------
AR := $(PLATFORM)ar
CC := $(PLATFORM)gcc
CXX := $(PLATFORM)g++

# define more attribute
ifeq ($(TARGET_TYPE),"shared")
	CFLAGS += -fPIC
	CXXFLAGS += -fPIC
endif

ifeq ($(DEBUG),"yes")
	BUILD := debug
	MACRO += DEBUG
	CFLAGS += -g -Wall -rdynamic -funwind-tables
	CXXFLAGS += -g -Wall -rdynamic -funwind-tables
else
	BUILD := release
	MACRO += NDEBUG
	CFLAGS := -Wall -O2
	CXXFLAGS += -Wall -O2
endif

CFLAGS += $(PLATFORM_FLAGS) -std=gnu99
CXXFLAGS += $(PLATFORM_FLAGS) -std=c++11

#LDFLAGS = -lpthread -lrt -ldl
LDFLAGS = $(addprefix -L, $(LIB_DIR))
CPPFLAGS = $(addprefix -I, $(INC_DIR))
DEFINES = $(addprefix -D,$(MACRO))

LIBS += -lpthread -lrt -ldl

#------------------------ Files Preprocess --------------------------

# make output dir
OUTPATH += $(BUILD)
$(shell mkdir -p $(OUTPATH) > /dev/null)

SOURCE_FILES = $(foreach dir,$(SRC_DIR),$(wildcard $(dir)/*.cpp))
SOURCE_FILES += $(foreach dir,$(SRC_DIR),$(wildcard $(dir)/*.cc))
SOURCE_FILES += $(foreach dir,$(SRC_DIR),$(wildcard $(dir)/*.c))

OBJECT_FILES = $(patsubst %.c,%.o,$(patsubst %.cc,%.o,$(patsubst %.cpp,%.o,$(SOURCE_FILES))))

DEPENDENCE_FILES := $(OBJECT_FILES:%.o=%.d)
DEPENDENCE_FILES := $(foreach file,$(DEPENDENCE_FILES),$(OUTPATH)/$(notdir $(file)))
DEPFLAGS = -MMD -MP -MF $(OUTPATH)/$(*F).d

#------------------------Rules Settings -----------------------------
$(OUTPATH)/$(TARGET): $(OBJECT_FILES) 
ifeq ($(TARGET_TYPE), "app")
	@-echo $(OBJECT_FILES) $(SOURCE_FILES)
	$(CXX) -o $@ -Wl,-rpath . $^ $(LDFLAGS) $(LIBS)
else
ifeq ($(TARGET_TYPE), "shared")
	@-echo $(OBJECT_FILES) $(SOURCE_FILES)
	$(CC) -o $@ -shared -fPIC -rdynamic -Wl,-rpath . $^ $(LDFLAGS) $(LIBS)
else
ifeq ($(OUTTYPE),"static")
	$(AR) -rc $@ $^
else  # make ko

endif
endif
endif

#	@echo make ok, output: $(OUTPATH)/$(OUTFILE)  source files: $(SOURCE_FILES) source dir: $(SRC_DIR) dependence files: $(DEPENDENCE_FILES)

%.o : %.c
	$(CC) $(LDFLAGS) $(CPPFLAGS) $(CFLAGS) $(DEFINES) -c $(DEPFLAGS) -o $@ $<
	
%.o : %.cpp
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $(CXXFLAGS) $(DEFINES) -c $(DEPFLAGS) -o $@ $<

%.o : %.cc
	$(CXX) $(LDFLAGS) $(CPPFLAGS) $(CXXFLAGS) $(DEFINES) -c $(DEPFLAGS) -o $@ $<
	
-include $(DEPENDENCE_FILES)

.PHONY: install
install:
ifeq ($(TARGET_TYPE), "app")
	@-mkdir -p $(prefix)/bin > /dev/null
	@-cp -rvf $(OUTPATH)/$(TARGET) $(prefix)/bin/
else
ifeq ($(TARGET_TYPE), "shared")
	@-mkdir -p $(prefix)/lib > /dev/null
	@-cp -rvf $(OUTPATH)/$(TARGET) $(prefix)/lib/
endif
endif

.PHONY: clean
clean:
	@-rm -rf $(OBJECT_FILES) $(OUTPATH) $(DEPENDENCE_FILES)
