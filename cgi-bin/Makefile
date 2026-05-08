AR      := $(CROSS_COMPILER)ar
CC      := $(CROSS_COMPILER)gcc
CXX     := $(CROSS_COMPILER)g++
STRIP	:= $(CROSS_COMPILER)strip

OBJ_DIR		:= build
NAME_MODULE	:= FastCGI
TARGET		:= $(OBJ_DIR)/$(NAME_MODULE)

OPTIMIZE_OPTION	:= -g -s -O3
WARNNING_OPTION	+=	

-include sources/Makefile.mk

# Flag compiler options
CXXFLAGS += $(OPTIMIZE_OPTION)		\
			$(WARNNING_OPTION)		\
			$(RELEASE_FLAG)			\
			-Wall -pipe

ifndef ENVIR_DIR
$(error You MUST define ENVIR_DIR)
else
CXXFLAGS += -DENVIR_ROOT_DIR=\"$(ENVIR_DIR)\"
endif

# Flags Libraries
LDLIBS += -lpthread -lrt -lm 
LDFLAGS	+= -Wl,-Map=$(OBJ_DIR)/$(NAME_MODULE).map
		
all: create $(TARGET)
	$(STRIP) $(TARGET)
	@size $(TARGET)

create:
	@echo mkdir -p $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: %.cpp
	@echo CXX $<
	@$(CXX) -c -o $@ $< $(CXXFLAGS) -std=c++17 $(LDFLAGS)

$(OBJ_DIR)/%.o: %.c
	@echo CXX $<
	@$(CC) -c -o $@ $< $(CXXFLAGS) $(LDFLAGS)

$(TARGET): $(OBJ)
	@echo ---------- START LINK PROJECT ----------
	@echo $(CXX) -o $@ $^ $(CXXFLAGS)
	@$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LDLIBS)

flash: $(TARGET)
ifndef ENVIR_DIR
	$(error You MUST define ENVIR_DIR)
else
	@cp --remove-destination $< $(ENVIR_DIR)/bin
	@echo "Flash completed"
endif

clean:
	@echo rm -rf $(OBJ_DIR)
	@rm -rf $(OBJ_DIR)

.PHONY: all create clean strip flash