# BSH-iputils — BrightSpace Reference

BSH-iputils is a fork of iputils that adds **BrightSpace** spatial awareness
to every network tool. Standard ping and traceroute report milliseconds with no
spatial grounding. BSH-iputils adds chord distance, light-floor, and
efficiency to every measurement.

---

## BrightSpace Units

| Unit | Definition | Equivalent |
|------|-----------|------------|
| **BrightMeter (BM)** | Distance light travels in 1 second | 299,792 km |
| **milliBrightMeter (mBM)** | Distance light travels in 1 millisecond | 299.792 km |
| **milliday (md)** | 1/1000 of a day | 86.4 seconds = 86,400 ms |
| **microday (µd)** | 1/1,000,000 of a day | 86.4 ms |
| **nanoday (nd)** | 1/1,000,000,000 of a day | 86.4 µs |

### Key relationships

- 1 ms RTT → light-floor = 0.5 mBM = ~150 km (one-way at *c*)
- 1 mBM = 299.792 km  →  1 km = 0.003336 mBM
- Efficiency = (geo distance in mBM) / (RTT/2 in mBM) × 100%
  - 100% means the signal would need to travel at exactly *c* in a straight line
  - \>100% means geoIP placement is inconsistent with the measured RTT
  - Typical well-routed continental links: 40–70%

---

## BSPACE Coordinate Protocol

All `b*` tools use the same coordinate priority chain:

```
--my-ecef  >  --my-coord  >  BSH SDI geo socket ($BSH_GEO_SOCK)  >  auto-geoIP
```

The first source that provides a valid position wins; lower tiers are ignored.
The **target** is always auto-geolocated via `ip-api.com` unless an explicit
`--target-ecef` or `--target-coord` flag is given.

### BSH SDI v2 geo socket (RFC SDI v2 §8)

When running inside BSH, the `BSH_GEO_SOCK` environment variable is set by the
SDI agent. It points to a *path file* that contains the actual Unix-domain
socket path. The agent holds the device's current location fix and serves it
to authorised programs.

| Env var | Role |
|---------|------|
| `BSH_GEO_SOCK` | Path file whose contents are the geo socket path. Set by BSH SDI agent; **does not contain location data**. |

**Authorization:** each `b*` tool must be listed in `~/.config/bsh/geo-allow`
(one binary path per line) before the agent will serve it a fix. The agent
authenticates callers via `SO_PEERCRED`; no secret or token is required.

**Escape hatch:** if the tool is not in the allowlist, wrap the invocation:

```sh
bsh-geo --exec -- bping 8.8.8.8
```

**Why not plain env vars?** Putting coordinates in env vars makes them visible
to every child process and in `/proc/<pid>/environ`. The SDI geo socket is
UID-gated and per-invocation; only authorised tools can read it.

### Coordinate source priority

All `b*` tools evaluate sources in this order, highest first:

```
--my-ecef  >  --my-coord  >  BSH SDI geo socket ($BSH_GEO_SOCK)  >  auto-geoIP
```

### Source tags in output

| Tag | Meaning |
|-----|---------|
| `[ecef]` | ECEF coordinates from CLI `--my-ecef` |
| `[coord]` | Explicit lat/lon from CLI `--my-coord` |
| `[sdi]` | Lat/lon from BSH SDI v2 geo socket |
| `[sdi:ecef]` | Full ECEF from BSH SDI v2 geo socket (spacetime block present) |
| `[~geoIP]` | Auto-geolocated via ip-api.com (approximate) |

---

## bping

BrightSpace-aware ping. Measures RTT and derives spatial metrics.

### Usage

```
bping [options] <destination>
```

### Options

| Flag | Description |
|------|-------------|
| `--my-ecef=x,y,z` | Your ECEF position in BrightMeters (audit-grade, goes into shell history) |
| `--my-coord=lat,lon` | Your position as decimal degrees (goes into shell history) |
| `--target-ecef=x,y,z` | Target ECEF position in BrightMeters |
| `--target-coord=lat,lon` | Target position as decimal degrees |
| `--hops` | Show per-hop traceroute with BrightDate units and geoIP location per hop |
| `-h`, `--help` | Show help |

### Location provider

| Env var | Description |
|---------|-------------|
| `BSH_GEO_SOCK` | Path file to the BSH SDI geo socket. Set automatically by BSH; tool must be in `~/.config/bsh/geo-allow`. |

### Output fields

