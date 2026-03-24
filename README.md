# VS-PUT v2.0 - Printer Utility Tool

A production-ready, modular CLI tool for monitoring printer toner levels, page counts, and status via SNMP (Simple Network Management Protocol). Now with multi-vendor support, configurable thresholds, and advanced filtering.

## What's New in v2.0

🎯 **Multi-Vendor Support** - Konica Minolta AND Xerox printers
⚙️ **Configurable Thresholds** - Set custom low/critical toner levels
🎨 **Multiple Display Layouts** - Table, Compact, Detailed, Minimal
🔍 **Advanced Filtering** - Show only low toner, critical, offline, or specific colors
📍 **DNS-Based Configuration** - Automatic location parsing from DNS names
🔧 **Modular Architecture** - Easy to extend with new vendors and features

## Features

✅ **Real SNMP Implementation** - Uses Net-SNMP library for actual printer queries
✅ **Multiple Vendors** - Konica Minolta bizhub & Xerox multifunction printers
✅ **Multiple Printers** - Monitor multiple printers simultaneously
✅ **Configurable Thresholds** - Define low (default 20%) and critical (default 10%) levels
✅ **Visual Toner Bars** - Easy-to-read toner level indicators with low/critical markers
✅ **Advanced Filtering** - Show only printers matching specific criteria
✅ **Multiple Layouts** - Choose from 4 different display formats
✅ **Page Counters** - Track total pages printed
✅ **JSON Configuration** - Simple, DNS-based printer management
✅ **Connection Testing** - Verify printer connectivity when adding
✅ **Cross-platform Ready** - Linux tested, Windows compilation supported

## Requirements

### Ubuntu/Debian (Required)
```bash
sudo apt-get update
sudo apt-get install build-essential libsnmp-dev snmp
```

### Other Linux Distributions
- **Fedora/RHEL**: `sudo dnf install gcc-c++ net-snmp-devel net-snmp-utils`
- **Arch**: `sudo pacman -S gcc net-snmp`
- **openSUSE**: `sudo zypper install gcc-c++ net-snmp-devel`

## Quick Start

### 1. Build
```bash
make
```

Or use the automated setup:
```bash
chmod +x setup.sh
./setup.sh
```

### 2. Configure Printer SNMP

