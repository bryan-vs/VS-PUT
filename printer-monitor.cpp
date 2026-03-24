// printer-monitor.cpp — VS-PUT v3.0
// SNMP printer toner/status monitor with parallel queries and adaptive display.
//
// Build: make   (requires libsnmp-dev)
// Usage: ./printer-monitor [-h]

#include <algorithm>
#include <cstring>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include <sys/ioctl.h>
#include <unistd.h>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

// ─── Data ────────────────────────────────────────────────────────────────────

struct Settings {
    int         lowToner  = 20;
    int         critToner = 10;
    std::string community = "public";
    int         timeout   = 2000000; // µs
    int         retries   = 1;
};

struct Printer {
    std::string dns;
    std::string name;      // derived: dns up to first '.'
    std::string vendor;    // "konica_minolta" | "xerox"
    std::string community;
};

struct Toner {
    char ch   = '?';  // K / C / M / Y
    int  pct  = -1;   // 0–100, -1 = unknown
    bool low  = false;
    bool crit = false;
};

struct Status {
    Printer     cfg;
    bool        online  = false;
    std::string state;
    int         pages   = -1;
    Toner       ink[4];
    int         nInk    = 0;
    bool        hasLow  = false;
    bool        hasCrit = false;
};

// ─── Config I/O ──────────────────────────────────────────────────────────────

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    return { std::istreambuf_iterator<char>(f), {} };
}

// Extract first JSON string or integer value for a given key.
static std::string jsonVal(const std::string& blob, const std::string& key) {
    auto pos = blob.find('"' + key + '"');
    if (pos == std::string::npos) return {};
    pos = blob.find(':', pos);
    if (pos == std::string::npos) return {};
    while (++pos < blob.size() && std::isspace((unsigned char)blob[pos])) {}
    if (pos >= blob.size()) return {};
    if (blob[pos] == '"') {
        auto end = blob.find('"', ++pos);
        return end == std::string::npos ? "" : blob.substr(pos, end - pos);
    }
    auto end = pos;
    while (end < blob.size() && (std::isdigit((unsigned char)blob[end]) || blob[end] == '-'))
        ++end;
    return blob.substr(pos, end - pos);
}

static std::string dnsToName(const std::string& dns) {
    auto dot = dns.find('.');
    return dot == std::string::npos ? dns : dns.substr(0, dot);
}

static std::vector<Printer> loadPrinters(const std::string& path) {
    auto blob = readFile(path);
    std::vector<Printer> ps;
    size_t pos = blob.find('[');
    if (pos == std::string::npos) return ps;
    while (true) {
        pos = blob.find('{', pos);
        if (pos == std::string::npos) break;
        auto end = blob.find('}', pos);
        if (end == std::string::npos) break;
        auto obj = blob.substr(pos, end - pos + 1);
        Printer p;
        p.dns       = jsonVal(obj, "dns");
        p.vendor    = jsonVal(obj, "vendor");
        p.community = jsonVal(obj, "community");
        if (!p.dns.empty() && !p.vendor.empty()) {
            p.name = dnsToName(p.dns);
            ps.push_back(p);
        }
        pos = end + 1;
    }
    return ps;
}

static Settings loadSettings(const std::string& path) {
    Settings s;
    auto blob = readFile(path);
    if (blob.empty()) return s;
    auto v = jsonVal(blob, "low_toner");
    if (!v.empty()) try { s.lowToner  = std::stoi(v); } catch (...) {}
    v = jsonVal(blob, "critical_toner");
    if (!v.empty()) try { s.critToner = std::stoi(v); } catch (...) {}
    v = jsonVal(blob, "community");
    if (!v.empty()) s.community = v;
    return s;
}

static void savePrinters(const std::string& path, const std::vector<Printer>& ps) {
    std::ofstream f(path);
    f << "{\n  \"printers\": [\n";
    for (size_t i = 0; i < ps.size(); ++i) {
        f << "    { \"dns\": \""       << ps[i].dns
          << "\", \"vendor\": \""      << ps[i].vendor
          << "\", \"community\": \""   << ps[i].community << "\" }";
        if (i + 1 < ps.size()) f << ',';
        f << '\n';
    }
    f << "  ]\n}\n";
}

// ─── SNMP ────────────────────────────────────────────────────────────────────

