#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <memory>
#include <map>
#include <functional>

// Net-SNMP includes
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

// ============================================================================
// CONFIGURATION & SETTINGS
// ============================================================================

struct Settings {
    int lowTonerThreshold;      // Percentage below which toner is considered low
    int criticalTonerThreshold; // Percentage below which toner is critical
    std::string defaultCommunity;
    int snmpTimeout;            // Timeout in microseconds
    int snmpRetries;

    Settings()
        : lowTonerThreshold(20)
        , criticalTonerThreshold(10)
        , defaultCommunity("public")
        , snmpTimeout(2000000)  // 2 seconds
        , snmpRetries(1)
    {}
};

// ============================================================================
// DATA STRUCTURES
// ============================================================================

struct TonerLevel {
    std::string color;
    int level;
    int maxLevel;
    int percentage;
    bool isLow;
    bool isCritical;

    TonerLevel() : level(-1), maxLevel(-1), percentage(-1), isLow(false), isCritical(false) {}
};

struct PrinterConfig {
    std::string dnsName;        // Primary identifier (e.g., "printer-b5-3.obspm.fr")
    std::string vendor;         // "konica_minolta" or "xerox"
    std::string community;      // SNMP community string

    // Derived from DNS name
    std::string shortName;      // "printer-b5-3" (without domain)
    std::string building;       // "b5"
    std::string floor;          // "3"

    PrinterConfig() : community("public") {}

    void parseDnsName(const std::string& domain = ".obspm.fr") {
        // Remove domain suffix to get short name
        size_t pos = dnsName.find(domain);
        if (pos != std::string::npos) {
            shortName = dnsName.substr(0, pos);
        } else {
            shortName = dnsName;
        }

        // Parse building and floor from short name
        // Expected format: printer-bX-Y or printer-bX
        std::string pattern = shortName;
        size_t bPos = pattern.find("-b");
        if (bPos != std::string::npos) {
            size_t dashAfterB = pattern.find("-", bPos + 2);
            if (dashAfterB != std::string::npos) {
                building = pattern.substr(bPos + 2, dashAfterB - bPos - 2);
                floor = pattern.substr(dashAfterB + 1);
                // Remove any non-numeric characters from floor
                floor.erase(std::remove_if(floor.begin(), floor.end(),
                    [](char c) { return !std::isdigit(c); }), floor.end());
            } else {
                building = pattern.substr(bPos + 2);
                floor = "";
            }
        }
    }
};

struct PrinterStatus {
    PrinterConfig config;
    std::string status;
    int pageCount;
    std::vector<TonerLevel> toners;
    bool online;
    std::string serialNumber;
    std::string model;
    bool hasLowToner;
    bool hasCriticalToner;

    PrinterStatus() : pageCount(-1), online(false), hasLowToner(false), hasCriticalToner(false) {}
};

// ============================================================================
// SNMP QUERY UTILITIES
// ============================================================================

class SNMPHelper {
private:
    static bool initialized;
    Settings settings;

public:
    SNMPHelper(const Settings& s) : settings(s) {
        if (!initialized) {
            init_snmp("printer-monitor");
            initialized = true;
        }
    }

    ~SNMPHelper() {
        // Don't cleanup here as other instances might still need it
    }

    static void cleanup() {
        snmp_shutdown("printer-monitor");
    }

    std::string snmpGet(const std::string& ip, const std::string& community,
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
        session.timeout = settings.snmpTimeout;
        session.retries = settings.snmpRetries;

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

    int snmpGetInt(const std::string& ip, const std::string& community,
                   const std::string& oidStr) {
        std::string result = snmpGet(ip, community, oidStr);
        if (result.empty()) return -1;
        try {
            return std::stoi(result);
        } catch (...) {
            return -1;
        }
    }
};

bool SNMPHelper::initialized = false;

// ============================================================================
// VENDOR ABSTRACTION
// ============================================================================

class PrinterVendor {
protected:
    std::string vendorName;
    Settings settings;

public:
    PrinterVendor(const std::string& name, const Settings& s)
        : vendorName(name), settings(s) {}

    virtual ~PrinterVendor() {}

    virtual PrinterStatus queryPrinter(const PrinterConfig& config,
                                      SNMPHelper& snmp) = 0;

    std::string getVendorName() const { return vendorName; }

protected:
    void evaluateTonerLevels(PrinterStatus& status) {
        status.hasLowToner = false;
        status.hasCriticalToner = false;

        for (auto& toner : status.toners) {
            if (toner.percentage >= 0) {
                toner.isLow = (toner.percentage <= settings.lowTonerThreshold);
                toner.isCritical = (toner.percentage <= settings.criticalTonerThreshold);

                if (toner.isLow) status.hasLowToner = true;
                if (toner.isCritical) status.hasCriticalToner = true;
            }
        }
    }
};

// ============================================================================
// KONICA MINOLTA VENDOR IMPLEMENTATION
// ============================================================================

class KonicaMinoltaVendor : public PrinterVendor {
private:
    // Standard Printer MIB OIDs
    const std::string OID_SYSTEM_DESC = "1.3.6.1.2.1.1.1.0";
    const std::string OID_DEVICE_STATUS = "1.3.6.1.2.1.25.3.5.1.1.1";
    const std::string OID_PAGE_COUNT = "1.3.6.1.2.1.43.10.2.1.4.1.1";

