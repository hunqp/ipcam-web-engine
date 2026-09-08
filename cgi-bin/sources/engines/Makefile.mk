CXXFLAGS += -Isources/engines

VPATH += sources/engines

OBJ += $(OBJ_DIR)/ng-get.o
OBJ += $(OBJ_DIR)/ng-put.o
OBJ += $(OBJ_DIR)/ng-post.o
OBJ += $(OBJ_DIR)/ng-update.o
OBJ += $(OBJ_DIR)/ng-delete.o
OBJ += $(OBJ_DIR)/ng-record.o
