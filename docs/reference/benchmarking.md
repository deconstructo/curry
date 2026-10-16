# Benchmarking and Real-Time Profiling

Curry ships a full benchmarking stack: Scheme benchmark suites that measure
allocation patterns and GC behaviour, a real-time telemetry pipeline that
streams results into InfluxDB, and a Grafana dashboard that shows everything
live as the benchmarks run.

---

## Prerequisites

The benchmark suites require the MQTT module (used to stream results):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_MODULE_MQTT=ON
cmake --build build -j$(nproc)
```

If `BUILD_MODULE_MQTT=OFF`, the benchmarks still run and print JSON to
stderr — MQTT telemetry is silently skipped.

---

## Quick start (with live Grafana dashboard)

```bash
# 1. Start the observability stack (downloads images on first run, ~1 min)
cd tools/bench-stack
docker compose up -d
cd ../..

# 2. Wait ~10 s for InfluxDB to initialise, then run the standard suite
./build/curry tests/bench.scm

# 3. Open the dashboard
open http://localhost:3000          # admin / admin
# → Dashboards → Curry Benchmarks → Curry Bench

# 4. Compare GC settings in another terminal while watching Grafana
GC_MARKERS=1 ./build/curry tests/bench.scm

# 5. Tear down when done
docker compose -f tools/bench-stack/docker-compose.yml down
```

Results appear in Grafana within 5 seconds of each benchmark completing.

---

## Benchmark suites

### `tests/bench.scm` — standard suite

The everyday benchmark.  Calibrated to complete in under 5 minutes.

```bash
./build/curry tests/bench.scm                        # all suites
./build/curry tests/bench.scm --suite throughput     # CPU throughput only (~90 s)
./build/curry tests/bench.scm --suite gc             # GC behaviour only (~3 min)
./build/curry tests/bench.scm --suite actors         # actor concurrency (~1 min)
```

**Throughput suite** — same workloads as `vm_bench.scm`, run under both VM
and tree-walker evaluators:

| Benchmark | What it measures |
|-----------|-----------------|
| `fib(28)/vm` | Pure recursion, no allocation — CPU and call overhead |
| `fib(28)/tw` | Same via tree-walker — evaluator overhead comparison |
| `tak(18,12,6)/vm` | Tak benchmark — non-tail recursion, stack depth |
| `tak(18,12,6)/tw` | Same via tree-walker |
| `count-down(1M)/vm` | Tight tail-recursive loop — TCO quality |
| `count-down(1M)/tw` | Same via tree-walker |

**GC suite** — allocation patterns designed to isolate specific GC
properties:

| Benchmark | What it measures |
|-----------|-----------------|
| `alloc-short` | 500 K cons cells, all die before next GC — short-lived allocation throughput |
| `alloc-medium` | 10 K 3-element lists, hold briefly then release — promotion and remembered-set cost |
| `alloc-large` | 100 × 1 K-element vectors — large-object path |
| `mutation` | 10 K-element vector, 200 K `vector-set!` calls — write-barrier cost in isolation |
| `mixed` | Mini-interpreter evaluating 5 K expressions — realistic allocation + survival mix |

**Actors suite:**

| Benchmark | What it measures |
|-----------|-----------------|
| `actor-ring/8x100` | 8 actors passing a list around a ring for 100 rounds — cross-thread allocation and mailbox write-barrier |

---

### `tests/bench_heavy.scm` — punishment suite

Calibrated for 2–15 seconds per benchmark.  Designed to make the GC work
hard and expose pressure points under sustained load.

```bash
./build/curry tests/bench_heavy.scm                  # all phases
./build/curry tests/bench_heavy.scm --suite gc       # GC suite only
./build/curry tests/bench_heavy.scm --label worker-1 # tag results in Grafana
```

Heavy GC benchmarks:

| Benchmark | Scale vs. standard |
|-----------|-------------------|
| `alloc-short/5M` | 10× more allocations |
| `alloc-medium/200K` | 20× more lists |
| `alloc-large/10K` | 100× more vectors |
| `mutation/10M` | 50× more mutations |
| `mixed/50K` | 10× more iterations |
| `retained/100K+5M` | Builds 100 K retained objects then hammers 5 M short-lived ones — tests collection cost with a large live set alongside heavy short-lived allocation |
| `actor-ring/32x500` | 32 actors, 500 rounds — heavy cross-thread GC pressure |

---

### `tests/bench_torture.scm` — multi-phase torture test

Extreme allocation scenarios, each phase targeting a different failure mode.
Intended for pre-release soak testing, not routine runs.

```bash
./build/curry tests/bench_torture.scm                # all phases
./build/curry tests/bench_torture.scm --phase heap   # single phase
```

---

### `tools/punish.sh` — parallel punishment run

Runs `bench_heavy.scm` in parallel across N workers, looping until
interrupted.  All workers publish to the same MQTT topic so the dashboard
shows the aggregate load.

```bash
tools/punish.sh                    # 4 workers, all suites, infinite
tools/punish.sh -j 8               # 8 workers
tools/punish.sh -j 2 -s gc         # 2 workers, GC suite only
tools/punish.sh -n 3               # stop after 3 complete rounds
```

Each worker is tagged `worker-N` in Grafana so results can be separated per
worker if needed.

---

## Reading results from the terminal

Even without the Docker stack, every benchmark prints a one-line summary to
stdout:

```
fib(28)/vm                    mean=178ms  p99=181ms  gc_stw=0
alloc-short                   mean=42ms   p99=45ms   gc_stw=3
actor-ring/8x100              mean=23ms   p99=25ms   gc_stw=0
```

Fields: `mean` and `p99` wall time per iteration, `gc_stw` number of
stop-the-world GC pauses across all iterations of that benchmark.

---

## Observability stack

### Architecture

```
bench.scm / bench_heavy.scm
  └─ (mqtt-publish) ──→ mosquitto:1883
                           └─ Telegraf (mqtt_consumer)
                                └─ InfluxDB:8086  bucket: benchmarks
                                     └─ Grafana:3000  (auto-provisioned)
