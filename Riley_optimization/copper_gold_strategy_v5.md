# Copper-Gold Ratio Economic Regime Strategy -- V5

> **Last Updated:** 2026-02-24 | **Backtest Period:** 2010-06-07 to 2025-11-12 (15.9 years, 4,011 trading days)

> **TL;DR:** The strategy uses the copper-to-gold ratio as a macro regime indicator to trade futures across rates, commodities, and equity indices. When copper outperforms gold, the signal is risk-on (short bonds, long equities/commodities); when gold outperforms copper, the signal is risk-off (long bonds, long gold, short commodities). V5 adds an HY credit spread confirmation filter and wider rebalance bands for UB/ZN, producing a Sharpe of 0.50, max drawdown of 14.1%, and $965K net P&L on $1M initial capital. The strategy just barely reaches statistical significance (t = 1.98) but remains below institutional allocation thresholds.

---

## 1. Executive Summary

This strategy extracts a macro regime signal from the copper-to-gold futures price ratio (HG/GC). Copper is an industrial bellwether sensitive to global growth expectations; gold is a safe-haven asset sensitive to real rates and risk aversion. Their ratio acts as a thermometer for aggregate risk appetite. When the ratio rises, the world is pricing in growth -- the strategy goes long equities and commodities, short bonds. When it falls, the world is pricing in contraction -- the strategy goes long bonds and gold, short commodities.

The raw signal is filtered through a five-layer architecture: a macro regime classifier (growth/inflation/liquidity), a DXY/USD filter, a per-commodity term structure filter, a China demand filter, and -- new in V5 -- a high-yield credit spread confirmation filter that halves position size when credit markets disagree with the Cu/Au signal. The strategy trades six active instruments (UB, ZN, GC, CL, SI, MNQ) and rebalances weekly or on signal changes.

V5 is the product of four development iterations and two rounds of systematic A/B testing. It turns $1M into $1.97M over 15.9 years (4.34% CAGR, Sharpe 0.50) with a maximum drawdown of 14.1%. The alpha is concentrated in rates -- Ultra Bond (UB) and 10-Year Note (ZN) account for 88.6% of net P&L. The strategy has negative correlation to the S&P 500 (-0.09), providing genuine portfolio diversification value, but remains below the Sharpe 0.80 threshold required for standalone institutional allocation.

---

## 2. What Changed from V2 to V5

### V2 Baseline (Sharpe 0.34)

V2 was the first clean backtest after six infrastructure bug fixes resolved the catastrophic data corruption in V1 (which showed -$9.3B P&L and 4,762% drawdown). V2 established the baseline: a Cu/Au ratio signal trading nine instruments, producing a Sharpe of 0.34 with 25.6% max drawdown and $762K estimated net P&L. No transaction cost model. Drawdown stop triggering on 61% of rebalances (poorly calibrated). GC and SI positions silently broken due to an implementation bug.

### V2 to V3: The Kitchen Sink (Sharpe 0.34 to 0.19)

V3 bundled five changes into a single release:

| Change | What It Did | V3 Result |
|---|---|---|
| Cu/Au detrending | Applied rolling-window detrending to remove secular drift in the ratio | Tripled NEUTRAL days (229 to 740), removing directional signal 18% of the time |
| Real rate signal overlay | Blended a real rate z-score into the composite signal | Increased signal flips from 6/yr to 8/yr, injecting noise |
| Volatility filter | Scaled position size down during high-vol regimes | Prevented oversizing into COVID and 2022 rate shock |
| Return smoothing | Applied exponential smoothing to daily returns before signal calculation | Introduced 2-3 days of lag per signal flip |
| Tighter rebalance bands | Narrowed bands from default to 2 contracts / 20% relative | Reduced turnover from 46.7x to 23.2x |

The net result was a 43% Sharpe decline. Risk metrics improved (max DD fell from 25.6% to 18.4%), but returns were destroyed (CAGR fell from 3.62% to 1.45%). The core lesson: **bundling changes makes it impossible to identify which ones help and which ones hurt.**

### V3 to V4: A/B Testing Saves the Strategy (Sharpe 0.19 to 0.40)

V4 isolated each V3 change and tested it individually against the V2 baseline. Results:

