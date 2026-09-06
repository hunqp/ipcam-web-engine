CXXFLAGS += -Isources/3rd-party/libkiwi

VPATH += sources/3rd-party/libkiwi

OBJ += $(OBJ_DIR)/kiwi_database.o
OBJ += $(OBJ_DIR)/kiwi_ringbuffer.o
OBJ += $(OBJ_DIR)/kiwi_credentials.o