// copper_gold_strategy.cpp
//
// Copper-Gold Ratio Economic Regime Strategy v2.0
// 100% consistent with copper_gold_strategy_v2.md
//
// No synthetic data. No lookahead bias. All signals use only data[0..i].

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <unordered_set>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <deque>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================
// Date utilities
// ============================================================
static std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static int parse_date(const std::string& d) {
    std::tm t = {};
    std::istringstream ss(d);
    ss >> std::get_time(&t, "%Y-%m-%d");
    if (ss.fail()) return -1;
    t.tm_isdst = -1;
    time_t ts = std::mktime(&t);
    return static_cast<int>(ts / 86400);
}

static std::string date_from_int(int day_key) {
    time_t ts = static_cast<time_t>(day_key) * 86400;
    std::tm* t = std::gmtime(&ts);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", t);
    return std::string(buf);
}

// ============================================================
// Data types
// ============================================================
struct OHLCVBar {
    double open = 0.0, high = 0.0, low = 0.0, close = 0.0, volume = 0.0;
};

using TimeSeries = std::map<int, double>;
using FuturesSeries = std::map<int, OHLCVBar>;

// ============================================================
// CSV loaders
// ============================================================
static FuturesSeries load_futures(const std::string& path) {
    FuturesSeries out;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[WARN] Cannot open: " << path << "\n";
        return out;
    }
    std::string line;
    std::getline(f, line); // skip header
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string date_s, o_s, h_s, l_s, c_s, v_s;
        std::getline(ss, date_s, ',');
        std::getline(ss, o_s, ',');
        std::getline(ss, h_s, ',');
        std::getline(ss, l_s, ',');
        std::getline(ss, c_s, ',');
        std::getline(ss, v_s, ',');

        int dk = parse_date(trim(date_s));
        if (dk < 0) continue;

        OHLCVBar bar;
        try {
            double raw_open = std::stod(trim(o_s));
            double raw_high = std::stod(trim(h_s));
            double raw_low = std::stod(trim(l_s));
            double raw_close = std::stod(trim(c_s));

            // Check if this is HG (Copper) - no conversion needed, data is in $/lb
            // The notional normalization formula handles the conversion
            bar.open = raw_open;
            bar.high = raw_high;
            bar.low = raw_low;
            bar.close = raw_close;

            bar.volume = v_s.empty() ? 0.0 : std::stod(trim(v_s));
        } catch (...) { continue; }
        out[dk] = bar;
    }
    return out;
}
// Helper function to check if all positions are zero
static bool all_positions_zero(const std::unordered_map<std::string, double>& positions) {
    for (const auto& [sym, qty] : positions) {
        if (qty != 0.0) return false;
    }
    return true;
}

static TimeSeries load_macro(const std::string& path) {
    TimeSeries out;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[WARN] Cannot open: " << path << "\n";
        return out;
    }
    std::string line;
    std::getline(f, line); // skip header
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string date_s, val_s;
        std::getline(ss, date_s, ',');
        std::getline(ss, val_s, ',');
        date_s = trim(date_s);
        val_s = trim(val_s);
        int dk = parse_date(date_s);
        if (dk < 0 || val_s.empty() || val_s == "." || val_s == "NA") continue;
        try { out[dk] = std::stod(val_s); } catch (...) { continue; }
    }
    return out;
}

// ============================================================
// Forward-fill
// ============================================================
static double ffill(const TimeSeries& ts, int dk) {
    auto it = ts.upper_bound(dk);
    if (it == ts.begin()) return std::numeric_limits<double>::quiet_NaN();
    return (--it)->second;
}

static double ffill_fut_close(const FuturesSeries& ts, int dk) {
    auto it = ts.upper_bound(dk);
    if (it == ts.begin()) return std::numeric_limits<double>::quiet_NaN();
    return (--it)->second.close;
}

// ============================================================
// Rolling statistics
// ============================================================
static std::vector<double> rolling_mean(const std::vector<double>& v, int window) {
    std::vector<double> out(v.size(), std::numeric_limits<double>::quiet_NaN());
    for (int i = window - 1; i < (int)v.size(); ++i) {
        double sum = 0.0;
        for (int j = i - window + 1; j <= i; ++j) sum += v[j];
        out[i] = sum / window;
    }
    return out;
}

static std::vector<double> rolling_std(const std::vector<double>& v, int window) {
    std::vector<double> out(v.size(), std::numeric_limits<double>::quiet_NaN());
    for (int i = window - 1; i < (int)v.size(); ++i) {
        double mean = 0.0;
        for (int j = i - window + 1; j <= i; ++j) mean += v[j];
        mean /= window;
        double var = 0.0;
        for (int j = i - window + 1; j <= i; ++j) var += (v[j] - mean) * (v[j] - mean);
        out[i] = std::sqrt(var / window);
    }
    return out;
}

static std::vector<double> rolling_zscore(const std::vector<double>& v, int window) {
    auto sma = rolling_mean(v, window);
    auto std_ = rolling_std(v, window);
    std::vector<double> out(v.size(), std::numeric_limits<double>::quiet_NaN());
    for (int i = 0; i < (int)v.size(); ++i) {
        if (!std::isnan(sma[i]) && !std::isnan(std_[i]) && std_[i] > 0.0)
            out[i] = (v[i] - sma[i]) / std_[i];
    }
    return out;
}

// ATR calculation
static std::vector<double> compute_atr(const std::vector<int>& dates,
                                       const FuturesSeries& fut, int window = 20) {
    std::vector<double> tr(dates.size(), std::numeric_limits<double>::quiet_NaN());
    for (int i = 1; i < (int)dates.size(); ++i) {
        auto it = fut.find(dates[i]);
        if (it == fut.end()) continue;
        auto it_prev = fut.lower_bound(dates[i]);
        if (it_prev == fut.begin()) continue;
        --it_prev;
        double prev_close = it_prev->second.close;
        double hi = it->second.high;
        double lo = it->second.low;
        tr[i] = std::max({hi - lo,
                          std::abs(hi - prev_close),
                          std::abs(lo - prev_close)});
    }
    return rolling_mean(tr, window);
}

// Average pairwise correlation
static double avg_pairwise_corr(
    const std::vector<std::vector<double>>& ret_series,
    int i, int window)
{
    int n = static_cast<int>(ret_series.size());
    if (n < 2 || i < window - 1) return 0.0;

    double sum_corr = 0.0;
    int pairs = 0;
    for (int a = 0; a < n; ++a) {
        for (int b = a + 1; b < n; ++b) {
            double ma = 0.0, mb = 0.0;
            int valid = 0;
            for (int k = i - window + 1; k <= i; ++k) {
                double ra = ret_series[a][k], rb = ret_series[b][k];
                if (!std::isnan(ra) && !std::isnan(rb)) {
                    ma += ra; mb += rb; ++valid;
                }
            }
            if (valid < 2) continue;
            ma /= valid; mb /= valid;
            double cov = 0.0, va = 0.0, vb = 0.0;
            for (int k = i - window + 1; k <= i; ++k) {
                double ra = ret_series[a][k], rb = ret_series[b][k];
                if (!std::isnan(ra) && !std::isnan(rb)) {
                    double da = ra - ma, db = rb - mb;
                    cov += da * db; va += da * da; vb += db * db;
                }
            }
            double denom = std::sqrt(va * vb);
            if (denom > 0.0) { sum_corr += cov / denom; ++pairs; }
        }
    }
    return (pairs > 0) ? sum_corr / pairs : 0.0;
}

// ============================================================
// Enums
// ============================================================
enum class MacroTilt { RISK_ON, RISK_OFF, NEUTRAL };
enum class Regime { GROWTH_POSITIVE, GROWTH_NEGATIVE, INFLATION_SHOCK, LIQUIDITY_SHOCK, NEUTRAL };
enum class DXYTrend { STRONG, WEAK, NEUTRAL };
enum class DXYFilter { CONFIRMED, SUSPECT, NEUTRAL };

static const char* tilt_str(MacroTilt t) {
    switch (t) {
        case MacroTilt::RISK_ON: return "RISK_ON";
        case MacroTilt::RISK_OFF: return "RISK_OFF";
        default: return "NEUTRAL";
    }
}

static const char* regime_str(Regime r) {
    switch (r) {
        case Regime::GROWTH_POSITIVE: return "GROWTH_POSITIVE";
        case Regime::GROWTH_NEGATIVE: return "GROWTH_NEGATIVE";
        case Regime::INFLATION_SHOCK: return "INFLATION_SHOCK";
        case Regime::LIQUIDITY_SHOCK: return "LIQUIDITY_SHOCK";
        default: return "NEUTRAL";
    }
}

// ============================================================
// Contract specifications - EXACT from document
// ============================================================
namespace ContractSpec {
struct Spec {
    double margin;         // initial margin ($)
    double notional;       // approx notional per contract ($)
    double tick_size;
    double tick_value;     // $ per tick
    double commission_rt;  // round-trip commission ($) — doc lines 449-458
    double spread_ticks;   // estimated spread in ticks — doc lines 449-458
    double slippage_ticks; // estimated slippage in ticks (one way) — doc lines 449-458
};

// Exact values from doc lines 449-458
// UB not listed in doc cost table — spread/slippage set to 0
static const std::unordered_map<std::string, Spec> specs = {
    {"HG",  {6000.0,  110000.0, 0.0005,   12.50,  2.50,  0.5, 0.5}},
    {"GC",  {11000.0, 200000.0, 0.10,     10.00,  2.50,  1.0, 0.5}},
    {"CL",  {7000.0,   75000.0, 0.01,     10.00,  2.50,  1.0, 1.0}},
    {"MES", {1500.0,   25000.0, 0.25,      1.25,  0.50,  1.0, 0.5}},
    {"MNQ", {2000.0,   40000.0, 0.25,      0.50,  0.50,  1.0, 0.5}},
    {"ZN",  {2500.0,  110000.0, 0.015625, 15.625, 1.50,  0.5, 0.5}},
    {"UB",  {9000.0,  130000.0, 0.03125,  31.25,  2.50,  0.0, 0.0}}, // not in doc cost table
    {"6J",  {4000.0,   80000.0, 0.000001, 12.50,  2.50,  1.0, 0.5}},
    {"SI",  {10000.0, 150000.0, 0.005,    25.00,  2.50,  1.0, 1.0}},
};

static Spec get(const std::string& sym) {
    auto it = specs.find(sym);
    if (it != specs.end()) return it->second;
    return {5000.0, 100000.0, 0.01, 10.0, 2.50, 1.0, 0.5};
}

// doc Phase 6 line 749: "spread + slippage + commission per contract"
// round-trip cost = commission + spread (paid once on entry) + 2 * slippage (entry + exit)
static double total_cost_rt(const std::string& sym) {
    const Spec s = get(sym);
    return s.commission_rt
         + (s.spread_ticks * s.tick_value)
         + (2.0 * s.slippage_ticks * s.tick_value);
}
}

