# Sensitivity study: latency x queue model x cross-fill rule (synthetic market)

## How to read this

The default model has the cross-fill rule ON: when the market moves onto a resting order, the order
fills once at our own price (DESIGN 3.5). On this synthetic market that rule creates most of the
FIFO models' fills, and those fills have strongly negative spread capture (a stale quote picked off
by a price move). The results with the rule ON are therefore dominated by cross-rule fills (see the
cross-share tables), and that mass of fills hides the effect of latency: PnL barely moves with L.
The latency effect is only visible with the rule OFF (ablation, Part 2), where markout and spread
capture fall as L grows. Read the two parts together; neither alone is the whole picture. The
magnitudes are properties of the synthetic market and do not transfer to a real venue, and a
backtest is not live PnL.

## Parameters

- seeds: 7, 8, 9 (generator `--seed`, clean feed, no faults)
- feed length: 1 h per seed, venue A, normalised by `mdtool normalize`
- strategy: market maker, `MMParams` defaults (quote 1 unit, skew at 20 units, max 50 units, skew 1 tick)
- fee: 0.1 bps, jitter 0; latency L is one way: entry = cancel = ack = L
- cross fills: run with `--cross-fills on` and `--cross-fills off` (grid dimension)
- generator command: `python3 tools/gen_feeds.py --seed S --hours 1 --no-verify`
- cells show `mean [min, max]` over seeds
- cross share = fills of kind CROSS / all fills (from `summary.csv`, equal to the CROSS count in `fills_detail.csv`)

# Part 1: cross-fill rule ON (default model)

## 1.1 total PnL (currency units), cross rule ON

| model | L = 0 ms | L = 1 ms | L = 5 ms | L = 20 ms |
|---|---|---|---|---|
| FIFO_BACK | -42.74 [-48.83, -35.72] | -42.65 [-48.59, -35.63] | -43.02 [-49.95, -35.49] | -42.37 [-53.06, -32.08] |
| FIFO_CA(0.25) | -42.47 [-48.12, -35.95] | -42.82 [-48.46, -36.33] | -42.62 [-48.79, -35.64] | -41.51 [-50.93, -31.24] |
| FIFO_CA(0.5) | -42.02 [-46.67, -37.07] | -41.91 [-46.54, -36.66] | -42.09 [-46.70, -36.61] | -40.88 [-48.32, -31.69] |
| OPTIMISTIC | -20.86 [-36.61, -0.83] | -20.94 [-35.79, -1.23] | -21.63 [-37.38, -0.87] | -21.48 [-34.52, -4.69] |

## 1.2 spread capture (currency units), cross rule ON

| model | L = 0 ms | L = 1 ms | L = 5 ms | L = 20 ms |
|---|---|---|---|---|
| FIFO_BACK | -29.09 [-29.85, -28.55] | -29.16 [-29.93, -28.62] | -29.30 [-30.00, -28.77] | -29.91 [-30.71, -29.24] |
| FIFO_CA(0.25) | -28.00 [-28.75, -27.39] | -28.04 [-28.72, -27.45] | -28.25 [-28.92, -27.68] | -28.97 [-29.78, -28.22] |
| FIFO_CA(0.5) | -27.30 [-28.05, -26.65] | -27.37 [-28.10, -26.73] | -27.53 [-28.25, -26.93] | -28.29 [-29.05, -27.53] |
| OPTIMISTIC | 7.32 [7.16, 7.47] | 7.14 [7.06, 7.20] | 6.71 [6.59, 6.88] | 4.92 [4.51, 5.26] |

## 1.3 qty-weighted 1 s markout (bps), cross rule ON