    // Konica Minolta Enterprise OIDs
    const std::string OID_KM_SERIAL = "1.3.6.1.4.1.18334.1.1.1.1.3.2.2.0";
    const std::string OID_KM_PAGE_COUNT = "1.3.6.1.4.1.18334.1.1.1.5.7.2.1.5.1";
    const std::string OID_KM_MODEL = "1.3.6.1.4.1.18334.1.1.1.1.1.12.0";

    struct TonerOID {
        std::string color;
        std::string levelOID;
        std::string maxOID;
    };

public:
    KonicaMinoltaVendor(const Settings& s) : PrinterVendor("Konica Minolta", s) {}

    PrinterStatus queryPrinter(const PrinterConfig& config, SNMPHelper& snmp) override {
        PrinterStatus status;
        status.config = config;
        status.online = false;

        // Test connectivity
        std::string sysDesc = snmp.snmpGet(config.dnsName, config.community, OID_SYSTEM_DESC);
        if (sysDesc.empty()) {
            status.status = "Offline/No SNMP";
            return status;
        }

        status.online = true;

        // Get device status
        int deviceStatus = snmp.snmpGetInt(config.dnsName, config.community, OID_DEVICE_STATUS);
        switch (deviceStatus) {
            case 1: status.status = "Unknown"; break;
            case 2: status.status = "Running"; break;
            case 3: status.status = "Warning"; break;
            case 4: status.status = "Testing"; break;
            case 5: status.status = "Down"; break;
            default: status.status = "Ready";
        }

        // Get page count (try standard MIB first, then KM-specific)
        status.pageCount = snmp.snmpGetInt(config.dnsName, config.community, OID_PAGE_COUNT);
        if (status.pageCount < 0) {
            status.pageCount = snmp.snmpGetInt(config.dnsName, config.community, OID_KM_PAGE_COUNT);
        }

        // Get serial number
        status.serialNumber = snmp.snmpGet(config.dnsName, config.community, OID_KM_SERIAL);

        // Get model
        status.model = snmp.snmpGet(config.dnsName, config.community, OID_KM_MODEL);
        if (status.model.empty()) {
            status.model = "Konica Minolta";
        }

        // Get toner levels
        status.toners = getTonerLevels(config, snmp);
        evaluateTonerLevels(status);

        return status;
    }

private:
    std::vector<TonerLevel> getTonerLevels(const PrinterConfig& config, SNMPHelper& snmp) {
        std::vector<TonerLevel> toners;

        // Try standard Printer MIB first
        toners = tryStandardMIB(config, snmp);

        // If that failed, try Konica Minolta specific OIDs
        if (toners.empty()) {
            toners = tryKonicaMinoltaOIDs(config, snmp);
        }

        return toners;
    }

    std::vector<TonerLevel> tryStandardMIB(const PrinterConfig& config, SNMPHelper& snmp) {
        std::vector<TonerLevel> toners;
        std::vector<std::string> colorNames = {"Black", "Cyan", "Magenta", "Yellow"};

        for (int i = 1; i <= 4; i++) {
            TonerLevel toner;

            // Get supply description
            std::string oidDesc = "1.3.6.1.2.1.43.11.1.1.6.1." + std::to_string(i);
            std::string desc = snmp.snmpGet(config.dnsName, config.community, oidDesc);

            // Get current level
            std::string oidLevel = "1.3.6.1.2.1.43.11.1.1.9.1." + std::to_string(i);
            int level = snmp.snmpGetInt(config.dnsName, config.community, oidLevel);

            // Get max capacity
            std::string oidMax = "1.3.6.1.2.1.43.11.1.1.8.1." + std::to_string(i);
            int maxCap = snmp.snmpGetInt(config.dnsName, config.community, oidMax);

            if (level >= 0 && maxCap > 0) {
                toner.color = parseColorFromDescription(desc, colorNames[(i-1) % 4]);
                toner.level = level;
                toner.maxLevel = maxCap;
                toner.percentage = (level * 100) / maxCap;

                if (toner.percentage > 100) toner.percentage = 100;
                if (toner.percentage < 0) toner.percentage = 0;

                toners.push_back(toner);
            }
        }

        return toners;
    }

    std::vector<TonerLevel> tryKonicaMinoltaOIDs(const PrinterConfig& config, SNMPHelper& snmp) {
        std::vector<TonerLevel> toners;

        std::vector<TonerOID> kmToners = {
            {"Black", "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.5.1", "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.6.1"},
            {"Cyan", "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.5.2", "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.6.2"},
            {"Magenta", "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.5.3", "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.6.3"},
            {"Yellow", "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.5.4", "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.6.4"}
        };

        for (const auto& kmt : kmToners) {
            int level = snmp.snmpGetInt(config.dnsName, config.community, kmt.levelOID);
            int maxCap = snmp.snmpGetInt(config.dnsName, config.community, kmt.maxOID);

            if (level >= 0 && maxCap > 0) {
                TonerLevel toner;
                toner.color = kmt.color;
                toner.level = level;
                toner.maxLevel = maxCap;
                toner.percentage = (level * 100) / maxCap;

                if (toner.percentage > 100) toner.percentage = 100;
                if (toner.percentage < 0) toner.percentage = 0;

                toners.push_back(toner);
            }
        }

        return toners;
    }

