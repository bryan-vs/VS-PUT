# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

VS-PUT v2.0 is a production-ready, modular CLI tool for monitoring printer toner levels, page counts, and status via SNMP. The tool supports multiple printer vendors through a plugin-style architecture and provides configurable thresholds, flexible filtering, and multiple display layouts.

**Supported Vendors:**
- Konica Minolta (bizhub series)
- Xerox (multifunction printers)

**Key Features:**
- Vendor abstraction layer for easy extensibility
- Configurable low/critical toner thresholds
- Multiple display layouts (table, compact, detailed, minimal)
- Advanced filtering (low toner, critical toner, specific colors, online/offline)
- DNS-based configuration with automatic location parsing
- Settings persistence via JSON configuration

## Build Commands

### Using Makefile (Recommended)
```bash
make                # Build the application
make clean          # Clean build artifacts
make install        # Install to /usr/local/bin (requires sudo)
make uninstall      # Remove from system (requires sudo)
make test           # Run basic tests
make debug          # Build with debug symbols
make release        # Build optimized release
```

### Manual Build
```bash
g++ -std=c++11 -o printer-monitor printer-monitor.cpp \
    $(net-snmp-config --cflags) $(net-snmp-config --libs)
```

### Dependencies
Required packages (Ubuntu/Debian):
```bash
sudo apt-get install build-essential libsnmp-dev snmp
```

## Configuration Files

### printers.json
DNS-based printer configuration. Name and location are automatically derived from DNS name.

Format:
```json
{
  "printers": [
    {
      "dns": "printer-b5-3.obspm.fr",
      "vendor": "konica_minolta",
      "community": "public"
    }
  ]
}
```

**DNS Naming Convention:**
- Format: `printer-bX-Y.domain.com`
- `bX`: Building number (e.g., `b5`, `b12`)
- `Y`: Floor or office number (optional)
- Domain suffix (e.g., `.obspm.fr`) is automatically stripped for short name

The system automatically parses:
- `shortName`: "printer-b5-3" (DNS without domain)
- `building`: "5" (extracted from bX pattern)
- `floor`: "3" (extracted from floor/office number)

### settings.json
System-wide configuration for thresholds and defaults.

Format:
```json
{
  "settings": {
    "low_toner_threshold": 20,
    "critical_toner_threshold": 10,
    "default_community": "public"
  }
}
```

**Configuration Parameters:**
- `low_toner_threshold`: Percentage at which toner is considered low (default: 20%)
- `critical_toner_threshold`: Percentage at which toner is critical (default: 10%)
- `default_community`: Default SNMP community string (default: "public")

## Usage Examples

### Basic Monitoring
```bash
./printer-monitor                    # Monitor all printers (table layout)
./printer-monitor -v                 # Verbose output during queries
```

### Display Layouts
```bash
./printer-monitor --table            # Table layout with bars (default)
./printer-monitor --compact          # Compact one-line per printer
./printer-monitor --detailed         # Full detailed information
./printer-monitor --minimal          # Minimal output (name: toner%)
```

### Filtering
```bash
./printer-monitor --low-toner        # Show only printers with low toner
./printer-monitor --critical-toner   # Show only critical toner printers
./printer-monitor --offline          # Show only offline printers
./printer-monitor --online           # Show only online printers
./printer-monitor --color Black --low  # Show printers with low black toner
```

### Combined Options
```bash
# Compact view of printers with low toner
./printer-monitor --low-toner --compact

# Detailed view of printers with critical magenta toner
./printer-monitor --color Magenta --low --critical-toner --detailed

# Verbose monitoring of online printers only
./printer-monitor --online -v
```

### Management
```bash
./printer-monitor --add              # Add a new printer
./printer-monitor --list             # List configured printers
./printer-monitor --help             # Show help
```

## Architecture

### Modular Design

The application is built with a highly modular architecture for easy extensibility:

**Core Components:**

