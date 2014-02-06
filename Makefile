
TARGET = pipe
SRCS = pipe.cc main.cc
HDRS = pipe.h

URL = http://10.182.63.61:19991/msgupload
ARGS = 

STRIP = strip
CXX = c++
override CFLAGS += -Wall -Werror
LDFLAGS = -lz

debug: 
	$(MAKE) CFLAGS="-g -fno-inline -O0" build

release:
	$(MAKE) CFLAGS="-DNDEBUG -O2" build
	$(STRIP) $(TARGET)

build: $(TARGET)

clean:
	-rm $(TARGET) *.o

run:
	./$(TARGET) -V -d $(URL) $(ARGS)

$(TARGET): $(SRCS:.cc=.o)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cc $(HDRS)
	$(CXX) $(CFLAGS) -c $<

.PHONY: debug release build clean run