    std::string parseColorFromDescription(const std::string& desc, const std::string& defaultColor) {
        if (desc.empty()) return defaultColor;

        if (desc.find("Black") != std::string::npos || desc.find("black") != std::string::npos ||
            desc.find("K") != std::string::npos) {
            return "Black";
        } else if (desc.find("Cyan") != std::string::npos || desc.find("cyan") != std::string::npos) {
            return "Cyan";
        } else if (desc.find("Magenta") != std::string::npos || desc.find("magenta") != std::string::npos) {
            return "Magenta";
        } else if (desc.find("Yellow") != std::string::npos || desc.find("yellow") != std::string::npos) {
            return "Yellow";
        }

        return defaultColor;
    }
};

// ============================================================================
// XEROX VENDOR IMPLEMENTATION
// ============================================================================

class XeroxVendor : public PrinterVendor {
private:
    // Standard Printer MIB OIDs
    const std::string OID_SYSTEM_DESC = "1.3.6.1.2.1.1.1.0";
    const std::string OID_DEVICE_STATUS = "1.3.6.1.2.1.25.3.5.1.1.1";
    const std::string OID_PAGE_COUNT = "1.3.6.1.2.1.43.10.2.1.4.1.1";

    // Xerox Enterprise OIDs (Enterprise ID: 253)
    const std::string OID_XEROX_SERIAL = "1.3.6.1.4.1.253.8.53.3.2.1.3.1";
    const std::string OID_XEROX_MODEL = "1.3.6.1.4.1.253.8.53.13.2.1.6.1.12.1";
    const std::string OID_XEROX_TOTAL_IMPRESSIONS = "1.3.6.1.4.1.253.8.53.13.2.1.6.1.20.1";

public:
    XeroxVendor(const Settings& s) : PrinterVendor("Xerox", s) {}

    PrinterStatus queryPrinter(const PrinterConfig& config, SNMPHelper& snmp) override {
        PrinterStatus status;
        status.config = config;
        status.online = false;

        // Test connectivity
        std::string sysDesc = snmp.snmpGet(config.dnsName, config.community, OID_SYSTEM_DESC);
        if (sysDesc.empty()) {
            status.status = "Offline/No SNMP";
            return status;
        }

        status.online = true;

        // Get device status
        int deviceStatus = snmp.snmpGetInt(config.dnsName, config.community, OID_DEVICE_STATUS);
        switch (deviceStatus) {
            case 1: status.status = "Unknown"; break;
            case 2: status.status = "Running"; break;
            case 3: status.status = "Warning"; break;
            case 4: status.status = "Testing"; break;
            case 5: status.status = "Down"; break;
            default: status.status = "Ready";
        }

        // Get page count (try standard MIB first, then Xerox-specific)
        status.pageCount = snmp.snmpGetInt(config.dnsName, config.community, OID_PAGE_COUNT);
        if (status.pageCount < 0) {
            status.pageCount = snmp.snmpGetInt(config.dnsName, config.community, OID_XEROX_TOTAL_IMPRESSIONS);
        }

        // Get serial number
        status.serialNumber = snmp.snmpGet(config.dnsName, config.community, OID_XEROX_SERIAL);

        // Get model
        status.model = snmp.snmpGet(config.dnsName, config.community, OID_XEROX_MODEL);
        if (status.model.empty()) {
            status.model = "Xerox";
        }

        // Get toner levels
        status.toners = getTonerLevels(config, snmp);
        evaluateTonerLevels(status);

        return status;
    }

private:
    std::vector<TonerLevel> getTonerLevels(const PrinterConfig& config, SNMPHelper& snmp) {
        std::vector<TonerLevel> toners;

        // Try standard Printer MIB (works for most Xerox printers)
        std::vector<std::string> colorNames = {"Black", "Cyan", "Magenta", "Yellow"};

        for (int i = 1; i <= 4; i++) {
            TonerLevel toner;

            // Get supply description
            std::string oidDesc = "1.3.6.1.2.1.43.11.1.1.6.1." + std::to_string(i);
            std::string desc = snmp.snmpGet(config.dnsName, config.community, oidDesc);

            // Get current level
            std::string oidLevel = "1.3.6.1.2.1.43.11.1.1.9.1." + std::to_string(i);
            int level = snmp.snmpGetInt(config.dnsName, config.community, oidLevel);

            // Get max capacity
            std::string oidMax = "1.3.6.1.2.1.43.11.1.1.8.1." + std::to_string(i);
            int maxCap = snmp.snmpGetInt(config.dnsName, config.community, oidMax);

            if (level >= 0 && maxCap > 0) {
                toner.color = parseColorFromDescription(desc, colorNames[(i-1) % 4]);
                toner.level = level;
                toner.maxLevel = maxCap;
                toner.percentage = (level * 100) / maxCap;

                if (toner.percentage > 100) toner.percentage = 100;
                if (toner.percentage < 0) toner.percentage = 0;

                toners.push_back(toner);
            }
        }

        return toners;
    }

