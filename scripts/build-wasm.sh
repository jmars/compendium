#!/bin/sh
# Build the compendium DNS server to wasm (emscripten), output to docs/.
# Adapted from dhall-c/scripts/build-wasm.sh.
# Requires: pacman -S emscripten clang lld llvm nodejs
#   (emscripten BUNDLES binaryen; do NOT also `pacman -S binaryen` — they conflict.)
set -euo pipefail
cd "$(dirname "$0")/.."

EMCONF="$(mktemp)"
cat > "$EMCONF" <<'EOF'
import os
NODE_JS = '/usr/bin/node'
LLVM_ROOT = '/usr/bin'
BINARYEN_ROOT = '/usr'
EMSCRIPTEN_ROOT = '/usr/lib/emscripten'
CACHE = os.path.expanduser('~/.cache/emscripten')
EOF

EMCC=/usr/lib/emscripten/emcc
OUT="$(mktemp -d)"

# FORCE_FILESYSTEM=1 is required: dnsd-wasm.c writes the JS-provided config
# string into MEMFS via fopen/fwrite, and config_load() fopen()s it back.
COMMON="-O2 -I src -I dhall-c/src -s MODULARIZE=1 -s ALLOW_MEMORY_GROWTH=1 -s TOTAL_STACK=5242880 -s FORCE_FILESYSTEM=1"
RUNTIME="-s EXPORTED_RUNTIME_METHODS=ccall,cwrap,stringToUTF8,UTF8ToString,lengthBytesUTF8,HEAPU8"

# dhall-c interpreter core (mirrors dhall-c/scripts/build-wasm.sh): excludes its
# entry/extra TUs (main/wasm/bench/lsp/json) and ssrf.c — http.c's socket+SSRF
# fetch path is #ifndef __EMSCRIPTEN__'d out (the wasm path uses a synchronous
# XHR with no ssrf references), so ssrf.c's symbols are not needed at link time.
CORE="dhall-c/src/arena.c dhall-c/src/lexer.c dhall-c/src/parser.c dhall-c/src/ast.c dhall-c/src/normalize.c dhall-c/src/typecheck.c dhall-c/src/builtins.c dhall-c/src/serialize.c dhall-c/src/import.c dhall-c/src/bignum.c dhall-c/src/sha256.c dhall-c/src/http.c"

# compendium core + wasm entry (NO main.c — has its own main; NO rl.c — the
# per-source rate limiter is irrelevant to a single-query browser demo).
EM_CONFIG="$EMCONF" "$EMCC" $COMMON \
  -s EXPORT_NAME=createDnsd \
  -s EXPORTED_FUNCTIONS=_dnsd_config,_dnsd_query,_dnsd_err,_dnsd_resp,_dnsd_resp_len,_dnsd_json,_dnsd_json_len,_malloc,_free \
  $RUNTIME \
  -o "$OUT/dnsd.js" \
  src/dnsd-wasm.c src/config.c src/dns.c $CORE

mkdir -p docs
cp "$OUT/dnsd.js" "$OUT/dnsd.wasm" docs/ 2>/dev/null \
  || cp "$OUT"/dnsd.js "$OUT"/dnsd.wasm docs/
rm -rf "$OUT" "$EMCONF"
ls -la docs/dnsd.js docs/dnsd.wasm
echo "built docs/dnsd.js + docs/dnsd.wasm"
