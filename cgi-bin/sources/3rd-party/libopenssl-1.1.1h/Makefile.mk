CXXFLAGS += -Isources/3rd-party/libopenssl-1.1.1h/include

LDFLAGS += -Lsources/3rd-party/libopenssl-1.1.1h/lib

LDLIBS += -lcrypto -lssl