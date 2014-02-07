
arm = no
url = http://10.182.63.61:19991/msgupload
args = 

TARGET = pipe
SRCS = pipe.cc main.cc
HDRS = pipe.h

LIBZ_DIR = zlib-1.2.8
LIBZ = $(LIBZ_DIR)/libz.a

override CXXFLAGS += -Wall -Werror

CC = cc
CXX = c++
STRIP = strip

ifeq ($(arm), yes)
	CC = arm-linux-gnueabi-gcc 
	CXX = arm-linux-gnueabi-g++ 
	STRIP = arm-linux-gnueabi-strip
endif

debug: 
	$(MAKE) CXXFLAGS="-g -fno-inline -O0" build

release:
	$(MAKE) CXXFLAGS="-DNDEBUG -O2" build
	$(STRIP) $(TARGET)

$(LIBZ): $(LIBZ_DIR)/Makefile
	$(MAKE) -C $(LIBZ_DIR) CC=$(CC)

$(LIBZ_DIR)/Makefile:
	cd $(LIBZ_DIR) && ./configure --static

build: $(TARGET)

clean:
	-rm $(TARGET) *.o
	$(MAKE) -C $(LIBZ_DIR) clean

run:
	./$(TARGET) -V -d $(url) $(args)

$(TARGET): $(SRCS:.cc=.o) $(LIBZ)
	$(CXX) -o $@ $^

%.o: %.cc $(HDRS)
	$(CXX) $(CXXFLAGS) -c $<

.PHONY: debug release build clean run
