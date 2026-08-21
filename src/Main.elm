module Main exposing (main)

{-| The compendium docs site as a plain `Browser.element` app.

This module renders the entire compendium site — landing, config, CLI, C API and
playground pages — using the shared `Fixpoint.*` design package
(`vendor/design/src` is a source-directory in this application's `elm.json`).

The first child of each view is `Fixpoint.Style.stylesheet`, which emits the
full brand stylesheet as a single `<style>` node. Because each page is
pre-rendered under happy-dom by `scripts/ssg.mjs`, that `<style>` node is
carried into the static HTML — the styling ships with the page instead of
living in a committed stylesheet.

The playground page embeds a `<compendium-playground>` custom element
(registered by `shell/mfe/compendium-playground.js`) which boots the wasm DNS
server client-side; in the static pre-render that element is empty.

The page to render is selected from the `pathname` flag. There is no
client-side interactivity in Elm itself (the model is the page, the only
message is `NoOp`) — the interactive playground is handled by the custom
element.

-}

import Browser
import Fixpoint.Callout
import Fixpoint.Card
import Fixpoint.Checks
import Fixpoint.Code
import Fixpoint.Cta
import Fixpoint.Footer
import Fixpoint.Grid
import Fixpoint.Headline
import Fixpoint.Hero
import Fixpoint.Nav
import Fixpoint.Section
import Fixpoint.Style
import Html exposing (Html, a, b, code, div, em, h3, li, node, p, pre, span, strong, text, ul)
import Html.Attributes exposing (attribute, class, href)


main : Program Flags Model Msg
main =
    Browser.element
        { init = init
        , update = update
        , view = view
        , subscriptions = subscriptions
        }


type alias Flags =
    { pathname : String }


{-| Which compendium page to render, derived from the `pathname` flag.
-}
type Page
    = Landing
    | Config
    | Cli
    | Api
    | Playground


type Msg
    = NoOp


type alias Model =
    Page


init : Flags -> ( Model, Cmd Msg )
init flags =
    ( parsePage (stripCompendiumPrefix flags.pathname), Cmd.none )


update : Msg -> Model -> ( Model, Cmd Msg )
update _ model =
    ( model, Cmd.none )


subscriptions : Model -> Sub Msg
subscriptions _ =
    Sub.none



-- HELPERS


{-| Strip a leading `/compendium` prefix and any surrounding slashes so the
result is the bare sub-page slug (e.g. `"/compendium/config/"` -> `"config"`,
`"/compendium/"` -> `""`). Falls back to `""` for `/`.
-}
stripCompendiumPrefix : String -> String
stripCompendiumPrefix raw =
    let
        withoutPrefix =
            if String.startsWith "/compendium" raw then
                String.dropLeft (String.length "/compendium") raw

            else if raw == "/" then
                ""

            else
                raw
    in
    withoutPrefix
        |> String.dropLeft (if String.startsWith "/" withoutPrefix then 1 else 0)
        |> (\s -> if String.endsWith "/" s then String.dropRight 1 s else s)


parsePage : String -> Page
parsePage path =
    case path of
        "" ->
            Landing

        "config" ->
            Config

        "cli" ->
            Cli

        "api" ->
            Api

        "playground" ->
            Playground

        _ ->
            Landing



-- VIEW


view : Model -> Html Msg
view model =
    div []
        [ Fixpoint.Style.stylesheet
        , navView
        , pageView model
        , footerView
        ]


navView : Html Msg
navView =
    Fixpoint.Nav.view
        { brand = span [ class "fx" ] [ text "fx://compendium" ]
        , links =
            [ Fixpoint.Nav.homeLink "https://fixpointlinux.org/" "fixpoint-linux"
            , Fixpoint.Nav.link "/compendium" "Overview"
            , Fixpoint.Nav.link "/compendium/config" "Config"
            , Fixpoint.Nav.link "/compendium/cli" "CLI"
            , Fixpoint.Nav.link "/compendium/api" "C API"
            ]
        , extra =
            [ a [ class "home", href "/compendium/playground", attribute "data-mfe-route" "/compendium/playground" ]
                [ text "Playground →" ]
            ]
        }


pageView : Page -> Html Msg
pageView page =
    case page of
        Landing ->
            landingView

        Config ->
            configView

        Cli ->
            cliView

        Api ->
            apiView

        Playground ->
            playgroundView


landingView : Html Msg
landingView =
    div []
        [ Fixpoint.Hero.view
            { prompt =
                [ Fixpoint.Hero.dollar
                , text " "
                , Fixpoint.Code.g "compendium"
                , text " — one C source, two targets"
                , Fixpoint.Hero.blink
                ]
            , title =
                [ text "An authoritative DNS server, "
                , Fixpoint.Hero.fx [ text "configured in Dhall" ]
                ]
            , tagline =
                [ text "A small, self-contained authoritative DNS server for "
                , Fixpoint.Code.inline "UDP"
                , text " (RFC 1035). The "
                , Fixpoint.Code.inline "same C code"
                , text " ships two ways: a single "
                , Fixpoint.Code.inline "native binary"
                , text " that runs on every major OS, and a "
                , Fixpoint.Code.inline "WebAssembly"
                , text " build that serves records right here in your browser."
                ]
            }
        , Fixpoint.Section.view
            { id = "targets"
            , title = "Compiled once for C. Delivered two ways."
            , hint = "// one source · two targets"
            , children =
                [ p []
                    [ text "Your zones are a "
                    , Fixpoint.Code.inline "Dhall"
                    , text " program, evaluated at startup by the interpreter core "
                    , Fixpoint.Code.inline "dnsd"
                    , text " shares with "
                    , a [ href "https://fixpointlinux.org/dhall-c/" ] [ text "dhall-c" ]
                    , text ". The same "
                    , Fixpoint.Code.inline "src/*.c"
                    , text " build into two artifacts that cover the whole spectrum — from your terminal to a browser tab."
                    ]
                , Fixpoint.Code.block
                    [ Fixpoint.Code.g "src/*.c"
                    , text "  — the server · Dhall config loader · DNS wire codec\n"
                    , text "├─ "
                    , Fixpoint.Code.c "src/main.c"
                    , text "    → cosmocc    → "
                    , Fixpoint.Code.g "dnsd.com"
                    , text "   one binary, many OSes\n"
                    , text "└─ "
                    , Fixpoint.Code.c "src/dnsd-wasm.c"
                    , text " → emscripten → "
                    , Fixpoint.Code.g "dnsd.wasm"
                    , text "  zero-install, in your browser"
                    ]
                , Fixpoint.Grid.grid
                    [ Fixpoint.Card.view
                        { n = "01"
                        , title = "Native binary — dnsd.com"
                        , body =
                            [ text "A single self-contained "
                            , strong [] [ text "~1 MB", text " Actually Portable Executable built with ", Fixpoint.Code.inline "cosmocc", text ". The same file runs natively on Linux, macOS, Windows, and the BSDs — no runtime, no VM, no recompile." ]
                            ]
                        }
                    , Fixpoint.Card.view
                        { n = "02"
                        , title = "In the browser — dnsd.wasm"
                        , body =
                            [ text "The same server compiled to a "
                            , strong [] [ text "~125 KB", text " ", Fixpoint.Code.inline ".wasm", text " module. It runs ", strong [] [ text "100% client-side", text " — edit the config and query the real server, no upload." ] ]
                            ]
                        }
                    ]
                , Fixpoint.Cta.view
                    { body =
                        [ strong [] [ text "Try it live." ]
                        , text " The playground runs the real config loader and DNS wire codec in your browser."
                        ]
                    , href = "/compendium/playground"
                    , label = "Open the Playground →"
                    , attrs = [ attribute "data-mfe-route" "/compendium/playground" ]
                    }
                ]
            }
        , Fixpoint.Section.view
            { id = "features"
            , title = "A server that is deliberately boring"
            , hint = "// records · semantics · Dhall config · hardening · one binary"
            , children =
                [ Fixpoint.Headline.view
                    [ Fixpoint.Headline.card
                        { n = "records"
                        , title = [ text "Eight record types" ]
                        , body = [ p [] [ Fixpoint.Code.inline "A", text ", ", Fixpoint.Code.inline "AAAA", text ", ", Fixpoint.Code.inline "CNAME", text ", ", Fixpoint.Code.inline "TXT", text ", ", Fixpoint.Code.inline "MX", text ", ", Fixpoint.Code.inline "NS", text ", ", Fixpoint.Code.inline "SOA", text ", and ", Fixpoint.Code.inline "CAA", text " (RFC 8659)." ] ]
                        }
                    , Fixpoint.Headline.card
                        { n = "semantics"
                        , title = [ text "Correct DNS semantics" ]
                        , body = [ p [] [ text "Authoritative answer / ", Fixpoint.Code.inline "NODATA", text " / ", Fixpoint.Code.inline "NXDOMAIN", text ", ", Fixpoint.Code.inline "ANY", text ", and suffix name-compression." ] ]
                        }
                    , Fixpoint.Headline.card
                        { n = "config"
                        , title = [ text "The config is code" ]
                        , body = [ p [] [ text "Zones are a ", Fixpoint.Code.inline "Dhall", text " program, typechecked and evaluated at startup — a typo is a type error, not a runtime surprise." ] ]
                        }
                    , Fixpoint.Headline.card
                        { n = "hardening"
                        , title = [ text "Public-server hardening" ]
                        , body = [ p [] [ text "Per-source + global rate limits, bounded answers with TC truncation, no recursion, full bounds-checking." ] ]
                        }
                    , Fixpoint.Headline.card
                        { n = "portable"
                        , title = [ text "One portable binary" ]
                        , body = [ p [] [ text "cosmocc → ", Fixpoint.Code.inline "dnsd.com", text " (APE) plus ", Fixpoint.Code.inline "dnsd.com.dbg", text " (ELF)." ] ]
                        }
                    , Fixpoint.Headline.card
                        { n = "unprivileged"
                        , title = [ text "Least privilege" ]
                        , body = [ p [] [ text "Runs unprivileged under ", Fixpoint.Code.inline "MemoryDenyWriteExecute", text " + a seccomp allowlist, with exactly one capability." ] ]
                        }
                    ]
                ]
            }
        ]


configView : Html Msg
configView =
    div []
        [ Fixpoint.Hero.view
            { prompt = [ Fixpoint.Hero.dollar, text " compendium/config", Fixpoint.Hero.blink ]
            , title = [ text "The Dhall config" ]
            , tagline = [ text "Zones and records, expressed as a typechecked Dhall program." ]
            }
        , Fixpoint.Section.view
            { id = "schema"
            , title = "The schema"
            , hint = "// Record union · Zone · Config"
            , children =
                [ p []
                    [ text "A config is a record of "
                    , Fixpoint.Code.inline "zones"
                    , text ", each a list of "
                    , Fixpoint.Code.inline "Record"
                    , text " union members. The interpreter evaluates the whole program at startup, then walks the normal form into the server's in-memory zone table."
                    ]
                , Fixpoint.Code.block
                    [ Fixpoint.Code.c "let Record ="
                    , text " < A     : { name : Text, ttl : Natural, value : Text }\n"
                    , text "           | AAAA  : { name : Text, ttl : Natural, value : Text }\n"
                    , text "           | CNAME : { name : Text, ttl : Natural, value : Text }\n"
                    , text "           | TXT   : { name : Text, ttl : Natural, value : Text }\n"
                    , text "           | MX    : { name : Text, ttl : Natural, priority : Natural, exchange : Text }\n"
                    , text "           | NS    : { name : Text, ttl : Natural, value : Text }\n"
                    , text "           | SOA   : { name : Text, ttl : Natural, mname : Text, rname : Text\n"
                    , text "                     , serial : Natural, refresh : Natural, retry : Natural\n"
                    , text "                     , expire : Natural, minimum : Natural }\n"
                    , text "           | CAA   : { name : Text, ttl : Natural, flags : Natural, tag : Text, value : Text } >\n"
                    , text "in  let Zone   = { name : Text, records : List Record }\n"
                    , text "in  let Config = { zones : List Zone }"
                    ]
                , Fixpoint.Callout.note
                    [ text "Owner names are relative to the zone ("
                    , Fixpoint.Code.inline "@"
                    , text " / "
                    , Fixpoint.Code.inline "\"\""
                    , text " = apex). rdata target names (CNAME/NS value, MX exchange, SOA mname/rname) are absolute FQDNs with a trailing dot."
                    ]
                ]
            }
        , Fixpoint.Section.view
            { id = "typechecked"
            , title = "Verified before it binds"
            , hint = "// config as code"
            , children =
                [ Fixpoint.Checks.view
                    [ li [] [ text "The config is a ", Fixpoint.Code.inline "Dhall", text " program, not a file — so it is typechecked against its schema before the server ever binds a port." ]
                    , li [] [ text "A typo in a record is a ", Fixpoint.Code.inline "type error", text " (e.g. a bad union label or value), reported at load." ]
                    , li [] [ text "See ", Fixpoint.Code.inline "config.example.dhall", text " for a full two-zone example." ]
                    ]
                ]
            }
        ]


cliView : Html Msg
cliView =
    div []
        [ Fixpoint.Hero.view
            { prompt = [ Fixpoint.Hero.dollar, text " compendium/cli", Fixpoint.Hero.blink ]
            , title = [ text "The dnsd CLI" ]
            , tagline = [ text "A single APE binary that reads a config and serves UDP." ]
            }
        , Fixpoint.Section.view
            { id = "usage"
            , title = "Usage"
            , hint = "// dnsd.com --config <file> [--port n] [--address ip]"
            , children =
                [ Fixpoint.Code.block
                    [ Fixpoint.Code.g "dnsd.com"
                    , text " "
                    , Fixpoint.Code.c "--config"
                    , text " "
                    , Fixpoint.Code.g "config.example.dhall"
                    , text "\n"
                    , Fixpoint.Code.g "dnsd.com"
                    , text " "
                    , Fixpoint.Code.c "-c"
                    , text " "
                    , Fixpoint.Code.g "config.example.dhall"
                    , text " "
                    , Fixpoint.Code.c "-p"
                    , text " "
                    , Fixpoint.Code.g "5353"
                    , text " "
                    , Fixpoint.Code.c "-a"
                    , text " "
                    , Fixpoint.Code.g "127.0.0.1"
                    ]
                , Fixpoint.Checks.view
                    [ li [] [ Fixpoint.Code.inline "-c, --config <file>", text " — Dhall config (required)." ]
                    , li [] [ Fixpoint.Code.inline "-p, --port <n>", text " — listen port (default ", Fixpoint.Code.inline "5353", text ")." ]
                    , li [] [ Fixpoint.Code.inline "-a, --address <ip>", text " — listen address (default ", Fixpoint.Code.inline "127.0.0.1", text ")." ]
                    ]
                ]
            }
        , Fixpoint.Section.view
            { id = "build"
            , title = "Build"
            , hint = "// Makefile · cosmocc"
            , children =
                [ Fixpoint.Code.block
                    [ Fixpoint.Code.c "# fetch the dhall-c interpreter core (once after clone)"
                    , text "\n"
                    , Fixpoint.Code.k "$"
                    , text " "
                    , Fixpoint.Code.g "git submodule update --init"
                    , text "\n"
                    , Fixpoint.Code.c "# build dnsd.com + dnsd.com.dbg"
                    , text "\n"
                    , Fixpoint.Code.k "$"
                    , text " "
                    , Fixpoint.Code.g "make"
                    , text "\n"
                    , Fixpoint.Code.c "# run the full suite (config/lookup/query/wire/rl + live UDP)"
                    , text "\n"
                    , Fixpoint.Code.k "$"
                    , text " "
                    , Fixpoint.Code.g "make test"
                    ]
                , p []
                    [ text "Requires "
                    , Fixpoint.Code.inline "cosmocc"
                    , text " (Cosmopolitan toolchain). The "
                    , Fixpoint.Code.inline "dhall-c"
                    , text " interpreter core is a git submodule at "
                    , Fixpoint.Code.inline "./dhall-c"
                    , text " (override with "
                    , Fixpoint.Code.inline "DHALL_C=<path>"
                    , text ")."
                    ]
                ]
            }
        ]


apiView : Html Msg
apiView =
    div []
        [ Fixpoint.Hero.view
            { prompt = [ Fixpoint.Hero.dollar, text " compendium/api", Fixpoint.Hero.blink ]
            , title = [ text "The C API" ]
            , tagline = [ text "A small, testable wire + lookup core, exposed through dnsd.h." ]
            }
        , Fixpoint.Section.view
            { id = "api"
            , title = "API surface"
            , hint = "// src/dnsd.h"
            , children =
                [ p []
                    [ text "The server's logic is split from the UDP loop so it can be unit-tested and embedded. "
                    , Fixpoint.Code.inline "config_load"
                    , text " evaluates a Dhall file into a "
                    , Fixpoint.Code.inline "DnsConfig"
                    , text "; "
                    , Fixpoint.Code.inline "dns_handle_query"
                    , text " turns one received packet into a response; "
                    , Fixpoint.Code.inline "dns_lookup"
                    , text " exposes the pure lookup semantics."
                    ]
                , Fixpoint.Code.block
                    [ Fixpoint.Code.k "#include"
                    , text " "
                    , Fixpoint.Code.g "dnsd.h"
                    , text "\n\n"
                    , Fixpoint.Code.c "// evaluate a Dhall config file into the server's zone table"
                    , text "\n"
                    , Fixpoint.Code.k "DnsConfig"
                    , text " "
                    , Fixpoint.Code.g "cfg"
                    , text ";\n"
                    , Fixpoint.Code.k "int"
                    , text " "
                    , Fixpoint.Code.g "rc"
                    , text " = "
                    , Fixpoint.Code.g "config_load"
                    , text "("
                    , Fixpoint.Code.g "&cfg"
                    , text ", "
                    , Fixpoint.Code.g "path"
                    , text ", err, "
                    , Fixpoint.Code.k "sizeof"
                    , text " err);\n\n"
                    , Fixpoint.Code.c "// look up a name (lowercase, trailing dot) of a type, filling answers"
                    , text "\n"
                    , Fixpoint.Code.k "int"
                    , text " "
                    , Fixpoint.Code.g "n"
                    , text ", "
                    , Fixpoint.Code.k "int"
                    , text " "
                    , Fixpoint.Code.g "rcode"
                    , text ";\n"
                    , Fixpoint.Code.g "dns_lookup"
                    , text "("
                    , Fixpoint.Code.g "&cfg"
                    , text ", "
                    , Fixpoint.Code.g "qn"
                    , text ", "
                    , Fixpoint.Code.g "T_A"
                    , text ", "
                    , Fixpoint.Code.g "ans"
                    , text ", "
                    , Fixpoint.Code.g "MAX_ANSWERS"
                    , text ", "
                    , Fixpoint.Code.g "&n"
                    , text ", "
                    , Fixpoint.Code.g "&rcode"
                    , text ");"
                    ]
                , p []
                    [ text "The same core, compiled to wasm, is what powers the "
                    , Fixpoint.Code.inline "<compendium-playground>"
                    , text " in your browser (via "
                    , Fixpoint.Code.inline "src/dnsd-wasm.c"
                    , text ")."
                    ]
                ]
            }
        , Fixpoint.Section.view
            { id = "wire"
            , title = "DNS wire format"
            , hint = "// RFC 1035 · record types · rcodes"
            , children =
                [ p []
                    [ text "Responses are capped at "
                    , Fixpoint.Code.inline "MAX_PKT"
                    , text " (512) bytes with "
                    , Fixpoint.Code.inline "TC"
                    , text " truncation; answers are capped at "
                    , Fixpoint.Code.inline "MAX_ANSWERS"
                    , text " (16) per response. The supported rcodes are "
                    , Fixpoint.Code.inline "NOERROR"
                    , text ", "
                    , Fixpoint.Code.inline "FORMERR"
                    , text ", "
                    , Fixpoint.Code.inline "NXDOMAIN"
                    , text ", "
                    , Fixpoint.Code.inline "NOTIMP"
                    , text ", and "
                    , Fixpoint.Code.inline "REFUSED"
                    , text ". EDNS0 is ignored (no large-response amplification)."
                    ]
                , Fixpoint.Callout.warn
                    [ text "The wire path is fully bounds-checked: label-length caps, compressed-pointer depth limits, and cached rdata parsing keep the remote path deterministic and memory-safe."
                    ]
                ]
            }
        , Fixpoint.Section.view
            { id = "ratelimit"
            , title = "Rate limiting"
            , hint = "// rl.c · token buckets"
            , children =
                [ p []
                    [ text "An authoritative nameserver on the open internet is a reflection/amplification target. "
                    , Fixpoint.Code.inline "rl.c"
                    , text " factors the pure token-bucket decision out of the UDP loop so it is unit-tested directly. Per-source "
                    , Fixpoint.Code.inline "burst 100 / 20 q/s"
                    , text "; a rotating-spoofed-source flood is bounded by a "
                    , Fixpoint.Code.inline "global"
                    , text " bucket "
                    , Fixpoint.Code.inline "burst 500 / 100 q/s"
                    , text "."
                    ]
                ]
            }
        ]


playgroundView : Html Msg
playgroundView =
    div []
        [ Fixpoint.Hero.view
            { prompt = [ Fixpoint.Hero.dollar, text " compendium/playground", Fixpoint.Hero.blink ]
            , title = [ text "Playground" ]
            , tagline = [ text "The real config loader and DNS wire codec, compiled to WebAssembly, running in this tab." ]
            }
        , Fixpoint.Section.view
            { id = "playground"
            , title = "Live demo"
            , hint = "// edit the Dhall config · pick a name + record type · query"
            , children =
                [ p []
                    [ text "Edit the Dhall config (a "
                    , Fixpoint.Code.inline "CodeMirror"
                    , text " editor with Dhall highlighting), type a name, pick a record type, then press "
                    , Fixpoint.Code.inline "Query"
                    , text ". The actual "
                    , Fixpoint.Code.inline "config_load"
                    , text " + "
                    , Fixpoint.Code.inline "dns_handle_query"
                    , text " (compiled from the same "
                    , Fixpoint.Code.inline "src/*.c"
                    , text ") evaluate it and answer — decoded rows plus the raw wire bytes."
                    ]
                , node "compendium-playground" [] []
                ]
            }
        ]


footerView : Html Msg
footerView =
    Fixpoint.Footer.view
        [ text "compendium — an authoritative DNS server "
        , a [ href "https://fixpointlinux.org/dhall-c/" ] [ text "configured in Dhall" ]
        , text ", compiled to a portable APE and WebAssembly"
        , Fixpoint.Footer.sep
        , a [ href "https://github.com/fixpoint-linux/compendium" ] [ text "github" ]
        , Fixpoint.Footer.sep
        , text "built with "
        , Fixpoint.Code.inline "cosmocc"
        , text " · runs in your browser via "
        , Fixpoint.Code.inline "emscripten"
        ]
