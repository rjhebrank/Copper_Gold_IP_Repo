# Cu/Au Macro Strategy -- V5 Metrics Summary

**Backtest**: 2010-06-07 to 2025-11-12 (15.9 yrs) | **Capital**: $1M | **Signal**: Cu/Au ratio + HY confirmation

---

## Version Progression

| Metric | V2 | V3 | V4 | V5 |
|---|---|---|---|---|
| **Sharpe** | 0.34 | 0.19 | 0.40 | **0.50** |
| **CAGR** | 3.62% | 1.45% | 4.00% | **4.34%** |
| **Max DD** | 25.63% | 18.42% | 16.39% | **14.12%** |
| **Profit Factor** | 1.075 | 1.045 | 1.083 | **1.103** |
| **Turnover** | 46.7x | 23.2x | 32.1x | **28.1x** |
| **Net P&L** | +$762K* | +$258K | +$868K | **+$965K** |
| **Win Rate** | 45.7% | 43.5% | 47.2% | **47.0%** |
| **t-stat** | 1.36 | 0.77 | 1.61 | **1.98** |

*V2 P&L estimated (pre-cost modeling)

---

## V5 Kill Gate Status

| Criterion | Target | V5 Actual | Status |
|---|---|---|---|
| **Sharpe Ratio** | >= 0.50 | 0.4979 | **FAIL** (by 0.002) |
| **Profit Factor** | >= 1.15 | 1.1032 | **FAIL** (by 0.047) |
| **Turnover** | < 25x | 28.13x | **FAIL** (by 3.1x) |

All three miss. Sharpe is within rounding error; PF and turnover gaps are material.

---

## Per-Instrument P&L

| Instrument | Net P&L | Win% | Trades | Role |
|---|---|---|---|---|
| **UB** (Ultra Bond) | +$681,484 | 51.9% | 108 | Alpha engine (70.6% of total) |
| **ZN** (10Y Note) | +$173,282 | 53.2% | 109 | Rates secondary |
| **GC** (Gold) | +$105,880 | 57.5% | 106 | Steady contributor |
| **MNQ** (Micro NQ) | +$49,388 | 21.7% | 46 | Tail capture |
| **SI** (Silver) | +$32,795 | 44.6% | 65 | New in V5 (was -$360 in V4) |
| **CL** (Crude) | -$77,705 | 50.5% | 103 | Net loser, but improves portfolio Sharpe |
| **TOTAL** | **+$965,123** | **47.0%** | **537** | |

UB+ZN = 88.6% of net P&L. Transaction costs: $193K (16.7% of gross).

---

## A/B Test Verdicts (V5 cycle)

- **Drop CL**: Sharpe 0.40 -> 0.39 -- KEEP CL (diversification value despite losses)
- **Wider rebalance bands**: Sharpe 0.40 -> 0.41 -- **ADOPTED** (reduced costs)
- **VIX filter ON**: Sharpe 0.40 -> 0.35 -- REVERTED (removed alpha in vol regimes)
- **HY spread confirmation ON**: Sharpe 0.40 -> 0.46 -- **ADOPTED** (best single improvement)
- **Signal-strength sizing**: Sharpe 0.40 -> 0.25 -- REVERTED (catastrophic; signal is binary)

Hit rate: 2 of 5 proposed changes helped. 3 of 5 hurt.

---

## Bottom Line

V5 is the best version across every risk-adjusted metric (Sharpe 0.34 -> 0.50 from V2, max DD cut from 25.6% to 14.1%) and the first to reach statistical significance (t = 1.98). However, all three kill gate criteria miss their thresholds, and 71% of P&L comes from a single instrument. The signal is approaching its theoretical ceiling of ~0.55-0.65 Sharpe; reaching 0.80+ allocable grade would require a third independent signal factor or fundamentally different architecture.