    std::string parseColorFromDescription(const std::string& desc, const std::string& defaultColor) {
        if (desc.empty()) return defaultColor;

        if (desc.find("Black") != std::string::npos || desc.find("black") != std::string::npos ||
            desc.find("K") != std::string::npos) {
            return "Black";
        } else if (desc.find("Cyan") != std::string::npos || desc.find("cyan") != std::string::npos) {
            return "Cyan";
        } else if (desc.find("Magenta") != std::string::npos || desc.find("magenta") != std::string::npos) {
            return "Magenta";
        } else if (desc.find("Yellow") != std::string::npos || desc.find("yellow") != std::string::npos) {
            return "Yellow";
        }

        return defaultColor;
    }
};

// ============================================================================
// VENDOR FACTORY
// ============================================================================

class VendorFactory {
private:
    Settings settings;
    std::map<std::string, std::shared_ptr<PrinterVendor>> vendors;

public:
    VendorFactory(const Settings& s) : settings(s) {
        vendors["konica_minolta"] = std::make_shared<KonicaMinoltaVendor>(settings);
        vendors["xerox"] = std::make_shared<XeroxVendor>(settings);
    }

    std::shared_ptr<PrinterVendor> getVendor(const std::string& vendorName) {
        auto it = vendors.find(vendorName);
        if (it != vendors.end()) {
            return it->second;
        }
        return nullptr;
    }

    std::vector<std::string> getAvailableVendors() const {
        std::vector<std::string> vendorList;
        for (const auto& pair : vendors) {
            vendorList.push_back(pair.first);
        }
        return vendorList;
    }
};

// ============================================================================
// CONFIGURATION FILE PARSER
// ============================================================================

class ConfigParser {
public:
    static std::vector<PrinterConfig> parsePrinterList(const std::string& filename) {
        std::vector<PrinterConfig> printers;
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
            PrinterConfig config;

            config.dnsName = extractValue(obj, "dns");
            config.vendor = extractValue(obj, "vendor");

            std::string customCommunity = extractValue(obj, "community");
            if (!customCommunity.empty()) {
                config.community = customCommunity;
            }

            if (!config.dnsName.empty() && !config.vendor.empty()) {
                config.parseDnsName();
                printers.push_back(config);
            }

            pos = end + 1;
        }

        return printers;
    }

    static Settings parseSettings(const std::string& filename) {
        Settings settings;
        std::ifstream file(filename);

        if (!file.is_open()) {
            return settings;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

        // Extract settings values
        std::string lowTh = extractValue(content, "low_toner_threshold");
        if (!lowTh.empty()) {
            try { settings.lowTonerThreshold = std::stoi(lowTh); } catch (...) {}
        }

        std::string critTh = extractValue(content, "critical_toner_threshold");
        if (!critTh.empty()) {
            try { settings.criticalTonerThreshold = std::stoi(critTh); } catch (...) {}
        }

        std::string community = extractValue(content, "default_community");
        if (!community.empty()) {
            settings.defaultCommunity = community;
        }

        return settings;
    }

    static void savePrinterList(const std::string& filename,
                               const std::vector<PrinterConfig>& printers) {
        std::ofstream file(filename);

        file << "{\n  \"printers\": [\n";

        for (size_t i = 0; i < printers.size(); i++) {
            file << "    {\n";
            file << "      \"dns\": \"" << printers[i].dnsName << "\",\n";
            file << "      \"vendor\": \"" << printers[i].vendor << "\",\n";
            file << "      \"community\": \"" << printers[i].community << "\"\n";
            file << "    }";

            if (i < printers.size() - 1) {
                file << ",";
            }
            file << "\n";
        }

        file << "  ]\n}\n";
    }

    static void saveSettings(const std::string& filename, const Settings& settings) {
        std::ofstream file(filename);

        file << "{\n";
        file << "  \"settings\": {\n";
        file << "    \"low_toner_threshold\": " << settings.lowTonerThreshold << ",\n";
        file << "    \"critical_toner_threshold\": " << settings.criticalTonerThreshold << ",\n";
        file << "    \"default_community\": \"" << settings.defaultCommunity << "\"\n";
        file << "  }\n";
        file << "}\n";
    }

private:
    static std::string extractValue(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";

        pos = json.find(":", pos);
        if (pos == std::string::npos) return "";

        // Skip whitespace
        pos++;
        while (pos < json.length() && std::isspace(json[pos])) pos++;

        // Check if value is a string (quoted) or number
        if (json[pos] == '\"') {
            pos++;
            size_t end = json.find("\"", pos);
            if (end == std::string::npos) return "";
            return json.substr(pos, end - pos);
        } else {
            // Number or boolean
            size_t end = pos;
            while (end < json.length() &&
                   (std::isdigit(json[end]) || json[end] == '.' ||
                    json[end] == '-' || json[end] == '+')) {
                end++;
            }
            return json.substr(pos, end - pos);
        }
    }
};

// ============================================================================
// DISPLAY SYSTEM
// ============================================================================

class DisplayLayout {
protected:
    Settings settings;

public:
    DisplayLayout(const Settings& s) : settings(s) {}
    virtual ~DisplayLayout() {}
    virtual void render(const std::vector<PrinterStatus>& statuses) = 0;

protected:
    std::string getTonerBar(const TonerLevel& toner, int width = 10) {
        int filled = (toner.percentage * width) / 100;

        std::string bar = "[";
        for (int i = 0; i < width; i++) {
            if (i < filled) {
                if (toner.isCritical) {
                    bar += "!";
                } else if (toner.isLow) {
                    bar += "▓";
                } else {
                    bar += "█";
                }
            } else {
                bar += "░";
            }
        }
        bar += "]";

        return bar;
    }

