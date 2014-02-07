
arm = no
url = http://10.182.63.61:19991/msgupload
args = 

TARGET = pipe
SRCS = pipe.cc main.cc
HDRS = pipe.h

LIBZ_DIR = zlib-1.2.8
LIBZ = $(LIBZ_DIR)/libz.a

override CXXFLAGS += -Wall -Werror -I$(LIBZ_DIR)

CC = cc
CXX = c++
STRIP = strip
LDFLAGS = 

ifeq ($(arm), yes)
	CC = arm-linux-gnueabi-gcc 
	CXX = arm-linux-gnueabi-g++ 
	STRIP = arm-linux-gnueabi-strip
	LDFLAGS = 
endif

debug: 
	$(MAKE) CXXFLAGS="-g -fno-inline -O0" build

release:
	$(MAKE) CXXFLAGS="-DNDEBUG -O2" build
	$(STRIP) $(TARGET)

build: $(TARGET)

clean:
	-rm -f $(TARGET) *.o
	$(MAKE) -C $(LIBZ_DIR) clean

run:
	./$(TARGET) -V -d $(url) $(args)

$(TARGET): $(SRCS:.cc=.o) $(LIBZ)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(LIBZ): $(LIBZ_DIR)/Makefile
	$(MAKE) -C $(LIBZ_DIR) CC=$(CC)

$(LIBZ_DIR)/Makefile:
	cd $(LIBZ_DIR) && ./configure --static

%.o: %.cc $(HDRS)
	$(CXX) $(CXXFLAGS) -c $<

.PHONY: debug release build clean run