// doc: "Base Notional Allocation — allocation weights by asset class"
static const std::unordered_map<std::string, double> ASSET_WEIGHTS = {
    {"equity_index", 0.30},  // MES, MNQ
    {"commodities",  0.35},  // CL, HG, GC, SI
    {"fixed_income", 0.25},  // ZN, UB
    {"fx",           0.10},  // 6J
};

static std::string asset_class(const std::string& sym) {
    if (sym == "MES" || sym == "MNQ") return "equity_index";
    if (sym == "CL" || sym == "HG" || sym == "GC" || sym == "SI") return "commodities";
    if (sym == "ZN" || sym == "UB") return "fixed_income";
    if (sym == "6J") return "fx";
    return "commodities";
}

// doc: "Position Limits" table (line 615-618)
// Only equity index and commodity have per-instrument limits
// Fixed income (ZN, UB) and FX (6J) have no per-instrument caps in document
static const std::unordered_map<std::string, double> SINGLE_NOTIONAL_LIMIT = {
    {"MES", 0.20}, {"MNQ", 0.20},
    {"HG",  0.15}, {"GC",  0.15}, {"CL",  0.15}, {"SI",  0.15}
    // ZN, UB, 6J intentionally omitted - no per-instrument limits in doc
};
static constexpr double MAX_TOTAL_EQUITY_NOTIONAL    = 0.35;
static constexpr double MAX_TOTAL_COMMODITY_NOTIONAL = 0.40;

// ============================================================
// Per-day output
// ============================================================
struct DailySignal {
    std::string date;
    double cu_gold_ratio = 0.0;
    double roc_10 = 0.0, roc_20 = 0.0, roc_60 = 0.0;
    double signal_ma = 0.0, ratio_zscore = 0.0, signal_z = 0.0, composite = 0.0;
    MacroTilt macro_tilt = MacroTilt::NEUTRAL;

    double growth_signal = 0.0, inflation_signal = 0.0, liquidity_score = 0.0;
    double real_rate_10y = 0.0, real_rate_chg_20d = 0.0, real_rate_zscore = 0.0;
    Regime regime = Regime::NEUTRAL;

    double dxy_momentum = 0.0;
    DXYTrend dxy_trend = DXYTrend::NEUTRAL;
    DXYFilter dxy_filter = DXYFilter::NEUTRAL;
    bool skip_gold_short = false;
    double china_adjustment = 1.0;
    bool boj_intervention = false;
    bool corr_spike_active = false;

    double size_multiplier = 1.0;
    std::unordered_map<std::string, double> target_contracts;
    double portfolio_equity = 0.0;
    double margin_utilization = 0.0;
    bool drawdown_warning = false;
    bool drawdown_stop = false;
    int signal_flips_trailing_year = 0;  // doc line 125, 587: max 8-12 flips/year
    double spx_price = 0.0;              // doc line 603: needed for SPX correlation metric
};

// ============================================================
// Strategy parameters - EXACTLY from document
// ============================================================
struct StrategyParams {
    // Layer 1
    int roc_10_window = 10;
    int roc_20_window = 20;
    int roc_60_window = 60;
    int ma_fast = 10;
    int ma_slow = 50;
    int zscore_window = 120;
    double zscore_thresh = 0.5;
    double composite_thresh = 0.0;
    double w1 = 0.33, w2 = 0.33, w3 = 0.34;

    // Layer 2
    int spx_mom_window = 60;
    int breakeven_window = 20;
    int liq_zscore_window = 60;
    int real_rate_chg_window = 20;
    int real_rate_z_window = 120;
    double liquidity_thresh = -1.5;

    // Layer 3
    int dxy_mom_window = 20;
    double dxy_mom_thresh = 0.03;

    // Correlation spike
    int corr_window = 20;
    double corr_thresh = 0.70;

    // BOJ filter
    double boj_move_thresh = 0.02;

    // Risk
    double leverage_target = 2.0;
    double max_margin_util = 0.50;
    double drawdown_warn = 0.10;
    double drawdown_stop = 0.15;
    int min_hold_days = 5;

    // China filter
    bool use_china_filter = true;
    double china_cli_thresh = -2.0;

    double initial_capital = 1000000.0;

    // Mode control
    bool use_fixed_positions = false;
    double fixed_position_size = 1.0;
};

// ============================================================
// Main strategy class
// ============================================================
class CopperGoldStrategy {
public:
    CopperGoldStrategy(const std::string& data_dir, const StrategyParams& p)
        : data_dir_(data_dir), p_(p) {}

    bool load_data() {
        auto fut_path = [&](const std::string& sym) {
            return data_dir_ + "/futures/" + sym + ".csv";
        };
        auto mac_path = [&](const std::string& name) {
            return data_dir_ + "/macro/" + name + ".csv";
        };

        std::cout << "[INFO] Loading futures data...\n";
        for (const auto& sym : {"HG", "GC", "CL", "SI", "ZN", "UB", "6J", "MES", "MNQ"}) {
            fut_[sym] = load_futures(fut_path(sym));
            if (fut_[sym].empty())
                std::cerr << "[WARN] No data for " << sym << "\n";
            else
                std::cout << "[INFO] Loaded " << fut_[sym].size() << " bars for " << sym << "\n";
        }

        // Verify units
        std::cout << "[DEBUG] GC first price: " << fut_["GC"].begin()->second.close << "\n";
        std::cout << "[DEBUG] GC last price:  " << fut_["GC"].rbegin()->second.close << "\n";
        std::cout << "[DEBUG] HG first price: " << fut_["HG"].begin()->second.close << "\n";
        std::cout << "[DEBUG] HG last price:  " << fut_["HG"].rbegin()->second.close << "\n";
        std::cout << "[DEBUG] 6J first price: " << fut_["6J"].begin()->second.close << "\n";
        std::cout << "[DEBUG] 6J last price:  " << fut_["6J"].rbegin()->second.close << "\n";

        std::cout << "[INFO] Loading macro data...\n";
        dxy_ts_ = load_macro(mac_path("dxy"));
        vix_ts_ = load_macro(mac_path("vix"));
        hy_ts_ = load_macro(mac_path("high_yield_spread"));
        breakeven_ts_ = load_macro(mac_path("breakeven_10y"));
        treasury_ts_ = load_macro(mac_path("treasury_10y"));
        tips_ts_ = load_macro(mac_path("tips_10y"));        // doc line 682
        spx_ts_ = load_macro(mac_path("spx"));
        fed_bs_ts_ = load_macro(mac_path("fed_balance_sheet"));
        china_cli_ts_ = load_macro(mac_path("china_leading_indicator"));
        cny_usd_ts_ = load_macro(mac_path("cny_usd"));     // doc line 689

        std::cout << "[INFO] DXY records: " << dxy_ts_.size() << "\n";
        std::cout << "[INFO] VIX records: " << vix_ts_.size() << "\n";
        std::cout << "[INFO] HY spread records: " << hy_ts_.size() << "\n";
        std::cout << "[INFO] Breakeven records: " << breakeven_ts_.size() << "\n";
        std::cout << "[INFO] Treasury records: " << treasury_ts_.size() << "\n";
        std::cout << "[INFO] TIPS records: " << tips_ts_.size() << "\n";
        std::cout << "[INFO] SPX records: " << spx_ts_.size() << "\n";
        std::cout << "[INFO] Fed BS records: " << fed_bs_ts_.size() << "\n";
        std::cout << "[INFO] China CLI records: " << china_cli_ts_.size() << "\n";
        std::cout << "[INFO] CNY/USD records: " << cny_usd_ts_.size() << "\n";

        return !fut_["HG"].empty() && !fut_["GC"].empty();
    }