| Change | Isolated Sharpe Impact | Decision |
|---|---|---|
| **Cu/Au detrending** | -84% | **Reverted** -- removed genuine level signal along with drift |
| **Real rate signal** | -28% | **Reverted** -- low independent predictive power, added noise |
| **Return smoothing** | -34% | **Reverted** -- introduced lag that compounded to 12-18 days/year of tracking error |
| **Volatility filter** | Positive | **Kept** -- prevented oversizing into 2020 and 2022 drawdowns |
| **Rebalance bands** | +35% | **Kept and widened** to 3 contracts / 40% relative |

Additionally, V4 fixed the GC/SI position sizing bug that had silenced both instruments since V1. Gold (GC) immediately became a $106K net contributor with the highest win rate in the portfolio (59%).

V4 result: Sharpe 0.40, max DD 16.4%, $868K net P&L. Drawdown stop triggered once in 882 rebalances (down from 1,030 times in V2).

### V4 to V5: Disciplined Addition (Sharpe 0.40 to 0.50)

Five candidate improvements were A/B tested in isolation against the V4 baseline:

| # | Proposal | Isolated Sharpe Impact | Decision |
|---|---|---|---|
| 1 | Drop CL (net loser, -$68K) | -4.2% (0.4045 to 0.3876) | **Rejected** -- CL provides portfolio diversification despite standalone losses |
| 2 | Wider rebalance bands for UB/ZN | +2.4% (0.4045 to 0.4141) | **Adopted** -- reduced bond turnover and costs |
| 3 | VIX filter (reduce size when VIX > 20/25) | -12.6% (0.4045 to 0.3537) | **Rejected** -- removed trades during the strategy's most profitable periods |
| 4 | HY spread confirmation filter | +12.7% (0.4045 to 0.4557) | **Adopted** -- largest single improvement in V2-V5 arc |
| 5 | Signal-strength position sizing (1.5x for strong signals) | -38.7% (0.4045 to 0.2479) | **Rejected** -- the Cu/Au signal is binary, not graded; amplifying "strong" signals amplified losses |

Only the two winners were combined into V5. The combined effect (Sharpe 0.4979) exceeded either change in isolation, confirming they are complementary rather than redundant.

**Hit rate for proposed improvements: 2 of 5 (40%).** This is consistent with the V3 lesson (where 2 of 5 bundled changes were beneficial) and demonstrates that strategies near their information-theoretic limit are fragile to perturbation. Incremental beats ambitious.

---

## 3. Signal Architecture

The signal flows through five layers, each acting as either a signal generator, a confirmation filter, or a size adjuster.

```
┌─────────────────────────────────────────────────────────┐
│              LAYER 1: Cu/Au Ratio Signal                │
│     ROC(20) + MA(10/50) crossover + Z-score(120)       │
│         -> Composite -> RISK_ON / RISK_OFF              │
└──────────────────────────┬──────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│            LAYER 2: Composite Signal Filtering          │
│  Regime classifier (growth/inflation/liquidity/neutral) │
│  + DXY momentum filter + China demand filter            │
│         -> Size multiplier adjustments                  │
└──────────────────────────┬──────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│       LAYER 3: HY Spread Confirmation (NEW V5)         │
│  If 20d HY spread change disagrees with Cu/Au tilt:    │
│         -> Halve position size (0.50x)                  │
└──────────────────────────┬──────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│         LAYER 4: Cu/Au Vol Filter + Term Structure      │
│  Ratio vol(63d) / vol(252d) -> dynamic size scaling     │
│  Per-commodity curve state -> trade expression matrix    │
└──────────────────────────┬──────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────┐
│              LAYER 5: Risk Management                   │
│  DD warn (15%, hysteresis at 12%) / DD stop (20%)       │
│  Rebalance bands / Position limits / Margin cap         │
└─────────────────────────────────────────────────────────┘
```

### Layer 1: Cu/Au Ratio -- Raw Regime Signal

The copper-to-gold ratio is computed using notional normalization:

```
Cu_notional = HG_price * 25,000   (dollars per contract)
Au_notional = GC_price * 100      (dollars per contract)
Ratio = Cu_notional / Au_notional
```

Three sub-signals are generated from the ratio:

