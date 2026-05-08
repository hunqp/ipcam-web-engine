CXXFLAGS += -Isources/impl

VPATH += sources/impl

OBJ += $(OBJ_DIR)/GET.o
OBJ += $(OBJ_DIR)/PUT.o
OBJ += $(OBJ_DIR)/POST.o
OBJ += $(OBJ_DIR)/UPDATE.o
OBJ += $(OBJ_DIR)/DELETE.o