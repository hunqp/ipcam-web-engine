CXXFLAGS += -Isources/websockets

VPATH += sources/websockets

OBJ += $(OBJ_DIR)/sha1.o
OBJ += $(OBJ_DIR)/base64.o
OBJ += $(OBJ_DIR)/handshake.o
OBJ += $(OBJ_DIR)/websockets.o