| Signal | Calculation | Output |
|---|---|---|
| Rate of Change | `ROC_20 = (ratio[t] / ratio[t-20]) - 1` | sign(ROC_20): +1 or -1 |
| MA Crossover | `SMA(10) vs SMA(50)` | +1 if fast > slow, else -1 |
| Z-Score Regime | `(ratio - SMA(120)) / STD(120)`, threshold +/-0.5 | +1, 0, or -1 |

Composite signal: `0.33 * sign(ROC_20) + 0.33 * MA_signal + 0.34 * Z_signal`

- Composite > 0.0: **RISK_ON**
- Composite < 0.0: **RISK_OFF**
- Equal to 0.0: **NEUTRAL** (flat all positions)

A 5-day minimum holding period prevents whipsawing: a new tilt must persist for 5 consecutive days before the signal flips. The signal flips approximately 6 times per year (96 total flips over 15.9 years).

**Signal distribution (V5 backtest):** RISK_ON 1,712 days (42.7%), RISK_OFF 2,070 days (51.6%), NEUTRAL 229 days (5.7%).

### Layer 2: Regime Classifier + DXY + Liquidity

The regime classifier uses three macro proxies to categorize the current environment:

| Proxy | Source | Calculation |
|---|---|---|
| Growth | SPX 60-day momentum | `(SPX[t] / SPX[t-60] - 1) * 100` |
| Inflation | 10Y breakeven 20-day change | `breakeven[t] - breakeven[t-20]` (threshold: 0.15 = 15bp) |
| Liquidity | Composite of VIX percentile, HY spread z-score, Fed balance sheet YoY | `(-VIX_pctl_scaled + -HY_z60 + FedBS_yoy_scaled) / 3` (threshold: -1.5) |

Classification logic:

```
if liquidity < -1.5:        LIQUIDITY_SHOCK  (size_mult = 0.0, flatten)
elif inflation > 0.15 and growth < 0.5:  INFLATION_SHOCK  (size_mult = 0.5)
elif growth > 0.5:           GROWTH_POSITIVE  (full size)
elif growth < -0.5:          GROWTH_NEGATIVE  (0.5x if tilt is RISK_ON)
else:                        NEUTRAL          (size_mult = 0.5)
```

**Regime distribution (V5):** GROWTH+ 2,853 days (71.1%), GROWTH- 648 days (16.2%), INFLATION 100 days (2.5%), LIQUIDITY 199 days (5.0%), NEUTRAL 211 days (5.3%).

The DXY filter checks 20-day USD momentum against a +/-3% threshold. When DXY momentum and the Cu/Au signal disagree (e.g., risk-on tilt + strong dollar), the signal is marked SUSPECT and size is halved. When they agree, the signal is CONFIRMED at full size.

The China demand filter uses the OECD CLI for China (deviation from 65-day average) with a threshold of -2.0. When China demand weakens significantly, the size multiplier is reduced by 30% (china_adj = 0.7).

### Layer 3: HY Spread Confirmation Filter (New in V5)

This is the single most impactful improvement in the V2-V5 development arc (+12.7% Sharpe in isolation).

The filter compares the 20-day change in the high-yield credit spread with the Cu/Au macro tilt:

```
hy_change_20d = HY_spread[t] - HY_spread[t-20]

if RISK_ON  and hy_change_20d > 0:  disagree (spreads widening contradicts risk-on)
if RISK_OFF and hy_change_20d < 0:  disagree (spreads tightening contradicts risk-off)

if disagree: size_mult *= 0.50
```

The mechanism: HY spreads widen during genuine risk-off episodes and narrow during genuine risk-on. Using HY as a second opinion on the Cu/Au signal filters out false signals that would have resulted in losses. Unlike the VIX filter (which gates trades indiscriminately and hurt Sharpe by 12.6%), HY confirmation improves trade selection by adjusting size rather than blocking entry.

### Layer 4: Cu/Au Volatility Filter and Term Structure

**Vol filter:** Compares 63-day (quarterly) realized volatility of the Cu/Au ratio returns against 252-day (annual) baseline volatility.

```
vol_ratio = rvol_63 / rvol_252

vol_ratio > 1.50:  vol_mult = 0.50  (vol expanding, cut size in half)
vol_ratio < 0.75:  vol_mult = 1.00  (vol compressed, full size)
0.75 to 1.50:      linear interpolation between 1.00 and 0.50
```