static std::once_flag g_snmpInit;

static void initSNMP() {
    // Suppress "read_config_store open failure" and all other SNMP stderr noise.
    netsnmp_ds_set_boolean(NETSNMP_DS_LIBRARY_ID, NETSNMP_DS_LIB_DONT_PERSIST_STATE, 1);
    snmp_disable_stderrlog();
    init_snmp("printer-monitor");
}

// Each call opens its own session — safe to call from multiple threads.
static std::string snmpGet(const std::string& host, const std::string& comm,
                            const std::string& oidStr, int timeout, int retries) {
    std::call_once(g_snmpInit, initSNMP);

    snmp_session sess = {};
    snmp_sess_init(&sess);
    sess.version       = SNMP_VERSION_2c;
    sess.peername      = const_cast<char*>(host.c_str()); // net-snmp reads only
    sess.community     = (u_char*)comm.c_str();
    sess.community_len = comm.size();
    sess.timeout       = timeout;
    sess.retries       = retries;

    snmp_session* ss = snmp_open(&sess);
    if (!ss) return {};

    oid     anOID[MAX_OID_LEN];
    size_t  oidLen = MAX_OID_LEN;
    if (!read_objid(oidStr.c_str(), anOID, &oidLen)) {
        snmp_close(ss);
        return {};
    }

    snmp_pdu* pdu  = snmp_pdu_create(SNMP_MSG_GET);
    snmp_add_null_var(pdu, anOID, oidLen);

    snmp_pdu*   resp = nullptr;
    std::string result;

    if (snmp_synch_response(ss, pdu, &resp) == STAT_SUCCESS
        && resp && resp->errstat == SNMP_ERR_NOERROR) {
        for (auto* v = resp->variables; v; v = v->next_variable) {
            if (v->type == ASN_OCTET_STR)
                result.assign((char*)v->val.string, v->val_len);
            else if (v->type == ASN_INTEGER || v->type == ASN_COUNTER || v->type == ASN_GAUGE)
                result = std::to_string(*v->val.integer);
        }
    }

    if (resp) snmp_free_pdu(resp);
    snmp_close(ss);
    return result;
}

static int snmpInt(const std::string& host, const std::string& comm,
                   const std::string& oid, int timeout, int retries) {
    auto s = snmpGet(host, comm, oid, timeout, retries);
    if (s.empty()) return -1;
    try { return std::stoi(s); } catch (...) { return -1; }
}

// ─── OIDs ────────────────────────────────────────────────────────────────────

// Standard Printer MIB — works for both KM and Xerox
static const std::string S_SYSDESC = "1.3.6.1.2.1.1.1.0";
static const std::string S_DEVSTAT = "1.3.6.1.2.1.25.3.5.1.1.1";
static const std::string S_PAGES   = "1.3.6.1.2.1.43.10.2.1.4.1.1";
static const std::string S_SUP_LVL = "1.3.6.1.2.1.43.11.1.1.9.1."; // + "1".."4"
static const std::string S_SUP_MAX = "1.3.6.1.2.1.43.11.1.1.8.1."; // + "1".."4"

// Konica Minolta enterprise fallback
static const std::string KM_PAGES = "1.3.6.1.4.1.18334.1.1.1.5.7.2.1.5.1";
static const std::string KM_LVL[] = {
    "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.5.1",
    "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.5.2",
    "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.5.3",
    "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.5.4",
};
static const std::string KM_MAX[] = {
    "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.6.1",
    "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.6.2",
    "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.6.3",
    "1.3.6.1.4.1.18334.1.1.1.5.7.2.2.1.6.4",
};

// Xerox enterprise fallback
static const std::string XRX_PAGES = "1.3.6.1.4.1.253.8.53.13.2.1.6.1.20.1";

static const char COLORS[4] = { 'K', 'C', 'M', 'Y' };

// ─── Printer Query ───────────────────────────────────────────────────────────

