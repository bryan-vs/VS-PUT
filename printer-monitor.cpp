#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <ctime>

// Net-SNMP includes
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

struct Printer {
    std::string name;
    std::string ip;
    std::string community;
    std::string location;
    std::string model;
};

struct TonerLevel {
    std::string color;
    int level;
    int maxLevel;
    int percentage;
};

struct PrinterStatus {
    std::string name;
    std::string ip;
    std::string model;
    std::string status;
    int pageCount;
    std::vector<TonerLevel> toners;
    bool online;
    std::string serialNumber;
    std::string firmwareVersion;
};

// Simple JSON parser for our specific format
class SimpleJSON {
public:
    static std::vector<Printer> parsePrinterList(const std::string& filename) {
        std::vector<Printer> printers;
        std::ifstream file(filename);
        
        if (!file.is_open()) {
            return printers;
        }
        
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        
        size_t pos = 0;
        while ((pos = content.find("{", pos)) != std::string::npos) {
            size_t end = content.find("}", pos);
            if (end == std::string::npos) break;
            
            std::string obj = content.substr(pos, end - pos + 1);
            Printer p;
            
            p.name = extractValue(obj, "name");
            p.ip = extractValue(obj, "ip");
            p.community = extractValue(obj, "community");
            p.location = extractValue(obj, "location");
            p.model = extractValue(obj, "model");
            
            if (!p.ip.empty()) {
                printers.push_back(p);
            }
            
            pos = end + 1;
        }
        
        return printers;
    }
    
    static void savePrinterList(const std::string& filename, 
                               const std::vector<Printer>& printers) {
        std::ofstream file(filename);
        
        file << "{\n  \"printers\": [\n";
        
        for (size_t i = 0; i < printers.size(); i++) {
            file << "    {\n";
            file << "      \"name\": \"" << printers[i].name << "\",\n";
            file << "      \"ip\": \"" << printers[i].ip << "\",\n";
            file << "      \"community\": \"" << printers[i].community << "\",\n";
            file << "      \"location\": \"" << printers[i].location << "\",\n";
            file << "      \"model\": \"" << printers[i].model << "\"\n";
            file << "    }";
            
            if (i < printers.size() - 1) {
                file << ",";
            }
            file << "\n";
        }
        
        file << "  ]\n}\n";
    }
    
private:
    static std::string extractValue(const std::string& json, 
                                    const std::string& key) {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        
        pos = json.find(":", pos);
        if (pos == std::string::npos) return "";
        
        pos = json.find("\"", pos);
        if (pos == std::string::npos) return "";
        pos++;
        
        size_t end = json.find("\"", pos);
        if (end == std::string::npos) return "";
        
        return json.substr(pos, end - pos);
    }
};

// SNMP Query implementation using Net-SNMP
class SNMPQuery {
private:
    static bool initialized;
    
    static void initSNMP() {
        if (!initialized) {
            init_snmp("printer-monitor");
            initialized = true;
        }
    }
    
    static std::string snmpGet(const std::string& ip, 
                               const std::string& community,
                               const std::string& oidStr) {
        struct snmp_session session, *ss;
        struct snmp_pdu *pdu, *response;
        oid anOID[MAX_OID_LEN];
        size_t anOID_len = MAX_OID_LEN;
        int status;
        std::string result;
        
        snmp_sess_init(&session);
        session.version = SNMP_VERSION_2c;
        session.peername = strdup(ip.c_str());
        session.community = (u_char*)strdup(community.c_str());
        session.community_len = strlen((char*)session.community);
        session.timeout = 2000000; // 2 seconds
        session.retries = 1;
        
        ss = snmp_open(&session);
        if (ss == NULL) {
            free(session.peername);
            free(session.community);
            return "";
        }
        
        pdu = snmp_pdu_create(SNMP_MSG_GET);
        
        if (!read_objid(oidStr.c_str(), anOID, &anOID_len)) {
            snmp_close(ss);
            free(session.peername);
            free(session.community);
            return "";
        }
        
        snmp_add_null_var(pdu, anOID, anOID_len);
        
        status = snmp_synch_response(ss, pdu, &response);
        
        if (status == STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR) {
            for (struct variable_list *vars = response->variables; 
                 vars; vars = vars->next_variable) {
                
                if (vars->type == ASN_OCTET_STR) {
                    result = std::string((char*)vars->val.string, vars->val_len);
                } else if (vars->type == ASN_INTEGER) {
                    result = std::to_string(*vars->val.integer);
                } else if (vars->type == ASN_COUNTER) {
                    result = std::to_string(*vars->val.integer);
                } else if (vars->type == ASN_GAUGE) {
                    result = std::to_string(*vars->val.integer);
                }
            }
        }
        
        if (response) snmp_free_pdu(response);
        snmp_close(ss);
        free(session.peername);
        free(session.community);
        
        return result;
    }
    
