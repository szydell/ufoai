TARGET             := cgame-multiplayer

# if the linking should be static
$(TARGET)_STATIC   ?= $(STATIC)
ifeq ($($(TARGET)_STATIC),1)
$(TARGET)_LDFLAGS  += -static
endif

$(TARGET)_LINKER   := $(CXX)
$(TARGET)_CFLAGS   += -DCOMPILE_UFO $(SO_CFLAGS) $(MXML_CFLAGS)
$(TARGET)_LDFLAGS  += $(SO_LDFLAGS) $(MXML_LIBS) -lm
$(TARGET)_FILE     := base/$(TARGET).$(SO_EXT)

$(TARGET)_SRCS      = \
	client/cgame/multiplayer/cl_game_multiplayer.cpp \
	client/cgame/multiplayer/mp_callbacks.cpp \
	client/cgame/multiplayer/mp_serverlist.cpp

ifneq ($(HARD_LINKED_CGAME),1)
	$(TARGET)_SRCS += shared/mathlib.cpp \
		shared/shared.cpp \
		shared/utf8.cpp \
		shared/parse.cpp \
		shared/infostring.cpp \
		\
		game/q_shared.cpp \
		game/chr_shared.cpp \
		game/inv_shared.cpp \
		game/inventory.cpp
else
	$(TARGET)_IGNORE := yes
endif

$(TARGET)_OBJS     := $(call ASSEMBLE_OBJECTS,$(TARGET))
$(TARGET)_CXXFLAGS := $($(TARGET)_CFLAGS)
$(TARGET)_CCFLAGS  := $($(TARGET)_CFLAGS)
