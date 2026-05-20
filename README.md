## Why Bright Spacetime? Forensic Utility

In a world of decentralized nodes and "trusted" identities, **Ping vs. Distance** is your ultimate truth-detector. If bsh knows its coordinates, it stops treating the internet like a magic cloud and starts treating it like a physical network of silicon and light.

### The "Why" (Forensic Utility)

Standard networking tells you **Latency**, but it doesn't tell you **Efficiency**.

**The Cheat Check:** If a node in the BrightChain swarm claims to be "Local" (in D.C.) but returns a ping of 120ms, the math in bsh instantly flags it. Because **1 mbm = 1 ms**, a node 16 mbm away cannot physically respond faster than 16 ms. If it's slower than that, you're seeing routing overhead. If it's faster (which is impossible), you know the node is spoofing its location.

**The Routing Audit:** You can finally see how much "Legacy Debt" your ISP is costing you. If you're pinging a server 1 mbm away and getting a 40 ms response, you know your data is taking a massive, inefficient detour through legacy copper.

### The bsh Integration (The "How")

For bsh to handle this, it needs to be able to pipe coordinate data into its networking tools. To do so, it uses [SDI](https://bsh.digitaldefiance.org/#sdi]).

#### The bping Command

Instead of just showing time, `bping` provides the **Spacetime Efficiency Ratio**.

```bash
bping node-01.brightchain.org
# Result:
# Distance: 12.450 mbm
# Latency:  38.200 ms
# Efficiency: 32.5% (The light-speed limit is 12.45ms)
```

**Math:**

- **1 mbm = 1 ms** at light speed (in fiber/vacuum)
- If latency < physical minimum, the node is lying
- If latency ≫ minimum, you see routing overhead

# Bright iputils — BrightDate Networking Tools

[![Build Status](https://github.com/iputils/iputils/actions/workflows/ci.yml/badge.svg)](https://github.com/iputils/iputils/actions/workflows/ci.yml)
[![Coverity Status](https://scan.coverity.com/projects/1944/badge.svg?flat=1)](https://scan.coverity.com/projects/1944)

**Bright iputils** is a Bright Spacetime/Space-ready fork of iputils, providing modernized versions of classic Linux networking tools with a universal, timezone-free time system. All tools output and accept **BrightDate** scalars for timestamps, and are designed for future **BrightSpace** (spatial) and **Bright Spacetime** (4D) extensions—making time, space, and latency math consistent across platforms, timezones, and even planetary boundaries.

All binaries are renamed with a `b` prefix to coexist with system tools:

- `bping` — BrightDate/BrightSpace ping
- `bclockdiff` — BrightDate/BrightSpace clock difference
- `btracepath` — BrightDate/BrightSpace tracepath
- `barping` — BrightDate/BrightSpace arping

Each tool supports BrightDate output and accepts BrightDate values for time-based arguments (where applicable). The codebase is engineered for seamless integration with BrightSpace (spatial) and Bright Spacetime (4D) coordinates, making these the first networking utilities ready for a universal spacetime coordinate system.

---

## What is BrightDate? What is Bright Spacetime/BrightSpace?

> **BrightDate** is a universal, timezone-free time scalar: decimal SI days since the J2000.0 astronomical epoch (2000-01-01T11:58:55.816 UTC), computed on a TAI substrate. No leap seconds, no timezone bugs, no ambiguous conversions. All time math is simple float arithmetic: `b - a = elapsed days`. See the [BrightDate specification](https://github.com/Digital-Defiance/BrightChain/docs/papers/brightdate-specification.md) for details.

> **BrightSpace** and **Bright Spacetime** extend this to a full 4D coordinate system: `[t, x, y, z]`, where `t` is BrightDate and `x, y, z` are spatial coordinates in BrightMeters (bm), a unit exactly equal to the speed of light times one second. This enables latency, distance, and time to be handled with a single, universal system. See the [BrightSpace standard](https://github.com/Digital-Defiance/BrightChain/docs/papers/bright-space-standard.md) and [Bright Spacetime standard](https://github.com/Digital-Defiance/BrightChain/docs/papers/bright-spacetime-standard.md) for details.

**Example:**

- BD 0.0 = 2000-01-01T11:58:55.816 UTC (J2000.0)
- BD 9628 ≈ May 2026
- All timestamps are sortable, diffable, and timezone-agnostic
- BrightSpace coordinates: (x, y, z) in BrightMeters (bm), e.g. Earth center = (0,0,0)
- Bright Spacetime: `[t, x, y, z]` — a universal 4D event

---

## Installation

```
$ ./configure && meson build
# cd builddir && meson install
```

Configuration can be adjusted (prefix, what is being built, etc.), see `meson_options.txt` and `meson.build`.
Build dependencies are listed in scripts in the `ci/` directory.

## Supported libc

- [glibc](https://www.gnu.org/software/libc/)
- [uClibc-ng](https://uclibc-ng.org/)
- [musl](https://musl.libc.org/)

## Contributing

### Issues

- If reporting a bug, please document how to reproduce it.

- Please always test the latest master branch.
- Finding the commit which introduced the problem helps (bisecting).
- Document the kernel and distribution that were used.
- Tests should ideally use network namespaces to not interfere with the rest of the system.

### Pull requests

- If fixing a bug, please document how to reproduce it.

- Finding the commit which introduced the problem helps (bisecting). Add `Fixme:` tag.
- If adding a feature, please describe why it's useful to add it.
- Commits should be signed: `Signed-off-by: Your Name <me@example.org>`, see
<https://www.kernel.org/doc/html/latest/process/submitting-patches.html#sign-your-work-the-developer-s-certificate-of-origin>.
- Although the coding style for most tools is ancient, new code should follow the Linux kernel coding style.
See <https://www.kernel.org/doc/html/latest/process/coding-style.html>.
- To update the code in the pull request, use `git push -f`. Do *not* open a new pull request.

### Reviewers

- Reviewers are very welcome. Post your comments or add `Reviewed-by: Your Name <me@example.org>`.

### Translators

Localization is hosted on [Fedora Weblate](https://translate.fedoraproject.org/projects/iputils/iputils/).

## Tools included in Bright iputils

- [`barping`](arping.c) — send ARP requests, BrightDate-aware
- [`bclockdiff`](bclockdiff.c) — measure clock difference, BrightDate output
- [`bping`](ping/bping.c) — ICMP ping with BrightDate timestamps and predicates
- [`btracepath`](tracepath.c) — trace network path, BrightDate output

All tools are drop-in replacements for their classic counterparts, but with BrightDate support and a `b` prefix. They can be used alongside system `ping`, `arping`, etc. without conflict.

## Tools removed from iputils

Some obsolete tools have been removed (see [#363](https://github.com/iputils/iputils/issues/363)).

## History

This project is a fork of [iputils](https://github.com/iputils/iputils), with BrightDate support and tool renaming. See upstream for full historical details.

## macOS/Homebrew Dependencies

If you are building on macOS, you must install required dependencies using Homebrew before building:

```
brew install gettext meson ninja libidn2 bsh
brew link --force gettext
```

See additional details and updates in [docs/homebrew-deps.md](docs/homebrew-deps.md).