```

### Services and credentials

| Service | URL | Login |
|---------|-----|-------|
| Grafana | http://localhost:3000 | admin / admin |
| InfluxDB | http://localhost:8086 | admin / currybench |
| Mosquitto | localhost:1883 | anonymous |

InfluxDB token: `curry-bench-token`  
InfluxDB org: `curry`  
InfluxDB bucket: `benchmarks`

### Grafana dashboard panels

Open **Dashboards → Curry Benchmarks → Curry Bench**.  The dashboard
auto-refreshes every 5 seconds.

| Panel | What it shows |
|-------|--------------|
| Mean time per benchmark | Time-series lines, one per benchmark/gc/mode combination.  Lets you watch how a benchmark's mean evolves across consecutive runs. |
| p99 latency over time | Tail latency per benchmark. |
| GC pause count per run | Bar chart: how many stop-the-world GC pauses happened during each benchmark run. |
| Max GC pause (gauge) | Running maximum single-pause time in µs.  Red threshold at 5 ms. |
| GC p99 pause (gauge) | p99 from the pause ring (last 1024 pauses) — more representative than the max. |
| Major GC count per run | Boehm collections per benchmark run. |
| Latest results table | Most recent results, side by side per `gc` tag, with ratio column. |
| Pause histogram | Live histogram of GC pause times from the pause ring, updated each run. |

### MQTT event schema

Every event is a JSON object published to `curry/bench/events`.

**Common fields** (all event types):

```json
{
  "event":        "start" | "result",
  "benchmark":    "alloc-short",
  "gc":           "boehm",
  "mode":         "vm" | "tw",
  "iterations":   10,
  "timestamp_ms": 1719446400000
}
```

**`result` events** add timing and GC statistics:

```json
{
  "mean":             42.3,
  "stddev":           1.2,
  "p50":              42.0,
  "p95":              44.8,
  "p99":              45.1,
  "min":              40.1,
  "max":              46.3,
  "gc_p99_us":        18,
  "gc_stw_count":     3,
  "gc_major_count":   3,
  "gc_stw_total_us":  1850,
  "gc_stw_max_us":    720,
  "gc_heap_bytes":    9125888
}
```

All time values are in milliseconds unless suffixed `_us` (microseconds).

You can subscribe to this topic directly with any MQTT client to build your
own consumers:

```bash
mosquitto_sub -h localhost -p 1883 -t 'curry/bench/events' | jq .
```

---

## Writing your own benchmark

Use `(gc-stats)` and `(gc-stats-reset!)` to measure GC behaviour around any
piece of code:

```scheme
(gc-stats-reset!)

(let loop ((i 100000))
  (when (> i 0)
    (cons i i)
    (loop (- i 1))))

(let ((s (gc-stats)))
  (display (assoc 'stw-count   s)) (newline)
  (display (assoc 'stw-max-ns  s)) (newline)
  (display (assoc 'stw-ring-ns s)) (newline))
```

This shows how many stop-the-world pauses happened and how long they were.

See [`gc.md`](gc.md) for the full `(gc-stats)` field reference.

---

## Comparing GC settings

Boehm reads its settings from environment variables, so the same suite can
be compared across settings without rebuilding:

```bash
./build/curry tests/bench.scm --suite gc                              > default.txt
GC_MARKERS=1 ./build/curry tests/bench.scm --suite gc                 > markers1.txt
GC_ENABLE_INCREMENTAL=1 GC_PAUSE_TIME_TARGET=5 \
  ./build/curry tests/bench.scm --suite gc                            > incr.txt
```

See [`gc.md`](gc.md#pause-times) for measured trade-offs between these
settings.