    static int snmpGetInt(const std::string& ip, 
                          const std::string& community,
                          const std::string& oidStr) {
        std::string result = snmpGet(ip, community, oidStr);
        if (result.empty()) return -1;
        try {
            return std::stoi(result);
        } catch (...) {
            return -1;
        }
    }

public:
    static void initialize() {
        initSNMP();
    }
    
    static void cleanup() {
        snmp_shutdown("printer-monitor");
    }
    
    static PrinterStatus queryPrinter(const Printer& printer) {
        PrinterStatus status;
        status.name = printer.name;
        status.ip = printer.ip;
        status.model = printer.model;
        status.online = false;
        
        // Test connectivity with system description
        std::string sysDesc = snmpGet(printer.ip, printer.community, 
                                     "1.3.6.1.2.1.1.1.0");
        
        if (sysDesc.empty()) {
            status.status = "Offline/No SNMP";
            return status;
        }
        
        status.online = true;
        
        // Get device status (hrDeviceStatus)
        int deviceStatus = snmpGetInt(printer.ip, printer.community,
                                     "1.3.6.1.2.1.25.3.5.1.1.1");
        switch (deviceStatus) {
            case 1: status.status = "Unknown"; break;
            case 2: status.status = "Running"; break;
            case 3: status.status = "Warning"; break;
            case 4: status.status = "Testing"; break;
            case 5: status.status = "Down"; break;
            default: status.status = "Ready";
        }
        
        // Get printer status (hrPrinterStatus)
        int printerStatus = snmpGetInt(printer.ip, printer.community,
                                      "1.3.6.1.2.1.25.3.5.1.1.1");
        
        // Get page count (prtMarkerLifeCount)
        status.pageCount = snmpGetInt(printer.ip, printer.community,
                                     "1.3.6.1.2.1.43.10.2.1.4.1.1");
        if (status.pageCount < 0) {
            // Try alternative OID for Konica Minolta
            status.pageCount = snmpGetInt(printer.ip, printer.community,
                                         "1.3.6.1.4.1.18334.1.1.1.5.7.2.1.5.1");
        }
        
        // Get serial number
        status.serialNumber = snmpGet(printer.ip, printer.community,
                                     "1.3.6.1.4.1.18334.1.1.1.1.3.2.2.0");
        
        // Get toner levels
        status.toners = getTonerLevels(printer.ip, printer.community);
        
        return status;
    }
    
private:
    static std::vector<TonerLevel> getTonerLevels(const std::string& ip,
                                                   const std::string& community) {
        std::vector<TonerLevel> toners;
        
        // Standard Printer MIB approach
        // Try indices 1-4 for typical CMYK or KCMY setup
        std::vector<std::string> colorNames = {"Black", "Cyan", "Magenta", "Yellow"};
        
        for (int i = 1; i <= 4; i++) {
            TonerLevel toner;
            
            // Get supply description
            std::string oidDesc = "1.3.6.1.2.1.43.11.1.1.6.1." + std::to_string(i);
            std::string desc = snmpGet(ip, community, oidDesc);
            
            // Get current level
            std::string oidLevel = "1.3.6.1.2.1.43.11.1.1.9.1." + std::to_string(i);
            int level = snmpGetInt(ip, community, oidLevel);
            
            // Get max capacity
            std::string oidMax = "1.3.6.1.2.1.43.11.1.1.8.1." + std::to_string(i);
            int maxCap = snmpGetInt(ip, community, oidMax);
            
            if (level >= 0 && maxCap > 0) {
                // Parse color from description or use default
                if (!desc.empty()) {
                    // Extract color name from description
                    if (desc.find("Black") != std::string::npos || 
                        desc.find("black") != std::string::npos ||
                        desc.find("K") != std::string::npos) {
                        toner.color = "Black";
                    } else if (desc.find("Cyan") != std::string::npos || 
                               desc.find("cyan") != std::string::npos) {
                        toner.color = "Cyan";
                    } else if (desc.find("Magenta") != std::string::npos || 
                               desc.find("magenta") != std::string::npos) {
                        toner.color = "Magenta";
                    } else if (desc.find("Yellow") != std::string::npos || 
                               desc.find("yellow") != std::string::npos) {
                        toner.color = "Yellow";
                    } else {
                        toner.color = colorNames[(i-1) % 4];
                    }
                } else {
                    toner.color = colorNames[(i-1) % 4];
                }
                
                toner.level = level;
                toner.maxLevel = maxCap;
                toner.percentage = (level * 100) / maxCap;
                
                // Sanity check
                if (toner.percentage > 100) toner.percentage = 100;
                if (toner.percentage < 0) toner.percentage = 0;
                
                toners.push_back(toner);
            }
        }
        
        // If standard MIB didn't work, try Konica Minolta specific OIDs
        if (toners.empty()) {
            // Konica Minolta specific toner OIDs
            struct KMToner {
                std::string name;
                std::string levelOID;
                std::string maxOID;
            };
            
            std::vector<KMToner> kmToners = {
                {"Black", "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.5.1", "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.6.1"},
                {"Cyan", "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.5.2", "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.6.2"},
                {"Magenta", "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.5.3", "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.6.3"},
                {"Yellow", "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.5.4", "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.6.4"}
            };
            
            for (const auto& kmt : kmToners) {
                int level = snmpGetInt(ip, community, kmt.levelOID);
                int maxCap = snmpGetInt(ip, community, kmt.maxOID);
                
                if (level >= 0 && maxCap > 0) {
                    TonerLevel toner;
                    toner.color = kmt.name;
                    toner.level = level;
                    toner.maxLevel = maxCap;
                    toner.percentage = (level * 100) / maxCap;
                    
                    if (toner.percentage > 100) toner.percentage = 100;
                    if (toner.percentage < 0) toner.percentage = 0;
                    
                    toners.push_back(toner);
                }
            }
        }
        
        return toners;
    }
};