| model | L = 0 ms | L = 1 ms | L = 5 ms | L = 20 ms |
|---|---|---|---|---|
| FIFO_BACK | -0.188 [-0.203, -0.173] | -0.188 [-0.203, -0.173] | -0.189 [-0.207, -0.173] | -0.194 [-0.208, -0.180] |
| FIFO_CA(0.25) | -0.169 [-0.187, -0.155] | -0.169 [-0.186, -0.156] | -0.173 [-0.189, -0.158] | -0.179 [-0.194, -0.165] |
| FIFO_CA(0.5) | -0.159 [-0.174, -0.144] | -0.160 [-0.176, -0.146] | -0.162 [-0.180, -0.147] | -0.170 [-0.186, -0.154] |
| OPTIMISTIC | 0.125 [0.113, 0.135] | 0.123 [0.113, 0.133] | 0.121 [0.112, 0.130] | 0.111 [0.102, 0.121] |

## 1.4 fill count, cross rule ON

| model | L = 0 ms | L = 1 ms | L = 5 ms | L = 20 ms |
|---|---|---|---|---|
| FIFO_BACK | 9635 [9622, 9662] | 9633 [9608, 9677] | 9643 [9611, 9677] | 9700 [9636, 9794] |
| FIFO_CA(0.25) | 9928 [9902, 9969] | 9925 [9898, 9973] | 9933 [9906, 9966] | 9975 [9917, 10070] |
| FIFO_CA(0.5) | 10105 [10060, 10127] | 10098 [10045, 10131] | 10097 [10031, 10132] | 10132 [10065, 10228] |
| OPTIMISTIC | 19284 [19026, 19613] | 19250 [18984, 19572] | 19069 [18812, 19390] | 18410 [18215, 18680] |

## 1.5 share of fills created by the cross rule, cross rule ON

| model | L = 0 ms | L = 1 ms | L = 5 ms | L = 20 ms |
|---|---|---|---|---|
| FIFO_BACK | 0.762 [0.755, 0.773] | 0.762 [0.756, 0.773] | 0.761 [0.756, 0.772] | 0.760 [0.755, 0.768] |
| FIFO_CA(0.25) | 0.736 [0.730, 0.745] | 0.736 [0.731, 0.745] | 0.736 [0.731, 0.746] | 0.737 [0.733, 0.744] |
| FIFO_CA(0.5) | 0.721 [0.715, 0.731] | 0.722 [0.715, 0.731] | 0.722 [0.716, 0.731] | 0.723 [0.719, 0.730] |
| OPTIMISTIC | 0.380 [0.378, 0.383] | 0.380 [0.378, 0.384] | 0.382 [0.380, 0.385] | 0.387 [0.385, 0.391] |

## 1.6 fill rate (orders with a fill / accepted orders), cross rule ON

| model | L = 0 ms | L = 1 ms | L = 5 ms | L = 20 ms |
|---|---|---|---|---|
| FIFO_BACK | 0.295 [0.293, 0.296] | 0.296 [0.294, 0.297] | 0.296 [0.295, 0.297] | 0.299 [0.297, 0.302] |
| FIFO_CA(0.25) | 0.300 [0.299, 0.301] | 0.302 [0.300, 0.303] | 0.302 [0.301, 0.303] | 0.305 [0.302, 0.307] |
| FIFO_CA(0.5) | 0.303 [0.303, 0.304] | 0.305 [0.305, 0.305] | 0.305 [0.305, 0.306] | 0.308 [0.306, 0.310] |
| OPTIMISTIC | 0.472 [0.466, 0.481] | 0.483 [0.476, 0.492] | 0.481 [0.474, 0.490] | 0.473 [0.469, 0.480] |

## 1.7 optimistic inflation relative to FIFO_BACK (seed means), cross rule ON

| L (ms) | PnL OPT - BACK | PnL CA(0.5) - BACK | fills OPT - BACK | fills CA(0.5) - BACK | fills OPT / BACK |
|---|---|---|---|---|---|
| 0 | 21.89 | 0.73 | 9648 | 469 | 2.00x |
| 1 | 21.71 | 0.74 | 9617 | 466 | 2.00x |
| 5 | 21.39 | 0.93 | 9426 | 454 | 1.98x |
| 20 | 20.89 | 1.49 | 8710 | 432 | 1.90x |