1. **Vendor Abstraction Layer** (printer-monitor.cpp:210-242)
   - `PrinterVendor` base class
   - Virtual `queryPrinter()` method for vendor-specific implementations
   - Shared toner evaluation logic in `evaluateTonerLevels()`

2. **Vendor Implementations**
   - `KonicaMinoltaVendor` (printer-monitor.cpp:248-413): Konica Minolta bizhub series
   - `XeroxVendor` (printer-monitor.cpp:419-535): Xerox multifunction printers
   - Each vendor handles its own SNMP OIDs and query strategies

3. **Vendor Factory** (printer-monitor.cpp:541-567)
   - Central registry for all vendors
   - Runtime vendor selection based on configuration
   - Easy addition of new vendors

4. **Display System** (printer-monitor.cpp:713-958)
   - `DisplayLayout` base class with common utilities
   - Four layout implementations:
     - `TableLayout`: Rich table with bars and summary
     - `CompactLayout`: One-line per printer
     - `DetailedLayout`: Full printer details
     - `MinimalLayout`: Bare minimum output

5. **Filtering System** (printer-monitor.cpp:964-1066)
   - `PrinterFilter` interface
   - Composable filters (low toner, critical, offline, color-specific)
   - Multiple filters can be applied simultaneously

6. **Configuration System** (printer-monitor.cpp:573-707)
   - `ConfigParser` for JSON parsing
   - DNS name parsing with location extraction
   - Settings persistence

### SNMP Implementation

**Protocol:** SNMPv2c (community-based authentication)
**Default Community:** "public" (configurable per printer and globally)
**Timeout:** 2 seconds (configurable in Settings)
**Retries:** 1 (configurable in Settings)
**Port:** UDP 161 (standard SNMP)

### Vendor-Specific OIDs

#### Konica Minolta
**Standard Printer MIB (tried first):**
- System Description: `1.3.6.1.2.1.1.1.0`
- Device Status: `1.3.6.1.2.1.25.3.5.1.1.1`
- Page Count: `1.3.6.1.2.1.43.10.2.1.4.1.1`
- Supply Description: `1.3.6.1.2.1.43.11.1.1.6.1.X`
- Supply Level: `1.3.6.1.2.1.43.11.1.1.9.1.X`
- Supply Max: `1.3.6.1.2.1.43.11.1.1.8.1.X`

**Konica Minolta Enterprise OIDs (fallback, Enterprise ID: 18334):**
- Toner Levels: `1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.5.X`
- Toner Max: `1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.6.X`
- Serial Number: `1.3.6.1.4.1.18334.1.1.1.1.3.2.2.0`
- Page Count: `1.3.6.1.4.1.18334.1.1.1.5.7.2.1.5.1`
- Model: `1.3.6.1.4.1.18334.1.1.1.1.1.12.0`

Where X = 1 (Black), 2 (Cyan), 3 (Magenta), 4 (Yellow)

#### Xerox
**Standard Printer MIB (primary):**
- Uses same standard OIDs as Konica Minolta
- Most Xerox printers fully support standard Printer MIB

**Xerox Enterprise OIDs (fallback, Enterprise ID: 253):**
- Serial Number: `1.3.6.1.4.1.253.8.53.3.2.1.3.1`
- Model: `1.3.6.1.4.1.253.8.53.13.2.1.6.1.12.1`
- Total Impressions: `1.3.6.1.4.1.253.8.53.13.2.1.6.1.20.1`

## Adding New Printer Vendors

To add support for a new vendor:

1. **Create Vendor Class** (inherit from `PrinterVendor`)
   ```cpp
   class NewVendor : public PrinterVendor {
   public:
       NewVendor(const Settings& s) : PrinterVendor("Vendor Name", s) {}

       PrinterStatus queryPrinter(const PrinterConfig& config,
                                  SNMPHelper& snmp) override {
           // Implement vendor-specific query logic
           // Use evaluateTonerLevels() to apply thresholds
       }
   };
   ```

