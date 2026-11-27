## Health summary 2026-11-27
Data: Wed 11-25 16:01 to Fri 11-27 16:01 (Waterloo time). Problems: 0 (no alert).

### Verdict
No problems found.

### Problems and what to do
None.

### Connection
- Delivered reports ranged from 97.9% to 99.4% across the pairs, with pair 1 being the lowest at 97.9%.
- Pair 1 had 11 gaps of 2+ minutes, with the most during 19:00 (5), 16:00 (2), and 18:00 (2).
- Pair 2 had 2 gaps of 2+ minutes, occurring at 17:00 (1) and 21:00 (1).
- No pairs were silent.

### Cycles and detection
- Washer sj-w2 was the busiest machine with 14 cycles, while dryer sj-d1 was the quietest with 7 cycles.
- Median cycle lengths were 45 to 50 minutes for dryers and 36 to 37 minutes for washers.
- While idle, dryer sj-d1's highest reading was 2.5, reaching the 2.5 start threshold, and 27 reports came within 15% of it. Dryer sj-d2 reached 4.4, just under the 4.51 threshold, with 12 reports within 15% of it. Washers sj-w1 and sj-w2 reached highest idle readings of 115459 and 94902 against thresholds of 120000 and 100000, each with 1 report within 15% of its threshold.
- Worth watching: sj-d1: 2 false start(s) in 2 days (ran under 2 min, not counted as cycles) at Thu 11-26 08:10, Fri 11-27 12:22; 1 of them while neighbour sj-w1 was running.
- Worth watching: sj-w1: 1 cycle(s) ran to the 42 min hard cap at Thu 11-26 20:20 (sj-w2 also running).
- Worth watching: sj-d2: 7 false start(s) in 2 days (ran under 2 min, not counted as cycles) at Wed 11-25 22:04, Thu 11-26 12:20, Thu 11-26 15:12, Thu 11-26 16:44, Fri 11-27 10:44, Fri 11-27 12:00, Fri 11-27 15:51; 7 of them while neighbour sj-w2 was running.
- Worth watching: sj-w2: 1 cycle(s) ran to the 42 min hard cap at Thu 11-26 20:31 (sj-w1 also running).

### Laundry room usage
- Based on 4 monitored machines from Mon 11-23 16:01 to Fri 11-27 16:01 (4 days).
- The busiest times of day were 20:00-21:00 (1.35 of 4 machines running on average), 11:00-12:00 (1.33 of 4 machines running on average), and 12:00-13:00 (1.25 of 4 machines running on average).
- The quietest daytime hours between 08:00 and 23:00 were 09:00-10:00 (0.09 of 4 machines running on average), 08:00-09:00 (0.25 of 4 machines running on average), and 16:00-17:00 (0.48 of 4 machines running on average).
- Thu 11-26 was the busiest full day with 24 cycles started (busiest hour 20:00-21:00), and Tue 11-24 was the quietest full day with 9 cycles started (busiest hour 20:00-21:00). Mon 11-23 (1 cycle) and Fri 11-27 (12 cycles) were only partly covered.

### Board health
- Each pair had 1 planned restart and 0 unplanned restarts; pair 1 had 1 website-unreachable reset.
- All sensors on all monitored machines are ok.
- Lowest free memory was 83 KB on pair 2 and 88 KB on pair 1.

### Trend since last summary
First summary, no trend yet.

<details><summary>Numbers</summary>

| Pair | Reports arrived | Gaps 2+ min | Longest gap | Unplanned restarts | Website resets | Dryer cycles | Washer cycles | Washer 25:00 / 42:00 ends |
|---|---|---|---|---|---|---|---|---|
| 1 | 97.9% | 11 | 10.3 min | 0 | 1 | 7 | 10 | 0 / 1 of 10 |
| 2 | 99.4% | 2 | 2.3 min | 0 | 0 | 12 | 14 | 0 / 1 of 10 |

</details>

<!-- health-stats {"1":{"deliveredPct":97.9,"gaps2min":11,"unplannedRestarts":0,"dryerCycles":7,"washerCycles":10},"2":{"deliveredPct":99.4,"gaps2min":2,"unplannedRestarts":0,"dryerCycles":12,"washerCycles":14}} -->
<!-- health-events [{"id":"sj-w1","kind":"cap","end":"2026-11-27T01:20:06.607Z","with":["sj-w2"]},{"id":"sj-w2","kind":"cap","end":"2026-11-27T01:31:38.347Z","with":["sj-w1"]}] -->
