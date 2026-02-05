# Copper-Gold Ratio Economic Regime Strategy v2.1

> **Last Updated:** 2026-02-05 | **Data Complete:** All 5 layers fully implementable

> **TL;DR:** The strategy uses the copper-to-gold ratio as a macro regime indicator—when copper outperforms gold, it signals risk-on (long equities, commodities, short bonds), and when gold outperforms copper, it signals risk-off (long bonds, gold, short equities). To avoid false signals, the strategy filters trades through a regime classifier (growth vs inflation vs liquidity shock), USD/DXY filter, term structure confirmation, and China demand filter. All data layers are fully available.

---

## Executive Summary

This strategy uses the copper-to-gold ratio (HG/GC) as a macro regime indicator with enhanced filtering layers to distinguish true economic regime shifts from noise (USD effects, liquidity squeezes, inflation vs growth dynamics).

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                        LAYER 1: MACRO REGIME                        │
│                     Cu/Gold Ratio Signal Family                     │
└─────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      LAYER 2: REGIME CLASSIFIER                     │
│              Growth Shock │ Inflation Shock │ Liquidity Shock       │
└─────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                        LAYER 3: USD FILTER                          │
│                  DXY Regime + Liquidity Proxy                       │
└─────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                   LAYER 4: TERM STRUCTURE FILTER                    │
│         Backwardation/Contango per Commodity (2nd month data)       │
└─────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         LAYER 5: CHINA FILTER                       │
│            CLI + Credit + SHFE Inventory + CNY/USD                  │
└─────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      TRADE EXPRESSION & SIZING                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Layer 1: Cu/Gold Ratio Signal Generation

### Data Normalization (Critical)

HG and GC have different quote conventions and contract multipliers:

| Contract | Quote Unit | Multiplier | Contract Size |
|----------|------------|------------|---------------|
| HG (Copper) | cents/lb | 25,000 | 25,000 lbs |
| GC (Gold) | $/oz | 100 | 100 troy oz |

**Normalization Options:**

1. **Notional Normalization** (Recommended)
   ```
   Cu_notional = HG_price * 25000 / 100  (convert cents to dollars)
   Au_notional = GC_price * 100
   Ratio = Cu_notional / Au_notional
   ```

2. **Spot Proxy Normalization**
   ```
   Use consistent spot prices ($/lb for copper, $/oz for gold)
   Ratio = (Cu_spot * 1.0) / (Au_spot / 14.583)  # normalize to common unit
   ```

3. **Return-Based Ratio**
   ```
   Ratio_return = ln(HG_t / HG_t-1) - ln(GC_t / GC_t-1)
   ```

### Rolling Convention

| Parameter | Specification |
|-----------|---------------|
| Price Source | Front-month settlement (or 2nd month if within 5 days of expiry) |
| Roll Trigger | 5 business days before first notice date |
| Roll Method | Calendar spread (roll to next liquid month) |
| Adjustment | Ratio-adjusted continuous series (preserve percentage changes) |

### Signal Family (Replace Single 20-day ROC)

Generate multiple signals and require consensus or weighted combination:

#### Signal 1: Rate of Change (ROC)
```python
ROC_10 = (ratio_t / ratio_t-10) - 1
ROC_20 = (ratio_t / ratio_t-20) - 1
ROC_60 = (ratio_t / ratio_t-60) - 1
```

#### Signal 2: Moving Average Crossover
```python
MA_fast = SMA(ratio, 10)
MA_slow = SMA(ratio, 50)
Signal_MA = 1 if MA_fast > MA_slow else -1
```

#### Signal 3: Z-Score Regime
```python
ratio_zscore = (ratio - SMA(ratio, 120)) / STD(ratio, 120)
Signal_Z = 1 if ratio_zscore > 0.5 else (-1 if ratio_zscore < -0.5 else 0)
```

#### Composite Signal
```python
composite = w1*sign(ROC_20) + w2*Signal_MA + w3*Signal_Z
macro_tilt = "RISK_ON" if composite > threshold else "RISK_OFF"
```

### Robustness Requirements

- Minimum 15 years of history ✅ (available data: 2010-06-07 to 2025-11-12, **15.4 years**)
- Stability check: signal should not flip more than 8-12x per year

---

## Layer 2: Macro Regime Classifier

The Cu/Gold signal alone cannot distinguish between different macro drivers. Classify the current regime to adjust trade mappings.

### Regime Types

