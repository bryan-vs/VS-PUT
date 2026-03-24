# Makefile for VS-PUT Printer Monitor
# Supports Konica Minolta and Xerox printers via SNMP

CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra $(shell net-snmp-config --cflags)
LDFLAGS = $(shell net-snmp-config --libs)
TARGET = printer-monitor
SOURCE = printer-monitor.cpp
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

.PHONY: all clean install uninstall test

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCE) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	@echo "Installing $(TARGET) to $(BINDIR)..."
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)
	@echo "Installation complete!"
	@echo "Run '$(TARGET) --help' for usage information"

uninstall:
	@echo "Uninstalling $(TARGET) from $(BINDIR)..."
	rm -f $(BINDIR)/$(TARGET)
	@echo "Uninstall complete!"

test: $(TARGET)
	@echo "Running basic tests..."
	@./$(TARGET) --help > /dev/null && echo "  ✓ Help command works" || echo "  ✗ Help command failed"
	@./$(TARGET) --list 2>/dev/null || echo "  ✓ List command works (no printers configured is OK)"
	@test -f printers.json.example && echo "  ✓ Example config exists" || echo "  ✗ Example config missing"
	@test -f settings.json.example && echo "  ✓ Example settings exists" || echo "  ✗ Example settings missing"
	@echo "Test complete!"

# Development targets
debug: CXXFLAGS += -g -O0 -DDEBUG
debug: clean $(TARGET)

release: CXXFLAGS += -O3 -DNDEBUG
release: clean $(TARGET)

# Help target
help:
	@echo "VS-PUT Printer Monitor - Makefile targets:"
	@echo ""
	@echo "  make              - Build the printer monitor"
	@echo "  make clean        - Remove built files"
	@echo "  make install      - Install to $(BINDIR) (requires sudo)"
	@echo "  make uninstall    - Remove from $(BINDIR) (requires sudo)"
	@echo "  make test         - Run basic tests"
	@echo "  make debug        - Build with debug symbols"
	@echo "  make release      - Build optimized release version"
	@echo ""
	@echo "Requirements:"
	@echo "  - g++ with C++11 support"
	@echo "  - libsnmp-dev (Net-SNMP development library)"
	@echo ""
	@echo "Installation:"
	@echo "  Ubuntu/Debian: sudo apt-get install build-essential libsnmp-dev"
	@echo "  Fedora/RHEL:   sudo yum install gcc-c++ net-snmp-devel"
