#!/bin/bash

# Production Build Script for Printer Monitor
# Supports Ubuntu x86_64 with full SNMP implementation

set -e

echo "=========================================="
echo "Printer Monitor - Production Build"
echo "=========================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Detect OS
OS="$(uname -s)"
case "${OS}" in
    Linux*)     PLATFORM=Linux;;
    Darwin*)    PLATFORM=Mac;;
    CYGWIN*|MINGW*|MSYS*) PLATFORM=Windows;;
    *)          PLATFORM="UNKNOWN:${OS}"
esac

echo "Detected platform: $PLATFORM"
echo ""

# Check for required tools
echo "Checking dependencies..."
echo ""

MISSING_DEPS=0

# Check for g++
if ! command -v g++ &> /dev/null; then
    echo -e "${RED}✗ g++ not found${NC}"
    echo "  Install with: sudo apt-get install build-essential"
    MISSING_DEPS=1
else
    echo -e "${GREEN}✓ g++ found:${NC} $(g++ --version | head -n1)"
fi

# Check for Net-SNMP development files
if ! command -v net-snmp-config &> /dev/null; then
    echo -e "${RED}✗ Net-SNMP development files not found${NC}"
    echo "  Install with: sudo apt-get install libsnmp-dev snmp"
    MISSING_DEPS=1
else
    echo -e "${GREEN}✓ Net-SNMP found:${NC} $(net-snmp-config --version)"
    
    # Check if libraries are available
    if ! net-snmp-config --libs &> /dev/null; then
        echo -e "${RED}✗ Net-SNMP libraries not properly configured${NC}"
        MISSING_DEPS=1
    fi
fi

# Check for snmp tools (optional but recommended)
if command -v snmpget &> /dev/null; then
    echo -e "${GREEN}✓ SNMP tools found${NC}"
else
    echo -e "${YELLOW}⚠ SNMP command-line tools not found (optional)${NC}"
    echo "  Install with: sudo apt-get install snmp"
fi

echo ""

if [ $MISSING_DEPS -eq 1 ]; then
    echo -e "${RED}Missing required dependencies!${NC}"
    echo ""
    echo "Install all dependencies with:"
    echo "  sudo apt-get update"
    echo "  sudo apt-get install build-essential libsnmp-dev snmp"
    echo ""
    exit 1
fi

# Build
echo "Building printer-monitor..."
echo ""

make clean 2>/dev/null || true
make

if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}=========================================="
    echo "✓ Build successful!"
    echo -e "==========================================${NC}"
    echo ""
    echo "Binary: ./printer-monitor"
    echo ""
    echo "Quick Start:"
    echo "  1. Add a printer:"
    echo "     ./printer-monitor --add"
    echo ""
    echo "  2. Monitor printers:"
    echo "     ./printer-monitor"
    echo ""
    echo "Usage:"
    echo "  ./printer-monitor --help       # Show all options"
    echo "  ./printer-monitor --list       # List configured printers"
    echo "  ./printer-monitor --detailed   # Show detailed information"
    echo "  ./printer-monitor --verbose    # Verbose output"
    echo ""
    echo "Installation:"
    echo "  sudo make install              # Install to /usr/local/bin"
    echo ""
else
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi

# Create sample printers.json if it doesn't exist
if [ ! -f printers.json ]; then
    echo "Creating sample printers.json..."
    cat > printers.json << 'EOF'
{
  "printers": [
    {
      "name": "Example-KM-C308",
      "ip": "192.168.1.100",
      "community": "public",
      "location": "Main Office",
      "model": "Konica Minolta C308"
    }
  ]
}
EOF
    echo -e "${GREEN}✓ Created sample printers.json${NC}"
    echo ""
    echo "Edit printers.json with your printer details or use:"
    echo "  ./printer-monitor --add"
    echo ""
fi

# Test SNMP connectivity (optional)
echo ""
echo "Would you like to test SNMP connectivity to a printer? (y/n)"
read -r TEST_SNMP

if [ "$TEST_SNMP" = "y" ] || [ "$TEST_SNMP" = "Y" ]; then
    echo ""
    echo "Enter printer IP address:"
    read -r PRINTER_IP
    echo "Enter SNMP community string (default: public):"
    read -r COMMUNITY
    COMMUNITY=${COMMUNITY:-public}
    
    echo ""
    echo "Testing SNMP connection to $PRINTER_IP..."
    
    if snmpget -v2c -c "$COMMUNITY" "$PRINTER_IP" 1.3.6.1.2.1.1.1.0 2>/dev/null; then
        echo -e "${GREEN}✓ SNMP connection successful!${NC}"
        echo "You can now add this printer with:"
        echo "  ./printer-monitor --add"
    else
        echo -e "${RED}✗ SNMP connection failed${NC}"
        echo ""
        echo "Troubleshooting:"
        echo "  1. Check printer IP is correct: ping $PRINTER_IP"
        echo "  2. Verify SNMP is enabled on the printer"
        echo "  3. Check community string is correct"
        echo "  4. Ensure firewall allows UDP port 161"
        echo "  5. Try from printer web interface:"
        echo "     Network → SNMP Settings → Enable SNMPv2c"
    fi
fi

echo ""
echo -e "${GREEN}=========================================="
echo "Setup complete!"
echo -e "==========================================${NC}"
