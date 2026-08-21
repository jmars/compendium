-- Dhakefile.dhall — build the compendium docs site with dhake.
--
-- The C engine (dnsd.com and its test binaries) is built by the Makefile and
-- is UNTOUCHED. This Dhakefile drives ONLY the fixpoint-design docs site
-- (fixpointlinux.org/compendium), mirroring datalog-dafsa's site-only shape:
--
--     vendor/mfe-framework -> vendor/@mfe -> dist/elm.js -> dist/index.html
--
-- Run with dhake from this directory:
--
--     ./vendor/dhake/dhake.com                    # default target: dist/index.html
--     ./vendor/dhake/dhake.com dist/index.html    # same, explicit
--     ./vendor/dhake/dhake.com --list             # list the site targets
--
-- The wasm demo stays committed (docs/dnsd.js + docs/dnsd.wasm), so the site
-- build has no emscripten step: scripts/ssg.mjs copies the committed wasm +
-- CodeMirror assets from docs/ into dist/.

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

let Target = { deps : List Text, phony : Bool, recipe : List Action }

in  { targets =
        -- ─── docs site ──────────────────────────────────────────────────────
        -- mfe-framework submodule source; phony because its build is a plain
        -- `npm ci && npm run build` whose outputs live inside the submodule.
        [ { mapKey = "mfe-framework"
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
      , default = "dist/index.html"
      }
