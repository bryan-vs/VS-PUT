# VS-PUT Printer Utility Tool

A production-ready CLI tool for monitoring Konica Minolta printer toner levels, page counts, and status via SNMP (Simple Network Management Protocol).

## Features

✅ **Real SNMP Implementation** - Uses Net-SNMP library for actual printer queries  
✅ **Multiple Printers** - Monitor multiple printers simultaneously  
✅ **Visual Toner Bars** - Easy-to-read toner level indicators  
✅ **Page Counters** - Track total pages printed  
✅ **JSON Configuration** - Simple printer management  
✅ **Connection Testing** - Verify printer connectivity when adding  
✅ **Detailed Mode** - Comprehensive printer information display  
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
chmod +x setup.sh
./setup.sh
```

The setup script will:
- Check all dependencies
- Build the application
- Create sample configuration
- Optionally test SNMP connectivity

### 2. Configure Printer SNMP

On your Konica Minolta printer:
1. Access the web interface (http://PRINTER_IP)
2. Navigate to: **Network → SNMP Settings**
3. Enable **SNMPv2c**
4. Set community name to: **public** (or custom)
5. Ensure **Read** permission is granted
6. Save settings

### 3. Add Printers
```bash
./printer-monitor --add
```

Follow the prompts to add your printer. The tool will test connectivity automatically.

### 4. Monitor
```bash
./printer-monitor
```

## Usage

### Basic Commands

```bash
# Monitor all printers (default)
./printer-monitor

# Show detailed information
./printer-monitor --detailed

# Add a new printer
./printer-monitor --add

# List configured printers
./printer-monitor --list

# Verbose output during queries
./printer-monitor --verbose

# Use custom configuration file
./printer-monitor --file /path/to/custom.json

# Show help
./printer-monitor --help
```

### Example Output

**Table View** (default):
```
========================================================================================================================
Printer Name        IP Address      Status      Pages       Toner Levels (K/C/M/Y)
========================================================================================================================
Office-KM-C308      192.168.1.100   Running     15234       [████████░░] [██████░░░░] [███████░░░] [█████████░]
                                                            K:80%, C:60%, M:70%, Y:90%
Floor2-KM-C258      192.168.1.101   Warning     8421        [███░░░░░░░] [█████░░░░░] [████░░░░░░] [██████░░░░]
                                                            K:30%, C:50%, M:40%, Y:60%
Lab-KM-C368         192.168.1.102   OFFLINE     N/A         N/A
========================================================================================================================

Summary: 2 online, 1 warnings, 1 offline
```

**Detailed View** (`--detailed`):
```
================================================================================
Printer: Office-KM-C308
--------------------------------------------------------------------------------
IP Address:    192.168.1.100
Model:         Konica Minolta C308
Status:        Running
Serial:        A00000-12345
Page Count:    15234

Toner Levels:
  Black      [████████░░] 80% (1600/2000)
  Cyan       [██████░░░░] 60% (1200/2000)
  Magenta    [███████░░░] 70% (1400/2000)
  Yellow     [█████████░] 90% (1800/2000)
================================================================================
```

## Configuration

Printers are stored in `printers.json`:

```json
{
  "printers": [
    {
      "name": "Office-KM-C308",
      "ip": "192.168.1.100",
      "community": "public",
      "location": "Main Office",
      "model": "Konica Minolta C308"
    },
    {
      "name": "Floor2-KM-C258",
      "ip": "192.168.1.101",
      "community": "public",
      "location": "Floor 2 Copy Room",
      "model": "Konica Minolta C258"
    }
  ]
}
```

### Manual Configuration

You can edit `printers.json` directly:

- **name**: Friendly printer name
- **ip**: Printer IP address
- **community**: SNMP community string (usually "public")
- **location**: Physical location description
- **model**: Printer model

## SNMP Details

### Standard Printer MIB OIDs Used

The tool queries these standard SNMP OIDs:

| Data | OID | Description |
|------|-----|-------------|
| System Description | 1.3.6.1.2.1.1.1.0 | Basic device info |
| Device Status | 1.3.6.1.2.1.25.3.5.1.1.1 | Running/Warning/Down |
| Page Count | 1.3.6.1.2.1.43.10.2.1.4.1.1 | Total pages printed |
| Supply Description | 1.3.6.1.2.1.43.11.1.1.6.1.X | Toner color names |
| Supply Level | 1.3.6.1.2.1.43.11.1.1.9.1.X | Current toner level |
| Supply Max | 1.3.6.1.2.1.43.11.1.1.8.1.X | Maximum toner capacity |

### Konica Minolta Specific OIDs

If standard OIDs fail, the tool falls back to Konica Minolta enterprise OIDs:

| Data | OID Base | Description |
|------|----------|-------------|
| Toner Levels | 1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.5.X | KM-specific toner levels |
| Toner Max | 1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.6.X | KM-specific max capacity |
| Serial Number | 1.3.6.1.4.1.18334.1.1.1.1.3.2.2.0 | Device serial number |

Where X = 1 (Black), 2 (Cyan), 3 (Magenta), 4 (Yellow)

## Testing SNMP Manually

### Check connectivity:
```bash
snmpwalk -v2c -c public 192.168.1.100 system
```

### Get toner levels:
```bash
# Black toner
snmpget -v2c -c public 192.168.1.100 1.3.6.1.2.1.43.11.1.1.9.1.1