    std::vector<DailySignal> run() {
        // Date range
        int start_dk = std::max(fut_["HG"].begin()->first, fut_["GC"].begin()->first);
        int end_dk = std::min(fut_["HG"].rbegin()->first, fut_["GC"].rbegin()->first);

        std::cout << "[INFO] Date range: " << date_from_int(start_dk)
                  << " to " << date_from_int(end_dk) << "\n";

        // Build calendar
        std::map<int, bool> date_set;
        for (const auto& [sym, series] : fut_) {
            for (const auto& [dk, bar] : series) {
                if (dk >= start_dk && dk <= end_dk)
                    date_set[dk] = true;
            }
        }
        std::vector<int> dates;
        for (const auto& [dk, _] : date_set) dates.push_back(dk);
        std::sort(dates.begin(), dates.end());

        int n = dates.size();
        std::cout << "[INFO] Total trading days: " << n << "\n";

        // Extract price series
        auto extract_close = [&](const std::string& sym) {
            std::vector<double> v(n, std::numeric_limits<double>::quiet_NaN());
            for (int i = 0; i < n; ++i)
                v[i] = ffill_fut_close(fut_[sym], dates[i]);
            return v;
        };

        auto extract_macro = [&](const TimeSeries& ts) {
            std::vector<double> v(n, std::numeric_limits<double>::quiet_NaN());
            for (int i = 0; i < n; ++i)
                v[i] = ffill(ts, dates[i]);
            return v;
        };

        std::vector<double> hg = extract_close("HG");
        std::vector<double> gc = extract_close("GC");
        std::vector<double> cl = extract_close("CL");
        std::vector<double> si = extract_close("SI");
        std::vector<double> zn = extract_close("ZN");
        std::vector<double> ub = extract_close("UB");
        std::vector<double> jy = extract_close("6J");
        std::vector<double> mes = extract_close("MES");
        std::vector<double> mnq = extract_close("MNQ");

        // ================================================================
        // DATA VALIDATION - Check for unrealistic price moves
        // ================================================================
        for (int i = 1; i < n; ++i) {
            double hg_change = std::abs(hg[i] - hg[i-1]) / hg[i-1];
            double gc_change = std::abs(gc[i] - gc[i-1]) / gc[i-1];
            double cl_change = std::abs(cl[i] - cl[i-1]) / cl[i-1];

            if (hg_change > 0.5) {  // 50% move in one day
                std::cout << "[ERROR] Unrealistic HG price move: "
                          << date_from_int(dates[i-1]) << " " << hg[i-1]
                          << " -> " << date_from_int(dates[i]) << " " << hg[i]
                          << " (" << (hg_change*100) << "%)\n";
            }
            if (gc_change > 0.5) {
                std::cout << "[ERROR] Unrealistic GC price move: "
                          << date_from_int(dates[i-1]) << " " << gc[i-1]
                          << " -> " << date_from_int(dates[i]) << " " << gc[i]
                          << " (" << (gc_change*100) << "%)\n";
            }
            if (cl_change > 0.5) {
                std::cout << "[ERROR] Unrealistic CL price move: "
                          << date_from_int(dates[i-1]) << " " << cl[i-1]
                          << " -> " << date_from_int(dates[i]) << " " << cl[i]
                          << " (" << (cl_change*100) << "%)\n";
            }
        }


        std::vector<double> dxy = extract_macro(dxy_ts_);
        std::vector<double> vix = extract_macro(vix_ts_);
        std::vector<double> hy = extract_macro(hy_ts_);
        std::vector<double> breakeven = extract_macro(breakeven_ts_);
        std::vector<double> treasury = extract_macro(treasury_ts_);
        std::vector<double> spx = extract_macro(spx_ts_);
        std::vector<double> fed_bs = extract_macro(fed_bs_ts_);
        std::vector<double> china_cli = extract_macro(china_cli_ts_);

        // ================================================================
        // Layer 1: Cu/Gold Ratio - NOTIONAL NORMALIZATION
        // ================================================================
        std::vector<double> ratio(n, std::numeric_limits<double>::quiet_NaN());
        for (int i = 0; i < n; ++i) {
            if (!std::isnan(hg[i]) && !std::isnan(gc[i]) && gc[i] > 0.0) {
                // Cu_notional = HG_price * 25000 / 100 (convert cents to dollars)
                // Au_notional = GC_price * 100
                double cu_notional = hg[i] * 25000.0;
                double au_notional = gc[i] * 100.0;
                ratio[i] = cu_notional / au_notional;
            }
        }

        // Signal calculations
        auto ratio_sma10 = rolling_mean(ratio, p_.ma_fast);
        auto ratio_sma50 = rolling_mean(ratio, p_.ma_slow);
        auto ratio_sma120 = rolling_mean(ratio, p_.zscore_window);
        auto ratio_std120 = rolling_std(ratio, p_.zscore_window);

        // SPX momentum (60d)
        std::vector<double> spx_mom(n, std::numeric_limits<double>::quiet_NaN());
        for (int i = p_.spx_mom_window; i < n; ++i) {
            if (!std::isnan(spx[i]) && !std::isnan(spx[i - p_.spx_mom_window]) && spx[i - p_.spx_mom_window] > 0.0)
                spx_mom[i] = ((spx[i] / spx[i - p_.spx_mom_window]) - 1.0) * 100.0;
        }

        // Breakeven change (20d)
        std::vector<double> be_chg(n, std::numeric_limits<double>::quiet_NaN());
        for (int i = p_.breakeven_window; i < n; ++i) {
            if (!std::isnan(breakeven[i]) && !std::isnan(breakeven[i - p_.breakeven_window]))
                be_chg[i] = breakeven[i] - breakeven[i - p_.breakeven_window];
        }

        // Real rates
        std::vector<double> real_rate(n, std::numeric_limits<double>::quiet_NaN());
        for (int i = 0; i < n; ++i) {
            if (!std::isnan(treasury[i]) && !std::isnan(breakeven[i]))
                real_rate[i] = treasury[i] - breakeven[i];
        }

        std::vector<double> rr_chg(n, std::numeric_limits<double>::quiet_NaN());
        for (int i = p_.real_rate_chg_window; i < n; ++i) {
            if (!std::isnan(real_rate[i]) && !std::isnan(real_rate[i - p_.real_rate_chg_window]))
                rr_chg[i] = real_rate[i] - real_rate[i - p_.real_rate_chg_window];
        }

        auto rr_zscore = rolling_zscore(real_rate, p_.real_rate_z_window);

        // Z-scores for liquidity
        auto vix_z60 = rolling_zscore(vix, p_.liq_zscore_window);
        auto hy_z60 = rolling_zscore(hy, p_.liq_zscore_window);

        // Fed balance sheet YoY growth
        std::vector<double> fed_bs_yoy(n, std::numeric_limits<double>::quiet_NaN());
        for (int i = 252; i < n; ++i) {
            if (!std::isnan(fed_bs[i]) && !std::isnan(fed_bs[i - 252]) && fed_bs[i - 252] > 0.0)
                fed_bs_yoy[i] = (fed_bs[i] / fed_bs[i - 252]) - 1.0;
        }

        // Z-score the fed balance sheet YoY growth for normalization
        // (outline lines 218-222: all three components must be on comparable scale)
        auto fed_bs_yoy_z60 = rolling_zscore(fed_bs_yoy, p_.liq_zscore_window);

        // DXY moving averages
        auto dxy_sma50 = rolling_mean(dxy, 50);
        auto dxy_sma200 = rolling_mean(dxy, 200);

        // China CLI 3-month average
        auto china_sma65 = rolling_mean(china_cli, 65);

        // ATRs for volatility adjustment
        auto gc_atr = compute_atr(dates, fut_["GC"], 20);
        auto si_atr = compute_atr(dates, fut_["SI"], 20);

        // Pre-compute returns for correlation
        auto make_ret = [&](const std::vector<double>& px) {
            std::vector<double> r(n, std::numeric_limits<double>::quiet_NaN());
            for (int i = 1; i < n; ++i)
                if (!std::isnan(px[i]) && !std::isnan(px[i-1]) && px[i-1] > 0.0)
                    r[i] = std::log(px[i] / px[i-1]);
            return r;
        };

        std::vector<std::vector<double>> all_rets = {
            make_ret(hg), make_ret(gc), make_ret(cl), make_ret(si),
            make_ret(zn), make_ret(ub), make_ret(jy),
            make_ret(mes), make_ret(mnq)
        };

        // ================================================================
        // MAIN LOOP
        // ================================================================
        std::vector<DailySignal> signals;
        signals.reserve(n);

        double equity = p_.initial_capital;
        double peak_equity = equity;

        MacroTilt prev_tilt = MacroTilt::NEUTRAL;
        MacroTilt pending_tilt = MacroTilt::NEUTRAL;
        int pending_count = 0;

        // Track positions and entry prices
        std::unordered_map<std::string, double> positions;
        std::unordered_map<std::string, double> entry_prices;
        for (const char* s : {"HG", "GC", "CL", "SI", "ZN", "UB", "6J", "MES", "MNQ"}) {
            positions[s] = 0.0;
            entry_prices[s] = std::numeric_limits<double>::quiet_NaN();
        }

        // Point values
        const std::unordered_map<std::string, double> POINT_VALUE = {
            {"HG", 250.0},    // 1 cent = $250
            {"GC", 100.0},    // $1 = $100
            {"CL", 1000.0},   // $1 = $1000
            {"SI", 5000.0},   // $1 = $5000
            {"ZN", 1000.0},   // 1 point = $1000
            {"UB", 1000.0},   // 1 point = $1000
            {"6J", 12.50},    // 1 pip = $12.50
            {"MES", 5.0},     // 1 point = $5
            {"MNQ", 2.0}      // 1 point = $2
        };

        // Price vectors for easy access
        std::unordered_map<std::string, const std::vector<double>*> px_map = {
            {"HG", &hg}, {"GC", &gc}, {"CL", &cl}, {"SI", &si},
            {"ZN", &zn}, {"UB", &ub}, {"6J", &jy}, {"MES", &mes}, {"MNQ", &mnq}
        };

        // State variables that persist across iterations (weekly rebalance, regime tracking)
        // State variables that persist across iterations (weekly rebalance, regime tracking)
        Regime     prev_regime_state     = Regime::NEUTRAL;
        DXYFilter  prev_dxy_filter_state = DXYFilter::NEUTRAL;
        std::deque<int> flip_dates_deque;
        MacroTilt  last_flip_tilt = MacroTilt::NEUTRAL;
        double     last_equity_debug = equity;
        int        infl_shock_days = 0;


        for (int i = 0; i < n; ++i) {
            if (std::isnan(ratio[i])) continue;

            // ============================================================
            // Layer 1: Signal Generation
            double roc10 = std::numeric_limits<double>::quiet_NaN();
            double roc20 = std::numeric_limits<double>::quiet_NaN();
            double roc60 = std::numeric_limits<double>::quiet_NaN();

            if (i >= p_.roc_10_window && !std::isnan(ratio[i - p_.roc_10_window]) && ratio[i - p_.roc_10_window] > 0.0)
                roc10 = (ratio[i] / ratio[i - p_.roc_10_window]) - 1.0;
            if (i >= p_.roc_20_window && !std::isnan(ratio[i - p_.roc_20_window]) && ratio[i - p_.roc_20_window] > 0.0)
                roc20 = (ratio[i] / ratio[i - p_.roc_20_window]) - 1.0;
            if (i >= p_.roc_60_window && !std::isnan(ratio[i - p_.roc_60_window]) && ratio[i - p_.roc_60_window] > 0.0)
                roc60 = (ratio[i] / ratio[i - p_.roc_60_window]) - 1.0;

            double signal_ma = 0.0;
            if (!std::isnan(ratio_sma10[i]) && !std::isnan(ratio_sma50[i]))
                signal_ma = (ratio_sma10[i] > ratio_sma50[i]) ? 1.0 : -1.0;

            double zscore = std::numeric_limits<double>::quiet_NaN();
            double signal_z = 0.0;
            if (!std::isnan(ratio_sma120[i]) && !std::isnan(ratio_std120[i]) && ratio_std120[i] > 0.0) {
                zscore = (ratio[i] - ratio_sma120[i]) / ratio_std120[i];
                if (zscore > p_.zscore_thresh) signal_z = 1.0;
                else if (zscore < -p_.zscore_thresh) signal_z = -1.0;
            }

            double composite = std::numeric_limits<double>::quiet_NaN();
            if (!std::isnan(roc20) && !std::isnan(zscore)) {
                double sign_roc20 = (roc20 > 0.0) ? 1.0 : -1.0;
                composite = p_.w1 * sign_roc20 + p_.w2 * signal_ma + p_.w3 * signal_z;
            }

            MacroTilt raw_tilt = MacroTilt::NEUTRAL;
            if (!std::isnan(composite)) {
                if (composite > p_.composite_thresh) raw_tilt = MacroTilt::RISK_ON;
                else if (composite < -p_.composite_thresh) raw_tilt = MacroTilt::RISK_OFF;
            }

            // Minimum holding period
            MacroTilt macro_tilt = prev_tilt;
            if (raw_tilt != prev_tilt) {
                if (raw_tilt != pending_tilt) {
                    pending_tilt = raw_tilt;
                    pending_count = 1;
                } else {
                    pending_count++;
                }
                if (pending_count >= p_.min_hold_days) {
                    prev_tilt = pending_tilt;
                    macro_tilt = pending_tilt;
                    pending_count = 0;
                }
            } else {
                pending_tilt = raw_tilt;
                pending_count = 0;
                macro_tilt = prev_tilt;
            }

            // ============================================================
            // Signal Stability Check (doc line 125, 357, 587, 607)
            // "signal should not flip more than 8-12x per year"
            // ============================================================
            // Track flips in trailing 252-day window (1 trading year)
            // We record flip dates in a deque and count those within 252 days
            auto& flip_dates = flip_dates_deque;
            // Capture tilt_just_changed BEFORE any mutation, so it can be reused
            // for rebalance detection later in the same iteration (outline lines 361, 587)
            bool tilt_just_changed = (macro_tilt != last_flip_tilt);
            if (tilt_just_changed) {
                flip_dates.push_back(i);
            }
            while (!flip_dates.empty() && (i - flip_dates.front()) > 252)
                flip_dates.pop_front();
            int flips_trailing_year = static_cast<int>(flip_dates.size());

            // ============================================================
            // Layer 2: Regime Classifier
            // ============================================================
            double growth = std::isnan(spx_mom[i]) ? 0.0 : spx_mom[i];
            double inflation = std::isnan(be_chg[i]) ? 0.0 : be_chg[i];

            // EXACT from outline lines 175-179:
            // Composite liquidity indicator = normalize(
            //    -1 * VIX_percentile_60d +              # from vix.csv
            //    -1 * high_yield_spread_zscore +        # from high_yield_spread.csv
            //    fed_balance_sheet_growth_yoy           # from fed_balance_sheet.csv
            // )

            // Calculate VIX percentile (60-day)
            double vix_percentile = 0.0;
            if (i >= 60) {
                std::vector<double> vix_window;
                for (int k = i - 59; k <= i; ++k) {
                    if (!std::isnan(vix[k])) vix_window.push_back(vix[k]);
                }
                if (!vix_window.empty()) {
                    std::sort(vix_window.begin(), vix_window.end());
                    double current_vix = vix[i];
                    auto it = std::lower_bound(vix_window.begin(), vix_window.end(), current_vix);
                    int rank = std::distance(vix_window.begin(), it);
                    vix_percentile = static_cast<double>(rank) / vix_window.size();
                }
            }

            // High yield spread z-score (already computed as hy_z60)
            double hy_zscore = std::isnan(hy_z60[i]) ? 0.0 : hy_z60[i];

            // Fed balance sheet YoY growth (already computed as fed_bs_yoy)

            // Normalize components (simplified normalization: scale each to approx -3 to +3 range)
            // Composite liquidity indicator per outline lines 217-222:
            // normalize(-1*VIX_percentile_60d + -1*HY_spread_zscore + fed_bs_yoy) / 3
            // All three components z-scored for comparable scale
            // Doc: normalize the sum of three components, not pre-z-score each
            // fed_bs_yoy is already a ratio (e.g. 0.15 = 15% YoY growth)
            // Scale it to be comparable to the other components (approx -3 to +3)
            double fbs_raw       = std::isnan(fed_bs_yoy[i]) ? 0.0 : fed_bs_yoy[i];
            double vix_component = -1.0 * (vix_percentile * 6.0 - 3.0);
            double hy_component  = -1.0 * hy_zscore;
            double fbs_component = fbs_raw * 10.0;  // scale: 0.3 YoY growth -> +3.0
            double liquidity = (vix_component + hy_component + fbs_component) / 3.0;

            // DEBUG: Print liquidity values around Nov 2014
            if (dates[i] >= parse_date("2014-11-01") && dates[i] <= parse_date("2014-12-31")) {
                std::cout << date_from_int(dates[i])
                          << " liquidity: " << liquidity
                          << " vix_comp: " << vix_component
                          << " hy_comp: " << hy_component
                          << " fbs_comp: " << fbs_component
                          << " thresh: " << p_.liquidity_thresh << "\n";
            }

            double rr_val = std::isnan(real_rate[i]) ? 0.0 : real_rate[i];
            double rr_chg_val = std::isnan(rr_chg[i]) ? 0.0 : rr_chg[i];
            double rr_z_val = std::isnan(rr_zscore[i]) ? 0.0 : rr_zscore[i];

            Regime regime = Regime::NEUTRAL;
            if (liquidity < p_.liquidity_thresh) {
                regime = Regime::LIQUIDITY_SHOCK;
            } else if (inflation > 0.10 && growth < 0.5) {
                regime = Regime::INFLATION_SHOCK;
            } else if (growth > 0.5) {
                regime = Regime::GROWTH_POSITIVE;
            } else if (growth < -0.5) {
                regime = Regime::GROWTH_NEGATIVE;
            }

            // DEBUG: breakeven units check
            if (i % 252 == 0) {
                std::cout << "[BE_CHECK] " << date_from_int(dates[i])
                          << " breakeven=" << breakeven[i]
                          << " be_chg_20d=" << (std::isnan(be_chg[i]) ? 0.0 : be_chg[i])
                          << " growth=" << growth
                          << " inflation_check=" << (inflation > 0.10 && growth < 0.5)
                          << "\n";
            }

            // DEBUG: count inflation shock days
            if (regime == Regime::INFLATION_SHOCK) infl_shock_days++;

            // DEBUG: Track regime changes (your existing code, keep it)
            static Regime last_debug_regime = Regime::NEUTRAL;
            if (regime != last_debug_regime && dates[i] >= parse_date("2014-11-01")) {
                last_debug_regime = regime;
            }

            // ============================================================
            // Layer 3: DXY Filter
            // ============================================================
            DXYTrend dxy_trend = DXYTrend::NEUTRAL;
            if (!std::isnan(dxy[i]) && !std::isnan(dxy_sma50[i]) && !std::isnan(dxy_sma200[i])) {
                if (dxy[i] > dxy_sma50[i] && dxy_sma50[i] > dxy_sma200[i])
                    dxy_trend = DXYTrend::STRONG;
                else if (dxy[i] < dxy_sma50[i] && dxy_sma50[i] < dxy_sma200[i])
                    dxy_trend = DXYTrend::WEAK;
            }

            double dxy_mom = std::numeric_limits<double>::quiet_NaN();
            if (i >= p_.dxy_mom_window && !std::isnan(dxy[i]) && !std::isnan(dxy[i - p_.dxy_mom_window]) && dxy[i - p_.dxy_mom_window] > 0.0)
                dxy_mom = (dxy[i] / dxy[i - p_.dxy_mom_window]) - 1.0;

            DXYFilter dxy_filter = DXYFilter::NEUTRAL;
            // Lines 657-664 - CURRENT CODE:
            // EXACT from outline lines 160-169 (DXY Filter Rules table):
            if (!std::isnan(dxy_mom)) {
                if (dxy_mom > p_.dxy_mom_thresh) {  // DXY momentum > +3%
                    if (macro_tilt == MacroTilt::RISK_ON) {
                        dxy_filter = DXYFilter::SUSPECT;      // "Suspect - may be USD squeeze, not growth. Reduce size 50%"
                    } else if (macro_tilt == MacroTilt::RISK_OFF) {
                        dxy_filter = DXYFilter::CONFIRMED;    // "Confirmed risk-off + USD strength. Full risk-off"
                    } else {
                        dxy_filter = DXYFilter::NEUTRAL;
                    }
                } else if (dxy_mom < -p_.dxy_mom_thresh) {  // DXY momentum < -3%
                    if (macro_tilt == MacroTilt::RISK_ON) {
                        dxy_filter = DXYFilter::CONFIRMED;    // "Confirmed risk-on + USD weakness. Full risk-on"
                    } else if (macro_tilt == MacroTilt::RISK_OFF) {
                        dxy_filter = DXYFilter::SUSPECT;      // "Suspect - may be inflation/gold bid. Check regime classifier"
                    } else {
                        dxy_filter = DXYFilter::NEUTRAL;
                    }
                } else {
                    dxy_filter = DXYFilter::NEUTRAL;          // "DXY neutral. Trust Cu/Gold signal at full size"
                }
            }

            // Safe-haven override
            bool skip_gold_short = false;
            if (i > 0 && !std::isnan(vix[i]) && !std::isnan(gc[i]) && !std::isnan(gc[i-1]) && !std::isnan(spx[i]) && !std::isnan(spx[i-1])) {
                double vix90 = std::numeric_limits<double>::quiet_NaN();
                if (i >= 59) {
                    std::vector<double> vix_window;
                    for (int k = i - 59; k <= i; ++k)
                        if (!std::isnan(vix[k])) vix_window.push_back(vix[k]);
                    if (!vix_window.empty()) {
                        std::sort(vix_window.begin(), vix_window.end());
                        int idx = (int)(0.90 * vix_window.size());
                        if (idx >= (int)vix_window.size()) idx = (int)vix_window.size() - 1;
                        vix90 = vix_window[idx];
                    }
                }
                double gold_ret = (gc[i] / gc[i-1]) - 1.0;
                double eq_ret = (spx[i] / spx[i-1]) - 1.0;
                if (!std::isnan(vix90) && vix[i] > vix90 && gold_ret > 0.015 && eq_ret < -0.015)
                    skip_gold_short = true;
            }

            // China filter
            double china_adj = 1.0;
            if (p_.use_china_filter && !std::isnan(china_cli[i]) && !std::isnan(china_sma65[i])) {
                if ((china_cli[i] - china_sma65[i]) < p_.china_cli_thresh)
                    china_adj = 0.7;
            }

            // BOJ intervention
            bool boj_int = false;
            if (i > 0 && !std::isnan(jy[i]) && !std::isnan(jy[i-1]) && jy[i-1] > 0.0) {
                if (std::abs((jy[i] / jy[i-1]) - 1.0) > p_.boj_move_thresh)
                    boj_int = true;
            }

            // Correlation spike
            bool corr_spike = false;
            if (avg_pairwise_corr(all_rets, i, p_.corr_window) > p_.corr_thresh)
                corr_spike = true;

            // ============================================================
            // Size Multiplier (EXACT from doc)
            // ============================================================
            double size_mult = 1.0;

            if (regime == Regime::LIQUIDITY_SHOCK) {
                size_mult = 0.0;
            } else if (macro_tilt == MacroTilt::RISK_ON && regime == Regime::GROWTH_NEGATIVE) {
                size_mult = 0.5;
            } else if (regime == Regime::NEUTRAL) {
                size_mult = 0.5;
            } else if (regime == Regime::INFLATION_SHOCK) {
                size_mult = 0.5;
            }

            if (dxy_filter == DXYFilter::SUSPECT)
                size_mult *= 0.5;

            if (liquidity < p_.liquidity_thresh)
                size_mult *= 0.25;

            if (corr_spike)
                size_mult *= 0.5;

            size_mult *= china_adj;
        std::unordered_set<std::string> stopped_out_today;

            // ============================================================
            // P&L Calculation
            // ============================================================
            if (i > 0) {
                double daily_pnl = 0.0;
                for (const auto& [sym, qty] : positions) {
                    if (qty == 0.0) continue;
                    const auto* px = px_map[sym];
                    if (!px || std::isnan((*px)[i]) || std::isnan((*px)[i-1])) continue;
                    double pv = POINT_VALUE.count(sym) ? POINT_VALUE.at(sym) : 0.0;
                    double price_change = (*px)[i] - (*px)[i-1];
                    daily_pnl += qty * price_change * pv;
                }

                // Position-level stop: exit if position loss > 2 * ATR(20)
                // ATR is available for GC and SI; for others use a 20-day price std proxy
                for (auto& [sym, qty] : positions) {
                    if (qty == 0.0) continue;
                    const auto* px = px_map[sym];
                    if (!px || std::isnan((*px)[i])) continue;
                    double pv = POINT_VALUE.count(sym) ? POINT_VALUE.at(sym) : 0.0;

                    // Compute 20-day ATR for this symbol from price series
                    double atr20 = std::numeric_limits<double>::quiet_NaN();
                    if (i >= 20) {
                        double tr_sum = 0.0;
                        int tr_count = 0;
                        for (int k = i - 19; k <= i; ++k) {
                            auto it = fut_[sym].find(dates[k]);
                            if (it == fut_[sym].end()) continue;
                            auto it_prev = fut_[sym].lower_bound(dates[k]);
                            if (it_prev == fut_[sym].begin()) continue;
                            --it_prev;
                            double pc = it_prev->second.close;
                            double hi = it->second.high;
                            double lo = it->second.low;
                            tr_sum += std::max({hi - lo, std::abs(hi - pc), std::abs(lo - pc)});
                            tr_count++;
                        }
                        if (tr_count > 0) atr20 = tr_sum / tr_count;
                    }

                    if (!std::isnan(atr20) && atr20 > 0.0) {
                        double entry_px = entry_prices.count(sym) ? entry_prices.at(sym) : std::numeric_limits<double>::quiet_NaN();
                        if (!std::isnan(entry_px)) {
                            double dollar_atr = atr20 * std::abs(qty) * pv;
                            double position_dollar_loss = -(qty * ((*px)[i] - entry_px) * pv);
                            if (position_dollar_loss > 2.0 * dollar_atr) {
                                qty = 0.0;
                                entry_prices[sym] = std::numeric_limits<double>::quiet_NaN();
                                stopped_out_today.insert(sym);
                            }
                        }
                    }
                }



                equity += daily_pnl;

                if (daily_pnl > 10000 || daily_pnl < -10000) {
                    std::cout << "        Post-equity: $" << equity << "\n";
                }

                if (equity > peak_equity) peak_equity = equity;


                // Add sanity check
                if (equity > last_equity_debug * 1.5 || equity < last_equity_debug * 0.5) {
                    std::cout << "[WARN] Unusual equity change: "
                              << last_equity_debug << " -> " << equity << "\n";
                }
                last_equity_debug = equity;
            }

            // Drawdown stops
            bool dd_warn = false, dd_stop = false;
            double drawdown = (peak_equity > 0.0) ? (peak_equity - equity) / peak_equity : 0.0;
            if (drawdown > p_.drawdown_stop) {
                size_mult = 0.0;
                dd_stop = true;
            } else if (drawdown > p_.drawdown_warn) {
                size_mult *= 0.5;
                dd_warn = true;
            }
            if (dd_stop && all_positions_zero(positions)) {
                peak_equity = equity;
            }
            if (dd_stop || dd_warn) {
                std::cout << "[DD] " << date_from_int(dates[i])
                          << " equity=" << equity
                          << " peak=" << peak_equity
                          << " dd=" << drawdown
                          << " stop=" << dd_stop
                          << " warn=" << dd_warn << "\n";
            }

            // ============================================================
            // POSITION SIZING
            // Doc line 365: "Weekly rebalance check (Fridays)"
            // Positions only change on:
            //   1. Friday (weekly calendar rebalance)
            //   2. Signal flip (composite crosses threshold) — macro_tilt changed
            //   3. Regime change (classifier changed state)
            //   4. Filter trigger (DXY or liquidity hits threshold)
            //   5. Stop-loss triggered (drawdown or ATR stop)
            // ============================================================
            // Determine if today is a rebalance day
            bool is_friday = false;
            {
                time_t ts = static_cast<time_t>(dates[i]) * 86400;
                std::tm* t = std::gmtime(&ts);
                is_friday = (t->tm_wday == 5); // 0=Sun,1=Mon,...,5=Fri
            }
            // Track previous regime for change detection
            Regime&    prev_regime     = prev_regime_state;
            DXYFilter& prev_dxy_filter = prev_dxy_filter_state;
            static Regime pending_regime = Regime::NEUTRAL;
            static int pending_regime_count = 0;
            if (regime != prev_regime) {
                if (regime == pending_regime) {
                    pending_regime_count++;
                } else {
                    pending_regime = regime;
                    pending_regime_count = 1;
                }
            } else {
                pending_regime = regime;
                pending_regime_count = 0;
            }
            bool regime_changed = (pending_regime_count >= 3);
            bool filter_triggered = (dxy_filter == DXYFilter::SUSPECT && prev_dxy_filter != DXYFilter::SUSPECT);
            bool stop_triggered   = dd_stop || dd_warn;
            // last_flip_tilt tracks the confirmed macro_tilt from previous day exactly
            bool tilt_changed = tilt_just_changed;

            bool do_rebalance = is_friday || regime_changed || filter_triggered ||
                    stop_triggered || tilt_changed;

            if (do_rebalance) {
                std::cout << "[REB] " << date_from_int(dates[i])
                          << " fri=" << is_friday
                          << " regime=" << regime_changed
                          << " filter=" << filter_triggered
                          << " stop=" << stop_triggered
                          << " tilt=" << tilt_changed
                          << " flat=" << all_positions_zero(positions)
                          << " size_mult=" << size_mult        // ADD THIS
                          << " dd=" << drawdown               // ADD THIS
                          << "\n";

            }

            // FORCE REBALANCE if we have no positions but size_mult says we should trade
            if (!do_rebalance && size_mult > 0.0 && all_positions_zero(positions)) {
                do_rebalance = true;
            }

            prev_regime     = regime;
            prev_dxy_filter = dxy_filter;

            std::unordered_map<std::string, double> new_positions;
            // If not a rebalance day, carry existing positions forward
            if (!do_rebalance) {
                new_positions = positions;
                // Still apply stop-loss checks below (ATR stop runs regardless)
                // Recalculate margin_util with current positions
                double margin_util = 0.0;
                for (const auto& [sym, qty] : positions)
                    margin_util += std::abs(qty) * ContractSpec::get(sym).margin;
                margin_util = (equity > 0.0) ? margin_util / equity : 0.0;
                // Save signal and continue
                DailySignal sig;
                sig.date = date_from_int(dates[i]);
                sig.cu_gold_ratio = ratio[i];
                sig.roc_10 = std::isnan(roc10) ? 0.0 : roc10;
                sig.roc_20 = std::isnan(roc20) ? 0.0 : roc20;
                sig.roc_60 = std::isnan(roc60) ? 0.0 : roc60;
                sig.signal_ma = signal_ma;
                sig.ratio_zscore = std::isnan(zscore) ? 0.0 : zscore;
                sig.signal_z = signal_z;
                sig.composite = std::isnan(composite) ? 0.0 : composite;
                sig.macro_tilt = macro_tilt;
                sig.growth_signal = growth;
                sig.inflation_signal = inflation;
                sig.liquidity_score = liquidity;
                sig.real_rate_10y = rr_val;
                sig.real_rate_chg_20d = rr_chg_val;
                sig.real_rate_zscore = rr_z_val;
                sig.regime = regime;
                sig.dxy_momentum = std::isnan(dxy_mom) ? 0.0 : dxy_mom;
                sig.dxy_trend = dxy_trend;
                sig.dxy_filter = dxy_filter;
                sig.skip_gold_short = skip_gold_short;
                sig.china_adjustment = china_adj;
                sig.boj_intervention = boj_int;
                sig.corr_spike_active = corr_spike;
                sig.size_multiplier = size_mult;
                sig.target_contracts = positions;
                sig.portfolio_equity = equity;
                sig.margin_utilization = margin_util;
                sig.drawdown_warning = dd_warn;
                sig.drawdown_stop = dd_stop;
                sig.signal_flips_trailing_year = flips_trailing_year;
                sig.spx_price = std::isnan(spx[i]) ? 0.0 : spx[i];
                signals.push_back(sig);
                continue;
            }

            double margin_util = 0.0;

            if (!p_.use_fixed_positions) {
                // FULL POSITION SIZING MODE

                // Helper lambda for calculating contract sizes
                auto contracts_for = [&](const std::string& sym,
                                         double direction,
                                         double vol_adj = 1.0) -> double {
                    if (std::abs(direction) < 1e-9 || std::abs(size_mult) < 1e-9)
                        return 0.0;

                    const auto cls = asset_class(sym);
                    double w = ASSET_WEIGHTS.count(cls) ? ASSET_WEIGHTS.at(cls) : 0.1;
                    double notional_alloc = equity * p_.leverage_target * w;
                    const auto spec = ContractSpec::get(sym);
                    double raw = (notional_alloc / spec.notional) * size_mult * vol_adj;
                    return std::floor(raw * direction + 0.5);
                };

                // SI volatility adjustment
                double si_adj = std::numeric_limits<double>::quiet_NaN();
                if (!std::isnan(gc_atr[i]) && !std::isnan(si_atr[i]) && si_atr[i] > 0.0) {
                    double gc_dollar_atr = gc_atr[i] * 100.0;
                    double si_dollar_atr = si_atr[i] * 5000.0;
                    if (si_dollar_atr > 0.0)
                        si_adj = gc_dollar_atr / si_dollar_atr;
                }

                double boj_factor = boj_int ? 0.5 : 1.0;

                // TRADE EXPRESSIONS - EXACT from doc
                if (macro_tilt == MacroTilt::RISK_ON) {
                    if (regime == Regime::INFLATION_SHOCK) {
                        new_positions["MES"] = 0.0;
                        new_positions["MNQ"] = 0.0;
                        new_positions["HG"]  = contracts_for("HG", 1.0) * china_adj;
                        new_positions["CL"]  = contracts_for("CL", 1.0);
                        new_positions["SI"]  = std::isnan(si_adj) ? 0.0 : contracts_for("SI", 1.0, si_adj);
                        new_positions["GC"]  = skip_gold_short ? 0.0 : contracts_for("GC", -1.0);
                        new_positions["ZN"]  = contracts_for("ZN", -1.0);
                        new_positions["UB"]  = contracts_for("UB", -1.0);
                        new_positions["6J"]  = contracts_for("6J", -1.0) * boj_factor;
                    } else {
                        new_positions["MES"] = contracts_for("MES", 1.0);
                        new_positions["MNQ"] = contracts_for("MNQ", 1.0);
                        new_positions["HG"]  = contracts_for("HG",  1.0) * china_adj;
                        new_positions["CL"]  = contracts_for("CL",  1.0);
                        new_positions["GC"]  = skip_gold_short ? 0.0 : contracts_for("GC", -1.0);
                        new_positions["SI"]  = std::isnan(si_adj) ? 0.0 : contracts_for("SI",  1.0, si_adj);
                        new_positions["ZN"]  = contracts_for("ZN", -1.0);
                        new_positions["UB"]  = contracts_for("UB", -1.0);
                        new_positions["6J"]  = contracts_for("6J", -1.0) * boj_factor;
                    }
                } else if (macro_tilt == MacroTilt::RISK_OFF) {
                    if (regime == Regime::INFLATION_SHOCK) {
                        new_positions["GC"]  = contracts_for("GC",  1.0);
                        new_positions["ZN"]  = contracts_for("ZN", -1.0);
                        new_positions["UB"]  = contracts_for("UB", -1.0);
                        new_positions["MES"] = contracts_for("MES", -0.5);
                        new_positions["MNQ"] = contracts_for("MNQ", -0.5);
                        new_positions["HG"]  = 0.0;
                        new_positions["CL"]  = 0.0;
                        new_positions["SI"]  = std::isnan(si_adj) ? 0.0 : contracts_for("SI",  1.0, si_adj);
                        new_positions["6J"]  = 0.0;
                    } else {
                        new_positions["MES"] = contracts_for("MES", -1.0);
                        new_positions["MNQ"] = contracts_for("MNQ", -1.0);
                        new_positions["HG"]  = contracts_for("HG",  -1.0);
                        new_positions["CL"]  = contracts_for("CL",  -1.0);
                        new_positions["GC"]  = contracts_for("GC",   1.0);
                        new_positions["SI"]  = std::isnan(si_adj) ? 0.0 : contracts_for("SI",   1.0, si_adj);
                        new_positions["ZN"]  = contracts_for("ZN",   1.0);
                        new_positions["UB"]  = contracts_for("UB",   1.0);
                        new_positions["6J"]  = contracts_for("6J",   1.0) * boj_factor;
                    }
                } else {
                    for (const char* s : {"HG","GC","CL","SI","MES","MNQ","ZN","UB","6J"})
                        new_positions[s] = 0.0;
                }

                // ============================================================
                // POSITION LIMITS - EXACT from doc
                // ============================================================
                // Per-instrument notional cap


                // Right after new_positions are calculated, before position limits
                std::cout << "[SIZING] " << date_from_int(dates[i])
                          << " equity=" << equity
                          << " size_mult=" << size_mult
                          << " GC_raw=" << (equity * p_.leverage_target * 0.35 / 200000.0 * size_mult)
                          << " GC_final=" << new_positions["GC"]
                          << " com_notional_before_cap="
                          << (std::abs(new_positions["HG"]) * 110000.0 +
                              std::abs(new_positions["GC"]) * 200000.0 +
                              std::abs(new_positions["CL"]) * 75000.0 +
                              std::abs(new_positions["SI"]) * 150000.0)
                          << " com_cap=" << (equity * MAX_TOTAL_COMMODITY_NOTIONAL)
                          << "\n";

                for (auto& [sym, qty] : new_positions) {
                    auto lit = SINGLE_NOTIONAL_LIMIT.find(sym);
                    if (lit == SINGLE_NOTIONAL_LIMIT.end()) continue;
                    double max_q = std::floor(equity * lit->second / ContractSpec::get(sym).notional);
                    if (std::abs(qty) > max_q)
                        qty = std::copysign(max_q, qty);
                }

                // Total directional equity cap
                {
                    double eq_not = 0.0;
                    for (const auto& s : {"MES", "MNQ"}) {
                        auto it = new_positions.find(s);
                        if (it != new_positions.end())
                            eq_not += std::abs(it->second) * ContractSpec::get(s).notional;
                    }
                    double max_eq = equity * MAX_TOTAL_EQUITY_NOTIONAL;
                    if (eq_not > max_eq && eq_not > 0.0) {
                        double scale = max_eq / eq_not;
                        for (const auto& s : {"MES", "MNQ"}) {
                            auto it = new_positions.find(s);
                            if (it != new_positions.end())
                                it->second = std::floor(it->second * scale + 0.5);
                        }
                    }
                }

                // Total directional commodity cap
                {
                    double com_not = 0.0;
                    for (const auto& s : {"HG", "GC", "CL", "SI"}) {
                        auto it = new_positions.find(s);
                        if (it != new_positions.end())
                            com_not += std::abs(it->second) * ContractSpec::get(s).notional;
                    }
                    double max_com = equity * MAX_TOTAL_COMMODITY_NOTIONAL;
                    if (com_not > max_com && com_not > 0.0) {
                        double scale = max_com / com_not;
                        for (const auto& s : {"HG", "GC", "CL", "SI"}) {
                            auto it = new_positions.find(s);
                            if (it != new_positions.end())
                                it->second = std::floor(it->second * scale + 0.5);
                        }
                    }
                }

                // Margin utilization
                double total_margin = 0.0;
                for (const auto& [sym, qty] : new_positions)
                    total_margin += std::abs(qty) * ContractSpec::get(sym).margin;
                margin_util = (equity > 0.0) ? total_margin / equity : 0.0;
                if (margin_util > p_.max_margin_util && margin_util > 0.0) {
                    double scale = p_.max_margin_util / margin_util;
                    for (auto& [sym, qty] : new_positions)
                        qty = std::floor(qty * scale + 0.5);
                    margin_util = p_.max_margin_util;
                }

            } else {
                // TEST MODE: fixed positions
                double pos_size = p_.fixed_position_size * size_mult;

                if (macro_tilt == MacroTilt::RISK_ON) {
                    if (regime == Regime::INFLATION_SHOCK) {
                        new_positions["HG"] = pos_size;
                        new_positions["CL"] = pos_size;
                        new_positions["SI"] = pos_size;
                        new_positions["GC"] = skip_gold_short ? 0.0 : -pos_size;
                        new_positions["ZN"] = -pos_size;
                        new_positions["UB"] = -pos_size;
                        new_positions["6J"] = -pos_size * (boj_int ? 0.5 : 1.0);
                        new_positions["MES"] = 0.0;
                        new_positions["MNQ"] = 0.0;
                    } else {
                        new_positions["MES"] = pos_size;
                        new_positions["MNQ"] = pos_size;
                        new_positions["HG"] = pos_size;
                        new_positions["CL"] = pos_size;
                        new_positions["SI"] = pos_size;
                        new_positions["GC"] = skip_gold_short ? 0.0 : -pos_size;
                        new_positions["ZN"] = -pos_size;
                        new_positions["UB"] = -pos_size;
                        new_positions["6J"] = -pos_size * (boj_int ? 0.5 : 1.0);
                    }
                } else if (macro_tilt == MacroTilt::RISK_OFF) {
                    if (regime == Regime::INFLATION_SHOCK) {
                        new_positions["GC"] = pos_size;
                        new_positions["ZN"] = -pos_size;
                        new_positions["UB"] = -pos_size;
                        new_positions["SI"] = pos_size;
                        new_positions["MES"] = -pos_size * 0.5;
                        new_positions["MNQ"] = -pos_size * 0.5;
                        new_positions["HG"] = 0.0;
                        new_positions["CL"] = 0.0;
                        new_positions["6J"] = 0.0;
                    } else {
                        new_positions["MES"] = -pos_size;
                        new_positions["MNQ"] = -pos_size;
                        new_positions["HG"] = -pos_size;
                        new_positions["CL"] = -pos_size;
                        new_positions["GC"] = pos_size;
                        new_positions["SI"] = pos_size;
                        new_positions["ZN"] = pos_size;
                        new_positions["UB"] = pos_size;
                        new_positions["6J"] = pos_size * (boj_int ? 0.5 : 1.0);
                    }
                }
            }
            // Respect ATR stops - don't re-enter stopped positions on same day
            for (const auto& sym : stopped_out_today) {
                new_positions[sym] = 0.0;
            }

            // Update positions for next day — deduct full transaction costs on changes
            // doc Phase 6 line 749: "spread + slippage + commission per contract"
            for (const auto& [sym, new_qty] : new_positions) {
                double old_qty = positions.count(sym) ? positions.at(sym) : 0.0;
                double qty_change = std::abs(new_qty - old_qty);
                if (qty_change > 0.0) {
                    double total_cost = ContractSpec::total_cost_rt(sym) * qty_change;
                    equity -= total_cost;
                }
                // Record entry price when position opens from flat
                if (old_qty == 0.0 && new_qty != 0.0) {
                    const auto* px = px_map.count(sym) ? px_map.at(sym) : nullptr;
                    entry_prices[sym] = (px && !std::isnan((*px)[i])) ? (*px)[i] : std::numeric_limits<double>::quiet_NaN();
                } else if (new_qty == 0.0) {
                    entry_prices[sym] = std::numeric_limits<double>::quiet_NaN();
                }
            }
            positions = new_positions;

            // Print debug every 6 months (126 trading days)


                // Show non-zero positions (limit to 5 to keep output manageable)
                std::string pos_str;
                int pos_count = 0;
                for (const auto& [sym, qty] : positions) {
                    if (qty != 0.0 && pos_count < 5) {
                        char buf[50];
                        snprintf(buf, sizeof(buf), "%s:%.0f ", sym.c_str(), qty);
                        pos_str += buf;
                        pos_count++;
                    }
                }
                if (!pos_str.empty())
                    std::cout << "        Positions: " << pos_str << "\n";


            // Print warning if position sizes get too large
            if (!p_.use_fixed_positions) {
                for (const auto& [sym, qty] : positions) {
                    if (std::abs(qty) > 100) {
                        //std::cout << "[WARN] Large position: " << sym << " = " << qty
                                  //<< " at " << date_from_int(dates[i]) << "\n";
                    }
                }
            }

            // ============================================================
            // Save signal
            // ============================================================
            DailySignal sig;
            sig.date = date_from_int(dates[i]);
            sig.cu_gold_ratio = ratio[i];
            sig.roc_10 = std::isnan(roc10) ? 0.0 : roc10;
            sig.roc_20 = std::isnan(roc20) ? 0.0 : roc20;
            sig.roc_60 = std::isnan(roc60) ? 0.0 : roc60;
            sig.signal_ma = signal_ma;
            sig.ratio_zscore = std::isnan(zscore) ? 0.0 : zscore;
            sig.signal_z = signal_z;
            sig.composite = std::isnan(composite) ? 0.0 : composite;
            sig.macro_tilt = macro_tilt;

            sig.growth_signal = growth;
            sig.inflation_signal = inflation;
            sig.liquidity_score = liquidity;
            sig.real_rate_10y = rr_val;
            sig.real_rate_chg_20d = rr_chg_val;
            sig.real_rate_zscore = rr_z_val;
            sig.regime = regime;

            sig.dxy_momentum = std::isnan(dxy_mom) ? 0.0 : dxy_mom;
            sig.dxy_trend = dxy_trend;
            sig.dxy_filter = dxy_filter;
            sig.skip_gold_short = skip_gold_short;
            sig.china_adjustment = china_adj;
            sig.boj_intervention = boj_int;
            sig.corr_spike_active = corr_spike;

            sig.size_multiplier = size_mult;
            sig.target_contracts = positions;
            sig.portfolio_equity = equity;
            sig.margin_utilization = margin_util;
            sig.drawdown_warning = dd_warn;
            sig.drawdown_stop = dd_stop;
            sig.signal_flips_trailing_year = flips_trailing_year;
            sig.spx_price = std::isnan(spx[i]) ? 0.0 : spx[i];

            signals.push_back(sig);
            last_flip_tilt = macro_tilt;
        }
            // ================================================================
            // DIAGNOSTIC SUMMARY - runs once after full backtest
            // ================================================================
            {
                int n_signals = static_cast<int>(signals.size());
                std::cout << "\n";
                std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
                std::cout << "║                  DIAGNOSTIC SUMMARY                         ║\n";
                std::cout << "╚══════════════════════════════════════════════════════════════╝\n";

                // ── 1. DATA INGESTION CHECK ──────────────────────────────────────
                std::cout << "\n── 1. DATA INGESTION (first valid prices) ──\n";
                for (const auto& sym : {"HG","GC","CL","SI","ZN","UB","6J","MES","MNQ"}) {
                    auto it = fut_.find(sym);
                    if (it == fut_.end() || it->second.empty()) {
                        std::cout << "  " << sym << ": NO DATA\n";
                    } else {
                        auto& bar = it->second.begin()->second;
                        std::cout << "  " << sym << ": first=" << std::fixed << std::setprecision(4)
                                  << bar.close
                                  << "  last=" << it->second.rbegin()->second.close
                                  << "  bars=" << it->second.size() << "\n";
                    }
                }
                std::cout << "  DXY records : " << dxy_ts_.size()
                          << "   VIX: " << vix_ts_.size()
                          << "   HY: " << hy_ts_.size()
                          << "   FedBS: " << fed_bs_ts_.size() << "\n";

                // ── 2. RATIO SANITY ─────────────────────────────────────────────
                std::cout << "\n── 2. CU/GOLD RATIO ──\n";
                double r_min = 1e9, r_max = -1e9, r_first = std::numeric_limits<double>::quiet_NaN();
                int r_valid = 0;
                for (int i = 0; i < n; ++i) {
                    if (std::isnan(ratio[i])) continue;
                    if (std::isnan(r_first)) r_first = ratio[i];
                    r_min = std::min(r_min, ratio[i]);
                    r_max = std::max(r_max, ratio[i]);
                    ++r_valid;
                }
                std::cout << "  Valid days: " << r_valid
                          << "  First value: " << std::setprecision(5) << r_first
                          << "  Range: [" << r_min << ", " << r_max << "]\n";
                std::cout << "  Expected range ~[0.2, 1.2]"
                          << (r_min > 0.1 && r_max < 2.0 ? "  ✓" : "  ✗ UNEXPECTED") << "\n";

                // ── 3. SIGNAL & REGIME DISTRIBUTION ────────────────────────────
                std::cout << "\n── 3. SIGNAL & REGIME DISTRIBUTION ──\n";
                int cnt_ron=0, cnt_roff=0, cnt_neut=0;
                int cnt_gpos=0, cnt_gneg=0, cnt_inf=0, cnt_liq=0, cnt_rneu=0;
                double liq_min=1e9, liq_max=-1e9, liq_sum=0; int liq_n=0;
                for (const auto& s : signals) {
                    if      (s.macro_tilt == MacroTilt::RISK_ON)  ++cnt_ron;
                    else if (s.macro_tilt == MacroTilt::RISK_OFF) ++cnt_roff;
                    else                                           ++cnt_neut;
                    switch (s.regime) {
                        case Regime::GROWTH_POSITIVE: ++cnt_gpos; break;
                        case Regime::GROWTH_NEGATIVE: ++cnt_gneg; break;
                        case Regime::INFLATION_SHOCK: ++cnt_inf;  break;
                        case Regime::LIQUIDITY_SHOCK: ++cnt_liq;  break;
                        default:                      ++cnt_rneu; break;
                    }
                    liq_min = std::min(liq_min, s.liquidity_score);
                    liq_max = std::max(liq_max, s.liquidity_score);
                    liq_sum += s.liquidity_score;
                    ++liq_n;
                }
                std::cout << "  Tilt   RISK_ON=" << cnt_ron << "  RISK_OFF=" << cnt_roff
                          << "  NEUTRAL=" << cnt_neut << "  (total=" << n_signals << ")\n";
                std::cout << "  Regime GROWTH+=" << cnt_gpos << "  GROWTH-=" << cnt_gneg
                          << "  INFL=" << cnt_inf << "  LIQ=" << cnt_liq << "  NEUT=" << cnt_rneu << "\n";
                if (liq_n > 0)
                    std::cout << "  Liquidity score  min=" << std::setprecision(3) << liq_min
                              << "  max=" << liq_max
                              << "  mean=" << (liq_sum/liq_n) << "\n";
                bool tilt_ok   = cnt_ron > 10 && cnt_roff > 10;
                bool liq_ok    = liq_min < -0.5 && liq_max > 0.5;
                std::cout << "  Tilt spread " << (tilt_ok ? "✓" : "✗ CHECK - one side dominates")
                          << "   Liquidity spread " << (liq_ok ? "✓" : "✗ CHECK - liquidity barely moves") << "\n";

                // ── 4. FLIP COUNTER ─────────────────────────────────────────────
                std::cout << "\n── 4. SIGNAL FLIPS ──\n";
                int total_flips = 0;
                MacroTilt prev_t2 = MacroTilt::NEUTRAL;
                for (const auto& s : signals) {
                    if (s.macro_tilt != prev_t2) { ++total_flips; prev_t2 = s.macro_tilt; }
                }
                double yrs = n_signals / 252.0;
                double fpy = (yrs > 0) ? total_flips / yrs : 0;
                std::cout << "  Total flips: " << total_flips
                          << "  Years: " << std::setprecision(1) << yrs
                          << "  Flips/year: " << std::setprecision(1) << fpy
                          << (fpy <= 12 ? "  ✓" : "  ✗ EXCEEDS 12/year") << "\n";

                // ── 5. POSITION ACTIVITY CHECK ──────────────────────────────────
                std::cout << "\n── 5. POSITION ACTIVITY ──\n";
                int days_with_positions = 0;
                double max_abs_gc = 0, max_abs_hg = 0;
                for (const auto& s : signals) {
                    bool any = false;
                    for (const auto& [sym, qty] : s.target_contracts) {
                        if (qty != 0) { any = true; }
                        if (sym == "GC") max_abs_gc = std::max(max_abs_gc, std::abs(qty));
                        if (sym == "HG") max_abs_hg = std::max(max_abs_hg, std::abs(qty));
                    }
                    if (any) ++days_with_positions;
                }
                double pct_invested = (n_signals > 0) ? 100.0 * days_with_positions / n_signals : 0;
                std::cout << "  Days with any position: " << days_with_positions
                          << " / " << n_signals
                          << " (" << std::setprecision(1) << pct_invested << "%)"
                          << (days_with_positions > 0 ? "  ✓" : "  ✗ NEVER TRADED") << "\n";
                std::cout << "  Max GC contracts ever held: " << max_abs_gc
                          << "   Max HG: " << max_abs_hg << "\n";

                // ── 6. EQUITY / RETURN CHECK ────────────────────────────────────
                std::cout << "\n── 6. EQUITY ──\n";
                double final_eq = signals.empty() ? p_.initial_capital : signals.back().portfolio_equity;
                double total_ret = (final_eq / p_.initial_capital) - 1.0;
                double ann_ret   = (yrs > 0) ? std::pow(1.0 + total_ret, 1.0 / yrs) - 1.0 : 0.0;
                std::cout << "  Start: $" << std::fixed << std::setprecision(0) << p_.initial_capital
                          << "   End: $" << final_eq
                          << "   Total: " << std::setprecision(1) << total_ret * 100 << "%"
                          << "   Ann: " << ann_ret * 100 << "%\n";
                bool eq_sane = (final_eq > p_.initial_capital * 0.01) && (final_eq < p_.initial_capital * 50.0);
                std::cout << "  Equity range " << (eq_sane ? "✓" : "✗ EXTREME - check P&L") << "\n";

                // ── 7. SPOT-CHECK: 5 EVENLY-SPACED SIGNAL DAYS ─────────────────
                std::cout << "\n── 7. SPOT-CHECK (5 evenly-spaced days) ──\n";
                std::cout << "  Date         Ratio   Comp   Tilt     Regime             Liq    SizeMult  Equity\n";
                std::cout << "  ----------  ------  ------  -------  -----------------  -----  --------  ----------\n";
                if (n_signals >= 5) {
                    for (int k = 0; k < 5; ++k) {
                        int idx = k * (n_signals - 1) / 4;
                        const auto& s = signals[idx];
                        char line[256];
                        snprintf(line, sizeof(line),
                            "  %-10s  %6.4f  %+6.3f  %-7s  %-17s  %+5.2f  %8.2f  %10.0f\n",
                            s.date.c_str(),
                            s.cu_gold_ratio,
                            s.composite,
                            tilt_str(s.macro_tilt),
                            regime_str(s.regime),
                            s.liquidity_score,
                            s.size_multiplier,
                            s.portfolio_equity);
                        std::cout << line;
                    }
                }

                std::cout << "\n── 8. INFLATION SHOCK TRIGGER COUNT ──\n";
                std::cout << "  INFLATION_SHOCK days: " << infl_shock_days
                          << " out of " << n_signals << " total"
                          << " (" << (100.0 * infl_shock_days / n_signals) << "%)\n";
                std::cout << "  If breakeven is in % units (e.g. 2.5 = 2.5%):\n";
                std::cout << "    threshold 0.10 = 10bp change over 20d (fires easily)\n";
                std::cout << "    threshold 1.00 = 100bp change over 20d (spec intent)\n";
                std::cout << "  Expected: ~2-5% of days. If much higher, threshold is wrong.\n";

                std::cout << "\n══════════════════════════════════════════════════════════════\n";
            }
            // ================================================================

        return signals;
    }

private:
    std::string data_dir_;
    StrategyParams p_;