static Status queryPrinter(const Printer& p, const Settings& s) {
    Status st;
    st.cfg = p;

    const std::string& host = p.dns;
    const std::string& comm = p.community.empty() ? s.community : p.community;

    if (snmpGet(host, comm, S_SYSDESC, s.timeout, s.retries).empty()) {
        st.state = "offline";
        return st;
    }
    st.online = true;

    int devSt = snmpInt(host, comm, S_DEVSTAT, s.timeout, s.retries);
    switch (devSt) {
        case 2:  st.state = "running"; break;
        case 3:  st.state = "warning"; break;
        case 5:  st.state = "down";    break;
        default: st.state = "ready";
    }

    st.pages = snmpInt(host, comm, S_PAGES, s.timeout, s.retries);
    if (st.pages < 0)
        st.pages = snmpInt(host, comm,
            p.vendor == "xerox" ? XRX_PAGES : KM_PAGES, s.timeout, s.retries);

    // Try standard Printer MIB toner OIDs first.
    bool gotToner = false;
    for (int i = 0; i < 4; ++i) {
        int lvl = snmpInt(host, comm, S_SUP_LVL + std::to_string(i + 1), s.timeout, s.retries);
        int max = snmpInt(host, comm, S_SUP_MAX + std::to_string(i + 1), s.timeout, s.retries);
        if (lvl < 0 || max <= 0) continue;
        Toner& t = st.ink[st.nInk++];
        t.ch     = COLORS[i];
        t.pct    = std::min(100, std::max(0, lvl * 100 / max));
        t.low    = t.pct <= s.lowToner;
        t.crit   = t.pct <= s.critToner;
        if (t.low)  st.hasLow  = true;
        if (t.crit) st.hasCrit = true;
        gotToner = true;
    }

    // If standard MIB yielded nothing, try KM enterprise OIDs.
    if (!gotToner && p.vendor != "xerox") {
        for (int i = 0; i < 4; ++i) {
            int lvl = snmpInt(host, comm, KM_LVL[i], s.timeout, s.retries);
            int max = snmpInt(host, comm, KM_MAX[i], s.timeout, s.retries);
            if (lvl < 0 || max <= 0) continue;
            Toner& t = st.ink[st.nInk++];
            t.ch     = COLORS[i];
            t.pct    = std::min(100, std::max(0, lvl * 100 / max));
            t.low    = t.pct <= s.lowToner;
            t.crit   = t.pct <= s.critToner;
            if (t.low)  st.hasLow  = true;
            if (t.crit) st.hasCrit = true;
        }
    }

    return st;
}

// ─── Display ─────────────────────────────────────────────────────────────────

static int termCols() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return (int)ws.ws_col;
    return 80;
}

// ANSI helpers — no-ops when !tty
static void A(bool tty, const char* code) { if (tty) std::cout << code; }
#define RESET  "\033[0m"
#define DIM    "\033[2m"
#define BOLD   "\033[1m"
#define RED    "\033[31m"
#define YELLOW "\033[33m"
#define GREEN  "\033[32m"

static std::string tonerBar(const Toner& t, int w) {
    int n = std::min(w, t.pct * w / 100);
    std::string b = "[";
    for (int i = 0; i < w; ++i)
        b += (i < n) ? (t.crit ? '!' : (t.low ? '+' : '#')) : '.';
    b += ']';
    return b;
}

