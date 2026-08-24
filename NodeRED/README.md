# MUTHUR → InfluxDB (Node-RED)

A Node-RED flow that subscribes to the whole `MUTHUR` MQTT tree and writes
every reading into InfluxDB, deriving the measurement, tags and field names
from the topic itself. Adding a topic to a sketch needs no change here.

Import `muthur-to-influxdb.json` via **Menu → Import → select a file**.

## What it produces

Topics in this repo all have the shape:

```
MUTHUR / <class> / <node> / <metric>
  |         |         |        |
  |         |         |        `- Influx field name  (RATE_LPM, TEMP_C, ...)
  |         |         `---------- Influx measurement (FLOW, CMPST, PLNT, BASE)
  |         `-------------------- tag "class"        (NDATA, DDATA, DIAG, PND)
  `------------------------------ tag "group"        (always MUTHUR)
```

So `MUTHUR/NDATA/FLOW/RATE_LPM` = `7.50` becomes:

| | |
|---|---|
| measurement | `FLOW` |
| tags | `group=MUTHUR class=NDATA node=FLOW` |
| fields | `rate_lpm=7.5` |
| timestamp | ingest time |

Field names are lower-cased; the measurement and tag values keep the
topic's casing.

### JSON payloads

The `STATUS` topics publish a JSON document rather than a scalar. Those
expand to **one field per key**, so a single message writes the whole
snapshot:

```
MUTHUR/DIAG/FLOW/STATUS
{"device":"Arduino UNO R4 WiFi","rssi":-47,"uptime":3600,"rate_lpm":7.50, ...}
```

becomes measurement `FLOW`, tags `group=MUTHUR class=DIAG node=FLOW
device="Arduino UNO R4 WiFi"`, and fields `rssi=-47 uptime=3600
rate_lpm=7.5 total_l=342.125 hour_l=120 last_hour_l=210 moisture_pct=47
moisture_raw=610 pulses=153900`.

Nested objects are flattened with `_` (none of the current payloads nest).

**Numbers and booleans become fields; strings become tags.** A string is
descriptive metadata — `device` is the only one today — and tags are
indexed and queryable. Keep an eye on this if you ever publish a string
that varies per message: that would spawn a new Influx series each time.

### `class` keeps the duplicates apart

`rate_lpm` is published both as its own topic every 10s and inside the
`STATUS` document every 30s. They do **not** collide: the `class` tag
differs (`NDATA` vs `DIAG`), and tags are part of the series key, so they
land in separate series. Filter on `class="NDATA"` for the primary
telemetry and `class="DIAG"` for the diagnostics snapshot.

## What it deliberately drops

The firmware publishes `--` when it has no reading — a thermocouple fault,
or a moisture probe that isn't fitted. Writing that as `0` would invent
data, so those messages are dropped rather than stored. An absent point is
the honest representation.

Also dropped, each with a warning in the debug sidebar:

| Input | Why |
|-------|-----|
| `--`, `nan`, `null`, `undefined` | firmware "no reading" sentinels |
| empty payload | a retained-message clear, not a reading |
| malformed JSON | logged with the parse error, not guessed at |
| a topic that isn't 4 segments starting `MUTHUR` | not ours |
| a JSON document whose values were all sentinels | nothing left to write |

## Setup after importing

**Credentials are never exported by Node-RED**, so two config nodes need
filling in:

1. **MUTHUR broker** — pre-set to `192.168.5.110:1883` to match the
   sketches. Add a username/password if your broker wants one.
2. **MUTHUR InfluxDB** — pre-set for **InfluxDB 2.0** at
   `http://127.0.0.1:8086`. Set the URL and paste your **token**.

Then on the `InfluxDB` batch node set your **Organization** (`my-org`
placeholder) and **Bucket** (`muthur`).

### Running InfluxDB 1.x instead

In the InfluxDB config node set **Version** to `1.x`, and set hostname and
port rather than URL. The batch node's **Database** is already `muthur`, so
that is all that changes. Note that on 1.x every number is written as a
float; 1.8-flux and 2.0 can store true integers if you suffix a numeric
string with `i`, which this flow does not do.

## Querying it

Flux (2.0):

```flux
from(bucket: "muthur")
  |> range(start: -7d)
  |> filter(fn: (r) => r._measurement == "FLOW" and r.class == "NDATA")
  |> filter(fn: (r) => r._field == "hourly_l")
```

InfluxQL (1.x):

```sql
SELECT "hourly_l" FROM "FLOW" WHERE "class" = 'NDATA' AND time > now() - 7d
```

Daily harvest from the hourly series:

```flux
from(bucket: "muthur")
  |> range(start: -30d)
  |> filter(fn: (r) => r._measurement == "FLOW" and r._field == "hourly_l")
  |> aggregateWindow(every: 1d, fn: sum, createEmpty: false)
```

## Timestamps

Points are stamped with **Node-RED's ingest time**, because none of the
devices has a real-time clock. Two consequences:

- `HOURLY_L` arrives at the *end* of the hour it describes, so a bar
  attributed to 15:00 covers 14:00–15:00.
- If a device buffers nothing across a network outage — and these don't —
  a gap is a real gap. Nothing backfills.

If you later add the RTC to the R4, the honest upgrade is to publish a
device timestamp and use it here instead.

## On Sparkplug B

The topic tree is Sparkplug-*shaped* rather than Sparkplug B compliant,
which is why this flow parses it directly instead of using a Sparkplug
decoder node. The differences, for the record:

| Sparkplug B | This tree |
|-------------|-----------|
| `spBv1.0/{group}/{msg_type}/{edge_node}/{device}` — 5 segments | `MUTHUR/{class}/{node}/{metric}` — 4, no namespace prefix |
| message types `NBIRTH NDEATH DBIRTH DDEATH NDATA DDATA NCMD DCMD STATE` | `NDATA` and `DDATA` are real; `DIAG` and `PND` are not Sparkplug types |
| payload is a Google Protobuf `Payload` message | payload is an ASCII number or a JSON document |
| last segment is a *device id*; metrics live inside the payload | last segment is the *metric name* |
| birth certificates declare every metric with type and alias | no birth certificates |
| `bdSeq` per session, `seq` 0-255 per message | no sequence numbers |
| death certificate registered as the MQTT Last Will | no LWT |

None of that is a problem for this flow — the convention is consistent and
parses cleanly. It only matters if you want to point Sparkplug-aware tooling
(Ignition, HiveMQ's Sparkplug features, Chariot) at the broker and have it
self-discover the metrics.

Going properly compliant would mean, on the device side: protobuf encoding
(nanopb plus the Eclipse Tahu `.proto`), emitting an NBIRTH listing every
metric with its datatype on each connect, registering an NDEATH as the
LWT, and carrying `seq`/`bdSeq`. That is comfortably within the RA4M1's
256 KB but tight on the SAMD21. On the Node-RED side you would swap the
function node here for `node-red-contrib-sparkplug-plus` or Tahu's decoder.

What you would gain is automatic metric discovery with real datatypes, and
genuine stale/offline detection via the death certificate — that second
one is the real prize, since today a silent node is indistinguishable from
a node reporting nothing.

## Files

| File | |
|------|--|
| `muthur-to-influxdb.json` | the importable flow |
| `README.md` | this file |