bool SNMPQuery::initialized = false;

// Display utilities
class Display {
public:
    static void printTable(const std::vector<PrinterStatus>& statuses) {
        std::cout << "\n";
        printSeparator();
        printHeader();
        printSeparator();
        
        for (const auto& status : statuses) {
            printPrinterRow(status);
        }
        
        printSeparator();
        
        // Print summary
        int online = 0, offline = 0, warning = 0;
        for (const auto& status : statuses) {
            if (!status.online) {
                offline++;
            } else if (status.status == "Warning") {
                warning++;
            } else {
                online++;
            }
        }
        
        std::cout << "\nSummary: " << online << " online, " 
                  << warning << " warnings, " 
                  << offline << " offline\n";
        
        std::cout << "\n";
    }
    
    static void printDetailed(const std::vector<PrinterStatus>& statuses) {
        for (const auto& status : statuses) {
            std::cout << "\n" << std::string(80, '=') << "\n";
            std::cout << "Printer: " << status.name << "\n";
            std::cout << std::string(80, '-') << "\n";
            std::cout << "IP Address:    " << status.ip << "\n";
            std::cout << "Model:         " << status.model << "\n";
            std::cout << "Status:        " << (status.online ? status.status : "OFFLINE") << "\n";
            
            if (!status.serialNumber.empty()) {
                std::cout << "Serial:        " << status.serialNumber << "\n";
            }
            
            if (status.online) {
                if (status.pageCount >= 0) {
                    std::cout << "Page Count:    " << status.pageCount << "\n";
                }
                
                if (!status.toners.empty()) {
                    std::cout << "\nToner Levels:\n";
                    for (const auto& toner : status.toners) {
                        std::cout << "  " << std::left << std::setw(10) << toner.color
                                  << getTonerBar(toner) << " "
                                  << std::right << std::setw(3) << toner.percentage << "%"
                                  << " (" << toner.level << "/" << toner.maxLevel << ")\n";
                    }
                }
            }
        }
        std::cout << "\n" << std::string(80, '=') << "\n\n";
    }
    
private:
    static void printSeparator() {
        std::cout << std::string(120, '=') << "\n";
    }
    
