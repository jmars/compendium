let Record =
      < A     : { name : Text, ttl : Natural, value : Text }
      | AAAA  : { name : Text, ttl : Natural, value : Text }
      | CNAME : { name : Text, ttl : Natural, value : Text }
      | TXT   : { name : Text, ttl : Natural, value : Text }
      | MX    : { name : Text, ttl : Natural, priority : Natural, exchange : Text }
      | NS    : { name : Text, ttl : Natural, value : Text }
      | SOA   : { name : Text, ttl : Natural, mname : Text, rname : Text, serial : Natural
                , refresh : Natural, retry : Natural, expire : Natural, minimum : Natural }
      | CAA   : { name : Text, ttl : Natural, flags : Natural, tag : Text, value : Text }
      >
in  let Zone   = { name : Text, records : List Record }
in  let Config = { zones : List Zone }
in  { zones =
      [ { name = "example.com."
        , records =
          [ < SOA   = { name = "@", ttl = 3600, mname = "ns1.example.com.", rname = "hostmaster.example.com."
                      , serial = 2024010101, refresh = 7200, retry = 3600, expire = 1209600, minimum = 300 } >
          , < NS    = { name = "@", ttl = 3600, value = "ns1.example.com." } >
          , < A     = { name = "@", ttl = 3600, value = "192.0.2.1" } >
          , < AAAA  = { name = "@", ttl = 3600, value = "2001:db8::1" } >
          , < MX    = { name = "@", ttl = 3600, priority = 10, exchange = "mail.example.com." } >
          , < CNAME = { name = "www", ttl = 3600, value = "example.com." } >
          , < TXT   = { name = "@", ttl = 3600, value = "v=spf1 -all" } >
          , < CAA   = { name = "@", ttl = 3600, flags = 0, tag = "issue", value = "letsencrypt.org" } >
          ]
        }
      , { name = "example.org."
        , records =
          [ < SOA = { name = "@", ttl = 3600, mname = "ns1.example.org.", rname = "admin.example.org."
                    , serial = 1, refresh = 7200, retry = 3600, expire = 1209600, minimum = 300 } >
          , < A   = { name = "@", ttl = 300, value = "203.0.113.7" } >
          ]
        }
      ]
    } : Config
