# compendium

A small, self-contained **authoritative DNS server** for **UDP**, configured in
[Dhall](https://dhall-lang.org/) and compiled into a single portable APE binary
with [cosmocc](https://github.com/jart/cosmopolitan).

It links the [dhall-c](https://github.com/jmars/dhall-c) interpreter core to
parse, typecheck and normalize a Dhall config file describing zones and records,
then serves those records authoritatively over UDP (RFC 1035).

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

Requires `cosmocc` and the `dhall-c` core as a sibling directory
(`../dhall-c`; override with `DHALL_C=<path>`).

```sh
make            # builds dnsd.com + dnsd.com.dbg
make test       # runs the full suite (config/lookup/query/wire/rl + live UDP)
```

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