# Cyan toner
snmpget -v2c -c public 192.168.1.100 1.3.6.1.2.1.43.11.1.1.9.1.2
```

### Get page count:
```bash
snmpget -v2c -c public 192.168.1.100 1.3.6.1.2.1.43.10.2.1.4.1.1
```

## Installation

### System-wide installation:
```bash
sudo make install
```

This installs to `/usr/local/bin/printer-monitor`, making it available system-wide.

### Uninstall:
```bash
sudo make uninstall
```

## Troubleshooting

### "Offline/No SNMP" Status

**Possible causes:**
1. Printer is powered off or not on network
2. SNMP is not enabled on printer
3. Wrong IP address
4. Firewall blocking UDP port 161
5. Wrong community string

**Solutions:**
```bash
# Test basic connectivity
ping 192.168.1.100

# Test SNMP access
snmpwalk -v2c -c public 192.168.1.100 system

# Check if port is open
nmap -sU -p 161 192.168.1.100
```

### No Toner Data

Some Konica Minolta models may use different OIDs. If standard OIDs don't work:

1. Browse available OIDs:
```bash
snmpwalk -v2c -c public 192.168.1.100 1.3.6.1.4.1.18334
```

2. Look for toner-related OIDs in the output
3. Update the code with correct OIDs for your model

### Permission Denied

If you get permission errors:
```bash
# Run with sudo
sudo ./printer-monitor

# Or fix permissions
chmod +x printer-monitor
```

### Build Errors

**Missing Net-SNMP:**
```bash
sudo apt-get install libsnmp-dev
```

**Compiler errors:**
```bash
# Ensure build tools are installed
sudo apt-get install build-essential

# Check g++ version (need 4.8+)
g++ --version
```

## Windows Compilation

### Using MSYS2 (Recommended)

1. Install MSYS2 from https://www.msys2.org/
2. Open MSYS2 MinGW 64-bit terminal
3. Install dependencies:
```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-net-snmp
```
4. Build:
```bash
g++ -std=c++11 -o printer-monitor.exe printer_monitor.cpp \
    $(net-snmp-config --cflags) $(net-snmp-config --libs)
```

### Using Visual Studio

1. Download Net-SNMP Windows binaries
2. Create new C++ Console Application
3. Add include/lib directories to project settings
4. Add `netsnmp.lib` to linker input
5. Build solution

### Cross-Compilation from Linux

```bash
# Install MinGW
sudo apt-get install mingw-w64

# Note: Net-SNMP for Windows needed separately
# Build (if Net-SNMP libs available)
make windows
```

## Automation & Integration

### Cron Job (Linux)
Monitor printers every hour:
```bash
# Edit crontab
crontab -e

# Add line:
0 * * * * /usr/local/bin/printer-monitor >> /var/log/printer-monitor.log 2>&1
```

### Email Alerts
Combine with mail command:
```bash
#!/bin/bash
OUTPUT=$(./printer-monitor)
if echo "$OUTPUT" | grep -q "Warning\|OFFLINE"; then
    echo "$OUTPUT" | mail -s "Printer Alert" admin@example.com
fi
```

### Nagios/Icinga Integration
Create check script:
```bash
#!/bin/bash
OUTPUT=$(./printer-monitor -f /etc/printer-monitor/printers.json)
if echo "$OUTPUT" | grep -q "OFFLINE"; then
    echo "CRITICAL - Printer offline"
    exit 2
elif echo "$OUTPUT" | grep -q "Warning"; then
    echo "WARNING - Printer warning"
    exit 1
else
    echo "OK - All printers operational"
    exit 0
fi
```

## Advanced Usage

### Multiple Configuration Files

Organize printers by location:
```bash
# Office printers
./printer-monitor -f office.json

# Warehouse printers
./printer-monitor -f warehouse.json

# All printers
./printer-monitor -f all.json
```

### Script Integration

```bash
#!/bin/bash
# Check if any toner below 20%
./printer-monitor --detailed | grep -E "[0-9]{1}%" && {
    echo "Low toner detected!"
    # Send notification, create ticket, etc.
}
```

## Supported Models

Tested with:
- Konica Minolta bizhub C308
- Konica Minolta bizhub C258
- Konica Minolta bizhub C368
- Konica Minolta bizhub C458
- Konica Minolta bizhub C558

Should work with any Konica Minolta printer supporting SNMPv2c and standard Printer MIB.

## Security Considerations

1. **SNMP Community Strings**: Change default "public" to something secure
2. **Network Segmentation**: Keep printers on separate VLAN
3. **Read-Only Access**: Ensure SNMP is configured as read-only
4. **Firewall Rules**: Limit SNMP access to monitoring hosts only
5. **SNMPv3**: Consider upgrading printers to SNMPv3 for authentication

## Contributing

Contributions welcome! Areas for improvement:
- Additional printer brand support
- SNMPv3 implementation
- Web dashboard
- Email alerting
- Database logging
- REST API

## License

MIT License - See LICENSE file for details

## Changelog

### v1.0.0 (Production Release)
- Full Net-SNMP integration
- Konica Minolta specific OID support
- Connection testing on printer addition
- Detailed view mode
- Verbose output option
- Enhanced error handling
- Production-ready SNMP queries

## Support

For issues, questions, or contributions:
- Open an issue on GitHub
- Check printer SNMP settings
- Review troubleshooting section
- Test with `snmpwalk` command first

## Acknowledgments

- Net-SNMP project for SNMP library
- Konica Minolta for SNMP MIB documentation
- Printer Working Group for standard Printer MIB
