# Curry bench-stack

Real-time benchmark monitoring: MQTT → Telegraf → InfluxDB → Grafana.

## Quick start

```bash
# 1. Start the stack (first run downloads images, ~1 minute)
docker compose up -d

# 2. Wait for InfluxDB to initialise (~10 seconds)
sleep 10

# 3. Run benchmarks — results stream into Grafana in real time
./build/curry tests/bench.scm --suite throughput     # fast (~90s)
./build/curry tests/bench.scm --suite gc             # GC behaviour
./build/curry tests/bench.scm                        # all suites

# 4. Open Grafana
open http://localhost:3000   # admin / admin
# Navigate to Dashboards → Curry Benchmarks → Curry Bench

# 5. Compare GC backends (run in separate terminal while watching Grafana)
# The gc backend is selected via a compile-time switch; bench.scm
# detects which is active via (gc-stats) and tags results accordingly.

# 6. Stop the stack
docker compose down
```

## Architecture

```
bench.scm
  └─ (curry mqtt) publish → mosquitto:1883
                              └─ telegraf (mqtt_consumer)
                                   └─ influxdb:8086 (bucket: benchmarks)
                                        └─ grafana:3000 (auto-provisioned)
```

## Dashboard panels

| Panel | What it shows |
|-------|--------------|
| Mean time per benchmark | Time-series by benchmark/gc/mode |
| p99 latency | Tail latency comparison boehm vs gen |
| Minor GC count | How many nursery collections per run |
| Max minor GC pause (gauge) | Red threshold at 5ms |
| GC p99 pause (gauge) | From the 256-entry pause ring buffer |
| Last major GC count | Boehm major collections per run |
| Latest results table | boehm vs gen side-by-side, last run |

Dashboard auto-refreshes every 5 seconds.

## Credentials

| Service | URL | Login |
|---------|-----|-------|
| Grafana | http://localhost:3000 | admin / admin |
| InfluxDB | http://localhost:8086 | admin / currybench |
| InfluxDB token | — | curry-bench-token |
| MQTT | localhost:1883 | (none — anonymous) |
