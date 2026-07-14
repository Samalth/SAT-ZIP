# SAT-ZIP benchmarks

Run: `cmake --build build --config Release --target benchmark && ./build/Release/benchmark [file]`

Methodology: 64 KiB blocks, best-of-25 passes (min time), throughput vs input bytes,
per-block state reset timed (independent-block semantics, matches LZ4 clearing its
context per call), decode correctness re-verified before any number is reported.
LZ4 v1.9.4 fetched at configure time into `build/_lz4` (not vendored into the repo).

Numbers are machine-specific (MSVC 19.50 /O2 /arch:AVX2, x86_64). Track *deltas*, not absolutes.

## Baseline — Mode S (commit: harness landed)
| Corpus  | SAT-ZIP comp/decomp MB/s | ratio | LZ4 comp/decomp MB/s | ratio |
|---------|--------------------------|-------|----------------------|-------|
| text    | 800 / 1723               | 3.20  | 677 / 1511           | 1.79  |
| mixed   | 740 / 1380               | 2.90  | 988 / 2363           | 2.94  |
| zeros   | 12784 / 1107             | 234.9 | 11445 / 6494         | 245.5 |
| random  | 536 / 18785              | 1.00  | 6562 / 18979         | 1.00  |

## After findings #1 + #2 (skip-acceleration + small-offset pattern copy)
| Corpus  | SAT-ZIP comp/decomp MB/s | ratio | LZ4 comp/decomp MB/s | ratio | vs baseline |
|---------|--------------------------|-------|----------------------|-------|-------------|
| text    | 820 / 2229               | 3.20  | 683 / 1552           | 1.79  | decode +29% |
| mixed   | 831 / 2622               | 2.89  | 970 / 2385           | 2.94  | decode +90%, now > LZ4 |
| zeros   | 14507 / 6592             | 234.9 | 10726 / 6622         | 245.5 | decode +495%, now ~LZ4 |
| random  | 4707 / 19845             | 1.00  | 6742 / 20692         | 1.00  | compress +778% |

Decode now wins or ties LZ4 on all four corpora. Remaining gaps are compress-side:
`mixed` 831 vs 970 (1.17x) and `random` 4707 vs 6742 (1.43x).

## Findings
1. **[DONE]** Small-offset decode pattern-expansion — RLE decode 1107 -> 6592 (~LZ4).
2. **[DONE]** Compress skip-acceleration — incompressible 536 -> 4707 (12x gap -> 1.4x).
3. **[PARTIAL]** Ordinary-match decode now beats LZ4 (mixed 2622 vs 2385) as a side
   effect of #1 + relaxed match-copy guard. Wider match copy still possible.

### Next backlog (compress-side)
- Lazy/second-position match check to lift `mixed` compress without hurting decode.
- Tune skip trigger / cheaper per-attempt hash to close the `random` compress gap.
- Then Mode R (SIMD gather) and tANS.