| Regime | Characteristics | Cu/Gold Behavior | Correct Response |
|--------|-----------------|------------------|------------------|
| **Growth Shock (+)** | Rising growth expectations, equities up, yields up | Rising | Long equities, commodities; short bonds |
| **Growth Shock (-)** | Falling growth expectations, equities down, yields down | Falling | Long bonds, gold; short equities |
| **Inflation Shock** | Rising inflation, yields up, equities mixed, commodities up | May rise (Cu up) or fall (Au up) | Reduce equity beta, favor commodities, short duration |
| **Liquidity Shock** | USD squeeze, credit spreads widen, correlations spike | Often falls (both fall, Au less) | Reduce all risk, long USD, long quality bonds |

### Regime Detection Variables

```python
# Growth proxy
growth_signal = SPX_momentum_60d  # or ISM, PMI if available

# Inflation proxy
inflation_signal = breakeven_10y_change_20d  # or CPI surprises

# Liquidity proxy
liquidity_signal = -1 * (DXY_zscore + credit_spread_zscore + VIX_zscore) / 3

# Real rates
real_rate_signal = TIPS_10y_change_20d
```

### Classification Logic

```python
def classify_regime(growth, inflation, liquidity):
    if liquidity < -1.5:
        return "LIQUIDITY_SHOCK"
    elif inflation > 1.0 and growth < 0.5:
        return "INFLATION_SHOCK"
    elif growth > 0.5:
        return "GROWTH_POSITIVE"
    elif growth < -0.5:
        return "GROWTH_NEGATIVE"
    else:
        return "NEUTRAL"
```

### Trade Mapping by Regime

| Regime | Cu/Gold Rising | Cu/Gold Falling |
|--------|----------------|-----------------|
| GROWTH_POSITIVE | Full risk-on book | Reduce to neutral |
| GROWTH_NEGATIVE | Cautious, reduced size | Full risk-off book |
| INFLATION_SHOCK | Long commodities only, no equity beta | Long gold, short duration |
| LIQUIDITY_SHOCK | **NO TRADE** - flatten all | **NO TRADE** - flatten all |
| NEUTRAL | Half size, follow Cu/Gold | Half size, follow Cu/Gold |

---

## Layer 3: USD / DXY Filter

### Purpose

Both copper and gold are USD-denominated. A strong USD move can shift the ratio without any change in underlying growth sentiment.

### DXY Regime Detection

```python
DXY_MA_50 = SMA(DXY, 50)
DXY_MA_200 = SMA(DXY, 200)
DXY_trend = "STRONG" if DXY > DXY_MA_50 > DXY_MA_200 else \
            "WEAK" if DXY < DXY_MA_50 < DXY_MA_200 else "NEUTRAL"

DXY_momentum = (DXY / DXY_20d_ago) - 1
```

### Filter Rules

| DXY Condition | Cu/Gold Signal | Action |
|---------------|----------------|--------|
| DXY momentum > +3% (20d) | Rising | **Suspect** - may be USD squeeze, not growth. Reduce size 50% |
| DXY momentum > +3% (20d) | Falling | Confirmed risk-off + USD strength. Full risk-off |
| DXY momentum < -3% (20d) | Rising | Confirmed risk-on + USD weakness. Full risk-on |
| DXY momentum < -3% (20d) | Falling | **Suspect** - may be inflation/gold bid. Check regime classifier |
| DXY neutral | Any | Trust Cu/Gold signal at full size |

### Liquidity Proxy (Supplementary)

```python
# Composite liquidity indicator
# Data sources: vix.csv, high_yield_spread.csv, fed_balance_sheet.csv
liquidity_score = normalize(
    -1 * VIX_percentile_60d +              # from vix.csv
    -1 * high_yield_spread_zscore +        # from high_yield_spread.csv
    fed_balance_sheet_growth_yoy           # from fed_balance_sheet.csv (weekly, interpolate to daily)
)

if liquidity_score < -1.5:
    # Liquidity crunch - Cu/Gold signal unreliable
    reduce_all_positions(factor=0.25)
```

---

## Layer 4: Term Structure Filter (Per Commodity)

> **✅ DATA AVAILABLE:** 2nd month futures data has been sourced from Bloomberg for commodities (HG, GC, CL, SI) and fixed income (ZN, ZB). Term structure filter is fully implementable for all commodity positions. For treasuries, use ZN + ZN_2nd pair; ZB_2nd available for yield curve spread analysis.

### Purpose