**Term structure filter:** Per-commodity roll yield is computed from front-month vs. 2nd-month futures. Annualized roll yield above +2% = backwardation; below -2% = contango. Each commodity has a trade expression matrix that adjusts or blocks positions based on curve shape (e.g., never short CL in backwardation, always full size GC regardless of curve). UB/ZN use a yield curve proxy (ZN vs ZB_2nd) for steepening/flattening detection.

### Layer 5: Drawdown Management and Rebalance Bands

**Drawdown warning:** Engages at 15% drawdown from equity peak. Halves all position sizes. Uses hysteresis -- warning clears only when drawdown recovers below 12% (not the same 15% threshold), preventing rapid on/off cycling.

**Drawdown stop:** Triggers at 20% drawdown. Immediately liquidates all positions and enters a 20-bar cooldown. Re-entry requires 10 consecutive non-declining equity bars after cooldown expires. Peak equity resets to current equity on resume. In V5, the drawdown stop triggered zero times in 881 rebalances -- the best result across all versions.

**Rebalance bands (widened in V5):**

| Instrument | Absolute Band | Relative Band |
|---|---|---|
| UB, ZN | 4 contracts | 50% |
| All others | 3 contracts | 40% |

Same-direction position resizing is suppressed unless the change exceeds both the absolute and relative thresholds. Entry from flat, exit to flat, and direction flips always execute regardless of bands. The wider UB/ZN bands specifically target bond turnover, which accounts for 83% of total transaction costs.

---

## 4. Trading Universe

### Active Instruments

| Instrument | Role | Direction (RISK_ON) | Direction (RISK_OFF) | V5 Net P&L | % of Total |
|---|---|---|---|---|---|
| **UB** (Ultra T-Bond) | Duration / rate positioning | Short | Long | +$681,484 | 70.6% |
| **ZN** (10Y T-Note) | Duration / rate positioning | Short | Long | +$173,282 | 18.0% |
| **GC** (Gold) | Safe haven / precious metal | Short (unless crisis) | Long | +$105,880 | 11.0% |
| **MNQ** (Micro Nasdaq) | Equity beta (risk-on only) | Long | Flat (no short) | +$49,388 | 5.1% |
| **SI** (Silver) | Hybrid industrial/precious | Long | Long (precious bid) | +$32,795 | 3.4% |
| **CL** (Crude Oil) | Commodity beta | Long | Short | -$77,705 | -8.1% |

### Dropped Instruments

| Instrument | Reason | V3/V4 Evidence |
|---|---|---|
| **6J** (Japanese Yen) | Pure cost drag. V3: $0.04 gross, -$14.4K net on 85 trades. Zero trades in V4/V5. | Commission + spread consumed all edge |
| **HG** (Copper) | Pure cost drag. Position sizes consistently round to zero (small contract notional vs. portfolio cap). HG price data still loaded for Cu/Au ratio calculation. | Never traded in any version |
| **MES** (Micro S&P 500) | Replaced by MNQ. Net P&L approximately zero after commissions. | MNQ captures equity leg more efficiently |

### Why CL Stays Despite Being a Net Loser

CL lost $77,705 in V5. The V5 A/B test on dropping CL showed that removing it actually reduces portfolio Sharpe from 0.4045 to 0.3876 (-4.2%). CL's drawdowns are partially anti-correlated with UB/ZN drawdowns, providing portfolio-level diversification that outweighs its standalone losses. Per-instrument P&L does not tell the whole story.

### MNQ: Long-Only in Risk-On

MNQ only trades long during RISK_ON periods. Short MNQ in RISK_OFF was removed after analysis showed it was a persistent loser since the 2019 contract launch -- shorting Nasdaq during a structural tech bull market generated consistent losses. MNQ now acts as a tail-capture instrument: 21.7% win rate but profitable overall through asymmetric payoffs ($12.9K avg win vs $11.2K avg loss).

---

## 5. Risk Management

### Position Size Cascade

The size multiplier starts at 1.0 and is reduced through a cascade of filters:

```
size_mult = 1.0
  * regime_adjustment      (0.0 for liquidity shock, 0.5 for inflation/neutral/conflicting growth)
  * dxy_filter             (0.5 if SUSPECT)
  * hy_confirmation        (0.5 if Cu/Au disagrees with credit direction)  [NEW V5]
  * correlation_spike      (0.5 if avg pairwise corr > 0.70)
  * china_adjustment       (0.7 if China CLI deviation < -2.0)
  * vol_filter             (0.5 to 1.0, linear in vol_ratio)
  * drawdown_warning       (0.5 if DD > 15%)
  * drawdown_stop          (0.0 if DD > 20%)
  * equity_safety          (0.5 if equity < 10% of initial; 0.0 if equity <= 0)
```