## 1.8 all metrics, seed means, cross rule ON

| model | L (ms) | total_pnl | spread_capture | inventory_pnl | fees | markout_1s_bps | fills | fills_per_min | fill_rate | cancels_in_flight_filled | fills_cross | fills_trade_through | cross_share | max_abs_position |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| FIFO_BACK | 0 | -42.745 | -29.087 | -5.797 | 7.861 | -0.188 | 9635.3 | 160.595 | 0.295 | 0.000 | 7338.7 | 0.000 | 0.762 | 21.333 |
| FIFO_BACK | 1 | -42.648 | -29.160 | -5.626 | 7.861 | -0.188 | 9632.7 | 160.550 | 0.296 | 10.000 | 7339.7 | 9.000 | 0.762 | 21.400 |
| FIFO_BACK | 5 | -43.017 | -29.304 | -5.842 | 7.871 | -0.189 | 9643.0 | 160.722 | 0.296 | 56.333 | 7341.7 | 45.000 | 0.761 | 21.700 |
| FIFO_BACK | 20 | -42.371 | -29.914 | -4.537 | 7.920 | -0.194 | 9699.7 | 161.667 | 0.299 | 243.667 | 7374.7 | 181.000 | 0.760 | 21.667 |
| FIFO_CA(0.25) | 0 | -42.475 | -27.996 | -6.444 | 8.035 | -0.169 | 9928.3 | 165.478 | 0.300 | 0.000 | 7305.7 | 0.000 | 0.736 | 21.208 |
| FIFO_CA(0.25) | 1 | -42.820 | -28.044 | -6.742 | 8.034 | -0.169 | 9925.3 | 165.428 | 0.302 | 10.333 | 7307.0 | 9.333 | 0.736 | 21.642 |
| FIFO_CA(0.25) | 5 | -42.617 | -28.249 | -6.329 | 8.038 | -0.173 | 9932.7 | 165.550 | 0.302 | 58.000 | 7313.7 | 45.333 | 0.736 | 21.642 |
| FIFO_CA(0.25) | 20 | -41.505 | -28.967 | -4.461 | 8.078 | -0.179 | 9974.7 | 166.250 | 0.305 | 244.667 | 7352.0 | 182.333 | 0.737 | 21.558 |
| FIFO_CA(0.5) | 0 | -42.019 | -27.303 | -6.559 | 8.158 | -0.159 | 10104.7 | 168.417 | 0.303 | 0.000 | 7289.3 | 0.000 | 0.721 | 21.217 |
| FIFO_CA(0.5) | 1 | -41.912 | -27.371 | -6.385 | 8.156 | -0.160 | 10098.3 | 168.312 | 0.305 | 10.000 | 7288.7 | 9.333 | 0.722 | 21.317 |
| FIFO_CA(0.5) | 5 | -42.086 | -27.533 | -6.398 | 8.155 | -0.162 | 10097.3 | 168.295 | 0.305 | 57.667 | 7290.3 | 45.000 | 0.722 | 21.550 |
| FIFO_CA(0.5) | 20 | -40.877 | -28.286 | -4.409 | 8.182 | -0.170 | 10131.7 | 168.867 | 0.308 | 243.333 | 7328.3 | 181.000 | 0.723 | 21.633 |
| OPTIMISTIC | 0 | -20.858 | 7.318 | -13.917 | 14.259 | 0.125 | 19283.7 | 321.406 | 0.472 | 0.000 | 7328.3 | 0.000 | 0.380 | 21.233 |
| OPTIMISTIC | 1 | -20.935 | 7.145 | -13.842 | 14.237 | 0.123 | 19249.7 | 320.839 | 0.483 | 11.000 | 7324.7 | 9.333 | 0.380 | 21.233 |
| OPTIMISTIC | 5 | -21.632 | 6.706 | -14.228 | 14.109 | 0.121 | 19069.3 | 317.834 | 0.481 | 56.000 | 7282.3 | 42.000 | 0.382 | 21.867 |
| OPTIMISTIC | 20 | -21.479 | 4.916 | -12.778 | 13.617 | 0.111 | 18409.7 | 306.839 | 0.473 | 239.000 | 7130.0 | 167.333 | 0.387 | 22.200 |