    std::unordered_map<std::string, FuturesSeries> fut_;
    TimeSeries dxy_ts_, vix_ts_, hy_ts_, breakeven_ts_, treasury_ts_;
    TimeSeries tips_ts_;       // doc line 682: 10Y TIPS yield
    TimeSeries spx_ts_, fed_bs_ts_, china_cli_ts_;
    TimeSeries cny_usd_ts_;    // doc line 689: CNY/USD, China FX filter
};

// ============================================================
// Main
// ============================================================
int main(int argc, char* argv[]) {
    std::cout << "[INFO] Copper-Gold Strategy v2.0\n";

    std::string data_dir = "./data/raw";
    double initial_capital = 1000000.0;
    bool use_fixed = false;

    if (argc >= 2) data_dir = argv[1];
    if (argc >= 3) initial_capital = std::stod(argv[2]);
    if (argc >= 4) use_fixed = (std::string(argv[3]) == "fixed");

    std::cout << "[INFO] Data directory: " << data_dir << "\n";
    std::cout << "[INFO] Initial capital: $" << std::fixed << std::setprecision(2) << initial_capital << "\n";
    std::cout << "[INFO] Mode: " << (use_fixed ? "FIXED 1-CONTRACT" : "FULL POSITION SIZING") << "\n";

    StrategyParams params;
    params.initial_capital = initial_capital;
    params.use_fixed_positions = use_fixed;
    params.fixed_position_size = 1.0;

    CopperGoldStrategy strategy(data_dir, params);

    if (!strategy.load_data()) {
        std::cerr << "[ERROR] Failed to load data\n";
        return 1;
    }

    std::cout << "[INFO] Running signal generation...\n";
    auto signals = strategy.run();

    std::cout << "\n======= FINAL RESULTS =======\n";
    if (!signals.empty()) {
        const auto& last = signals.back();
        std::cout << "Final Date:   " << last.date << "\n";
        std::cout << "Final Equity: $" << std::fixed << std::setprecision(2) << last.portfolio_equity << "\n";
        std::cout << "Total Return: " << std::setprecision(2)
                  << ((last.portfolio_equity / initial_capital) - 1.0) * 100.0 << "%\n";
        std::cout << "Final Signal: " << tilt_str(last.macro_tilt) << "\n";
        std::cout << "Final Regime: " << regime_str(last.regime) << "\n";
        std::cout << "Signal Flips (trailing year): " << last.signal_flips_trailing_year << "\n";

        if (!use_fixed) {
            std::cout << "Final Margin Util: " << std::setprecision(1)
                      << (last.margin_utilization * 100.0) << "%\n";
            std::cout << "\nFinal Positions:\n";
            for (const auto& [sym, qty] : last.target_contracts) {
                if (qty != 0.0)
                    std::cout << "  " << sym << ": " << qty << " contracts\n";
            }
        }

        // ============================================================
        // Performance Acceptance Criteria (doc lines 591-608)
        // Sharpe >0.8, Sortino >1.0, MaxDD <20%, WinRate >45%,
        // ProfitFactor >1.3, AnnualTurnover <15x, Corr(SPX) <0.5
        // ============================================================
        std::cout << "\n======= PERFORMANCE METRICS =======\n";

        std::vector<double> daily_equity;
        daily_equity.push_back(initial_capital);
        for (const auto& sig : signals)
            daily_equity.push_back(sig.portfolio_equity);

        int N = (int)daily_equity.size() - 1;
        if (N < 2) { std::cout << "Insufficient data for metrics.\n"; return 0; }

        std::vector<double> daily_returns(N);
        for (int i = 0; i < N; ++i)
            daily_returns[i] = (daily_equity[i+1] - daily_equity[i]) / daily_equity[i];

        double total_days = N;
        double total_return = (daily_equity.back() / daily_equity.front()) - 1.0;
        double ann_return = std::pow(1.0 + total_return, 252.0 / total_days) - 1.0;

        double mean_ret = 0.0;
        for (double r : daily_returns) mean_ret += r;
        mean_ret /= N;

        double var = 0.0;
        for (double r : daily_returns) var += (r - mean_ret) * (r - mean_ret);
        var /= N;
        double ann_std = std::sqrt(var) * std::sqrt(252.0);

        double sharpe = (ann_std > 0.0) ? ann_return / ann_std : 0.0;

        double downside_var = 0.0;
        int downside_count = 0;
        for (double r : daily_returns) {
            if (r < 0.0) { downside_var += r * r; downside_count++; }
        }
        double downside_std = (downside_count > 0)
            ? std::sqrt(downside_var / downside_count) * std::sqrt(252.0) : 0.0;
        double sortino = (downside_std > 0.0) ? ann_return / downside_std : 0.0;

        double peak = initial_capital, max_dd = 0.0;
        for (double eq : daily_equity) {
            if (eq > peak) peak = eq;
            double dd = (peak - eq) / peak;
            if (dd > max_dd) max_dd = dd;
        }

        double gross_profit = 0.0, gross_loss = 0.0;
        int wins = 0;
        for (double r : daily_returns) {
            double pnl = r * daily_equity[0];
            if (pnl > 0.0) { gross_profit += pnl; wins++; }
            else if (pnl < 0.0) { gross_loss += std::abs(pnl); }
        }
        double win_rate = (N > 0) ? (double)wins / N : 0.0;
        double profit_factor = (gross_loss > 0.0) ? gross_profit / gross_loss : 0.0;

        double total_notional_traded = 0.0;
        for (int i = 1; i < (int)signals.size(); ++i) {
            for (const auto& [sym, qty] : signals[i].target_contracts) {
                double prev_qty = 0.0;
                auto it = signals[i-1].target_contracts.find(sym);
                if (it != signals[i-1].target_contracts.end()) prev_qty = it->second;
                total_notional_traded += std::abs(qty - prev_qty) * ContractSpec::get(sym).notional;
            }
        }
        double avg_equity = 0.0;
        for (const auto& sig : signals) avg_equity += sig.portfolio_equity;
        avg_equity /= (double)signals.size();
        double years = total_days / 252.0;
        double annual_turnover = (avg_equity > 0.0 && years > 0.0)
            ? (total_notional_traded / years) / avg_equity : 0.0;

        int all_flips = 0;
        MacroTilt prev_t = MacroTilt::NEUTRAL;
        for (const auto& sig : signals) {
            if (sig.macro_tilt != prev_t) { all_flips++; prev_t = sig.macro_tilt; }
        }
        double flips_per_year = (years > 0.0) ? all_flips / years : 0.0;

        // Correlation to SPX — doc line 603: minimum < 0.5, target < 0.3
        double corr_spx = 0.0;
        {
            std::vector<double> spx_rets, strat_rets;
            for (int i = 1; i < (int)signals.size(); ++i) {
                if (signals[i].spx_price > 0.0 && signals[i-1].spx_price > 0.0) {
                    spx_rets.push_back((signals[i].spx_price / signals[i-1].spx_price) - 1.0);
                    strat_rets.push_back((signals[i].portfolio_equity / signals[i-1].portfolio_equity) - 1.0);
                }
            }
            int M = (int)spx_rets.size();
            if (M > 2) {
                double ms = 0.0, mp = 0.0;
                for (int j = 0; j < M; ++j) { ms += strat_rets[j]; mp += spx_rets[j]; }
                ms /= M; mp /= M;
                double cov = 0.0, vs = 0.0, vp = 0.0;
                for (int j = 0; j < M; ++j) {
                    double ds = strat_rets[j] - ms, dp = spx_rets[j] - mp;
                    cov += ds * dp; vs += ds * ds; vp += dp * dp;
                }
                double denom = std::sqrt(vs * vp);
                if (denom > 0.0) corr_spx = cov / denom;
            }
        }

        std::cout << std::fixed << std::setprecision(4);
        std::cout << "Annualized Return:    " << ann_return * 100.0 << "%\n";
        std::cout << "Annualized Volatility:" << ann_std * 100.0 << "%\n";
        std::cout << "Sharpe Ratio:         " << sharpe
                  << (sharpe >= 0.8 ? " ✓ (>=0.8)" : " ✗ (<0.8 threshold)") << "\n";
        std::cout << "Sortino Ratio:        " << sortino
                  << (sortino >= 1.0 ? " ✓ (>=1.0)" : " ✗ (<1.0 threshold)") << "\n";
        std::cout << "Max Drawdown:         " << max_dd * 100.0 << "%"
                  << (max_dd < 0.20 ? " ✓ (<20%)" : " ✗ (>=20% threshold)") << "\n";
        std::cout << "Win Rate:             " << win_rate * 100.0 << "%"
                  << (win_rate >= 0.45 ? " ✓ (>=45%)" : " ✗ (<45% threshold)") << "\n";
        std::cout << "Profit Factor:        " << profit_factor
                  << (profit_factor >= 1.3 ? " ✓ (>=1.3)" : " ✗ (<1.3 threshold)") << "\n";
        std::cout << "Annual Turnover:      " << annual_turnover << "x"
                  << (annual_turnover < 15.0 ? " ✓ (<15x)" : " ✗ (>=15x threshold)") << "\n";
        std::cout << "Correlation to SPX:   " << corr_spx
                  << (std::abs(corr_spx) < 0.5 ? " ✓ (<0.5)" : " ✗ (>=0.5 threshold)") << "\n";
        std::cout << "Signal Flips/Year:    " << std::setprecision(1) << flips_per_year
                  << (flips_per_year <= 12.0 ? " ✓ (<=12)" : " ✗ (>12 — overfit warning)") << "\n";

        std::cout << "\n======= KILL CRITERIA =======\n";
        if (flips_per_year > 20.0)
            std::cout << "❌ KILL CRITERION TRIGGERED: Signal flips " << flips_per_year
                      << "/year (>20 threshold). Stop development.\n";
        else
            std::cout << "✓ Kill criterion passed: " << flips_per_year
                      << " flips/year (<=20 threshold)\n";
    }

    return 0;
}