### Position Limits

| Limit | Threshold |
|---|---|
| Single equity instrument notional | 20% of equity |
| Single commodity instrument notional | 15% of equity |
| Total equity notional | 35% of equity |
| Total commodity notional | 40% of equity |
| Max margin utilization | 50% of equity |
| Position-level stop loss | 2x ATR(20) from entry |

### Rebalance Triggers

Positions are recalculated on:
1. Weekly (Friday)
2. Signal flip (macro tilt changes after 5-day confirmation)
3. Regime change (confirmed after 3 consecutive days)
4. DXY filter transition (NEUTRAL to SUSPECT)
5. Drawdown stop or warning trigger
6. Force-rebalance if all positions are zero but size_mult > 0 (recover from flat)

### Transaction Cost Model

Round-trip cost per contract = commission + spread (1x tick_value) + slippage (2x tick_value):

| Contract | Commission (RT) | Spread Cost | Slippage Cost | Total RT Cost |
|---|---|---|---|---|
| UB | $4.70 | $31.25 | $62.50 | $98.45 |
| ZN | $4.50 | $7.81 | $15.63 | $27.94 |
| GC | $5.14 | $10.00 | $10.00 | $25.14 |
| CL | $4.90 | $10.00 | $10.00 | $24.90 |
| SI | $5.14 | $25.00 | $50.00 | $80.14 |
| MNQ | $1.30 | $0.50 | $1.00 | $2.80 |

Commissions use IBKR rates ($0.85/side + exchange + clearing + NFA). Total V5 transaction costs: $192,887 (16.7% of $1.16M gross P&L).

---

## 6. Performance Summary

### Version Comparison

| Metric | V2 | V3 | V4 | V5 |
|---|---|---|---|---|
| **Sharpe Ratio** | 0.34 | 0.19 | 0.40 | **0.50** |
| **CAGR** | 3.62% | 1.45% | 4.00% | **4.34%** |
| **Max Drawdown** | 25.63% | 18.42% | 16.39% | **14.12%** |
| **Net P&L** | +$762K* | +$258K | +$868K | **+$965K** |
| **Win Rate** | 45.65% | 43.48% | 47.22% | **47.02%** |
| **Profit Factor** | 1.075 | 1.045 | 1.083 | **1.103** |
| **Annual Turnover** | 46.66x | 23.24x | 32.07x | **28.13x** |
| **SPX Correlation** | -0.166 | -0.053 | -0.083 | **-0.093** |
| **Transaction Costs** | N/A | $143K | $230K | **$193K** |
| **DD Stop Triggers** | 1,030 (61%) | 3 | 1 (0.1%) | **0 (0.0%)** |
| **t-statistic** | 1.36 | 0.77 | 1.61 | **1.98** |

*V2 P&L estimated (pre-cost modeling).

### Kill Criteria Status

| Criterion | Allocable Target | V5 Kill Gate | V5 Result | Status |
|---|---|---|---|---|
| Sharpe Ratio | >= 0.80 | >= 0.50 | 0.4979 | **FAIL** (by 0.002) |
| Sortino Ratio | >= 1.00 | -- | 0.4976 | FAIL |
| Max Drawdown | < 20% | -- | 14.12% | **PASS** |
| Win Rate | >= 45% | -- | 47.02% | **PASS** |
| Profit Factor | >= 1.30 | >= 1.15 | 1.1032 | **FAIL** (by 0.047) |
| Annual Turnover | < 15x | < 25x | 28.13x | **FAIL** (by 3.1x) |
| SPX Correlation | < 0.50 | -- | -0.093 | **PASS** |
| Signal Flips/Year | <= 12 | -- | 6.0 | **PASS** |

V5 passes 4 of 8 allocable criteria (same as V4). All four failing metrics are closer to their thresholds than in any prior version. The Sharpe miss is within estimation uncertainty for a 15.9-year sample. The profit factor and turnover gaps are more material.

### Per-Instrument P&L

