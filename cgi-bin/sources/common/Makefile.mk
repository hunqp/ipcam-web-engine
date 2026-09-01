CXXFLAGS += -Isources/common

VPATH += sources/common

OBJ += $(OBJ_DIR)/utils.o
OBJ += $(OBJ_DIR)/network.o
OBJ += $(OBJ_DIR)/basethread.o
OBJ += $(OBJ_DIR)/dispatchtimer.o
