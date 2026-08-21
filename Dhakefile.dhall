-- Dhakefile.dhall — build the compendium DNS server + docs site with dhake.
--
-- Replaces the former Makefile.  The C engine (dnsd.com and its test binaries)
-- links the dhall-c interpreter core (git submodule at ./dhall-c) and is built
-- with cosmocc; the fixpoint-design docs site
-- (fixpointlinux.org/compendium) is an Elm app built via elm + scripts/ssg.mjs.
-- The docs build mirrors datalog-dafsa's site shape:
--
--     vendor/mfe-framework -> vendor/@mfe -> dist/elm.js -> dist/index.html
--
-- Run with dhake from this directory:
--
--     ./vendor/dhake/dhake.com                    # default: `all` (C engine)
--     ./vendor/dhake/dhake.com all                # build all C binaries
--     ./vendor/dhake/dhake.com test               # run the full C test suite
--     ./vendor/dhake/dhake.com dist/index.html    # build the docs site
--     ./vendor/dhake/dhake.com --list             # list all targets
--
-- ─── verified builds ────────────────────────────────────────────────────────
-- Each C compile target pins its expected *output* hash (`hash`) and the
-- expected hash of every *source* dependency (`depsHash`).  The cosmocc APE
-- output is deterministic (same toolchain + sources + flags => identical
-- bytes), so the output pin is sound — verified: a fresh build reproduces the
-- committed binaries byte-for-byte.  If a hash goes stale (edit a source,
-- bump the toolchain), rebuild with `dhake --warn-hash-mismatch` to print the
-- actual hashes and copy them into this file.
-- ───────────────────────────────────────────────────────────────────────────

let Action =
      < Shell : Text
      | Copy : { from : Text, to : Text }
      | Mkdir : < Plain : Text | Parents : { path : Text, parents : Bool } >
      | Rm : < Plain : Text | Recursive : { path : Text, recursive : Bool } >
      | Touch : Text
      | Move : { from : Text, to : Text }
      | Symlink : { from : Text, to : Text }
      | Chmod : { path : Text, mode : Text }
      | Echo : Text
      | Env : { key : Text, value : Text }
      | Run : { argv : List Text }
      >

let Target = { deps : List Text, phony : Bool, recipe : List Action
             , hash : Optional Text
             , depsHash : Optional (List { path : Text, hash : Text })
             }

-- dhall-c interpreter core (linked by the engine and every stage-N check
-- except rl_check, which only uses src/rl.c + src/rl_check.c).
let core =
      [ "dhall-c/src/arena.c"
      , "dhall-c/src/lexer.c"
      , "dhall-c/src/parser.c"
      , "dhall-c/src/ast.c"
      , "dhall-c/src/normalize.c"
      , "dhall-c/src/typecheck.c"
      , "dhall-c/src/builtins.c"
      , "dhall-c/src/serialize.c"
      , "dhall-c/src/import.c"
      , "dhall-c/src/bignum.c"
      , "dhall-c/src/sha256.c"
      , "dhall-c/src/ssrf.c"
      , "dhall-c/src/http.c"
      ]

-- dhall-c core headers (dhall.h pulls in ssrf.h).
let coreHdr = [ "dhall-c/src/dhall.h", "dhall-c/src/ssrf.h" ]