Use the commodity's own term structure to confirm whether micro conditions support the macro view.

### Term Structure Calculation

```python
# Data sources:
# Front month: HG.csv, GC.csv, CL.csv, ZN.csv, SI.csv
# Back month:  HG_2nd.csv, GC_2nd.csv, CL_2nd.csv, ZN_2nd.csv, SI_2nd.csv
# Note: ZB_2nd.csv available for 30Y T-Bond, use with ZN for yield curve spread analysis

def calc_term_structure(front_price, back_price, days_between=30):
    """
    Returns annualized roll yield (positive = backwardation, negative = contango)
    """
    roll_yield = (front_price / back_price - 1) * (365 / days_between)
    return roll_yield

# Classification
def classify_curve(roll_yield):
    if roll_yield > 0.02:  # >2% annualized
        return "BACKWARDATION"
    elif roll_yield < -0.02:
        return "CONTANGO"
    else:
        return "FLAT"
```

### Trade Expression Matrix

#### Crude Oil (CL)

| Macro Tilt | Term Structure | Trade Expression |
|------------|----------------|------------------|
| RISK_ON | Backwardation | Long calendar spread (long front, short back) |
| RISK_ON | Contango | Skip or minimal outright long |
| RISK_ON | Flat | Small outright long |
| RISK_OFF | Contango | Short outright or short carry spread |
| RISK_OFF | Backwardation | **Skip** - tight markets squeeze shorts |
| RISK_OFF | Flat | Small outright short |

#### Copper (HG)

| Macro Tilt | Term Structure | Trade Expression |
|------------|----------------|------------------|
| RISK_ON | Backwardation | Outright long (trend + carry aligned) |
| RISK_ON | Contango | Outright long, reduced size |
| RISK_OFF | Contango | Outright short |
| RISK_OFF | Backwardation | **Skip** - don't short tight copper |

#### Gold (GC)

| Macro Tilt | Term Structure | Trade Expression |
|------------|----------------|------------------|
| RISK_ON | Any | Short outright (gold typically in contango, less relevant) |
| RISK_OFF | Any | Long outright |

#### Treasuries (ZN, UB)

| Macro Tilt | Yield Curve | Trade Expression |
|------------|-------------|------------------|
| RISK_ON | Steepening | Short ZN (belly), or steepener spread |
| RISK_ON | Flattening | Short ZN outright, reduced size |
| RISK_OFF | Steepening | Long UB (long end), or steepener |
| RISK_OFF | Flattening | Long ZN outright |

---

## Position Sizing Framework

### Base Notional Allocation

```python
TOTAL_NOTIONAL = account_equity * leverage_target  # e.g., 2x

# Allocation weights by asset class
weights = {
    "equity_index": 0.30,  # MES, MNQ
    "commodities": 0.35,   # CL, HG, GC
    "fixed_income": 0.25,  # ZN, UB
    "fx": 0.10             # 6J
}
```

### Size Adjustments

```python
def adjusted_size(base_size, regime, dxy_filter, term_structure_confirm=None):
    multiplier = 1.0

    # Regime adjustment
    if regime == "LIQUIDITY_SHOCK":
        multiplier *= 0.0  # No trade
    elif regime == "NEUTRAL":
        multiplier *= 0.5
    elif regime in ["INFLATION_SHOCK"]:
        multiplier *= 0.5  # More cautious

    # DXY filter adjustment
    if dxy_filter == "SUSPECT":
        multiplier *= 0.5

    # Term structure confirmation
    if term_structure_confirm == False:
        multiplier *= 0.0  # Skip this commodity
    elif term_structure_confirm == "PARTIAL":
        multiplier *= 0.5

    return base_size * multiplier
```

---

## Signal Timing & Holding Period

### Intended Horizon

Cu/Gold is a **slow-moving regime indicator** (weeks to months), not a short-term trading signal.

| Parameter | Specification |
|-----------|---------------|
| Signal Evaluation | Daily (EOD) |
| Minimum Holding Period | 5 trading days |
| Expected Regime Duration | 20-60 trading days |
| Maximum Signal Flips per Year | 8-12 |

### Rebalance Triggers

1. **Signal flip** - Cu/Gold composite crosses threshold
2. **Regime change** - Classifier changes state
3. **Filter trigger** - DXY or liquidity proxy hits threshold
4. **Term structure flip** - Commodity curve changes shape (backwardation ↔ contango)
5. **China demand shift** - Significant change in China demand indicators
6. **Calendar** - Weekly rebalance check (Fridays)

