-include sources/methods/Makefile.mk
-include sources/driver/Makefile.mk
-include sources/common/Makefile.mk
-include sources/3rd-party/Makefile.mk
-include sources/media/Makefile.mk
-include sources/websockets/Makefile.mk

CXXFLAGS += -Isources

VPATH += sources

OBJ += $(OBJ_DIR)/main.o
OBJ += $(OBJ_DIR)/streamer.o
OBJ += $(OBJ_DIR)/cgi_debug.o
OBJ += $(OBJ_DIR)/authorise.o
OBJ += $(OBJ_DIR)/http_utils.o
OBJ += $(OBJ_DIR)/attemps_login.o