static void renderTable(const std::vector<Status>& ss, bool tty) {
    if (ss.empty()) { std::cout << "(no results)\n"; return; }

    int cols = termCols();

    // Name column width: longest name + 1, capped at 26.
    int nameW = 8;
    for (auto& s : ss) nameW = std::max(nameW, (int)s.cfg.name.size());
    nameW = std::min(nameW + 1, 26);

    // Fixed portion: name + " ● " (3) + state (9) + pages (8)
    // Each of 4 toner slots: "[bar] X:NNN%  " = 10 + barW chars
    int barW = std::max(3, (cols - nameW - 3 - 9 - 8 - 4 * 10) / 4);
    barW = std::min(barW, 20);

    std::string sep(cols, '-');
    std::cout << sep << '\n';

    int nOnline = 0, nWarn = 0, nOff = 0, nLow = 0, nCrit = 0;

    for (auto& s : ss) {
        if      (!s.online)             nOff++;
        else if (s.state == "warning")  nWarn++;
        else                            nOnline++;
        if (s.hasLow)  nLow++;
        if (s.hasCrit) nCrit++;

        // Name
        std::string nm = s.cfg.name;
        if ((int)nm.size() >= nameW) nm = nm.substr(0, nameW - 2) + "..";
        std::cout << std::left << std::setw(nameW) << nm;

        // Online indicator
        A(tty, s.online ? GREEN : DIM);
        std::cout << (s.online ? " \xe2\x97\x8f " : " \xe2\x97\x8b "); // ● / ○
        A(tty, RESET);

        // State
        std::string stStr = s.online ? s.state : "offline";
        bool isAlert = !s.online || s.state == "warning" || s.state == "down";
        A(tty, isAlert ? YELLOW : "");
        std::cout << std::left << std::setw(9) << stStr;
        A(tty, RESET);

        // Pages
        std::cout << std::right << std::setw(7);
        if (s.online && s.pages >= 0) std::cout << s.pages;
        else                          std::cout << "-";
        std::cout << ' ' << std::left;

        // Toners
        if (s.online && s.nInk > 0) {
            for (int i = 0; i < s.nInk; ++i) {
                const Toner& t = s.ink[i];
                A(tty, t.crit ? RED : (t.low ? YELLOW : GREEN));
                std::cout << tonerBar(t, barW) << ' ' << t.ch << ':';
                std::cout << std::right << std::setw(3) << t.pct << "% " << std::left;
                A(tty, RESET);
            }
        } else if (!s.online) {
            A(tty, DIM); std::cout << "-"; A(tty, RESET);
        } else {
            std::cout << "no toner data";
        }
        std::cout << '\n';
    }

    std::cout << sep << '\n';
    std::cout << nOnline << " online  " << nWarn << " warning  " << nOff << " offline";
    if (nLow || nCrit)
        std::cout << "   " << nLow << " low  " << nCrit << " critical";
    std::cout << '\n';
}

static void renderDetailed(const std::vector<Status>& ss, bool tty) {
    int cols  = std::min(termCols(), 80);
    std::string sep(cols, '-');
    for (auto& s : ss) {
        std::cout << sep << '\n';
        A(tty, BOLD); std::cout << s.cfg.name << '\n'; A(tty, RESET);
        std::cout << "  dns     " << s.cfg.dns   << '\n'
                  << "  vendor  " << s.cfg.vendor << '\n'
                  << "  state   " << (s.online ? s.state : "offline") << '\n';
        if (s.online) {
            if (s.pages >= 0) std::cout << "  pages   " << s.pages << '\n';
            for (int i = 0; i < s.nInk; ++i) {
                const Toner& t = s.ink[i];
                std::cout << "  " << t.ch << "       ";
                A(tty, t.crit ? RED : (t.low ? YELLOW : GREEN));
                std::cout << tonerBar(t, 20) << ' '
                          << std::right << std::setw(3) << t.pct << '%';
                if      (t.crit) std::cout << "  CRITICAL";
                else if (t.low)  std::cout << "  low";
                A(tty, RESET);
                std::cout << '\n';
            }
        }
    }
    std::cout << sep << '\n';
}

// ─── Filtering ───────────────────────────────────────────────────────────────

struct Filter {
    bool low         = false;
    bool critical    = false;
    bool offlineOnly = false;
    bool onlineOnly  = false;
    char color       = 0;     // 'K','C','M','Y', or 0 = no color filter
    bool colorLow    = false; // true when --color K --low
};

static bool passes(const Status& s, const Filter& f) {
    if (f.offlineOnly && s.online)   return false;
    if (f.onlineOnly  && !s.online)  return false;
    if (f.critical    && !s.hasCrit) return false;
    if (f.color) {
        bool found = false;
        for (int i = 0; i < s.nInk; ++i) {
            if (s.ink[i].ch == f.color) {
                if (f.colorLow && !s.ink[i].low) return false;
                found = true;
                break;
            }
        }
        if (!found) return false;
    } else if (f.low && !s.hasLow) {
        return false;
    }
    return true;
}

// ─── CLI ─────────────────────────────────────────────────────────────────────

static void usage(const char* prog) {
    std::cout <<
        "usage: " << prog << " [options]\n\n"
        "  -c FILE      printer config (default: printers.json)\n"
        "  -s FILE      settings file  (default: settings.json)\n"
        "  --add        add a printer interactively\n"
        "  --list       list configured printers\n"
        "  --detailed   full per-printer view\n"
        "  --low        show only printers with low toner\n"
        "  --critical   show only printers with critical toner\n"
        "  --offline    show only offline printers\n"
        "  --online     show only online printers\n"
        "  --color X    filter by toner color: K C M Y\n"
        "               combine with --low for low color only\n"
        "  -v           print query progress to stderr\n"
        "  -h           show this help\n";
}