```
Instr     Gross P&L Trades   Win%    Avg Win   Avg Loss      Costs      Net P&L
--------------------------------------------------------------------------------
UB        798,344    108    51.9%   28,217    15,034    116,860    681,484
ZN        211,500    109    53.2%    8,403     5,409     38,219    173,282
GC        113,070    106    57.5%    6,175     5,991      7,190    105,880
MNQ        50,587     46    21.7%   12,924    11,237      1,198     49,388
SI         47,300     65    44.6%   12,163     8,484     14,505     32,795
CL        -62,790    103    50.5%   10,415    11,850     14,915    -77,705
HG              0      0     0.0%        0         0          0          0
6J              0      0     0.0%        0         0          0          0
MES             0      0     0.0%        0         0          0          0
--------------------------------------------------------------------------------
TOTAL   1,158,010    537    49.5%   13,037     9,584    192,887    965,123
```

**Concentration:** UB + ZN = $854,766 (88.6% of net P&L). With GC and MNQ, top-4 = $1,010,032 (104.7%, reflecting CL drag). SI's emergence as a fifth contributor ($32.8K, up from -$360 in V4) is the main diversification improvement.

---

## 7. Honest Assessment

### What Works

1. **The Cu/Au ratio genuinely predicts rate direction.** UB and ZN have been consistent alpha generators across all clean versions (V2, V4, V5). The signal captures macro regime shifts that manifest most strongly in duration positioning.

2. **HY confirmation is a real improvement.** It improved UB (the alpha engine) by $48K, flipped SI from breakeven to +$33K contributor, and improved MNQ loss management. The mechanism is sound: credit spreads provide independent confirmation of risk appetite, filtering false Cu/Au signals.

3. **A/B testing works.** The V3 disaster (bundling five changes, 43% Sharpe decline) taught the discipline of isolated testing. V4 and V5 each used this framework to make net-positive changes. The 40% hit rate on proposed improvements is informative -- most "good ideas" actually degrade performance.

4. **Negative SPX correlation.** At -0.09, the strategy provides genuine decorrelation from equities. This has been stable across all versions (-0.05 to -0.17 range).

5. **Drawdown management is well-calibrated.** Zero drawdown stop triggers in 881 rebalances. The hysteresis mechanism (warn at 15%, clear at 12%) prevents rapid on/off cycling. Max DD of 14.1% is the best across all versions.

### What Doesn't Work

1. **Concentration risk.** 70.6% of net P&L from one instrument (UB). This is functionally a leveraged bond bet with macro timing, not a diversified macro strategy. If the Cu/Au signal's predictive power for rates degrades, the strategy collapses.

2. **Profit factor of 1.10 leaves no margin.** For every dollar lost, the strategy earns $1.10. One bad quarter of execution slippage, model degradation, or cost increases could flip this negative. Institutional minimum is typically 1.30.

3. **Turnover remains elevated.** 28.1x annual turnover produces $193K in costs on $1.16M gross -- 16.7% cost ratio. UB alone accounts for $117K in costs. The wider V5 bands helped but did not solve the structural issue: frequent rebalancing of large-notional bond futures generates high turnover by construction.

4. **CL is a persistent loser.** -$77K net with a 50.5% win rate and unfavorable avg W/L ratio (0.88x). It stays only because of portfolio diversification effects, but this is an unsatisfying justification for an instrument that actively destroys alpha.

5. **The signal is binary, not graded.** The catastrophic failure of signal-strength sizing (-38.7% Sharpe) confirms that the Cu/Au ratio tells you risk-on or risk-off, but nothing about magnitude. This limits the strategy's ability to size aggressively when conviction is genuinely high.

### Theoretical Ceiling

The Cu/Au ratio is a single-factor macro signal augmented by a confirmation filter (HY spreads). Across V2 through V5:

- V2: Sharpe 0.34 (raw signal, broken infrastructure)
- V4: Sharpe 0.40 (clean signal, optimized parameters)
- V5: Sharpe 0.50 (clean signal + HY confirmation)

Each iteration produced diminishing marginal improvements. The V5 A/B tests demonstrate that most additional tweaks hurt rather than help -- a signature of a strategy operating near its information-theoretic limit.

The estimated ceiling for a two-factor macro signal (Cu/Au + HY) trading 5-6 instruments is approximately **Sharpe 0.55-0.65**. Reaching 0.80+ would require:

- A third independent signal factor (momentum, volatility term structure, positioning data)
- Instrument-specific signal calibration (not uniform Cu/Au-driven allocation)
- Dynamic hedging or options overlay to improve payoff structure
- Or a fundamentally different architecture

### What Would Be Needed for Live Deployment

1. **Sharpe above 0.65** with at least a 10-year out-of-sample test
2. **Profit factor above 1.20** to survive real-world execution friction
3. **No single instrument above 50% of P&L** -- reduce UB concentration
4. **Turnover below 20x** -- further band widening or less frequent rebalancing
5. **Live market microstructure analysis** for UB/ZN execution (VWAP feasibility, market impact at target sizes)
6. **Stress testing** against historical rate regimes the backtest didn't cover (e.g., 2006-2008 if data were extended)

Alternatively, the strategy could be deployed as a **sub-allocation in a multi-strategy portfolio** where the -0.09 SPX correlation and rates-focused alpha provide diversification value, even with a Sharpe of 0.50.

---

## 8. A/B Test Log

### V4 A/B Tests (Isolating V3 Changes)

| Test | V3 Setting | V4 Setting | Sharpe Impact | Verdict |
|---|---|---|---|---|
| Cu/Au detrending | ON | **OFF** | +84% | **Reverted** |
| Real rate signal overlay | ON | **OFF** | +28% | **Reverted** |
| Return smoothing | ON | **OFF** | +34% | **Reverted** |
| Volatility filter | ON | **ON** | Positive | **Kept** |
| Rebalance bands | 2 / 20% | **3 / 40%** | +35% | **Widened** |

### V5 A/B Tests (Candidate Improvements)

| # | Test | Variant | Sharpe | Max DD | PF | Turnover | Costs | Verdict |
|---|---|---|---|---|---|---|---|---|
| -- | **V4 Baseline** | -- | 0.4045 | 16.39% | 1.083 | 32.1x | $230K | -- |
| 1 | Drop CL | CL removed | 0.3876 | 15.47% | 1.080 | 30.9x | $199K | **REJECT** |
| 2a | Wider UB/ZN bands | 4 / 50% | 0.4141 | 16.28% | 1.085 | 31.5x | $221K | **ADOPT** |
| 2b | Biweekly rebalance | Combined | 0.2995 | 17.12% | 1.065 | 30.0x | $167K | **REJECT** |
| 3a | VIX filter | >20 threshold | 0.3537 | 16.20% | 1.074 | 31.5x | $211K | **REJECT** |
| 3b | VIX filter | >25 threshold | 0.3652 | 16.65% | 1.076 | 32.3x | $227K | **REJECT** |
| 4 | HY confirmation | ON | 0.4557 | 14.82% | 1.095 | 30.0x | $208K | **ADOPT** |
| 5a | Signal-strength sizing | 1.5x | 0.2479 | 19.46% | 1.061 | 38.5x | $259K | **REJECT** |
| 5b | Signal-strength sizing | 1.0x conservative | 0.3816 | 16.08% | 1.081 | 32.6x | $224K | **REJECT** |
| -- | **V5 Final** | **Bands + HY** | **0.4979** | **14.12%** | **1.103** | **28.1x** | **$193K** | -- |

### Key Takeaways Across All A/B Tests

1. **Confirmation beats filtering.** HY confirmation (+12.7%) improves trade selection. VIX filter (-12.6%) removes trades indiscriminately, including profitable ones. The difference: filters gate entry; confirmation signals adjust sizing.

2. **Portfolio effects are real.** CL is a -$78K standalone loser that improves portfolio Sharpe by 0.017 when included. Instrument-level P&L is necessary but not sufficient.

3. **The signal is binary.** Signal-strength sizing failed at every level tested (-38.7% at 1.5x, -5.7% at 1.0x). The Cu/Au ratio does not contain reliable magnitude information.

4. **Bundling destroys attribution.** V3 bundled 5 changes; 3 were destructive. V4 and V5 tested in isolation and achieved net gains. This is the foundational methodological lesson of the project.

5. **Hit rate is 40%.** Across V3-V5, 4 of 10 proposed changes improved the strategy. Most reasonable-sounding ideas degrade a strategy operating near its ceiling.