    static void printHeader() {
        std::cout << std::left
                  << std::setw(20) << "Printer Name"
                  << std::setw(16) << "IP Address"
                  << std::setw(12) << "Status"
                  << std::setw(10) << "Pages"
                  << std::setw(62) << "Toner Levels (K/C/M/Y)"
                  << "\n";
    }
    
    static void printPrinterRow(const PrinterStatus& status) {
        std::cout << std::left
                  << std::setw(20) << truncate(status.name, 19)
                  << std::setw(16) << status.ip
                  << std::setw(12) << (status.online ? status.status : "OFFLINE");
        
        if (status.online) {
            if (status.pageCount >= 0) {
                std::cout << std::setw(10) << status.pageCount;
            } else {
                std::cout << std::setw(10) << "N/A";
            }
            
            if (!status.toners.empty()) {
                for (const auto& toner : status.toners) {
                    std::cout << getTonerBar(toner) << " ";
                }
                std::cout << "\n" << std::string(58, ' ');
                bool first = true;
                for (const auto& toner : status.toners) {
                    if (!first) std::cout << ", ";
                    std::cout << toner.color.substr(0, 1) << ":" 
                              << toner.percentage << "%";
                    first = false;
                }
            } else {
                std::cout << "No toner data";
            }
        } else {
            std::cout << std::setw(10) << "N/A"
                      << "N/A";
        }
        
        std::cout << "\n";
    }
    
    static std::string getTonerBar(const TonerLevel& toner) {
        const int barWidth = 10;
        int filled = (toner.percentage * barWidth) / 100;
        
        std::string bar = "[";
        for (int i = 0; i < barWidth; i++) {
            if (i < filled) {
                bar += "█";
            } else {
                bar += "░";
            }
        }
        bar += "]";
        
        return bar;
    }
    
    static std::string truncate(const std::string& str, size_t width) {
        if (str.length() <= width) return str;
        return str.substr(0, width - 3) + "...";
    }
};

void printUsage(const char* prog) {
    std::cout << "Printer Monitor v1.0 - Konica Minolta SNMP Monitor\n\n"
              << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  -h, --help           Show this help message\n"
              << "  -f, --file FILE      Specify printer list file (default: printers.json)\n"
              << "  -a, --add            Add a new printer to the list\n"
              << "  -l, --list           List configured printers\n"
              << "  -m, --monitor        Monitor printers (default action)\n"
              << "  -d, --detailed       Show detailed printer information\n"
              << "  -v, --verbose        Verbose output\n\n"
              << "Examples:\n"
              << "  " << prog << "                  # Monitor all printers\n"
              << "  " << prog << " --add            # Add a new printer\n"
              << "  " << prog << " --detailed       # Show detailed information\n"
              << "  " << prog << " -f custom.json   # Use custom printer list\n\n"
              << "Requirements:\n"
              << "  - Printers must have SNMP enabled (SNMPv2c)\n"
              << "  - Default community string: 'public'\n"
              << "  - Network connectivity to printer IPs\n";
}

void addPrinter(const std::string& filename) {
    std::vector<Printer> printers = SimpleJSON::parsePrinterList(filename);
    
    Printer newPrinter;
    
    std::cout << "\nAdd New Konica Minolta Printer\n";
    std::cout << "==============================\n\n";
    
    std::cout << "Printer Name: ";
    std::getline(std::cin, newPrinter.name);
    
    std::cout << "IP Address: ";
    std::getline(std::cin, newPrinter.ip);
    
    std::cout << "SNMP Community (default: public): ";
    std::getline(std::cin, newPrinter.community);
    if (newPrinter.community.empty()) {
        newPrinter.community = "public";
    }
    
    std::cout << "Location: ";
    std::getline(std::cin, newPrinter.location);
    
    std::cout << "Model (e.g., C308, C258): ";
    std::getline(std::cin, newPrinter.model);
    
    // Test connection
    std::cout << "\nTesting connection to " << newPrinter.ip << "...";
    std::cout.flush();
    
    PrinterStatus test = SNMPQuery::queryPrinter(newPrinter);
    
    if (test.online) {
        std::cout << " SUCCESS!\n";
        std::cout << "Printer is online and responding to SNMP queries.\n";
    } else {
        std::cout << " FAILED!\n";
        std::cout << "Warning: Could not connect to printer.\n";
        std::cout << "The printer will be added but may show as offline.\n";
        std::cout << "Please verify:\n";
        std::cout << "  - IP address is correct\n";
        std::cout << "  - SNMP is enabled on the printer\n";
        std::cout << "  - Community string is correct\n";
        std::cout << "  - Network connectivity\n\n";
        
        std::cout << "Add anyway? (y/n): ";
        std::string confirm;
        std::getline(std::cin, confirm);
        if (confirm != "y" && confirm != "Y") {
            std::cout << "Cancelled.\n";
            return;
        }
    }
    
    printers.push_back(newPrinter);
    SimpleJSON::savePrinterList(filename, printers);
    
    std::cout << "\nPrinter added successfully to " << filename << "!\n";
}