---

## Real Rates & Gold Dynamics

### Why This Matters

Gold's behavior is heavily driven by **real interest rates** (nominal rates minus inflation expectations). The Cu/Gold ratio can move because of gold repricing on real rates, not copper repricing on growth.

### Real Rate Signal

```python
real_rate_10y = nominal_10y_yield - breakeven_10y

real_rate_change_20d = real_rate_10y - real_rate_10y_20d_ago
real_rate_zscore = (real_rate_10y - SMA(real_rate_10y, 120)) / STD(real_rate_10y, 120)
```

### Interpretation Matrix

| Real Rate Move | Cu/Gold Move | Likely Driver | Action |
|----------------|--------------|---------------|--------|
| Rising | Rising | Growth optimism (Cu up, Au down on real rates) | Confirmed risk-on |
| Rising | Falling | Real rate drag on gold dominates | Check if Cu flat - may be pure rates move, reduce conviction |
| Falling | Rising | Inflation expectations rising (Cu up, Au up but Cu more) | Inflation regime - favor commodities |
| Falling | Falling | Risk-off flight to gold | Confirmed risk-off |

### Safe-Haven Demand Override

Gold can rise despite rising real rates during geopolitical crises. Monitor:

```python
# Geopolitical/crisis indicator
crisis_indicator = (
    VIX > VIX_90th_percentile and
    gold_1d_return > 1.5% and
    equity_1d_return < -1.5%
)

if crisis_indicator:
    # Gold rising on safe-haven, not fundamentals
    # Do NOT short gold even if Cu/Gold signals risk-on
    skip_gold_short = True
```

---

## Layer 5: China Demand Filter

> **✅ DATA AVAILABLE:** All China indicators have been sourced from Bloomberg. This layer is fully implementable.

### Why This Matters

China consumes ~50% of global copper. Chinese demand shifts can move copper independently of global growth sentiment.

### China Indicators to Monitor

| Indicator | Source | File | Signal |
|-----------|--------|------|--------|
| OECD Leading Indicator for China | Bloomberg (OECNKLAF) | ✅ `china_leading_indicator_update.csv` | Leading indicator for Cu demand |
| China PMI | Bloomberg (CPMINDX) | ✅ `china_pmi.csv` | Manufacturing activity |
| CNY/USD | FX markets | ✅ `cny_usd.csv` | Weak CNY can suppress Cu imports |
| China Total Loans | Bloomberg (CNLNTTL) | ✅ `china_credit.csv` | Credit expansion → construction → Cu demand |
| Shanghai Copper Inventory | Bloomberg (SHFCCOPD) | ✅ `shfe_copper_inventory.csv` | Rising inventory = weak demand |

### China Filter Implementation

```python
# Data sources:
# china_leading_indicator_update.csv - OECD CLI for China (monthly)
# china_pmi.csv - China Manufacturing PMI (monthly)
# china_credit.csv - China Total Loans (monthly)
# shfe_copper_inventory.csv - Shanghai copper inventory (weekly)
# cny_usd.csv - CNY/USD exchange rate (daily)

# Composite China demand signal
china_cli_momentum = china_leading_indicator - china_leading_indicator_3m_avg
china_pmi_zscore = (china_pmi - SMA(china_pmi, 12)) / STD(china_pmi, 12)
shfe_inventory_change = (shfe_inventory / shfe_inventory_3m_avg) - 1
cny_momentum = (cny_usd / cny_usd_20d_avg) - 1  # positive = CNY weakening

# Composite score (negative = weakening China demand)
china_demand_score = (
    0.30 * normalize(china_cli_momentum) +
    0.25 * normalize(china_pmi_zscore) +
    0.25 * normalize(-shfe_inventory_change) +  # invert: rising inventory = weak
    0.20 * normalize(-cny_momentum)              # invert: weak CNY = weak demand
)

if china_demand_score < -1.0:
    # China demand weakening significantly
    # Cu/Gold may fall due to China, not global risk-off
    china_adjustment = 0.5  # Reduce Cu-related conviction significantly
elif china_demand_score < -0.5:
    china_adjustment = 0.7  # Moderate reduction
else:
    china_adjustment = 1.0  # No adjustment
```

---

## Transaction Costs & Execution

### Cost Assumptions

