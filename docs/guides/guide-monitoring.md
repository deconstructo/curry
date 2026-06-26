# Monitoring Curry programs: GC instrumentation, benchmarks, and live dashboards

This guide covers how to measure GC behaviour in your own code, run the real-time
monitoring stack, extend Grafana with your own panels, and run the entire stack
without Docker using Apple's native container runtime.

For the field-level reference — every `(gc-stats)` key, every MQTT field, every
Grafana panel — see [`docs/reference/benchmarking.md`](../reference/benchmarking.md).
For the profiling API see [`docs/reference/profiling.md`](../reference/profiling.md).

---

## Part 1 — Measuring GC behaviour in your own code

### The `(gc-stats)` primitive

`(gc-stats)` returns a snapshot of the current GC counters as an association list.
`(gc-stats-reset!)` zeros them so consecutive runs can be compared cleanly.

```scheme
;; Measure GC activity around any expression
(gc-stats-reset!)

(let loop ((i 500000))
  (when (> i 0)
    (cons i (cons i '()))    ; allocate two cells per iteration
    (loop (- i 1))))

(let ((s (gc-stats)))
  (display "minor GC count : ") (display (cdr (assq 'minor-count   s))) (newline)
  (display "minor GC max µs: ") (display (cdr (assq 'minor-max-us  s))) (newline)
  (display "major GC count : ") (display (cdr (assq 'major-count   s))) (newline)
  (display "heap bytes     : ") (display (cdr (assq 'heap-size-bytes s))) (newline))
```

Under `--gc boehm` (the default), `minor-count` is always 0 and `minor-max-us`
is always 0 — Boehm has no minor/major split; only `major-count` and
`heap-size-bytes` are meaningful.

Under `--gc generational`, all fields are live.

### Reading the pause ring

The `pause-ring` field is a vector of the last 256 minor GC pause times in
microseconds (circular, oldest first). Use it to compute percentiles without
retaining the full history:

```scheme
(define (percentile vec p)
  (let* ((sorted (vector->list vec))
         (sorted (list-sort < (filter positive? sorted)))
         (n      (length sorted))
         (idx    (inexact->exact (floor (* p (- n 1))))))
    (if (zero? n) 0 (list-ref sorted idx))))

(gc-stats-reset!)
(run-my-workload)

(let* ((s    (gc-stats))
       (ring (cdr (assq 'pause-ring s))))
  (display "p50 minor pause: ") (display (percentile ring 0.50)) (display " µs") (newline)
  (display "p95 minor pause: ") (display (percentile ring 0.95)) (display " µs") (newline)
  (display "p99 minor pause: ") (display (percentile ring 0.99)) (display " µs") (newline))
```

### When to use `(gc-stats)` vs `(curry profiling)`

Use `(gc-stats)` when you want to know whether a workload is GC-bound and what
the collector is doing.  Use `(curry profiling)` (see
[profiling.md](../reference/profiling.md)) when you want to know which
*functions* are consuming time.

A typical workflow: profile first to find the hot path, then use `(gc-stats)` to
determine whether the hot path is CPU-bound (minor-count = 0) or GC-bound
(minor-count > 0 or major-count rising).

### Comparing GC backends on your own workload

```bash
# Run under Boehm (default), capture terminal output
./build/curry my-workload.scm > boehm-stats.txt 2>&1

# Run under generational GC
./build/curry --gc generational my-workload.scm > gen-stats.txt 2>&1

diff boehm-stats.txt gen-stats.txt
```

Key observations to make:
- **Generational wins** when most allocations are short-lived (list processing,
  iterative tree transforms, CAS expression rewrites).
- **Boehm wins** when the live set is large and stable, or when the code is
  bignum-heavy (GMP integers live outside the nursery).
- When `minor-count` is high and `minor-max-us` is low (< 1 ms), the generational
  backend is well-matched to your workload.
- When `major-count` is high under generational, the old generation is filling up:
  consider `--gc-nursery-size 2M` to promote objects less aggressively.

---

## Part 2 — Starting the monitoring stack

### Option A: Docker Compose

The fastest path on any platform.

```bash
# Start all four services in the background
cd tools/bench-stack
docker compose up -d

# Wait for InfluxDB to finish its one-time initialisation
sleep 10

# Run benchmarks (from the repo root)
cd ../..
./build/curry tests/bench.scm

# Open the dashboard
open http://localhost:3000    # admin / admin
# → Dashboards → Curry Benchmarks → Curry Bench

# Stop and remove containers (data volumes are preserved)
docker compose -f tools/bench-stack/docker-compose.yml down

# Also remove the volumes (wipes all stored data)
docker compose -f tools/bench-stack/docker-compose.yml down -v
```