-- sha256 of each dhall-c core source + header (verified-build integrity).
let coreHashes =
      [ { path = "dhall-c/src/arena.c"
        , hash = "sha256:d025633194ecae134ce25f47ed30d025cda3633ef7df749be8f812cac85a4b5e"
        }
      , { path = "dhall-c/src/lexer.c"
        , hash = "sha256:2eecc4703e64d2ee186ed3b65b87f973bbc3e5dc79bfc36096bd914bf33794ce"
        }
      , { path = "dhall-c/src/parser.c"
        , hash = "sha256:74a69085371f99db39f6ab3035b6027f3ed47b14c97094c844159cd48db634a4"
        }
      , { path = "dhall-c/src/ast.c"
        , hash = "sha256:b5bcc35d08e94e8a8e9d1b9707d37e74b31c6d9dc2901b571c5481b00a42db12"
        }
      , { path = "dhall-c/src/normalize.c"
        , hash = "sha256:323604b338f6e9f12a8a7552df38efd80574bfa8de15590d036efff699718ea2"
        }
      , { path = "dhall-c/src/typecheck.c"
        , hash = "sha256:13b939ce603fa23c50819adac63a77f0d2c8565f68df3c6cf7ace58376452d70"
        }
      , { path = "dhall-c/src/builtins.c"
        , hash = "sha256:bd8a279c18368f67fae78753dc7f7d0d8edba4651acaa34b960fcf05b42fc936"
        }
      , { path = "dhall-c/src/serialize.c"
        , hash = "sha256:1d47a1d828072c6c9284afe28410fa2ddde5dc18984583be620df8dcf27f20a9"
        }
      , { path = "dhall-c/src/import.c"
        , hash = "sha256:48d5014f36bac6bcbe836e612635b1954a963a7316658588c1eb4ed738b6858e"
        }
      , { path = "dhall-c/src/bignum.c"
        , hash = "sha256:01b43c3c980f88b80da7f26836458540c7fa611df5b5dc205f670aa5dc5188fd"
        }
      , { path = "dhall-c/src/sha256.c"
        , hash = "sha256:dfdd76023d85b821e735ecad9b0be3ef11129656feb018874461a00329ab279e"
        }
      , { path = "dhall-c/src/ssrf.c"
        , hash = "sha256:807c8acf89548b023df3393cc5f43ab31b0024c3b52c8482355b6162cff1cf81"
        }
      , { path = "dhall-c/src/http.c"
        , hash = "sha256:9dbbd36a61b2980bea214bb49eb64d19dae1ce6654685e3f77afccd3cbc453e3"
        }
      , { path = "dhall-c/src/dhall.h"
        , hash = "sha256:b1874785500777aa182e6bba791942660df8190253555a8017bc90d23a2107dc"
        }
      , { path = "dhall-c/src/ssrf.h"
        , hash = "sha256:5987d7ea8ce6ac1d6dfdcec1e199cd44ccb3235d537cd5724528867038451a3f"
        }
      ]

-- sha256 of the compendium engine sources (verified-build integrity).
let engineSrc =
      [ { path = "src/config.c"
        , hash = "sha256:47b872a762744366c32e450ab39e6fde8af4fd875beeefb505f25e1449e3c170"
        }
      , { path = "src/dns.c"
        , hash = "sha256:13635665f48956d0021f0c60ba8e6cbb701ed051a4a6a9b7101088e055dba62e"
        }
      , { path = "src/rl.c"
        , hash = "sha256:41e2cb9616c1d6814bececd31cd27070ee8246dfe8fdb18e9efb9cc64f2752a0"
        }
      , { path = "src/main.c"
        , hash = "sha256:c52786a6e8b21658156f728b425d30f8d27983f9427510416c2277ba587cc8b7"
        }
      , { path = "src/dnsd.h"
        , hash = "sha256:c50cd2bb05101640c8d08eb64e3cd6db844ea32a0483929475edd91673fcd2e8"
        }
      ]

-- sha256 of each stage-N check's own TU (compiled in addition to the engine
-- sources above or, for rl_check, in place of the core).
let checkSrc =
      [ { path = "src/config_check.c"
        , hash = "sha256:61cbc54092c047f3469528ba3397ad20e85e9f7c0bdf95c973228987a4320d78"
        }
      , { path = "src/wire_check.c"
        , hash = "sha256:de57a72f7ae62c32d5fa15788681de65556867998b172eab2acc683f806f15f6"
        }
      , { path = "src/lookup_check.c"
        , hash = "sha256:026d3079a92e1e50de3849288e23532df96d83c1368b2690c01b75bfcefe7ac7"
        }
      , { path = "src/query_check.c"
        , hash = "sha256:a5eeb6029a0b7e9affb39a6abee05abb5f208fe934146051a00bcf740e7aa998"
        }
      , { path = "src/rl_check.c"
        , hash = "sha256:c00df6287db5269aa079b49ce390a46d90af612f33fbb3bf562f4a4132b20574"
        }
      ]