**For Konica Minolta:**
1. Access web interface (http://PRINTER_IP)
2. Navigate to: **Network → SNMP Settings**
3. Enable **SNMPv2c**
4. Set community name to: **public** (or custom)
5. Ensure **Read** permission is granted
6. Save settings

**For Xerox:**
1. Access web interface (http://PRINTER_IP)
2. Navigate to: **Properties → Connectivity → Protocols**
3. Enable **SNMP v1/v2c**
4. Set Read Community to: **public** (or custom)
5. Apply settings

### 3. Configure Settings (Optional)
Edit `settings.json` to customize thresholds:
```json
{
  "settings": {
    "low_toner_threshold": 20,
    "critical_toner_threshold": 10,
    "default_community": "public"
  }
}
```

### 4. Add Printers
```bash
./printer-monitor --add
```

Example configuration in `printers.json`:
```json
{
  "printers": [
    {
      "dns": "printer-b5-3.obspm.fr",
      "vendor": "konica_minolta",
      "community": "public"
    },
    {
      "dns": "printer-b12-2.obspm.fr",
      "vendor": "xerox",
      "community": "public"
    }
  ]
}
```

### 5. Monitor
```bash
./printer-monitor
```

## Usage

### Basic Commands

```bash
# Monitor all printers (default - table layout)
./printer-monitor

# Monitor with verbose output
./printer-monitor -v

# Add a new printer
./printer-monitor --add

# List configured printers
./printer-monitor --list

# Show help
./printer-monitor --help
```

### Display Layouts

```bash
# Table layout with visual bars (default)
./printer-monitor --table

# Compact one-line per printer
./printer-monitor --compact

# Full detailed information
./printer-monitor --detailed

# Minimal output (printer: toner%)
./printer-monitor --minimal
```

### Filtering Options

```bash
# Show only printers with low toner
./printer-monitor --low-toner

# Show only printers with critical toner
./printer-monitor --critical-toner

# Show only offline printers
./printer-monitor --offline

# Show only online printers
./printer-monitor --online

# Show printers with low black toner
./printer-monitor --color Black --low

# Show printers with low cyan toner
./printer-monitor --color Cyan --low
```

### Combined Examples

```bash
# Compact view of printers with low toner
./printer-monitor --low-toner --compact

# Detailed view of printers with critical toner
./printer-monitor --critical-toner --detailed

# Show only online printers with low magenta toner
./printer-monitor --online --color Magenta --low

# Monitor with custom config and settings
./printer-monitor -c custom.json -s custom-settings.json
```

## Example Output

### Table Layout (Default)
```
========================================================================================================================
Printer Name        DNS/Location         Status      Pages       Toner Levels (K/C/M/Y)
========================================================================================================================
printer-b5-3        printer-b5-3.obs     Running     15234       [████████▓▓] K: 80%  [██████░░░░] C: 60%*  [███████░░░] M: 70%  [█████████░] Y: 90%
printer-b12-2       printer-b12-2.ob     Warning     8421        [███!!!!!!!] K:  8%! [█████░░░░░] C: 50%  [████░░░░░░] M: 40%  [██████░░░░] Y: 60%
========================================================================================================================

Summary: 2 online, 0 warnings, 0 offline | Toner: 1 low (*), 1 critical (!)
```

Legend:
- `█` = Normal toner level
- `▓` = Low toner level (below threshold)
- `!` = Critical toner level (below critical threshold)
- `*` = Low warning marker
- `!` = Critical warning marker

### Compact Layout
```
[●] printer-b5-3              K: 80% C: 60% M: 70% Y: 90%
[●] printer-b12-2             K:  8% C: 50% M: 40% Y: 60%
[○] printer-b7-1              N/A
```

### Detailed Layout
```
================================================================================
Printer: printer-b5-3
--------------------------------------------------------------------------------
DNS Name:      printer-b5-3.obspm.fr
Vendor:        konica_minolta
Location:      Building 5, Floor 3
Status:        Running
Model:         Konica Minolta bizhub C308
Serial:        A00000-12345
Page Count:    15234

Toner Levels:
  Black      [████████████████▓▓▓▓] 80% (1600/2000)
  Cyan       [████████████░░░░░░░░] 60% (1200/2000) [LOW]
  Magenta    [██████████████░░░░░░] 70% (1400/2000)
  Yellow     [██████████████████░░] 90% (1800/2000)
================================================================================
```

### Minimal Layout
```
printer-b5-3: K80%, C60%, M70%, Y90%
printer-b12-2: K8%, C50%, M40%, Y60%
```

## Configuration

### DNS-Based Naming Convention

The tool automatically parses printer information from DNS names:

Format: `printer-bX-Y.domain.com`
- `bX`: Building number (e.g., `b5`, `b12`)
- `Y`: Floor or office number (optional)

Example: `printer-b5-3.obspm.fr`
- Short name: `printer-b5-3`
- Building: `5`
- Floor: `3`

### Settings Configuration

`settings.json` contains global configuration:
```json
{
  "settings": {
    "low_toner_threshold": 20,
    "critical_toner_threshold": 10,
    "default_community": "public"
  }
}
```

Parameters:
- **low_toner_threshold**: Percentage at which toner is marked as low (default: 20%)
- **critical_toner_threshold**: Percentage at which toner is critical (default: 10%)
- **default_community**: Default SNMP community string (default: "public")

### Printer Configuration

`printers.json` contains printer list:
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

Supported vendors:
- `konica_minolta` - Konica Minolta bizhub series
- `xerox` - Xerox multifunction printers

## SNMP Details

### Konica Minolta OIDs

**Standard Printer MIB (tried first):**
- System Description: `1.3.6.1.2.1.1.1.0`
- Device Status: `1.3.6.1.2.1.25.3.5.1.1.1`
- Page Count: `1.3.6.1.2.1.43.10.2.1.4.1.1`
- Supply Level: `1.3.6.1.2.1.43.11.1.1.9.1.X`
- Supply Max: `1.3.6.1.2.1.43.11.1.1.8.1.X`

**Konica Minolta Enterprise OIDs (fallback):**
- Toner Levels: `1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.5.X`
- Toner Max: `1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.6.X`
- Serial: `1.3.6.1.4.1.18334.1.1.1.1.3.2.2.0`

### Xerox OIDs

**Standard Printer MIB (primary):**
- Uses same standard OIDs as Konica Minolta
- Most Xerox printers fully support standard Printer MIB

**Xerox Enterprise OIDs (fallback):**
- Serial: `1.3.6.1.4.1.253.8.53.3.2.1.3.1`
- Model: `1.3.6.1.4.1.253.8.53.13.2.1.6.1.12.1`
- Total Impressions: `1.3.6.1.4.1.253.8.53.13.2.1.6.1.20.1`

Where X = 1 (Black), 2 (Cyan), 3 (Magenta), 4 (Yellow)

## Testing SNMP Manually

### Check connectivity:
```bash
snmpwalk -v2c -c public printer-b5-3.obspm.fr system
```

### Get toner levels:
```bash
# Black toner
snmpget -v2c -c public printer-b5-3.obspm.fr 1.3.6.1.2.1.43.11.1.1.9.1.1

# Cyan toner
snmpget -v2c -c public printer-b5-3.obspm.fr 1.3.6.1.2.1.43.11.1.1.9.1.2
```

### Get page count:
```bash
snmpget -v2c -c public printer-b5-3.obspm.fr 1.3.6.1.2.1.43.10.2.1.4.1.1
```

## Installation

### System-wide installation:
```bash
sudo make install
```

This installs to `/usr/local/bin/printer-monitor`.

### Uninstall:
```bash
sudo make uninstall
```

### Build Options:
```bash
make              # Standard build
make debug        # Build with debug symbols
make release      # Optimized release build
make clean        # Remove build artifacts
make test         # Run basic tests
```

## Architecture

VS-PUT v2.0 uses a modular, plugin-based architecture:

### Core Components:

1. **Vendor Abstraction Layer** - Base `PrinterVendor` class for easy extensibility
2. **Vendor Implementations** - Separate classes for Konica Minolta and Xerox
3. **Display System** - Four interchangeable layout renderers
4. **Filtering System** - Composable filters for advanced queries
5. **Configuration System** - JSON-based settings with DNS parsing

### Adding New Vendors

Easy to extend! See `CLAUDE.md` for detailed instructions on adding new printer vendors.

## Troubleshooting

### "Offline/No SNMP" Status

**Possible causes:**
1. Printer powered off or not on network
2. SNMP not enabled on printer
3. Wrong DNS name or IP address
4. Firewall blocking UDP port 161
5. Wrong community string

**Solutions:**
```bash
# Test basic connectivity
ping printer-b5-3.obspm.fr

# Test SNMP access
snmpwalk -v2c -c public printer-b5-3.obspm.fr system

# Check if port is open
nmap -sU -p 161 printer-b5-3.obspm.fr
```

### No Toner Data

Try browsing available OIDs:
```bash
# For Konica Minolta
snmpwalk -v2c -c public printer-b5-3.obspm.fr 1.3.6.1.4.1.18334

# For Xerox
snmpwalk -v2c -c public printer-b12-2.obspm.fr 1.3.6.1.4.1.253
```

### Build Errors

```bash
# Missing Net-SNMP
sudo apt-get install libsnmp-dev

# Missing compiler
sudo apt-get install build-essential

# Check g++ version (need 4.8+ for C++11)
g++ --version
```

## Automation & Integration

### Cron Job
Monitor printers every hour:
```bash
crontab -e

# Add:
0 * * * * /usr/local/bin/printer-monitor --low-toner >> /var/log/printer-low-toner.log 2>&1
```

### Email Alerts
```bash
#!/bin/bash
OUTPUT=$(./printer-monitor --critical-toner --compact)
if [ ! -z "$OUTPUT" ]; then
    echo "$OUTPUT" | mail -s "CRITICAL: Printer Toner Low" admin@example.com
fi
```

### Nagios/Icinga Check
```bash
#!/bin/bash
OUTPUT=$(./printer-monitor --critical-toner)
if echo "$OUTPUT" | grep -q "critical (!)" ; then
    echo "CRITICAL - Printers with critical toner"
    exit 2
fi

OUTPUT=$(./printer-monitor --low-toner)
if echo "$OUTPUT" | grep -q "low (*)" ; then
    echo "WARNING - Printers with low toner"
    exit 1
fi

echo "OK - All printers have adequate toner"
exit 0
```

## Supported Models

### Konica Minolta
- bizhub C308, C258, C368, C458, C558
- Any Konica Minolta printer with SNMPv2c support

### Xerox
- VersaLink series (C400, C500, C600, C7000, C8000, C9000)
- WorkCentre series
- AltaLink series
- Any Xerox MFP with SNMPv2c support

## Security Considerations

1. **SNMP Community Strings**: Change default "public" in production
2. **Read-Only Access**: Configure SNMP as read-only on printers
3. **Network Segmentation**: Keep printers on separate VLAN
4. **Firewall Rules**: Limit SNMP access (UDP 161) to monitoring hosts
5. **SNMPv3**: Future enhancement for encrypted communication

## Contributing

Contributions welcome! Areas for improvement:
- Additional printer vendors (HP, Canon, Brother, Epson)
- SNMPv3 implementation
- Web dashboard
- Email alerting built-in
- Database logging
- REST API
- Grafana/Prometheus metrics

To add a new vendor, see the detailed guide in `CLAUDE.md`.

## License

MIT License - See LICENSE file for details

## Changelog

### v2.0.0 (Major Release)
- **NEW**: Multi-vendor support (Konica Minolta + Xerox)
- **NEW**: Configurable low/critical toner thresholds
- **NEW**: Multiple display layouts (table, compact, detailed, minimal)
- **NEW**: Advanced filtering system (low toner, critical, offline, color-specific)
- **NEW**: DNS-based configuration with automatic location parsing
- **NEW**: Modular architecture for easy extensibility
- **NEW**: Makefile with install/uninstall targets
- **IMPROVED**: Enhanced visual indicators for toner levels
- **IMPROVED**: Better error handling and reporting
- **IMPROVED**: Comprehensive documentation

### v1.0.0 (Initial Release)
- Net-SNMP integration
- Konica Minolta support
- Basic monitoring functionality

## Support

For issues or questions:
1. Check the troubleshooting section
2. Test with `snmpwalk` command
3. Review `CLAUDE.md` for architecture details
4. Open an issue on GitHub

## Acknowledgments

- Net-SNMP project for SNMP library
- Konica Minolta for SNMP MIB documentation
- Xerox for enterprise OID documentation
- Printer Working Group for standard Printer MIB specifications