# Part 2: cross-fill rule OFF (ablation)

## 2.1 total PnL (currency units), cross rule OFF

| model | L = 0 ms | L = 1 ms | L = 5 ms | L = 20 ms |
|---|---|---|---|---|
| FIFO_BACK | -11.31 [-17.33, -2.99] | -11.63 [-17.45, -3.11] | -12.08 [-17.22, -3.96] | -11.78 [-17.11, -3.12] |
| FIFO_CA(0.25) | -11.26 [-17.01, -5.23] | -11.26 [-16.80, -5.03] | -11.87 [-16.76, -5.75] | -10.11 [-14.92, -2.97] |
| FIFO_CA(0.5) | -10.65 [-16.01, -4.64] | -10.65 [-16.36, -4.34] | -11.62 [-16.74, -5.61] | -9.79 [-14.06, -3.35] |
| OPTIMISTIC | 9.71 [-2.26, 23.30] | 9.51 [-2.58, 23.30] | 8.36 [-4.97, 22.64] | 6.01 [-5.63, 17.59] |

## 2.2 spread capture (currency units), cross rule OFF

| model | L = 0 ms | L = 1 ms | L = 5 ms | L = 20 ms |
|---|---|---|---|---|
| FIFO_BACK | 5.73 [5.62, 5.93] | 5.66 [5.52, 5.85] | 5.44 [5.34, 5.59] | 4.51 [4.41, 4.61] |
| FIFO_CA(0.25) | 6.48 [6.37, 6.66] | 6.40 [6.29, 6.57] | 6.18 [6.06, 6.30] | 5.17 [5.07, 5.28] |
| FIFO_CA(0.5) | 6.99 [6.80, 7.14] | 6.89 [6.69, 7.02] | 6.68 [6.43, 6.81] | 5.65 [5.50, 5.73] |
| OPTIMISTIC | 41.10 [40.79, 41.28] | 40.86 [40.58, 41.04] | 39.99 [39.78, 40.14] | 36.73 [36.47, 37.10] |

## 2.3 qty-weighted 1 s markout (bps), cross rule OFF

| model | L = 0 ms | L = 1 ms | L = 5 ms | L = 20 ms |
|---|---|---|---|---|
| FIFO_BACK | 0.406 [0.369, 0.444] | 0.402 [0.367, 0.440] | 0.370 [0.352, 0.402] | 0.279 [0.253, 0.307] |
| FIFO_CA(0.25) | 0.413 [0.389, 0.445] | 0.405 [0.381, 0.436] | 0.376 [0.360, 0.408] | 0.297 [0.260, 0.332] |
| FIFO_CA(0.5) | 0.416 [0.393, 0.461] | 0.408 [0.386, 0.448] | 0.376 [0.356, 0.411] | 0.297 [0.273, 0.334] |
| OPTIMISTIC | 0.448 [0.434, 0.465] | 0.446 [0.430, 0.462] | 0.439 [0.422, 0.457] | 0.419 [0.406, 0.436] |

## 2.4 fill count, cross rule OFF

| model | L = 0 ms | L = 1 ms | L = 5 ms | L = 20 ms |
|---|---|---|---|---|
| FIFO_BACK | 1993 [1940, 2087] | 2003 [1954, 2087] | 2088 [2027, 2161] | 2369 [2270, 2505] |
| FIFO_CA(0.25) | 2265 [2218, 2359] | 2280 [2237, 2366] | 2355 [2300, 2418] | 2611 [2514, 2742] |
| FIFO_CA(0.5) | 2431 [2379, 2488] | 2445 [2399, 2489] | 2512 [2441, 2547] | 2760 [2698, 2872] |
| OPTIMISTIC | 12025 [11860, 12140] | 12006 [11837, 12125] | 11920 [11757, 12032] | 11563 [11452, 11672] |