void listPrinters(const std::string& filename) {
    std::vector<Printer> printers = SimpleJSON::parsePrinterList(filename);
    
    if (printers.empty()) {
        std::cout << "No printers configured.\n";
        std::cout << "Use '" << "--add" << "' to add a printer.\n";
        return;
    }
    
    std::cout << "\nConfigured Printers (" << printers.size() << "):\n";
    std::cout << std::string(80, '=') << "\n\n";
    
    for (size_t i = 0; i < printers.size(); i++) {
        std::cout << (i + 1) << ". " << printers[i].name << "\n";
        std::cout << "   IP:       " << printers[i].ip << "\n";
        std::cout << "   Location: " << printers[i].location << "\n";
        std::cout << "   Model:    " << printers[i].model << "\n";
        std::cout << "   SNMP:     " << printers[i].community << "\n\n";
    }
}

void monitorPrinters(const std::string& filename, bool detailed, bool verbose) {
    std::vector<Printer> printers = SimpleJSON::parsePrinterList(filename);
    
    if (printers.empty()) {
        std::cout << "No printers configured.\n";
        std::cout << "Use '--add' to add printers.\n";
        return;
    }
    
    if (verbose) {
        std::cout << "Querying " << printers.size() << " printer(s)...\n";
    }
    
    std::vector<PrinterStatus> statuses;
    for (size_t i = 0; i < printers.size(); i++) {
        if (verbose) {
            std::cout << "  [" << (i+1) << "/" << printers.size() << "] "
                      << printers[i].name << " (" << printers[i].ip << ")...";
            std::cout.flush();
        }
        
        PrinterStatus status = SNMPQuery::queryPrinter(printers[i]);
        statuses.push_back(status);
        
        if (verbose) {
            std::cout << (status.online ? " OK" : " OFFLINE") << "\n";
        }
    }
    
    if (detailed) {
        Display::printDetailed(statuses);
    } else {
        Display::printTable(statuses);
    }
}

int main(int argc, char* argv[]) {
    std::string filename = "printers.json";
    std::string action = "monitor";
    bool detailed = false;
    bool verbose = false;
    
    // Initialize SNMP
    SNMPQuery::initialize();
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            SNMPQuery::cleanup();
            return 0;
        } else if (arg == "-f" || arg == "--file") {
            if (i + 1 < argc) {
                filename = argv[++i];
            } else {
                std::cerr << "Error: --file requires an argument\n";
                SNMPQuery::cleanup();
                return 1;
            }
        } else if (arg == "-a" || arg == "--add") {
            action = "add";
        } else if (arg == "-l" || arg == "--list") {
            action = "list";
        } else if (arg == "-m" || arg == "--monitor") {
            action = "monitor";
        } else if (arg == "-d" || arg == "--detailed") {
            detailed = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            SNMPQuery::cleanup();
            return 1;
        }
    }
    
    // Execute action
    try {
        if (action == "add") {
            addPrinter(filename);
        } else if (action == "list") {
            listPrinters(filename);
        } else {
            monitorPrinters(filename, detailed, verbose);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        SNMPQuery::cleanup();
        return 1;
    }
    
    // Cleanup SNMP
    SNMPQuery::cleanup();
    
    return 0;
}
