# compendium

A small, self-contained **authoritative DNS server** for **UDP**, configured in
[Dhall](https://dhall-lang.org/) and compiled into a single portable APE binary
with [cosmocc](https://github.com/jart/cosmopolitan).

It links the [dhall-c](https://github.com/jmars/dhall-c) interpreter core to
parse, typecheck and normalize a Dhall config file describing zones and records,
then serves those records authoritatively over UDP (RFC 1035).

▶ **Try it live in your browser** — [https://fixpointlinux.org/compendium/](https://fixpointlinux.org/compendium/)
(edit the Dhall config and query the real server, compiled to wasm, entirely client-side).

## Why

This project started as a simple question: *can a real, useful network server be
written in C, configured in a typed language, and shipped as one self-contained
binary?*

Three decisions shaped the answer:

**1. The config is code — so it's typechecked.**
dnsd doesn't parse a config file; it *interprets one*. Your zones are a Dhall
program, evaluated at startup by the same interpreter core this project shares
with [dhall-c](https://github.com/jmars/dhall-c). A typo in a record isn't a
silent runtime surprise — it's a type error before the server ever binds a
port. Configuration as code means configuration that's verified.

**2. One C source, many targets.**
The same lexer, parser, typechecker, and wire codec compile to:
- a single **~1 MB APE binary** (`dnsd.com`) that runs on Linux, macOS, Windows,
  and the BSDs — no runtime, no interpreter, just the file;
- **WebAssembly** (`dnsd.wasm`) that runs the *actual server*, client-side, in
  a browser tab.

You don't write DNS in C twice. You write it once and decide how to ship it.

**3. Public servers demand conservative engineering.**
An authoritative nameserver on the open internet is a reflection/amplification
target. So dnsd is deliberately boring: per-source *and* global rate limits,
answers capped with proper TC truncation, no recursion, no EDNS0 large-response
amplification, full bounds-checking on the wire path — and it runs unprivileged
under `MemoryDenyWriteExecute` + a seccomp allowlist, with exactly one
capability (`cap_net_bind_service`).

*Boring is the feature.* A DNS server that never needs a CVE is one you can
forget about.

## Features

- **Records:** `A`, `AAAA`, `CNAME`, `TXT`, `MX`, `NS`, `SOA`, `CAA` (RFC 8659)
- **Semantics:** authoritative answer / NODATA / NXDOMAIN, `ANY`, name-compression
- **Config:** Dhall, typechecked against its schema, evaluated at startup
- **Single portable binary:** cosmocc → `dnsd.com` (APE) + `dnsd.com.dbg` (ELF)
- **Hardened for public exposure:**
  - per-source + global token-bucket rate limiting (DoS)
  - bounded answer count (`MAX_ANSWERS`) with truncation (TC) bit
  - bounded name-compression probe depth, cached A/AAAA rdata, capped lookup
  - full bounds-checking on the remote wire path

## Build

Requires `cosmocc`. The `dhall-c` interpreter core is a **git submodule** at
`./dhall-c`. The build is driven by **dhake** (`Dhakefile.dhall`), which replaces
the former Makefile and uses **verified builds** — each C target pins the expected
sha256 of its output and every source dependency, so a build fails loudly if any
input or the deterministic APE output hashes to something unexpected.

```sh
git submodule update --init   # fetch the dhall-c core (once after clone)
./vendor/dhake/dhake.com      # builds dnsd.com + all test binaries (default: all)
./vendor/dhake/dhake.com all  # same, explicit
./vendor/dhake/dhake.com test # runs the full suite (config/lookup/query/wire/rl + live UDP)
./vendor/dhake/dhake.com dist/index.html   # build the docs site (fixpointlinux.org/compendium)
./vendor/dhake/dhake.com clean             # remove built binaries
```

If a source or the toolchain changes and a pinned hash goes stale, rebuild with
`./vendor/dhake/dhake.com --warn-hash-mismatch all` to print the actual hashes
and copy them into `Dhakefile.dhall`.

## Usage

```sh
./dnsd.com.dbg --config config.example.dhall --port 5353 --address 127.0.0.1
```

Options:
- `-c, --config <file>` — Dhall config (required)
- `-p, --port <n>` — listen port (default 5353)
- `-a, --address <ip>` — listen address (default 127.0.0.1)

## Config format

```dhall
let Record = < A     : { name : Text, ttl : Natural, value : Text }
             | AAAA  : { name : Text, ttl : Natural, value : Text }
             | CNAME : { name : Text, ttl : Natural, value : Text }
             | TXT   : { name : Text, ttl : Natural, value : Text }
             | MX    : { name : Text, ttl : Natural, priority : Natural, exchange : Text }
             | NS    : { name : Text, ttl : Natural, value : Text }
             | SOA   : { name : Text, ttl : Natural, mname : Text, rname : Text
                       , serial : Natural, refresh : Natural, retry : Natural
                       , expire : Natural, minimum : Natural }
             | CAA   : { name : Text, ttl : Natural, flags : Natural, tag : Text, value : Text } >
in  let Zone   = { name : Text, records : List Record }
in  let Config = { zones : List Zone }
in  { zones = [ { name = "example.com.", records = [ < A = { name = "@", ttl = 3600, value = "192.0.2.1" } > ] } ] } : Config
```

Owner names are relative to the zone (`@` / `""` = apex); rdata target names are
absolute FQDNs with a trailing dot. See `config.example.dhall` for a full example.

## Security / DoS notes

- Records-only authoritative: no recursion, no forwarding, no AXFR.
- EDNS0 is ignored (no large-response amplification); responses capped at 512 bytes.
- Rate limits (per-source burst 100 / 20 q/s; global burst 500 / 100 q/s) are
  tunable in `src/dnsd.h`.

## License

MIT
