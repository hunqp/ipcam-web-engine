CXXFLAGS += -Isources/3rd-party/libcurl/include

LDFLAGS += -Lsources/3rd-party/libcurl/lib

LDLIBS	+= -lcurl