| Contract | Estimated Spread | Commission (RT) | Slippage |
|----------|------------------|-----------------|----------|
| HG | 0.0005 (0.5 tick) | $2.50 | 0.5 tick |
| GC | 0.10 (1 tick) | $2.50 | 0.5 tick |
| CL | 0.01 (1 tick) | $2.50 | 1 tick |
| MES | 0.25 (1 tick) | $0.50 | 0.5 tick |
| MNQ | 0.25 (1 tick) | $0.50 | 0.5 tick |
| ZN | 0.5/32 (1 tick) | $1.50 | 0.5 tick |
| 6J | 0.000001 (1 tick) | $2.50 | 0.5 tick |
| SI | 0.005 (1 tick) | $2.50 | 1 tick |

### Roll Cost Estimation

```python
def estimate_roll_cost(front_price, back_price, spread_cost):
    """
    Roll cost = calendar spread slippage + 2x commission
    """
    theoretical_roll = back_price - front_price
    execution_cost = spread_cost + (2 * commission)
    return execution_cost

# Annual roll cost = rolls_per_year * cost_per_roll
# HG/GC roll ~4-6x per year
```

### Execution Protocol

| Parameter | Specification |
|-----------|---------------|
| Signal evaluation time | 4:00 PM ET (after cash close, before futures settle) |
| Order placement | MOC (Market on Close) for liquid contracts |
| Order type for rolls | Limit on calendar spread |
| Max participation rate | 5% of average daily volume |
| Execution window | Final 30 minutes of pit session |

---

## Margin Requirements

### Initial Margin Estimates (Subject to Change)

| Contract | Initial Margin | Maintenance | Notional (approx) |
|----------|----------------|-------------|-------------------|
| HG | $6,000 | $5,500 | ~$110,000 |
| GC | $11,000 | $10,000 | ~$200,000 |
| CL | $7,000 | $6,500 | ~$75,000 |
| MES | $1,500 | $1,400 | ~$25,000 |
| MNQ | $2,000 | $1,800 | ~$40,000 |
| ZN | $2,500 | $2,200 | ~$110,000 |
| UB | $9,000 | $8,500 | ~$130,000 |
| 6J | $4,000 | $3,600 | ~$80,000 |
| SI | $10,000 | $9,000 | ~$150,000 |

### Margin Utilization Rules

```python
MAX_MARGIN_UTILIZATION = 0.50  # Never use more than 50% of available margin

def check_margin_capacity(positions, account_equity):
    total_margin_required = sum(pos.margin for pos in positions)
    utilization = total_margin_required / account_equity

    if utilization > MAX_MARGIN_UTILIZATION:
        scale_factor = MAX_MARGIN_UTILIZATION / utilization
        return scale_all_positions(positions, scale_factor)
    return positions
```

---

## Silver (SI) and Yen (6J) Rules

### Silver (SI) - Hybrid Metal

Silver behaves as both industrial metal (like copper) and precious metal (like gold).

| Macro Tilt | Cu/Gold Signal | SI Action |
|------------|----------------|-----------|
| RISK_ON | Strong rising | Long SI (industrial demand) |
| RISK_ON | Weak rising | Skip SI (ambiguous) |
| RISK_OFF | Strong falling | Long SI (precious metal bid) |
| RISK_OFF | Weak falling | Long SI, reduced size |
| INFLATION_SHOCK | Any | Long SI (inflation hedge) |

```python
# Silver has higher vol than gold - size accordingly
SI_vol_adjustment = GC_ATR_20 / SI_ATR_20  # Typically ~0.6-0.7
SI_position_size = base_size * SI_vol_adjustment
```

### Japanese Yen (6J) - Safe Haven Currency

| Macro Tilt | Regime | 6J Action |
|------------|--------|-----------|
| RISK_ON | Any | Short 6J (carry trade on) |
| RISK_OFF | GROWTH_NEGATIVE | Long 6J (safe haven) |
| RISK_OFF | LIQUIDITY_SHOCK | Long 6J (deleveraging, carry unwind) |
| RISK_OFF | INFLATION_SHOCK | Skip 6J (inflation erodes currency) |

```python
# BOJ policy filter - if BOJ is actively intervening, reduce conviction
if abs(usdjpy_1d_move) > 2.0:  # >2% daily move suggests intervention
    reduce_6J_size(factor=0.5)
```

---

## Parameter Specifications

