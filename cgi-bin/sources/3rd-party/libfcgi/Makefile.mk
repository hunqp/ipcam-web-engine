CXXFLAGS += -Isources/3rd-party/libfcgi

VPATH += sources/3rd-party/libfcgi

OBJ += $(OBJ_DIR)/fcgio.o
OBJ += $(OBJ_DIR)/fcgiapp.o
OBJ += $(OBJ_DIR)/fcgi_stdio.o
ifeq ($(OS),Windows_NT)
OBJ += $(OBJ_DIR)/os_win32.o
else
OBJ += $(OBJ_DIR)/os_unix.o
endif

CXXFLAGS += -DHAVE_NETDB_H
CXXFLAGS += -DHAVE_UNISTD_H
CXXFLAGS += -DHAVE_SYS_SOCKET_H
CXXFLAGS += -DHAVE_SOCKLEN