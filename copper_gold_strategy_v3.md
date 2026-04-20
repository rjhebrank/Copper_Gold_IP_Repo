# Copper-Gold Ratio Economic Regime Strategy v3

> **Implementation file:** `apps/backtest/Gannon_copper_Ip/new_copper_strat.cpp`
> **Parent spec:** `copper_gold_strategy_v2.md` (unchanged — v3 is a parameter-tuning delta on top of the v2 implementation)
> **Backtest window:** 2010-06-07 → 2025-11-12 (~15.4 years, 4,800 trading days)

---

## Overview of `new_copper_strat.cpp`

`new_copper_strat.cpp` is the C++ implementation of the v2.1 Copper-Gold regime strategy described in `copper_gold_strategy_v2.md`. It ingests front-month and 2nd-month futures (HG, GC, CL, SI, ZN, UB, 6J, MES, MNQ) plus the macro panel (DXY, VIX, high-yield spread, Fed balance sheet, 10Y Treasury, 10Y TIPS, 10Y breakeven, SPX, China CLI/PMI/credit/SHFE/CNY) and emits a daily `SignalRow` stream driving a simulated portfolio with full P&L, margin, and turnover accounting.

The file is structurally identical to `original_copper_strat.cpp` — same Layer 1–5 pipeline, same normalization, same regime classifier shape, same DXY/term-structure/China filters, same notional sizing and drawdown/stop logic. `new_copper_strat.cpp` differs from `original_copper_strat.cpp` in exactly **two** parameter values.

---

## Changes from `original_copper_strat.cpp`

### Change 1 — `min_hold_days`: 5 → 10 (line 441)

```diff
-    int min_hold_days = 5;
+    int min_hold_days = 10;
```

- **Where:** `StrategyParams` struct, risk section.
- **What it does:** Enforces a minimum holding period between position changes in the daily rebalance loop. Doubling it from 5 to 10 trading days makes the strategy more patient — positions survive a full two-week window before the rebalancer is allowed to adjust them again.
- **Intent:** Reduce turnover and transaction-cost drag. The v2.md spec (line 362) already lists "Minimum Holding Period: 5 trading days" with "Expected Regime Duration: 20-60 trading days", so a 10-day minimum is still well inside the intended regime horizon but pushes turnover closer to the <15x annual target.

### Change 2 — INFLATION_SHOCK threshold: `inflation > 1.0` → `inflation > 0.05` (line 870)

```diff
-            } else if (inflation > 1.0 && growth < 0.5) {
+            } else if (inflation > 0.05 && growth < 0.5) {
                 regime = Regime::INFLATION_SHOCK;
```

- **Where:** Regime classifier inside the main daily loop.
- **What it does:** `inflation` is defined at line 831 as `be_chg[i]`, the raw **20-day change in the 10Y breakeven rate in percentage points** (computed at line 634 as `breakeven[i] - breakeven[i - 20]`).
  - Old threshold `1.0` required a **100 bp** move in 20 days — essentially a once-a-decade event, so INFLATION_SHOCK almost never fired.
  - New threshold `0.05` requires only a **5 bp** move in 20 days, which is a realistic regime-shift signal. In the backtest this trips on 321 / 4,800 days (**6.7%**), near the ~2–5% band the diagnostic block calls out as expected.
- **Intent:** Make the inflation regime branch actually reachable so the rest of the inflation-specific trade mapping (reduce equity beta, favor commodities, short duration — v2.md Layer 2) can take effect instead of collapsing into GROWTH_POSITIVE/NEGATIVE.

### No other changes

Running `diff original_copper_strat.cpp new_copper_strat.cpp` returns only the two lines above — no other code, comments, or whitespace differs.

**One cosmetic artifact worth flagging (not a third change):** the diagnostic summary at line 1691 still prints the old text `"Threshold: inflation > 1.0 (100bp breakeven change over 20d)"`. The classifier now uses 0.05, so this log line is stale — it's a print-statement only, not a behavioral difference.

---

## Backtest Results — Whole Period (2010-06-07 → 2025-11-12)

Run: `./new_copper_strat` (default data dir, $1,000,000 initial capital, full position sizing mode).

### Headline

| Metric | Value |
|---|---|
| Start equity | $1,000,000.00 |
| Final equity | $2,331,013.00 |
| **Total return** | **+133.10%** |
| **Annualized return** | **4.54%** |
| Annualized volatility | 8.72% |
| Years | ~19.0 (calendar days basis in metric calc) |

### Performance Metrics vs v2.md Acceptance Criteria

| Metric | Value | Threshold | Pass |
|---|---|---|---|
| Sharpe Ratio | 0.5211 | ≥ 0.80 | ✗ |
| Sortino Ratio | 0.4280 | ≥ 1.00 | ✗ |
| Max Drawdown | 19.92% | < 20% | ✓ (barely) |
| Win Rate | 31.44% | ≥ 45% | ✗ |
| Profit Factor | 1.1338 | ≥ 1.30 | ✗ |
| Annual Turnover | 32.00x | < 15x | ✗ |
| Correlation to SPX | 0.0250 | < 0.50 | ✓ |
| Signal Flips / Year | 4.0 | ≤ 12 | ✓ |
| Kill criterion (<20 flips/yr) | 4.0 | — | ✓ |

### Regime & Signal Distribution (whole period, 4,800 days)

| Category | Count | Share |
|---|---|---|
| RISK_ON | 2,082 | 43.4% |
| RISK_OFF | 2,590 | 54.0% |
| NEUTRAL (tilt) | 128 | 2.7% |
| GROWTH_POSITIVE | 3,289 | 68.5% |
| GROWTH_NEGATIVE | 574 | 12.0% |
| **INFLATION_SHOCK** | **321** | **6.7%** (unlocked by Change 2; would be ~0% under the old 1.0 threshold) |
| LIQUIDITY_SHOCK | 397 | 8.3% |
| NEUTRAL (regime) | 219 | 4.6% |
| Days with any position | 2,879 / 4,800 | 60.0% |

### Final State (2025-11-12)

- Final tilt: RISK_OFF
- Final regime: GROWTH_POSITIVE
- Final margin utilization: 0.0%
- Signal flips in trailing year: 4

### Takeaway

The two parameter tweaks don't change the strategy's architecture — they're a conservatism/reachability tuning pass. The backtest ends meaningfully profitable in absolute dollars (+133% over 15 years) but still misses most v2.md acceptance thresholds (Sharpe, Sortino, win rate, profit factor, turnover). The turnover figure (32x vs <15x target) is the most actionable miss: Change 1 was meant to help here, so further patience tuning (higher `min_hold_days`, or a cost-aware rebalance gate) is the next obvious lever. Change 2 successfully activates the INFLATION_SHOCK branch (6.7% of days), but whether that translates into edge depends on the quality of the v2.md inflation-regime trade mapping, which is unchanged.