### Signal Thresholds (Initial Values)

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| ROC lookback windows | [10, 20, 60] days | Short/medium/long momentum |
| MA crossover fast/slow | 10 / 50 days | Standard trend parameters |
| Z-score lookback | 120 days | ~6 months for regime baseline |
| Z-score threshold | ±0.5 | ~1/3 standard deviation |
| Composite threshold | 0.0 | Simple majority vote |
| Signal weights (w1, w2, w3) | 0.33, 0.33, 0.34 | Equal weight initially |

### Filter Thresholds

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| DXY momentum threshold | ±3% over 20d | Significant USD move |
| Liquidity score threshold | -1.5 | ~1.5 std below normal |
| Correlation spike threshold | 0.7 avg pairwise | Crisis indicator |
| Term structure threshold | ±2% annualized | Meaningful backwardation/contango |

### Risk Thresholds

| Parameter | Value |
|-----------|-------|
| Portfolio drawdown warning | 10% |
| Portfolio drawdown stop | 15% |
| Position stop (ATR multiple) | 2.0x ATR(20) |
| Max margin utilization | 50% |
| Max signal flips per year | 12 |

---

## Performance Acceptance Criteria

### Minimum Viable Strategy

| Metric | Minimum Threshold | Target |
|--------|-------------------|--------|
| Sharpe Ratio (net of costs) | 0.8 | 1.2+ |
| Sortino Ratio | 1.0 | 1.5+ |
| Max Drawdown | < 20% | < 15% |
| Win Rate | > 45% | > 50% |
| Profit Factor | > 1.3 | > 1.5 |
| Annual Turnover | < 15x | < 10x |
| Correlation to SPX | < 0.5 | < 0.3 |

### Kill Criteria (Stop Development If)

- Signal flips > 20x per year (overfit to noise)

---

## Risk Management

### Position Limits

| Asset Class | Max Notional % | Max Contracts |
|-------------|----------------|---------------|
| Single equity index | 20% | TBD based on account |
| Single commodity | 15% | TBD |
| Total directional equity | 35% | - |
| Total directional commodity | 40% | - |

### Stop Loss Rules

```python
# Portfolio level
if portfolio_drawdown_from_peak > 0.10:  # 10% drawdown
    reduce_all_positions(factor=0.5)

if portfolio_drawdown_from_peak > 0.15:  # 15% drawdown
    flatten_all()

# Position level
if position_loss > 2 * ATR_20:
    exit_position()
```

### Correlation Monitoring

```python
# If cross-asset correlations spike (crisis indicator)
rolling_corr = calc_pairwise_correlations(returns_20d)
avg_corr = rolling_corr.mean()

if avg_corr > 0.7:  # Correlations converging
    # Likely liquidity event
    reduce_all_positions(factor=0.5)
```

---

## Data Requirements

### Data Inventory (Available)

All data is stored in `data/raw/` with the following structure:

#### Futures Data (`data/raw/futures/`)

> **Format:** All futures files are continuous series (already roll-adjusted). Columns: `date, open, high, low, close, volume`

##### Front-Month Contracts

| Contract | Symbol | Rows | Date Range | Notes |
|----------|--------|------|------------|-------|
| Copper | HG.csv | 4,780 | 2010-06-07 to 2025-11-12 | Primary signal |
| Gold | GC.csv | 4,764 | 2010-06-07 to 2025-11-12 | Primary signal |
| Crude Oil | CL.csv | 4,787 | 2010-06-07 to 2025-11-12 | Commodity basket |
| Micro E-mini S&P 500 | MES.csv | 2,033 | 2019-05-05 to 2025-11-12 | Launched May 2019 |
| Micro E-mini Nasdaq | MNQ.csv | 2,033 | 2019-05-05 to 2025-11-12 | Launched May 2019 |
| 10Y T-Note | ZN.csv | 4,791 | 2010-06-07 to 2025-11-12 | Fixed income |
| Ultra T-Bond | UB.csv | 4,745 | 2010-06-07 to 2025-11-12 | Fixed income |
| Japanese Yen | 6J.csv | 4,787 | 2010-06-07 to 2025-11-12 | FX / safe haven |
| Silver | SI.csv | 4,740 | 2010-06-07 to 2025-11-12 | Hybrid metal |

##### 2nd-Month Contracts (Term Structure)