    std::string truncate(const std::string& str, size_t width) {
        if (str.length() <= width) return str;
        return str.substr(0, width - 3) + "...";
    }

    std::string getColorCode(const std::string& color) {
        if (color == "Black") return "K";
        if (color == "Cyan") return "C";
        if (color == "Magenta") return "M";
        if (color == "Yellow") return "Y";
        return color.substr(0, 1);
    }
};

class TableLayout : public DisplayLayout {
public:
    TableLayout(const Settings& s) : DisplayLayout(s) {}

    void render(const std::vector<PrinterStatus>& statuses) override {
        std::cout << "\n";
        printSeparator();
        printHeader();
        printSeparator();

        for (const auto& status : statuses) {
            printPrinterRow(status);
        }

        printSeparator();
        printSummary(statuses);
        std::cout << "\n";
    }

private:
    void printSeparator() {
        std::cout << std::string(120, '=') << "\n";
    }

    void printHeader() {
        std::cout << std::left
                  << std::setw(20) << "Printer Name"
                  << std::setw(18) << "DNS/Location"
                  << std::setw(12) << "Status"
                  << std::setw(10) << "Pages"
                  << std::setw(60) << "Toner Levels (K/C/M/Y)"
                  << "\n";
    }

    void printPrinterRow(const PrinterStatus& status) {
        std::cout << std::left
                  << std::setw(20) << truncate(status.config.shortName, 19)
                  << std::setw(18) << truncate(status.config.dnsName, 17)
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
                    std::cout << getColorCode(toner.color) << ":";
                    std::cout << std::right << std::setw(3) << toner.percentage << "%";
                    if (toner.isCritical) std::cout << "!";
                    else if (toner.isLow) std::cout << "*";
                    else std::cout << " ";
                    std::cout << " " << std::left;
                }
            } else {
                std::cout << "No toner data";
            }
        } else {
            std::cout << std::setw(10) << "N/A" << "N/A";
        }

        std::cout << "\n";
    }

    void printSummary(const std::vector<PrinterStatus>& statuses) {
        int online = 0, offline = 0, warning = 0;
        int lowToner = 0, criticalToner = 0;

        for (const auto& status : statuses) {
            if (!status.online) {
                offline++;
            } else {
                if (status.status == "Warning") warning++;
                else online++;

                if (status.hasLowToner) lowToner++;
                if (status.hasCriticalToner) criticalToner++;
            }
        }

        std::cout << "\nSummary: " << online << " online, "
                  << warning << " warnings, "
                  << offline << " offline | "
                  << "Toner: " << lowToner << " low (*), "
                  << criticalToner << " critical (!)\n";
    }
};

class CompactLayout : public DisplayLayout {
public:
    CompactLayout(const Settings& s) : DisplayLayout(s) {}

    void render(const std::vector<PrinterStatus>& statuses) override {
        std::cout << "\n";

        for (const auto& status : statuses) {
            std::cout << "[" << (status.online ? "●" : "○") << "] "
                      << std::setw(25) << std::left << status.config.shortName
                      << " ";

            if (status.online && !status.toners.empty()) {
                for (const auto& toner : status.toners) {
                    std::cout << getColorCode(toner.color) << ":"
                              << std::setw(3) << std::right << toner.percentage << "% ";
                }
            } else {
                std::cout << "N/A";
            }

            std::cout << "\n";
        }

        std::cout << "\n";
    }
};

class DetailedLayout : public DisplayLayout {
public:
    DetailedLayout(const Settings& s) : DisplayLayout(s) {}