```
bping 8.8.8.8:
  rtt min/avg/max/mdev  = 17.408/22.225/28.001/4.377 ms
  rtt min/avg/max/mdev  = 0.00020148/0.00025723/0.00032409/0.00005066 md
  light-floor (RTT/2)   = 11.113 mBM  (~3331 km)
  src [sdi]               37.7749, -122.4194
  tgt [~geoIP]            39.0300, -77.5000  (Ashburn, United States)
  geo distance          = 12.931 mBM  (~3876 km)
  light-spd limit       = 12.931 ms   (0.00014966 md)
  efficiency            = 116.36%  [warn: exceeds c — geoIP may be off]
```

| Field | Description |
|-------|-------------|
| `rtt min/avg/max/mdev (ms)` | Standard ping RTT statistics in milliseconds |
| `rtt min/avg/max/mdev (md)` | Same RTT values in millidays |
| `light-floor (RTT/2)` | Absolute lower bound on distance traveled. The signal *must* have traversed at least this many mBM (and km) one-way at *c*. Always shown regardless of coord source. |
| `src [tag]` | Your resolved coordinates and their source |
| `tgt [tag]` | Target's resolved coordinates and their source |
| `geo distance` | Haversine (great-circle) surface distance between the two points |
| `light-spd limit` | Minimum RTT physically possible given the geo distance |
| `efficiency` | How close the actual RTT is to the physical minimum: `geo_dist / light-floor × 100%`. >100% means geoIP is inconsistent with measured RTT (e.g. anycast, VPN, or wrong placement). |

When both sides are provided as `--*-ecef`, the output uses **ECEF chord** distance
(straight line through Earth) instead of Haversine, labelled `[ecef]`.

### Per-hop trace (`--hops`)

```
bping --hops 8.8.8.8

Per-hop trace:
  hop   host                                      location              rtt (ms)      rtt (md)   floor (mBM/km)
  ----  ----------------------------------------  --------------------  ----------  ------------  ----------------
  1     192.168.1.1                               San Jose, US               0.512  0.00000593   0.256/~77
  2     10.0.0.1                                                             1.234  0.00001429   0.617/~185
  3     ae-1.cr2.sjc1.us.example.net (1.2.3.4)   San Jose, US               3.421  0.00003959   1.711/~513
  ...
```

Each hop shows:
- `location` — city/country from geoIP of the hop's IP
- `rtt (ms)` / `rtt (md)` — average RTT to that hop in ms and millidays
- `floor (mBM/km)` — light-floor for that hop (RTT/2 in mBM / approximate km)

### Examples

```sh
# Basic — no coords, gets light-floor only + auto-geoIP for both ends
bping 8.8.8.8

# Explicit coords on CLI (appears in history)
bping --my-coord=37.7749,-122.4194 --target-coord=51.5074,-0.1278 8.8.8.8

# Audit-grade ECEF coords
bping --my-ecef=0.00421,-0.00615,0.00213 --target-ecef=0.00123,-0.00456,0.00789 8.8.8.8

# Full per-hop trace with geoIP
bping --hops 1.1.1.1
```

---

## bclockdiff

BrightDate-enhanced clock difference measurement. Reports the clock offset
between your machine and a remote host in standard units and optionally in
BrightDate units (days, microdays, nanodays).

### Usage

```
bclockdiff [options] <destination>
```

### Options

| Flag | Description |
|------|-------------|
| *(none)* | Use ICMP timestamp only (RFC 0792, page 16) |
| `-o` | Use IP timestamp and ICMP echo |
| `-o1` | Use three-term IP timestamp and ICMP echo |
| `-T`, `--time-format=<ctime\|iso>` | Display time format. `ctime` (default) or `iso` |
| `-I` | Alias for `--time-format=iso` |
| `-B`, `--brightdate` | Also output clock offset in BrightDate units |
| `--unit=<d\|ud\|nd>` | BrightDate unit: `d` = day, `ud` = microday (default), `nd` = nanoday |
| `-h`, `--help` | Show help |
| `-V`, `--version` | Print version and exit |

### BrightDate units for clock offsets

| Unit flag | Unit | 1 unit = | Typical NTP offset |
|-----------|------|----------|--------------------|
| `d` | day | 86,400,000 ms | ~1.2×10⁻⁸ d |
| `ud` (default) | microday | 0.0864 ms = 86.4 µs | ~11.6 µd |
| `nd` | nanoday | 0.0000864 ms = 86.4 ns | ~11,574 nd |

Microday (`ud`) is the most useful for typical NTP-synchronized systems:
a 1 ms offset = 11.57 µd, and sub-millisecond offsets resolve clearly.

### Examples

```sh
# Standard output (no BrightDate)
bclockdiff time.cloudflare.com

# With BrightDate in microdays (default unit)
bclockdiff -B time.cloudflare.com

# With BrightDate in nanodays (high precision)
bclockdiff -B --unit=nd time.cloudflare.com

# ISO time format + BrightDate
bclockdiff -I -B time.cloudflare.com
```

---

## btraceroute