## 2.5 share of fills created by the cross rule, cross rule OFF

| model | L = 0 ms | L = 1 ms | L = 5 ms | L = 20 ms |
|---|---|---|---|---|
| FIFO_BACK | 0.000 [0.000, 0.000] | 0.000 [0.000, 0.000] | 0.000 [0.000, 0.000] | 0.000 [0.000, 0.000] |
| FIFO_CA(0.25) | 0.000 [0.000, 0.000] | 0.000 [0.000, 0.000] | 0.000 [0.000, 0.000] | 0.000 [0.000, 0.000] |
| FIFO_CA(0.5) | 0.000 [0.000, 0.000] | 0.000 [0.000, 0.000] | 0.000 [0.000, 0.000] | 0.000 [0.000, 0.000] |
| OPTIMISTIC | 0.000 [0.000, 0.000] | 0.000 [0.000, 0.000] | 0.000 [0.000, 0.000] | 0.000 [0.000, 0.000] |

## 2.6 fill rate (orders with a fill / accepted orders), cross rule OFF

| model | L = 0 ms | L = 1 ms | L = 5 ms | L = 20 ms |
|---|---|---|---|---|
| FIFO_BACK | 0.057 [0.056, 0.060] | 0.058 [0.056, 0.060] | 0.060 [0.059, 0.062] | 0.070 [0.067, 0.073] |
| FIFO_CA(0.25) | 0.065 [0.063, 0.068] | 0.066 [0.064, 0.069] | 0.068 [0.066, 0.070] | 0.077 [0.074, 0.080] |
| FIFO_CA(0.5) | 0.070 [0.068, 0.072] | 0.071 [0.069, 0.072] | 0.073 [0.071, 0.074] | 0.081 [0.079, 0.084] |
| OPTIMISTIC | 0.305 [0.302, 0.307] | 0.313 [0.309, 0.315] | 0.311 [0.308, 0.314] | 0.308 [0.306, 0.310] |

## 2.7 optimistic inflation relative to FIFO_BACK (seed means), cross rule OFF

| L (ms) | PnL OPT - BACK | PnL CA(0.5) - BACK | fills OPT - BACK | fills CA(0.5) - BACK | fills OPT / BACK |
|---|---|---|---|---|---|
| 0 | 21.02 | 0.67 | 10033 | 439 | 6.03x |
| 1 | 21.14 | 0.98 | 10003 | 442 | 5.99x |
| 5 | 20.44 | 0.46 | 9831 | 423 | 5.71x |
| 20 | 17.79 | 1.99 | 9194 | 391 | 4.88x |

## 2.8 all metrics, seed means, cross rule OFF