| Contract | Symbol | Rows | Date Range | Notes |
|----------|--------|------|------------|-------|
| Copper 2nd Month | HG_2nd.csv | 4,055 | 2010-01-04 to 2026-02-05 | Pair with HG.csv |
| Gold 2nd Month | GC_2nd.csv | 4,054 | 2010-01-04 to 2026-02-05 | Pair with GC.csv |
| Crude Oil 2nd Month | CL_2nd.csv | 4,054 | 2010-01-04 to 2026-02-05 | Pair with CL.csv |
| 10Y T-Note 2nd Month | ZN_2nd.csv | 4,058 | 2010-01-04 to 2026-02-05 | Pair with ZN.csv |
| 30Y T-Bond 2nd Month | ZB_2nd.csv | 4,117 | 2010-01-05 to 2026-02-05 | Yield curve spread (no ZB front) |
| Silver 2nd Month | SI_2nd.csv | 4,054 | 2010-01-04 to 2026-02-05 | Pair with SI.csv |

##### Equity Index Backfill (2010-2019)

| Contract | Symbol | Rows | Date Range | Notes |
|----------|--------|------|------------|-------|
| S&P 500 Index | spx_backfill.csv | 4,048 | 2010-01-04 to 2026-02-05 | Backfill for MES |
| Nasdaq 100 Futures | NQ.csv | 4,057 | 2010-01-04 to 2026-02-05 | Backfill for MNQ |

**Additional futures available:** 6A, 6B, 6C, 6E, 6L, 6M, 6N, 6S, GF, HE, HO, KE, LE, M2K, MYM, NG, PL, RB, ZC, ZF, ZL, ZM, ZR, ZS, ZT, ZW

#### Macro Data (`data/raw/macro/`)

> **Format:** All macro files have columns: `date, <series_name>` (e.g., `date, treasury_10y`)

##### Core Macro Indicators

| Series | File | Rows | Date Range | Purpose |
|--------|------|------|------------|---------|
| 10Y Treasury Yield | treasury_10y.csv | 16,720 | 1962-01-02 to 2026-02-02 | Real rates calc |
| 10Y TIPS Yield | tips_10y.csv | 6,023 | 2003-01-02 to 2026-02-02 | Real rates calc |
| 10Y Breakeven Inflation | breakeven_10y.csv | 6,024 | 2003-01-02 to 2026-02-03 | Inflation proxy |
| VIX | vix.csv | 9,415 | 1990-01-02 to 2026-02-02 | Liquidity proxy |
| High Yield Spread | high_yield_spread.csv | 7,689 | 1996-12-31 to 2026-02-02 | Credit/liquidity proxy |
| Fed Balance Sheet | fed_balance_sheet.csv | 1,205 | 2003-01-01 to 2026-01-28 | Liquidity proxy (weekly) |
| USD Index (Trade-Weighted) | dxy.csv | 5,240 | 2006-01-02 to 2026-01-30 | USD filter |
| S&P 500 Index | spx.csv | 25,422 | 1928-01-03 to 2026-02-03 | Growth proxy |

##### China Filter Data

> **Format:** `date, <series_name>` (e.g., `date, china_pmi`)

| Series | File | Column | Rows | Date Range | Purpose |
|--------|------|--------|------|------------|---------|
| CNY/USD | cny_usd.csv | `cny_usd` | 16,469 | 1981-01-02 to 2026-02-03 | China FX filter |
| OECD China Leading Indicator | china_leading_indicator_update.csv | `china_leading_indicator` | 192 | 2010-01-31 to 2025-12-31 | China demand (CLI) |
| China PMI | china_pmi.csv | `china_pmi` | 193 | 2010-01-31 to 2026-01-31 | Manufacturing activity |
| China Total Loans | china_credit.csv | `china_credit` | 192 | 2010-01-31 to 2025-12-31 | Credit impulse proxy |
| Shanghai Copper Inventory | shfe_copper_inventory.csv | `shfe_copper_inventory` | 828 | 2010-01-01 to 2026-01-30 | Physical demand signal |
| China CLI (Alt) | china_cli_alt.csv | `china_cli_alt` | 192 | 2010-01-31 to 2025-12-31 | Alternative CLI series |

### Effective Date Ranges

| Scope | Start | End | Duration |
|-------|-------|-----|----------|
| **Full Strategy (All Layers)** | **2010-06-07** | **2025-11-12** | **~15.4 years** |
| With term structure filter | 2010-06-07 | 2025-11-12 | ~15.4 years |
| With China filter | 2010-06-07 | 2025-11-12 | ~15.4 years |
| With MES/MNQ micro contracts | 2019-05-05 | 2025-11-12 | ~6.5 years |
| Using SPX/NQ backfill for equities | 2010-06-07 | 2025-11-12 | ~15.4 years |