    void render(const std::vector<PrinterStatus>& statuses) override {
        for (const auto& status : statuses) {
            std::cout << "\n" << std::string(80, '=') << "\n";
            std::cout << "Printer: " << status.config.shortName << "\n";
            std::cout << std::string(80, '-') << "\n";
            std::cout << "DNS Name:      " << status.config.dnsName << "\n";
            std::cout << "Vendor:        " << status.config.vendor << "\n";

            if (!status.config.building.empty()) {
                std::cout << "Location:      Building " << status.config.building;
                if (!status.config.floor.empty()) {
                    std::cout << ", Floor " << status.config.floor;
                }
                std::cout << "\n";
            }

            std::cout << "Status:        " << (status.online ? status.status : "OFFLINE") << "\n";

            if (status.online) {
                if (!status.model.empty()) {
                    std::cout << "Model:         " << status.model << "\n";
                }

                if (!status.serialNumber.empty()) {
                    std::cout << "Serial:        " << status.serialNumber << "\n";
                }

                if (status.pageCount >= 0) {
                    std::cout << "Page Count:    " << status.pageCount << "\n";
                }

                if (!status.toners.empty()) {
                    std::cout << "\nToner Levels:\n";
                    for (const auto& toner : status.toners) {
                        std::cout << "  " << std::left << std::setw(10) << toner.color
                                  << getTonerBar(toner, 20) << " "
                                  << std::right << std::setw(3) << toner.percentage << "%"
                                  << " (" << toner.level << "/" << toner.maxLevel << ")";

                        if (toner.isCritical) {
                            std::cout << " [CRITICAL]";
                        } else if (toner.isLow) {
                            std::cout << " [LOW]";
                        }

                        std::cout << "\n";
                    }
                }
            }
        }
        std::cout << "\n" << std::string(80, '=') << "\n\n";
    }
};

class MinimalLayout : public DisplayLayout {
public:
    MinimalLayout(const Settings& s) : DisplayLayout(s) {}

    void render(const std::vector<PrinterStatus>& statuses) override {
        for (const auto& status : statuses) {
            if (status.online && !status.toners.empty()) {
                std::cout << status.config.shortName << ": ";
                bool first = true;
                for (const auto& toner : status.toners) {
                    if (!first) std::cout << ", ";
                    std::cout << getColorCode(toner.color) << toner.percentage << "%";
                    first = false;
                }
                std::cout << "\n";
            }
        }
    }
};

// ============================================================================
// FILTERING SYSTEM
// ============================================================================

class PrinterFilter {
public:
    virtual ~PrinterFilter() {}
    virtual bool shouldInclude(const PrinterStatus& status) const = 0;
    virtual std::string getDescription() const = 0;
};

class LowTonerFilter : public PrinterFilter {
public:
    bool shouldInclude(const PrinterStatus& status) const override {
        return status.online && status.hasLowToner;
    }

    std::string getDescription() const override {
        return "Printers with low toner";
    }
};

class CriticalTonerFilter : public PrinterFilter {
public:
    bool shouldInclude(const PrinterStatus& status) const override {
        return status.online && status.hasCriticalToner;
    }

    std::string getDescription() const override {
        return "Printers with critical toner";
    }
};

class OfflineFilter : public PrinterFilter {
public:
    bool shouldInclude(const PrinterStatus& status) const override {
        return !status.online;
    }

    std::string getDescription() const override {
        return "Offline printers";
    }
};

class OnlineFilter : public PrinterFilter {
public:
    bool shouldInclude(const PrinterStatus& status) const override {
        return status.online;
    }

    std::string getDescription() const override {
        return "Online printers";
    }
};

class SpecificColorFilter : public PrinterFilter {
private:
    std::string targetColor;
    bool lowOnly;

public:
    SpecificColorFilter(const std::string& color, bool lowOnly = false)
        : targetColor(color), lowOnly(lowOnly) {}

    bool shouldInclude(const PrinterStatus& status) const override {
        if (!status.online) return false;

        for (const auto& toner : status.toners) {
            if (toner.color == targetColor) {
                if (lowOnly) {
                    return toner.isLow;
                }
                return true;
            }
        }
        return false;
    }

    std::string getDescription() const override {
        return "Printers with " + targetColor + (lowOnly ? " toner low" : " toner");
    }
};

std::vector<PrinterStatus> applyFilters(const std::vector<PrinterStatus>& statuses,
                                        const std::vector<std::shared_ptr<PrinterFilter>>& filters) {
    if (filters.empty()) {
        return statuses;
    }

    std::vector<PrinterStatus> filtered;

    for (const auto& status : statuses) {
        bool include = true;
        for (const auto& filter : filters) {
            if (!filter->shouldInclude(status)) {
                include = false;
                break;
            }
        }

        if (include) {
            filtered.push_back(status);
        }
    }

    return filtered;
}

// ============================================================================
// COMMAND LINE INTERFACE
// ============================================================================

struct CommandOptions {
    std::string configFile;
    std::string settingsFile;
    std::string action;
    std::string layout;
    bool verbose;
    std::vector<std::shared_ptr<PrinterFilter>> filters;

    CommandOptions()
        : configFile("printers.json")
        , settingsFile("settings.json")
        , action("monitor")
        , layout("table")
        , verbose(false)
    {}
};