BrightSpace-aware traceroute. Shows the geographic path to a destination with
per-hop RTT in ms and millidays, light-floor, geoIP city/country, and a summary
comparing total path length to direct great-circle distance.

### Usage

```
btraceroute [options] <destination>
```

### Options

| Flag | Description |
|------|-------------|
| `--my-ecef=x,y,z` | Your ECEF position in BrightMeters |
| `--my-coord=lat,lon` | Your position as decimal degrees |
| `-m <maxhops>` | Maximum number of hops (default: 30) |
| `-q <nqueries>` | Queries per hop (default: 3) |
| `-h`, `--help` | Show help |

### Output

```
btraceroute to 8.8.8.8 (8.8.8.8), Ashburn, United States [~geoIP]
src [sdi]     37.7749, -122.4194

  hop   host                                      location                rtt (ms)      rtt (md)   floor(mBM/km)
  ----  ----------------------------------------  ----------------------  ---------  -----------  --------------
  1     192.168.1.1                               San Jose, US                0.512   0.00000593   0.256/~77
  2     ae-1.cr2.sjc1.example.net (1.2.3.4)       San Jose, US                3.421   0.00003959   1.711/~513
  ...
```

---

## bmtr

Continuous per-hop probe with a live in-place display (similar to `mtr`).
Shows rolling loss%, average RTT, stddev, and light-floor per hop.
Use `--report` for a plain-text summary (suitable for scripts and tests).

### Usage

```
bmtr [options] <destination>
```

### Options

| Flag | Description |
|------|-------------|
| `--my-ecef=x,y,z` | Your ECEF position in BrightMeters |
| `--my-coord=lat,lon` | Your position as decimal degrees |
| `-c <count>` | Stop after this many probe cycles (default: run forever) |
| `-i <interval>` | Seconds between cycles (default: 1) |
| `-m <maxhops>` | Maximum hops to discover (default: 30) |
| `--report` | Print a final report instead of live display |
| `-h`, `--help` | Show help |

### Examples

```sh
# Live display until Ctrl+C
bmtr 8.8.8.8

# 10 cycles then exit
bmtr -c 10 8.8.8.8

# Plain report (no ANSI, good for logging)
bmtr --report -c 5 8.8.8.8
```

---

## baudit

Multi-anchor distance bounding. Given one or more anchor points (each with a
known location and measured RTT), baudit computes the ring constraint from each
anchor, checks whether the target falls inside every ring, and reports a
weighted centroid estimate with a consistency verdict.

### Usage

```
baudit [options] <destination>
```

### Options

| Flag | Description |
|------|-------------|
| `--anchor=lat,lon,rtt_ms[,label]` | Add an anchor point (repeatable) |
| `--my-ecef=x,y,z` | Your ECEF position in BrightMeters |
| `--my-coord=lat,lon` | Your position as decimal degrees |
| `-c <count>` | Pings per measurement (default: 5) |
| `-h`, `--help` | Show help |

When the BSH SDI geo socket is available and the tool is in `~/.config/bsh/geo-allow`,
baudit automatically probes the destination and adds itself as an anchor.

### Examples

```sh
# Self-probe only (BSH SDI geo socket provides your position)
baudit 8.8.8.8

# Add explicit anchor vantage points
baudit --anchor=51.5074,-0.1278,42,London --anchor=35.6762,139.6503,130,Tokyo 8.8.8.8
```

---

## Tool status

| Tool | Status | Description |
|------|--------|-------------|
| `bping` | ✅ Working | ICMP probe with light-floor, chord distance, efficiency |
| `bclockdiff` | ✅ Working | Clock offset in BrightDate units |
| `btraceroute` | ✅ Working | Per-hop geoIP, RTT in ms/md, light-floor, path vs great-circle summary |
| `bmtr` | ✅ Working | Continuous rolling probe with live display and `--report` mode |
| `baudit` | ✅ Working | Multi-anchor distance bounding with consistency verdict |

---

## Quick reference card

```sh
# bping
bping HOST                            # light-floor + auto-geoIP
bping --hops HOST                     # + per-hop trace with locations

# btraceroute
btraceroute HOST                      # geographic hop-by-hop trace
btraceroute -m 15 -q 1 HOST           # faster: 15 hops max, 1 query each

# bmtr
bmtr HOST                             # live rolling display (Ctrl+C to stop)
bmtr --report -c 10 HOST              # 10 cycles, then print plain report

# baudit
baudit HOST                           # self-probe (BSH SDI geo socket)
baudit --anchor=LAT,LON,RTT HOST      # add extra anchor vantage point

# bclockdiff
bclockdiff -B HOST                    # clock offset in microdays
bclockdiff -B --unit=nd HOST          # clock offset in nanodays
```