2. **Register in VendorFactory**
   ```cpp
   VendorFactory(const Settings& s) : settings(s) {
       vendors["konica_minolta"] = std::make_shared<KonicaMinoltaVendor>(settings);
       vendors["xerox"] = std::make_shared<XeroxVendor>(settings);
       vendors["new_vendor"] = std::make_shared<NewVendor>(settings);  // Add here
   }
   ```

3. **Update Documentation**
   - Add vendor to help text in `printUsage()`
   - Document vendor-specific OIDs in CLAUDE.md and README.md

## Adding New Display Layouts

To add a new display layout:

1. **Create Layout Class** (inherit from `DisplayLayout`)
   ```cpp
   class CustomLayout : public DisplayLayout {
   public:
       CustomLayout(const Settings& s) : DisplayLayout(s) {}

       void render(const std::vector<PrinterStatus>& statuses) override {
           // Implement custom rendering
           // Use helper methods: getTonerBar(), truncate(), getColorCode()
       }
   };
   ```

2. **Register in monitorPrinters()**
   ```cpp
   if (options.layout == "custom") {
       layout = std::make_shared<CustomLayout>(settings);
   }
   ```

3. **Add Command-Line Option**
   - Add option parsing in `main()`
   - Update help text in `printUsage()`

## Adding New Filters

To add a new filter:

1. **Create Filter Class** (inherit from `PrinterFilter`)
   ```cpp
   class CustomFilter : public PrinterFilter {
   public:
       bool shouldInclude(const PrinterStatus& status) const override {
           // Return true if printer should be included
       }

       std::string getDescription() const override {
           return "Description for filter";
       }
   };
   ```

2. **Add Command-Line Option**
   ```cpp
   else if (arg == "--custom-filter") {
       options.filters.push_back(std::make_shared<CustomFilter>());
   }
   ```

## Testing SNMP Manually

```bash
# Test connectivity
snmpwalk -v2c -c public printer-b5-3.obspm.fr system

# Get toner levels (Black, standard MIB)
snmpget -v2c -c public printer-b5-3.obspm.fr 1.3.6.1.2.1.43.11.1.1.9.1.1

# Get page count
snmpget -v2c -c public printer-b5-3.obspm.fr 1.3.6.1.2.1.43.10.2.1.4.1.1
```

## Cross-Platform Notes

- **Linux**: Primary target platform (tested on Ubuntu x86_64)
- **Windows**: Compilation supported via MSYS2/MinGW or Visual Studio (requires Net-SNMP Windows binaries)
- **macOS**: Should work with Net-SNMP from Homebrew (untested)

## Security Considerations

- SNMP uses plain-text community strings (not encrypted)
- Default "public" community should be changed for production
- SNMP should be configured as read-only on printers
- Network access to UDP port 161 should be restricted via firewall
- SNMPv3 (with authentication/encryption) is not currently supported but recommended for future enhancement

## Code Organization

The single-file architecture (printer-monitor.cpp) is organized into logical sections:

1. **Lines 18-106**: Data structures (Settings, TonerLevel, PrinterConfig, PrinterStatus)
2. **Lines 108-205**: SNMP utilities (SNMPHelper class)
3. **Lines 207-242**: Vendor abstraction (PrinterVendor base class)
4. **Lines 244-413**: Konica Minolta implementation
5. **Lines 415-535**: Xerox implementation
6. **Lines 537-567**: Vendor factory
7. **Lines 569-707**: Configuration parser
8. **Lines 709-958**: Display system
9. **Lines 960-1066**: Filtering system
10. **Lines 1068-1305**: Command-line interface
11. **Lines 1306-1413**: Main entry point

## Development Workflow

1. Edit source code
2. Run `make` to build
3. Test with `./printer-monitor --help`
4. Use `make debug` for debugging
5. Use `make release` for optimized production build
6. Run `make test` for basic validation
7. Install with `sudo make install`