### Option B: Apple Containers (macOS 26+, Apple Silicon)

Apple's native container runtime (`container` CLI, available in macOS 26+)
runs OCI images directly on the Virtualization.framework with no Docker Desktop
needed.  The CLI is similar to Docker's, but each container is an independent
lightweight VM — there is no Compose equivalent and no shared DNS-based
inter-container networking.

The approach below uses host-port binding for each service and the
`host.containers.internal` hostname (which resolves to the host machine from
inside any container) for inter-service communication.

#### Step 1 — Install the `container` CLI

The `container` command ships with macOS 26 developer tools.  Verify:

```bash
container version
```

If not found, install the Command Line Tools package:

```bash
xcode-select --install
```

#### Step 2 — Pull images

```bash
container pull eclipse-mosquitto:2
container pull influxdb:2
container pull telegraf:latest
container pull grafana/grafana:latest
```

#### Step 3 — Start Mosquitto

```bash
container run -d \
  --name curry-mosquitto \
  -p 1883:1883 \
  -v "$(pwd)/tools/bench-stack/mosquitto.conf:/mosquitto/config/mosquitto.conf" \
  eclipse-mosquitto:2
```

Verify: `container logs curry-mosquitto` should show `mosquitto version 2.x starting`.

#### Step 4 — Start InfluxDB

```bash
container run -d \
  --name curry-influxdb \
  -p 8086:8086 \
  -e DOCKER_INFLUXDB_INIT_MODE=setup \
  -e DOCKER_INFLUXDB_INIT_USERNAME=admin \
  -e DOCKER_INFLUXDB_INIT_PASSWORD=currybench \
  -e DOCKER_INFLUXDB_INIT_ORG=curry \
  -e DOCKER_INFLUXDB_INIT_BUCKET=benchmarks \
  -e DOCKER_INFLUXDB_INIT_ADMIN_TOKEN=curry-bench-token \
  influxdb:2
```

Wait for it to be ready (the init script runs a one-time setup):

```bash
until container exec curry-influxdb influx ping --skip-verify 2>/dev/null; do
  sleep 2
done
echo "InfluxDB ready"
```

#### Step 5 — Start Telegraf with a host-networking config

Telegraf needs to reach Mosquitto and InfluxDB.  Since these are sibling
containers, not hosts on the same Docker network, they are only reachable via
the host machine.  Create an Apple-containers-specific Telegraf config:

```bash
mkdir -p /tmp/curry-bench

cat > /tmp/curry-bench/telegraf.conf << 'TOML'
[agent]
  interval            = "2s"
  round_interval      = true
  flush_interval      = "5s"

[[inputs.mqtt_consumer]]
  servers   = ["tcp://host.containers.internal:1883"]
  topics    = ["curry/bench/events"]
  client_id = "telegraf-curry-bench"
  data_format = "json_v2"

  [[inputs.mqtt_consumer.json_v2]]
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "mean"             type = "float"   optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "p99"              type = "float"   optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "gc_minor_count"   type = "int"     optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "gc_minor_max_us"  type = "int"     optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "gc_p99_us"        type = "int"     optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "gc_major_count"   type = "int"     optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "gc_heap_bytes"    type = "int"     optional = true
    [[inputs.mqtt_consumer.json_v2.field]]
      path = "gc_nursery_used"  type = "int"     optional = true
    [[inputs.mqtt_consumer.json_v2.tag]]
      path = "event"
    [[inputs.mqtt_consumer.json_v2.tag]]
      path = "benchmark"
    [[inputs.mqtt_consumer.json_v2.tag]]
      path = "gc"
    [[inputs.mqtt_consumer.json_v2.tag]]
      path = "mode"

[[outputs.influxdb_v2]]
  urls         = ["http://host.containers.internal:8086"]
  token        = "curry-bench-token"
  organization = "curry"
  bucket       = "benchmarks"
TOML

container run -d \
  --name curry-telegraf \
  -v /tmp/curry-bench/telegraf.conf:/etc/telegraf/telegraf.conf:ro \
  telegraf:latest
```

#### Step 6 — Start Grafana with a host-networking datasource

The provisioned datasource points to `http://influxdb:8086`, which only works
inside the Docker Compose network.  Create an override that uses the host
address instead:

```bash
mkdir -p /tmp/curry-bench/provisioning/datasources
mkdir -p /tmp/curry-bench/provisioning/dashboards

cat > /tmp/curry-bench/provisioning/datasources/influxdb.yaml << 'YAML'
apiVersion: 1
datasources:
  - name: InfluxDB
    type: influxdb
    access: proxy
    url: http://host.containers.internal:8086
    jsonData:
      version: Flux
      organization: curry
      defaultBucket: benchmarks
    secureJsonData:
      token: curry-bench-token
    isDefault: true
    editable: true
YAML

# Copy the dashboard JSON and the provider config from the repo
cp tools/bench-stack/grafana/provisioning/dashboards/curry-bench.json \
   /tmp/curry-bench/provisioning/dashboards/
cp tools/bench-stack/grafana/provisioning/dashboards/provider.yaml \
   /tmp/curry-bench/provisioning/dashboards/

container run -d \
  --name curry-grafana \
  -p 3000:3000 \
  -e GF_SECURITY_ADMIN_PASSWORD=admin \
  -e GF_USERS_ALLOW_SIGN_UP=false \
  -v /tmp/curry-bench/provisioning:/etc/grafana/provisioning:ro \
  grafana/grafana:latest
```

Open `http://localhost:3000` (admin / admin) and navigate to
**Dashboards → Curry Benchmarks → Curry Bench**.

#### Stopping and cleaning up

```bash
container stop curry-grafana curry-telegraf curry-influxdb curry-mosquitto
container rm   curry-grafana curry-telegraf curry-influxdb curry-mosquitto
rm -rf /tmp/curry-bench
```

#### Apple vs Docker comparison

| Feature | Docker Compose | Apple Containers |
|---------|---------------|-----------------|
| Setup | One `docker compose up -d` | Four separate `container run` commands |
| Inter-service DNS | Built-in (service names work) | Not available — use `host.containers.internal` |
| Config sharing | `docker-compose.yml` mounts volumes | Separate configs in `/tmp/curry-bench/` |
| Performance | Runs via hypervisor | Native Virtualization.framework — lower overhead |
| Data persistence | Named Docker volumes | Not covered above — add `-v /path/on/host:/data` per service |
| Requires Docker Desktop | Yes | No — built into macOS 26 |

---

## Part 3 — Customizing Grafana dashboards

### Anatomy of a Flux query

Every panel in the Curry Bench dashboard uses InfluxDB's Flux query language.
A typical panel query looks like this:

```flux
from(bucket: "benchmarks")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "mqtt_consumer")
  |> filter(fn: (r) => r["event"] == "result")
  |> filter(fn: (r) => r["_field"] == "mean")
  |> aggregateWindow(every: v.windowPeriod, fn: last, createEmpty: false)
  |> yield(name: "mean")
```

To edit a panel: click the panel title → **Edit** → select the **Query** tab.
The query editor has an **Explain** button that describes what each pipe stage does.

### Adding a new metric to an existing panel

Suppose you want to overlay `p50` latency alongside `mean` on the existing
time-series panel.  Duplicate the query (click **+ Add query**) and change
`r["_field"] == "mean"` to `r["_field"] == "p50"`.  Give the new series a
different alias under **Options → Legend**.

### Adding a new panel

1. Open the dashboard → click **Add** (top-right) → **Add visualization**.
2. Choose **InfluxDB** as the data source.
3. Start from this minimal template and adapt:

```flux
from(bucket: "benchmarks")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "mqtt_consumer")
  |> filter(fn: (r) => r["event"] == "result")
  |> filter(fn: (r) => r["_field"] == "gc_nursery_used")
  |> group(columns: ["benchmark", "gc"])
  |> aggregateWindow(every: v.windowPeriod, fn: last, createEmpty: false)
```

4. Choose a visualization type (Time series, Bar chart, Gauge, Stat, Table).
5. Click **Apply** to save the panel to the dashboard.

### Filtering to a specific benchmark

Add a filter stage after the measurement filter:

```flux
  |> filter(fn: (r) => r["benchmark"] == "alloc-short")
```

Or use a Grafana variable (Dashboard settings → Variables → Add variable) to
let users select a benchmark from a dropdown:

```flux
  |> filter(fn: (r) => r["benchmark"] == "${benchmark_var}")
```

### Comparing boehm vs generational on one panel

Group by the `gc` tag so each backend gets its own series:

```flux
from(bucket: "benchmarks")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "mqtt_consumer")
  |> filter(fn: (r) => r["event"] == "result")
  |> filter(fn: (r) => r["_field"] == "mean")
  |> group(columns: ["benchmark", "gc"])
  |> aggregateWindow(every: v.windowPeriod, fn: last, createEmpty: false)
```

The legend will show `alloc-short / boehm` and `alloc-short / generational` as
separate lines.

### Computing a ratio column in a table panel

Use Flux's `join` transformation to put both backends on the same row:

```flux
boehm = from(bucket: "benchmarks")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "mqtt_consumer"
            and r["event"] == "result"
            and r["_field"] == "mean"
            and r["gc"] == "boehm")
  |> last()

gen = from(bucket: "benchmarks")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "mqtt_consumer"
            and r["event"] == "result"
            and r["_field"] == "mean"
            and r["gc"] == "generational")
  |> last()

join(tables: {boehm: boehm, gen: gen}, on: ["benchmark"])
  |> map(fn: (r) => ({r with ratio: r._value_gen / r._value_boehm}))
```

The **Transformation** tab in the panel editor (not the query tab) also lets
you add calculated fields without writing Flux — useful for simple ratios.

### Persisting dashboard changes

Grafana stores dashboard edits only in its internal database by default.  To
commit your changes back to the repo:

1. Open the dashboard → **Dashboard settings** (gear icon) → **JSON Model**.
2. Copy the entire JSON.
3. Replace `tools/bench-stack/grafana/provisioning/dashboards/curry-bench.json`
   with the copied JSON.
4. Commit.

On the next `docker compose up -d` (or Apple containers run), Grafana reloads
the dashboard from the provisioned file automatically.

> **Note**: When a dashboard is provisioned from a file, the **Save** button in
> the Grafana UI is disabled — changes made in the browser are ephemeral.  This
> is intentional: it prevents an interactive edit from diverging silently from
> the committed version.  To make the **Save** button available for interactive
> editing, change `editable: false` to `editable: true` in
> `tools/bench-stack/grafana/provisioning/datasources/influxdb.yaml`, or
> temporarily set `GF_DASHBOARDS_ALLOW_UI_UPDATES_FROM_VIEWERS=true` in the
> Grafana environment.

---

## Part 4 — Publishing your own metrics to Grafana

Any Curry program can publish arbitrary metrics to the same pipeline.  The
Telegraf config accepts any JSON object on the `curry/bench/events` topic —
fields not listed in `telegraf.conf` are silently ignored, so you can add new
fields without restarting Telegraf, but you must add them to `telegraf.conf`
to make them visible in Grafana.

### Minimal example: publish a custom timing

```scheme
(import (curry mqtt))
(import (curry json))

(define client (mqtt-connect "localhost" 1883 "my-program"))

(define (publish-result name ms)
  (mqtt-publish client "curry/bench/events"
    (json-encode
      `((event      . "result")
        (benchmark  . ,name)
        (gc         . "boehm")
        (mode       . "custom")
        (mean       . ,ms)
        (iterations . 1)
        (timestamp_ms . ,(inexact->exact (floor (* 1000 (current-second)))))))
    1))   ; QoS 1 — wait for broker ack

(define t0 (current-jiffy))
(run-my-thing)
(define ms (* 1000.0 (/ (- (current-jiffy) t0) (jiffies-per-second))))

(publish-result "my-thing" ms)
(mqtt-disconnect client)
```

This appears on the **Mean time per benchmark** and **Latest results table**
panels immediately, tagged as `benchmark=my-thing mode=custom`.

### Adding a custom field

1. Add the field to your JSON payload:

```scheme
`((event          . "result")
  (benchmark      . "my-thing")
  ...
  (my_custom_ns   . ,elapsed-nanoseconds))
```

2. Add a field entry to `tools/bench-stack/telegraf.conf`:

```toml
[[inputs.mqtt_consumer.json_v2.field]]
  path     = "my_custom_ns"
  type     = "int"
  optional = true
```

3. Restart Telegraf (`docker compose restart telegraf` or
   `container restart curry-telegraf`).

4. In Grafana, create a new panel with
   `r["_field"] == "my_custom_ns"` in the Flux query.

---

## Part 5 — Checking which fields are available

If you are unsure which fields or tags Telegraf has ingested, query InfluxDB
directly:

```bash
# List all tags and their values in the benchmarks bucket
curl -s \
  -H "Authorization: Token curry-bench-token" \
  -H "Content-Type: application/json" \
  --data-urlencode 'q=SHOW TAG KEYS ON benchmarks' \
  "http://localhost:8086/query"
```

Or from the InfluxDB Data Explorer UI (`http://localhost:8086`, admin /
currybench → **Data Explorer**): select the `benchmarks` bucket and use the
schema browser on the left to see every measurement, tag, and field currently
stored.

---

## See also

- [`docs/reference/benchmarking.md`](../reference/benchmarking.md) — full field reference for `(gc-stats)`, MQTT schema, bench suite details
- [`docs/reference/profiling.md`](../reference/profiling.md) — `(curry profiling)` API, `**eval-profiler**`, `,profile` REPL command
- [`docs/reference/gc.md`](../reference/gc.md) — GC architecture, `gen_alloc_pinned`, pinned slot scanning, when to use each backend
- [`tools/bench-stack/README.md`](../../tools/bench-stack/README.md) — quick-reference card for the monitoring stack