### Data Sources

| Data Type | Source | Pull Date |
|-----------|--------|-----------|
| Original futures (front-month) | Historical data provider | Pre-existing |
| 2nd month futures | Bloomberg Terminal | 2026-02-05 |
| Core macro indicators | FRED | Pre-existing |
| China filter data | Bloomberg Terminal | 2026-02-05 |

### Execution Data

Execution parameters (spreads, commissions, margins) are specified in the **Transaction Costs & Execution** and **Margin Requirements** sections above.

---

## Implementation Phases

> **Note:** All data has been sourced and is available in `data/raw/`. All 5 strategy layers are fully implementable with the current dataset. Testing window: **2010-06-07 to 2025-11-12 (15.4 years)**.

### Phase 1: Data Pipeline
- [ ] Load futures data from `data/raw/futures/` (CSV format: date, open, high, low, close, volume)
- [ ] Load macro data from `data/raw/macro/` (CSV format: date, value)
- [ ] Validate data quality (missing days, outliers)
- [ ] Verify futures data is continuous (data is pre-rolled front-month series)
- [ ] Implement HG/GC normalization (notional method per Layer 1 spec)
- [ ] Verify normalization: ratio should be stationary in levels or trend-stationary

### Phase 2: Signal Generation
- [ ] Implement ROC signal family (10, 20, 60-day windows)
- [ ] Implement MA crossover signal (10/50-day)
- [ ] Implement Z-score regime signal (120-day lookback)
- [ ] Implement composite signal aggregation with configurable weights

### Phase 3: Regime Classifier
- [ ] Implement growth proxy (SPX momentum from `spx.csv`)
- [ ] Implement inflation proxy (breakeven rates from `breakeven_10y.csv`)
- [ ] Implement liquidity proxy (VIX from `vix.csv`, high yield spread from `high_yield_spread.csv`)
- [ ] Implement real rates calculation (`treasury_10y.csv` - `tips_10y.csv`)
- [ ] Implement regime classification logic (Growth/Inflation/Liquidity/Neutral)
- [ ] Map trade adjustments to regimes per the trade mapping matrix

### Phase 4: Filters
- [ ] Implement DXY filter (momentum and trend detection from `dxy.csv`)
- [ ] Implement real rates filter
- [ ] Implement term structure filter (using `*_2nd.csv` files for backwardation/contango detection)
- [ ] Implement China demand filter (using `china_leading_indicator_update.csv`, `china_pmi.csv`, `china_credit.csv`, `shfe_copper_inventory.csv`, `cny_usd.csv`)
- [ ] Implement safe-haven demand override for gold

### Phase 5: Position Sizing & Risk Management
- [ ] Implement base notional allocation by asset class
- [ ] Implement size adjustment logic (regime, DXY filter, term structure confirmation, China filter)
- [ ] Implement margin utilization checker (50% max cap)
- [ ] Implement position limits per asset class
- [ ] Implement portfolio-level drawdown monitoring (10% warning, 15% stop)
- [ ] Implement position-level stop logic (2x ATR)
- [ ] Implement correlation spike detection

### Phase 6: Execution Layer
- [ ] Implement transaction cost model (spread + slippage + commission per contract)
- [ ] Implement roll cost model (calendar spread costs)
- [ ] Implement order generator (target position output)

---

## Appendix: Historical Regime Reference

| Period | Regime Type | Cu/Gold | Outcome |
|--------|-------------|---------|---------|
| 2008 Q4 | Liquidity shock | Falling sharply | Risk-off correct, but correlations spiked |
| 2010-2011 | Growth + Inflation | Rising then flat | Mixed, QE distortions |
| 2013 | Taper tantrum | Falling | Risk-off worked |
| 2015-2016 | Growth scare + USD strength | Falling | Risk-off worked, DXY filter would help |
| 2018 Q4 | Liquidity shock | Falling | Risk-off worked |
| 2020 Q1 | Liquidity shock | Falling sharply | Risk-off correct |
| 2020 Q2-Q4 | Growth recovery | Rising | Risk-on correct |
| 2021 | Inflation shock | Rising then volatile | Mixed, inflation filter needed |
| 2022 | Inflation + rate shock | Volatile | Difficult, regime classifier critical |
| 2023 | Growth uncertainty | Choppy | Neutral regime, reduced sizing |