| model | L (ms) | total_pnl | spread_capture | inventory_pnl | fees | markout_1s_bps | fills | fills_per_min | fill_rate | cancels_in_flight_filled | fills_cross | fills_trade_through | cross_share | max_abs_position |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| FIFO_BACK | 0 | -11.314 | 5.727 | -15.888 | 1.153 | 0.406 | 1992.7 | 33.212 | 0.057 | 0.000 | 0.000 | 0.000 | 0.000 | 21.000 |
| FIFO_BACK | 1 | -11.629 | 5.657 | -16.119 | 1.166 | 0.402 | 2003.3 | 33.390 | 0.058 | 22.333 | 0.000 | 21.333 | 0.000 | 21.100 |
| FIFO_BACK | 5 | -12.078 | 5.439 | -16.297 | 1.220 | 0.370 | 2088.3 | 34.807 | 0.060 | 103.333 | 0.000 | 96.000 | 0.000 | 21.600 |
| FIFO_BACK | 20 | -11.781 | 4.512 | -14.883 | 1.410 | 0.279 | 2368.7 | 39.479 | 0.070 | 425.667 | 0.000 | 397.667 | 0.000 | 22.133 |
| FIFO_CA(0.25) | 0 | -11.265 | 6.479 | -16.411 | 1.332 | 0.413 | 2265.0 | 37.751 | 0.065 | 0.000 | 0.000 | 0.000 | 0.000 | 21.000 |
| FIFO_CA(0.25) | 1 | -11.256 | 6.398 | -16.310 | 1.344 | 0.405 | 2280.3 | 38.007 | 0.066 | 21.667 | 0.000 | 20.667 | 0.000 | 21.000 |
| FIFO_CA(0.25) | 5 | -11.869 | 6.184 | -16.660 | 1.393 | 0.376 | 2355.3 | 39.257 | 0.068 | 102.000 | 0.000 | 93.000 | 0.000 | 21.492 |
| FIFO_CA(0.25) | 20 | -10.106 | 5.175 | -13.711 | 1.571 | 0.297 | 2611.3 | 43.524 | 0.077 | 425.000 | 0.000 | 395.000 | 0.000 | 22.158 |
| FIFO_CA(0.5) | 0 | -10.645 | 6.990 | -16.184 | 1.451 | 0.416 | 2431.3 | 40.524 | 0.070 | 0.000 | 0.000 | 0.000 | 0.000 | 21.000 |
| FIFO_CA(0.5) | 1 | -10.651 | 6.885 | -16.074 | 1.462 | 0.408 | 2445.3 | 40.757 | 0.071 | 21.667 | 0.000 | 20.333 | 0.000 | 21.000 |
| FIFO_CA(0.5) | 5 | -11.620 | 6.678 | -16.791 | 1.508 | 0.376 | 2511.7 | 41.863 | 0.073 | 104.000 | 0.000 | 94.333 | 0.000 | 21.517 |
| FIFO_CA(0.5) | 20 | -9.792 | 5.652 | -13.766 | 1.677 | 0.297 | 2760.0 | 46.002 | 0.081 | 424.667 | 0.000 | 392.333 | 0.000 | 22.017 |
| OPTIMISTIC | 0 | 9.708 | 41.102 | -23.262 | 8.133 | 0.448 | 12025.3 | 200.429 | 0.305 | 0.000 | 0.000 | 0.000 | 0.000 | 21.000 |
| OPTIMISTIC | 1 | 9.510 | 40.860 | -23.228 | 8.122 | 0.446 | 12006.0 | 200.107 | 0.313 | 23.667 | 0.000 | 22.667 | 0.000 | 21.200 |
| OPTIMISTIC | 5 | 8.364 | 39.993 | -23.564 | 8.064 | 0.439 | 11919.7 | 198.668 | 0.311 | 109.333 | 0.000 | 103.000 | 0.000 | 21.667 |
| OPTIMISTIC | 20 | 6.010 | 36.730 | -22.911 | 7.808 | 0.419 | 11562.7 | 192.718 | 0.308 | 433.000 | 0.000 | 406.667 | 0.000 | 22.000 |

Attribution note: `pta` marks each fill against the market row at the fill's own timestamp. For a
CROSS fill that row is already the moved market, so the loss of a stale quote shows up in spread
capture (negative edge against the post-move mid), not in inventory PnL.

The market maker's fills change its position, skew and pulls, so its results need not follow the
queue-model ordering (DESIGN 3.6); the tables report what it actually does. The cancel-ahead
fraction f is not identifiable from these data.

## Statement

> These numbers come from a synthetic market (Project 3's generator on Project 1's price
> path) and a backtester with no market impact. Their sizes are properties of that
> generator: its trade rate, its random re-sizing of book levels, and its touch moves that
> delete and re-add levels without printing trades. They do not transfer to any real
> venue. The direction of the effect (optimistic fill assumptions raise fill counts and
> backtest PnL) is the finding; the magnitudes are not. A backtest is not live PnL: it
> cannot reproduce real queue dynamics, how other participants react to our orders, or
> real latency distributions. Senior HFT roles ask for a live track record, and no project
> here substitutes for one.