static void cmdList(const std::string& path) {
    auto ps = loadPrinters(path);
    if (ps.empty()) { std::cout << "no printers in " << path << '\n'; return; }
    for (size_t i = 0; i < ps.size(); ++i)
        std::cout << (i + 1) << ". "
                  << std::left << std::setw(28) << ps[i].name
                  << std::setw(16) << ps[i].vendor
                  << ps[i].dns << '\n';
}

static void cmdAdd(const std::string& path, const Settings& s) {
    auto ps = loadPrinters(path);
    Printer p;
    p.community = s.community;
    std::cout << "dns name: ";                           std::getline(std::cin, p.dns);
    std::cout << "vendor (konica_minolta/xerox): ";      std::getline(std::cin, p.vendor);
    std::string c;
    std::cout << "community [" << s.community << "]: "; std::getline(std::cin, c);
    if (!c.empty()) p.community = c;
    p.name = dnsToName(p.dns);
    ps.push_back(p);
    savePrinters(path, ps);
    std::cout << "added " << p.name << " to " << path << '\n';
}

// ─── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::string configFile   = "printers.json";
    std::string settingsFile = "settings.json";
    std::string action       = "monitor";
    bool        detailed     = false;
    bool        verbose      = false;
    Filter      flt;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "-h" || a == "--help")  { usage(argv[0]); return 0; }
        else if (a == "-v")                     verbose  = true;
        else if (a == "-c" && i+1 < argc)       configFile   = argv[++i];
        else if (a == "-s" && i+1 < argc)       settingsFile = argv[++i];
        else if (a == "--add")                  action   = "add";
        else if (a == "--list")                 action   = "list";
        else if (a == "--detailed")             detailed = true;
        else if (a == "--low")                  flt.low         = true;
        else if (a == "--critical")             flt.critical    = true;
        else if (a == "--offline")              flt.offlineOnly = true;
        else if (a == "--online")               flt.onlineOnly  = true;
        else if (a == "--color" && i+1 < argc) {
            std::string col = argv[++i];
            if      (col == "K" || col == "Black")   flt.color = 'K';
            else if (col == "C" || col == "Cyan")    flt.color = 'C';
            else if (col == "M" || col == "Magenta") flt.color = 'M';
            else if (col == "Y" || col == "Yellow")  flt.color = 'Y';
            else { std::cerr << "unknown color: " << col << '\n'; return 1; }
        } else {
            std::cerr << "unknown option: " << a << '\n';
            usage(argv[0]);
            return 1;
        }
    }

    // --color K --low means "K toner is low", not "any low toner AND has K toner"
    if (flt.color && flt.low) { flt.colorLow = true; flt.low = false; }

    Settings settings = loadSettings(settingsFile);

    if (action == "list") { cmdList(configFile); return 0; }
    if (action == "add")  { cmdAdd(configFile, settings); return 0; }

    auto printers = loadPrinters(configFile);
    if (printers.empty()) {
        std::cerr << "no printers in " << configFile << " — use --add\n";
        return 1;
    }
    for (auto& p : printers)
        if (p.community.empty()) p.community = settings.community;

    if (verbose) std::cerr << "querying " << printers.size() << " printers...\n";

    // Launch one async task per printer; each task owns its SNMP session.
    std::vector<std::future<Status>> futures;
    futures.reserve(printers.size());
    for (auto p : printers) // value-copy into lambda
        futures.push_back(std::async(std::launch::async,
            [p, settings]() { return queryPrinter(p, settings); }));

    std::vector<Status> results;
    results.reserve(futures.size());
    for (auto& f : futures) results.push_back(f.get());

    if (verbose) std::cerr << "done\n";

    std::vector<Status> view;
    for (auto& s : results)
        if (passes(s, flt)) view.push_back(s);

    bool tty = isatty(STDOUT_FILENO);
    if (detailed) renderDetailed(view, tty);
    else          renderTable(view, tty);

    snmp_shutdown("printer-monitor");
    return 0;
}
