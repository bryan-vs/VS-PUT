# VS-PUT Printer Monitor

CXX      = g++
CXXFLAGS = -std=c++11 -O2 -Wall $(shell net-snmp-config --cflags)
LDFLAGS  = $(shell net-snmp-config --libs) -lpthread
TARGET   = printer-monitor
SOURCE   = printer-monitor.cpp

.PHONY: all clean install uninstall debug

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

debug: CXXFLAGS += -g -O0
debug: clean $(TARGET)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)

uninstall:
	rm -f /usr/local/bin/$(TARGET)