void printUsage(const char* prog) {
    std::cout << "Printer Monitor v2.0 - Modular Printer SNMP Monitor\n\n"
              << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  -h, --help                Show this help message\n"
              << "  -c, --config FILE         Specify printer config file (default: printers.json)\n"
              << "  -s, --settings FILE       Specify settings file (default: settings.json)\n"
              << "  -a, --add                 Add a new printer to the list\n"
              << "  -l, --list                List configured printers\n"
              << "  -m, --monitor             Monitor printers (default action)\n"
              << "  -v, --verbose             Verbose output\n\n"
              << "Display Layouts:\n"
              << "  --table                   Table layout (default)\n"
              << "  --compact                 Compact layout\n"
              << "  --detailed                Detailed layout\n"
              << "  --minimal                 Minimal layout\n\n"
              << "Filters:\n"
              << "  --low-toner               Show only printers with low toner\n"
              << "  --critical-toner          Show only printers with critical toner\n"
              << "  --offline                 Show only offline printers\n"
              << "  --online                  Show only online printers\n"
              << "  --color COLOR --low       Show printers with specific low color (Black/Cyan/Magenta/Yellow)\n\n"
              << "Examples:\n"
              << "  " << prog << "                           # Monitor all printers (table layout)\n"
              << "  " << prog << " --low-toner                # Show only printers with low toner\n"
              << "  " << prog << " --critical-toner --compact # Compact view of critical toner printers\n"
              << "  " << prog << " --detailed                 # Detailed information for all printers\n"
              << "  " << prog << " --color Black --low        # Show printers with low black toner\n"
              << "  " << prog << " --add                      # Add a new printer\n\n"
              << "Supported Vendors:\n"
              << "  - konica_minolta (Konica Minolta bizhub series)\n"
              << "  - xerox (Xerox multifunction printers)\n\n"
              << "Configuration:\n"
              << "  Edit settings.json to configure:\n"
              << "  - low_toner_threshold (default: 20%)\n"
              << "  - critical_toner_threshold (default: 10%)\n"
              << "  - default_community (default: public)\n";
}