-- CFLAGS shared by every compile (matches the former Makefile).
let flags = "-std=c11 -O2 -g -Wall -Wextra -I dhall-c/src"

in  { targets =
        -- `all` + default: build every C binary.
        [ { mapKey = "all"
          , mapValue =
              { deps =
                  [ "dnsd.com"
                  , "config_check.com"
                  , "wire_check.com"
                  , "lookup_check.com"
                  , "query_check.com"
                  , "rl_check.com"
                  ]
              , phony = True
              , recipe = [] : List Action
              }
          }

        -- The engine: dnsd.com (authoritative UDP DNS server, cosmocc APE).
        , { mapKey = "dnsd.com"
          , mapValue =
              { deps = [ "src/config.c", "src/dns.c", "src/rl.c", "src/main.c", "src/dnsd.h" ]
                        # core # coreHdr
              , phony = False
              , hash = "sha256:0f8b2bb9fd9c46cc86254d3546740da67cc0f2f728d54f2c2075888ca7a02509"
              , depsHash = engineSrc # coreHashes
              , recipe =
                  [ < Shell =
                        "cosmocc " ++ flags ++ " -o dnsd.com "
                      ++ "src/config.c src/dns.c src/rl.c src/main.c "
                      ++ "dhall-c/src/arena.c dhall-c/src/lexer.c dhall-c/src/parser.c "
                      ++ "dhall-c/src/ast.c dhall-c/src/normalize.c dhall-c/src/typecheck.c "
                      ++ "dhall-c/src/builtins.c dhall-c/src/serialize.c dhall-c/src/import.c "
                      ++ "dhall-c/src/bignum.c dhall-c/src/sha256.c dhall-c/src/ssrf.c "
                      ++ "dhall-c/src/http.c"
                    >
                  ]
              }
          }

        -- Stage-1 config walk test: load config.example.dhall, print zones/records.
        , { mapKey = "config_check.com"
          , mapValue =
              { deps = [ "src/config.c", "src/config_check.c", "src/dnsd.h" ] # core # coreHdr
              , phony = False
              , hash = "sha256:990e685970cf39b1497ae4593eec5d379d120e886dbf12c19d6e8eb1c5de31ee"
              , depsHash =
                  [ { path = "src/config.c"
                    , hash = "sha256:47b872a762744366c32e450ab39e6fde8af4fd875beeefb505f25e1449e3c170"
                    }
                  , { path = "src/config_check.c"
                    , hash = "sha256:61cbc54092c047f3469528ba3397ad20e85e9f7c0bdf95c973228987a4320d78"
                    }
                  , { path = "src/dnsd.h"
                    , hash = "sha256:c50cd2bb05101640c8d08eb64e3cd6db844ea32a0483929475edd91673fcd2e8"
                    }
                  ] # coreHashes
              , recipe =
                  [ < Shell =
                        "cosmocc " ++ flags ++ " -o config_check.com "
                      ++ "src/config.c src/config_check.c "
                      ++ "dhall-c/src/arena.c dhall-c/src/lexer.c dhall-c/src/parser.c "
                      ++ "dhall-c/src/ast.c dhall-c/src/normalize.c dhall-c/src/typecheck.c "
                      ++ "dhall-c/src/builtins.c dhall-c/src/serialize.c dhall-c/src/import.c "
                      ++ "dhall-c/src/bignum.c dhall-c/src/sha256.c dhall-c/src/ssrf.c "
                      ++ "dhall-c/src/http.c"
                    >
                  ]
              }
          }

        -- Stage-2 wire test: emit hex responses for all 7 record types + NXDOMAIN,
        -- decoded+asserted by tests/dnsproto.py.
        , { mapKey = "wire_check.com"
          , mapValue =
              { deps =
                  [ "src/config.c", "src/dns.c", "src/wire_check.c", "src/dnsd.h" ]
                  # core # coreHdr
              , phony = False
              , hash = "sha256:27a6be2c17d558bf1edea71c8f502091f47ef26114d7fa9eb028cb449c5849f4"
              , depsHash =
                  [ { path = "src/config.c"
                    , hash = "sha256:47b872a762744366c32e450ab39e6fde8af4fd875beeefb505f25e1449e3c170"
                    }
                  , { path = "src/dns.c"
                    , hash = "sha256:13635665f48956d0021f0c60ba8e6cbb701ed051a4a6a9b7101088e055dba62e"
                    }
                  , { path = "src/wire_check.c"
                    , hash = "sha256:de57a72f7ae62c32d5fa15788681de65556867998b172eab2acc683f806f15f6"
                    }
                  , { path = "src/dnsd.h"
                    , hash = "sha256:c50cd2bb05101640c8d08eb64e3cd6db844ea32a0483929475edd91673fcd2e8"
                    }
                  ] # coreHashes
              , recipe =
                  [ < Shell =
                        "cosmocc " ++ flags ++ " -o wire_check.com "
                      ++ "src/config.c src/dns.c src/wire_check.c "
                      ++ "dhall-c/src/arena.c dhall-c/src/lexer.c dhall-c/src/parser.c "
                      ++ "dhall-c/src/ast.c dhall-c/src/normalize.c dhall-c/src/typecheck.c "
                      ++ "dhall-c/src/builtins.c dhall-c/src/serialize.c dhall-c/src/import.c "
                      ++ "dhall-c/src/bignum.c dhall-c/src/sha256.c dhall-c/src/ssrf.c "
                      ++ "dhall-c/src/http.c"
                    >
                  ]
              }
          }

        -- Lookup-semantics test (no sockets): NXDOMAIN/NODATA/NOERROR/ANY + zone match.
        , { mapKey = "lookup_check.com"
          , mapValue =
              { deps =
                  [ "src/config.c", "src/dns.c", "src/lookup_check.c", "src/dnsd.h" ]
                  # core # coreHdr
              , phony = False
              , hash = "sha256:74d44f0cfda4d670ee02b18432381e5ec15ad741f06da429d69b9ed9935d17b4"
              , depsHash =
                  [ { path = "src/config.c"
                    , hash = "sha256:47b872a762744366c32e450ab39e6fde8af4fd875beeefb505f25e1449e3c170"
                    }
                  , { path = "src/dns.c"
                    , hash = "sha256:13635665f48956d0021f0c60ba8e6cbb701ed051a4a6a9b7101088e055dba62e"
                    }
                  , { path = "src/lookup_check.c"
                    , hash = "sha256:026d3079a92e1e50de3849288e23532df96d83c1368b2690c01b75bfcefe7ac7"
                    }
                  , { path = "src/dnsd.h"
                    , hash = "sha256:c50cd2bb05101640c8d08eb64e3cd6db844ea32a0483929475edd91673fcd2e8"
                    }
                  ] # coreHashes
              , recipe =
                  [ < Shell =
                        "cosmocc " ++ flags ++ " -o lookup_check.com "
                      ++ "src/config.c src/dns.c src/lookup_check.c "
                      ++ "dhall-c/src/arena.c dhall-c/src/lexer.c dhall-c/src/parser.c "
                      ++ "dhall-c/src/ast.c dhall-c/src/normalize.c dhall-c/src/typecheck.c "
                      ++ "dhall-c/src/builtins.c dhall-c/src/serialize.c dhall-c/src/import.c "
                      ++ "dhall-c/src/bignum.c dhall-c/src/sha256.c dhall-c/src/ssrf.c "
                      ++ "dhall-c/src/http.c"
                    >
                  ]
              }
          }

        -- Packet-dispatch test (no sockets): FORMERR/NOTIMP/REFUSED/EDNS0/QR-drop.
        , { mapKey = "query_check.com"
          , mapValue =
              { deps =
                  [ "src/config.c", "src/dns.c", "src/query_check.c", "src/dnsd.h" ]
                  # core # coreHdr
              , phony = False
              , hash = "sha256:ac0be8f4bc5f8b411ac4de489b7886cbb8be8c738a1658a74bb546553cbf507a"
              , depsHash =
                  [ { path = "src/config.c"
                    , hash = "sha256:47b872a762744366c32e450ab39e6fde8af4fd875beeefb505f25e1449e3c170"
                    }
                  , { path = "src/dns.c"
                    , hash = "sha256:13635665f48956d0021f0c60ba8e6cbb701ed051a4a6a9b7101088e055dba62e"
                    }
                  , { path = "src/query_check.c"
                    , hash = "sha256:a5eeb6029a0b7e9affb39a6abee05abb5f208fe934146051a00bcf740e7aa998"
                    }
                  , { path = "src/dnsd.h"
                    , hash = "sha256:c50cd2bb05101640c8d08eb64e3cd6db844ea32a0483929475edd91673fcd2e8"
                    }
                  ] # coreHashes
              , recipe =
                  [ < Shell =
                        "cosmocc " ++ flags ++ " -o query_check.com "
                      ++ "src/config.c src/dns.c src/query_check.c "
                      ++ "dhall-c/src/arena.c dhall-c/src/lexer.c dhall-c/src/parser.c "
                      ++ "dhall-c/src/ast.c dhall-c/src/normalize.c dhall-c/src/typecheck.c "
                      ++ "dhall-c/src/builtins.c dhall-c/src/serialize.c dhall-c/src/import.c "
                      ++ "dhall-c/src/bignum.c dhall-c/src/sha256.c dhall-c/src/ssrf.c "
                      ++ "dhall-c/src/http.c"
                    >
                  ]
              }
          }

        -- Rate-limiter unit test (no sockets, deterministic): token-bucket decision.
        -- Links only src/rl.c + src/rl_check.c (no dhall-c core).
        , { mapKey = "rl_check.com"
          , mapValue =
              { deps = [ "src/rl.c", "src/rl_check.c", "src/dnsd.h" ]
              , phony = False
              , hash = "sha256:a67a3d203fcb97ae20884daa81b5fe14e2cce0c03c61dc49f8b74593cb2d7df1"
              , depsHash =
                  [ { path = "src/rl.c"
                    , hash = "sha256:41e2cb9616c1d6814bececd31cd27070ee8246dfe8fdb18e9efb9cc64f2752a0"
                    }
                  , { path = "src/rl_check.c"
                    , hash = "sha256:c00df6287db5269aa079b49ce390a46d90af612f33fbb3bf562f4a4132b20574"
                    }
                  , { path = "src/dnsd.h"
                    , hash = "sha256:c50cd2bb05101640c8d08eb64e3cd6db844ea32a0483929475edd91673fcd2e8"
                    }
                  ]
              , recipe =
                  [ < Shell = "cosmocc " ++ flags ++ " -o rl_check.com src/rl.c src/rl_check.c" >
                  ]
              }
          }

        -- Run the full C test suite (all stage-N checks + live UDP).
        , { mapKey = "test"
          , mapValue =
              { deps = [ "all" ]
              , phony = True
              , recipe = [ < Shell = "bash tests/run.sh" > ]
              }
          }

        -- Browser wasm demo (emscripten) into docs/ + smoke-test.  Requires
        -- emscripten/clang/llvm on the host; the built docs/dnsd.js +
        -- docs/dnsd.wasm are committed so CI has no emscripten step.
        , { mapKey = "wasm"
          , mapValue =
              { deps = []
              , phony = True
              , recipe =
                  [ < Shell = "./scripts/build-wasm.sh" >
                  , < Shell = "node tests/wasm-smoke.js" >
                  ]
              }
          }

        -- Remove built C binaries.
        , { mapKey = "clean"
          , mapValue =
              { deps = []
              , phony = True
              , recipe =
                  [ < Rm = "dnsd.com" >
                  , < Rm = "dnsd.com.dbg" >
                  , < Rm = "dnsd.aarch64.elf" >
                  , < Rm = "config_check.com" >
                  , < Rm = "config_check.com.dbg" >
                  , < Rm = "config_check.aarch64.elf" >
                  , < Rm = "wire_check.com" >
                  , < Rm = "wire_check.com.dbg" >
                  , < Rm = "wire_check.aarch64.elf" >
                  , < Rm = "lookup_check.com" >
                  , < Rm = "lookup_check.com.dbg" >
                  , < Rm = "lookup_check.aarch64.elf" >
                  , < Rm = "query_check.com" >
                  , < Rm = "query_check.com.dbg" >
                  , < Rm = "query_check.aarch64.elf" >
                  , < Rm = "rl_check.com" >
                  , < Rm = "rl_check.com.dbg" >
                  , < Rm = "rl_check.aarch64.elf" >
                  ]
              }
          }

        -- ─── docs site (fixpoint design components) ─────────────────────────
        -- The docs site is an Elm app (src/Main.elm) rendered against the shared
        -- Fixpoint.* design package (vendor/design) + the @mfe/framework shell
        -- (vendor/mfe-framework). Pipeline, mirroring datalog-dafsa:
        --
        --   vendor/mfe-framework -> vendor/@mfe -> dist/elm.js -> dist/index.html
        --
        -- The `dist/index.html` target produces the full multi-route site
        -- (dist/index.html + dist/{config,cli,api,playground}/index.html + the
        -- wasm/CodeMirror assets copied by scripts/ssg.mjs from docs/). Run it
        -- explicitly with `dhake dist/index.html`. The C engine stays the
        -- default build; the site does not require emscripten (wasm is committed
        -- to docs/ and copied by the ssg).
        , { mapKey = "mfe-framework"
          , mapValue =
              { deps = []
              , phony = True
              , recipe =
                  [ < Shell = "cd vendor/mfe-framework && npm ci && npm run build" >
                  ]
              }
          }
        , { mapKey = "vendor-mfe"
          , mapValue =
              { deps = [ "mfe-framework" ]
              , phony = True
              , recipe =
                  [ < Rm = { path = "vendor/@mfe", recursive = True } >
                  , < Mkdir = { path = "vendor/@mfe/core", parents = True } >
                  , < Mkdir = { path = "vendor/@mfe/framework", parents = True } >
                  , < Shell =
                        "cp vendor/mfe-framework/packages/core/dist/*.js vendor/@mfe/core/"
                    >
                  , < Shell =
                        "cp vendor/mfe-framework/packages/framework/dist/*.js vendor/@mfe/framework/"
                    >
                  ]
              }
          }
        , { mapKey = "dist/elm.js"
          , mapValue =
              { deps = [ "src/Main.elm", "elm.json", "vendor/design/src" ]
              , phony = False
              , recipe =
                  [ < Shell =
                        "node_modules/elm/bin/elm make src/Main.elm --output=dist/elm.js --optimize"
                    >
                  ]
              }
          }
        , { mapKey = "dist/index.html"
          , mapValue =
              { deps =
                  [ "dist/elm.js"
                  , "vendor-mfe"
                  , "shell/index.html"
                  , "shell/pages.js"
                  , "shell/shell.js"
                  , "shell/templates/compendium-landing.html"
                  , "shell/templates/compendium-config.html"
                  , "shell/templates/compendium-cli.html"
                  , "shell/templates/compendium-api.html"
                  , "shell/templates/compendium-playground.html"
                  , "shell/templates/fixpoint.html"
                  , "shell/mfe/compendium-page.js"
                  , "shell/mfe/compendium-playground.js"
                  , "scripts/ssg.mjs"
                  , "docs/dnsd.js"
                  , "docs/dnsd.wasm"
                  , "docs/playground-ui.js"
                  , "docs/vendor/codemirror.min.js"
                  , "docs/vendor/codemirror.css"
                  , "docs/vendor/codemirror-simple.js"
                  , "docs/vendor/dhall-mode.js"
                  ]
              , phony = False
              , recipe = [ < Shell = "node scripts/ssg.mjs" > ]
              }
          }
        ]
      , default = "all"
      }
