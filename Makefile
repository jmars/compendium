# dnsd — authoritative records-only DNS server over UDP, configured by Dhall.
# Links the dhall-c interpreter core (git submodule at ./dhall-c, overridable
# via DHALL_C).
#  make            builds dnsd.com (+ dnsd.com.dbg) + test binaries
#  make test       runs the full suite: config_check, wire_check, end-to-end UDP
#  make clean

# Use := (not ?=) so the environment's CC=cc does not override cosmocc.
CC      := cosmocc
DHALL_C ?= dhall-c
CFLAGS   = -std=c11 -O2 -g -Wall -Wextra -I $(DHALL_C)/src

# dhall-c core sources (link directly, in dhall-c's own order; exclude its
# entry-point/extra TUs: main/wasm/bench/lsp and json.c which only LSP links).
CORE_SRCS = $(DHALL_C)/src/arena.c $(DHALL_C)/src/lexer.c \
            $(DHALL_C)/src/parser.c $(DHALL_C)/src/ast.c \
            $(DHALL_C)/src/normalize.c $(DHALL_C)/src/typecheck.c \
            $(DHALL_C)/src/builtins.c $(DHALL_C)/src/serialize.c \
            $(DHALL_C)/src/import.c $(DHALL_C)/src/bignum.c \
            $(DHALL_C)/src/sha256.c $(DHALL_C)/src/ssrf.c $(DHALL_C)/src/http.c

SRC = src/config.c src/dns.c src/rl.c src/main.c

all: dnsd.com config_check.com wire_check.com lookup_check.com query_check.com rl_check.com

dnsd.com: $(SRC) src/dnsd.h $(CORE_SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(CORE_SRCS)

# Stage-1 config walk test: load config.example.dhall, print zones/records.
config_check.com: src/config.c src/config_check.c src/dnsd.h $(CORE_SRCS)
	$(CC) $(CFLAGS) -o $@ src/config.c src/config_check.c $(CORE_SRCS)

# Stage-2 wire test: emit hex responses for all 7 record types + NXDOMAIN,
# decoded+asserted by tests/dnsproto.py.
wire_check.com: src/config.c src/dns.c src/wire_check.c src/dnsd.h $(CORE_SRCS)
	$(CC) $(CFLAGS) -o $@ src/config.c src/dns.c src/wire_check.c $(CORE_SRCS)

# Lookup-semantics test (no sockets): NXDOMAIN/NODATA/NOERROR/ANY + zone match.
lookup_check.com: src/config.c src/dns.c src/lookup_check.c src/dnsd.h $(CORE_SRCS)
	$(CC) $(CFLAGS) -o $@ src/config.c src/dns.c src/lookup_check.c $(CORE_SRCS)

# Packet-dispatch test (no sockets): FORMERR/NOTIMP/REFUSED/EDNS0/QR-drop.
query_check.com: src/config.c src/dns.c src/query_check.c src/dnsd.h $(CORE_SRCS)
	$(CC) $(CFLAGS) -o $@ src/config.c src/dns.c src/query_check.c $(CORE_SRCS)

# Rate-limiter unit test (no sockets, deterministic): token-bucket decision.
rl_check.com: src/rl.c src/rl_check.c src/dnsd.h
	$(CC) $(CFLAGS) -o $@ src/rl.c src/rl_check.c

# Run both standalone test binaries and the end-to-end UDP test.
test: all
	bash tests/run.sh

clean:
	rm -f dnsd.com dnsd.com.dbg dnsd.aarch64.elf \
	      config_check.com config_check.com.dbg config_check.aarch64.elf \
	      wire_check.com wire_check.com.dbg wire_check.aarch64.elf \
	      lookup_check.com lookup_check.com.dbg lookup_check.aarch64.elf \
	      query_check.com query_check.com.dbg query_check.aarch64.elf \
	      rl_check.com rl_check.com.dbg rl_check.aarch64.elf

# Build the browser wasm demo (emscripten) into docs/ and smoke-test it.
# Runs on the host where emscripten + node are installed; requires the
# dhall-c submodule (git submodule update --init). The built docs/dnsd.js +
# docs/dnsd.wasm are committed so CI (pages.yml) has no build step.
wasm:
	./scripts/build-wasm.sh
	@node tests/wasm-smoke.js

.PHONY: all test wasm clean