void addPrinter(const std::string& filename, const Settings& settings) {
    std::vector<PrinterConfig> printers = ConfigParser::parsePrinterList(filename);

    PrinterConfig newPrinter;
    newPrinter.community = settings.defaultCommunity;

    std::cout << "\nAdd New Printer\n";
    std::cout << "===============\n\n";

    std::cout << "DNS Name (e.g., printer-b5-3.obspm.fr): ";
    std::getline(std::cin, newPrinter.dnsName);

    std::cout << "Vendor (konica_minolta/xerox): ";
    std::getline(std::cin, newPrinter.vendor);

    std::cout << "SNMP Community (default: " << settings.defaultCommunity << "): ";
    std::string community;
    std::getline(std::cin, community);
    if (!community.empty()) {
        newPrinter.community = community;
    }

    newPrinter.parseDnsName();

    // Test connection
    std::cout << "\nTesting connection to " << newPrinter.dnsName << "...";
    std::cout.flush();

    VendorFactory factory(settings);
    auto vendor = factory.getVendor(newPrinter.vendor);

    if (!vendor) {
        std::cout << " FAILED!\n";
        std::cout << "Error: Unknown vendor '" << newPrinter.vendor << "'\n";
        std::cout << "Available vendors: ";
        auto vendors = factory.getAvailableVendors();
        for (size_t i = 0; i < vendors.size(); i++) {
            std::cout << vendors[i];
            if (i < vendors.size() - 1) std::cout << ", ";
        }
        std::cout << "\n";
        return;
    }

    SNMPHelper snmp(settings);
    PrinterStatus test = vendor->queryPrinter(newPrinter, snmp);

    if (test.online) {
        std::cout << " SUCCESS!\n";
        std::cout << "Printer is online and responding to SNMP queries.\n";
        std::cout << "Parsed info: " << newPrinter.shortName;
        if (!newPrinter.building.empty()) {
            std::cout << " (Building " << newPrinter.building;
            if (!newPrinter.floor.empty()) {
                std::cout << ", Floor " << newPrinter.floor;
            }
            std::cout << ")";
        }
        std::cout << "\n";
    } else {
        std::cout << " FAILED!\n";
        std::cout << "Warning: Could not connect to printer.\n";
        std::cout << "Please verify:\n";
        std::cout << "  - DNS name is correct and resolvable\n";
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
    ConfigParser::savePrinterList(filename, printers);

    std::cout << "\nPrinter added successfully to " << filename << "!\n";
}

void listPrinters(const std::string& filename) {
    std::vector<PrinterConfig> printers = ConfigParser::parsePrinterList(filename);

    if (printers.empty()) {
        std::cout << "No printers configured.\n";
        std::cout << "Use '--add' to add a printer.\n";
        return;
    }

    std::cout << "\nConfigured Printers (" << printers.size() << "):\n";
    std::cout << std::string(80, '=') << "\n\n";

    for (size_t i = 0; i < printers.size(); i++) {
        std::cout << (i + 1) << ". " << printers[i].shortName << "\n";
        std::cout << "   DNS:      " << printers[i].dnsName << "\n";
        std::cout << "   Vendor:   " << printers[i].vendor << "\n";
        if (!printers[i].building.empty()) {
            std::cout << "   Location: Building " << printers[i].building;
            if (!printers[i].floor.empty()) {
                std::cout << ", Floor " << printers[i].floor;
            }
            std::cout << "\n";
        }
        std::cout << "   SNMP:     " << printers[i].community << "\n\n";
    }
}

void monitorPrinters(const CommandOptions& options, const Settings& settings) {
    std::vector<PrinterConfig> printers = ConfigParser::parsePrinterList(options.configFile);

    if (printers.empty()) {
        std::cout << "No printers configured.\n";
        std::cout << "Use '--add' to add printers.\n";
        return;
    }

    VendorFactory factory(settings);
    SNMPHelper snmp(settings);

    if (options.verbose) {
        std::cout << "Querying " << printers.size() << " printer(s)...\n";
    }

    std::vector<PrinterStatus> statuses;
    for (size_t i = 0; i < printers.size(); i++) {
        if (options.verbose) {
            std::cout << "  [" << (i+1) << "/" << printers.size() << "] "
                      << printers[i].shortName << " (" << printers[i].dnsName << ")...";
            std::cout.flush();
        }

        auto vendor = factory.getVendor(printers[i].vendor);
        if (vendor) {
            PrinterStatus status = vendor->queryPrinter(printers[i], snmp);
            statuses.push_back(status);

            if (options.verbose) {
                std::cout << (status.online ? " OK" : " OFFLINE") << "\n";
            }
        } else {
            if (options.verbose) {
                std::cout << " ERROR (unknown vendor)\n";
            }
        }
    }

    // Apply filters
    std::vector<PrinterStatus> filtered = applyFilters(statuses, options.filters);

    if (!options.filters.empty()) {
        std::cout << "\nFilters applied: ";
        for (size_t i = 0; i < options.filters.size(); i++) {
            std::cout << options.filters[i]->getDescription();
            if (i < options.filters.size() - 1) std::cout << ", ";
        }
        std::cout << "\n";
        std::cout << "Showing " << filtered.size() << " of " << statuses.size() << " printers\n";
    }

    // Render with selected layout
    std::shared_ptr<DisplayLayout> layout;

    if (options.layout == "compact") {
        layout = std::make_shared<CompactLayout>(settings);
    } else if (options.layout == "detailed") {
        layout = std::make_shared<DetailedLayout>(settings);
    } else if (options.layout == "minimal") {
        layout = std::make_shared<MinimalLayout>(settings);
    } else {
        layout = std::make_shared<TableLayout>(settings);
    }

    layout->render(filtered);
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char* argv[]) {
    CommandOptions options;

    // Parse command line arguments
    std::string nextColorFilter;
    bool nextColorIsLow = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                options.configFile = argv[++i];
            } else {
                std::cerr << "Error: --config requires an argument\n";
                return 1;
            }
        } else if (arg == "-s" || arg == "--settings") {
            if (i + 1 < argc) {
                options.settingsFile = argv[++i];
            } else {
                std::cerr << "Error: --settings requires an argument\n";
                return 1;
            }
        } else if (arg == "-a" || arg == "--add") {
            options.action = "add";
        } else if (arg == "-l" || arg == "--list") {
            options.action = "list";
        } else if (arg == "-m" || arg == "--monitor") {
            options.action = "monitor";
        } else if (arg == "-v" || arg == "--verbose") {
            options.verbose = true;
        } else if (arg == "--table") {
            options.layout = "table";
        } else if (arg == "--compact") {
            options.layout = "compact";
        } else if (arg == "--detailed") {
            options.layout = "detailed";
        } else if (arg == "--minimal") {
            options.layout = "minimal";
        } else if (arg == "--low-toner") {
            options.filters.push_back(std::make_shared<LowTonerFilter>());
        } else if (arg == "--critical-toner") {
            options.filters.push_back(std::make_shared<CriticalTonerFilter>());
        } else if (arg == "--offline") {
            options.filters.push_back(std::make_shared<OfflineFilter>());
        } else if (arg == "--online") {
            options.filters.push_back(std::make_shared<OnlineFilter>());
        } else if (arg == "--color") {
            if (i + 1 < argc) {
                nextColorFilter = argv[++i];
            } else {
                std::cerr << "Error: --color requires an argument\n";
                return 1;
            }
        } else if (arg == "--low") {
            nextColorIsLow = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    // Process color filter if specified
    if (!nextColorFilter.empty()) {
        options.filters.push_back(std::make_shared<SpecificColorFilter>(nextColorFilter, nextColorIsLow));
    }

    // Load settings
    Settings settings = ConfigParser::parseSettings(options.settingsFile);

    // If settings file doesn't exist, create default
    std::ifstream settingsCheck(options.settingsFile);
    if (!settingsCheck.good()) {
        ConfigParser::saveSettings(options.settingsFile, settings);
        if (options.verbose) {
            std::cout << "Created default settings file: " << options.settingsFile << "\n";
        }
    }
    settingsCheck.close();

    // Execute action
    try {
        if (options.action == "add") {
            addPrinter(options.configFile, settings);
        } else if (options.action == "list") {
            listPrinters(options.configFile);
        } else {
            monitorPrinters(options, settings);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        SNMPHelper::cleanup();
        return 1;
    }

    SNMPHelper::cleanup();
    return 0;
}
