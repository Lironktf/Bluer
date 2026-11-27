## Health summary 2026-11-27
Data: Wed 11-25 13:57 to Fri 11-27 13:57 (Waterloo time). Problems: 0 (no alert).

### Verdict
No problems found.

### Problems and what to do
None.

### Connection
- Delivered reports ranged from 97.7% to 99.4% across the pairs.
- Pair 1 was the worst pair at 97.7% delivered.
- Pair 1 had 12 gaps of 2+ minutes, with the most at 19:00 (5), 16:00 (2), and 18:00 (2). Pair 2 had 2 gaps of 2+ minutes, with 1 at 17:00 and 1 at 21:00.
- Neither pair was silent.

### Cycles and detection
- The busiest machines were sj-d2 and sj-w2 with 13 cycles each. The quietest machines were sj-d1 with 8 cycles and sj-w1 with 9 cycles.
- Typical dryer median cycle lengths were 50 minutes for sj-d1 and 45 minutes for sj-d2. Typical washer median cycle lengths were 36 minutes for sj-w1 and 37 minutes for sj-w2.
- While idle, sj-d1 reached a high reading of 3.9 against its start threshold of 2.5 (27 reports near threshold), and sj-w1 reached 115459 against 120000 (1 report near threshold). For Pair 2, idle sj-d2 reached 9.9 against 4.51 (15 reports near threshold), and idle sj-w2 reached 94902 against 100000 (1 report near threshold).
- Worth watching: sj-d1: 2 false start(s) in 2 days (ran under 2 min, not counted as cycles) at Thu 11-26 08:10, Fri 11-27 12:22; 1 of them while sj-w1 was running.
- Worth watching: sj-w1: 1 cycle(s) ran to the 42 min hard cap at Thu 11-26 20:20.
- Worth watching: sj-d2: 7 false start(s) in 2 days (ran under 2 min, not counted as cycles) at Wed 11-25 14:26, Wed 11-25 22:04, Thu 11-26 12:20, Thu 11-26 15:12, Thu 11-26 16:44, Fri 11-27 10:44, Fri 11-27 12:00; 7 of them while sj-w2 was running.
- Worth watching: sj-w2: 1 cycle(s) ran to the 42 min hard cap at Thu 11-26 20:31.

### Laundry room usage
- Based on 4 monitored machines from Mon 11-23 13:57 to Fri 11-27 13:57 (4 days).
- The busiest times of day were 20:00-21:00 (1.35 of 4 machines running on average), 11:00-12:00 (1.33 running), and 12:00-13:00 (1.25 running).
- The quietest daytime times between 08:00 and 23:00 were 09:00-10:00 (0.09 of 4 machines running on average), 08:00-09:00 (0.25 running), and 22:00-23:00 (0.52 running).
- Among full days, the busiest was Thu 11-26 with 24 cycles started (busiest hour 20:00-21:00), and the quietest was Tue 11-24 with 9 cycles started (busiest hour 20:00-21:00). Mon 11-23 (7 cycles started) and Fri 11-27 (9 cycles started) were only partly covered.

### Board health
- Boards had 3 planned restarts (1 on Pair 1, 2 on Pair 2) and 0 unplanned restarts. Pair 1 had 1 website-unreachable reset.
- All sensors reported ok.
- Lowest free memory was 83 KB on Pair 2.

### Trend since last summary
First summary, no trend yet.

<details><summary>Numbers</summary>

| Pair | Reports arrived | Gaps 2+ min | Longest gap | Unplanned restarts | Website resets | Dryer cycles | Washer cycles | Washer 25:00 / 42:00 ends |
|---|---|---|---|---|---|---|---|---|
| 1 | 97.7% | 12 | 10.3 min | 0 | 1 | 8 | 9 | 0 / 1 of 10 |
| 2 | 99.4% | 2 | 2.3 min | 0 | 0 | 13 | 13 | 0 / 1 of 10 |

</details>

<!-- health-stats {"1":{"deliveredPct":97.7,"gaps2min":12,"unplannedRestarts":0,"dryerCycles":8,"washerCycles":9},"2":{"deliveredPct":99.4,"gaps2min":2,"unplannedRestarts":0,"dryerCycles":13,"washerCycles":13}} -->
