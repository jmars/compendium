/* config.c — evaluate a Dhall config file and walk its normal form into a
   DnsConfig (parse_source -> infer_type -> normalize -> walk the Term tree).
   No JSON round-trip; the config is typechecked against its own schema
   (partial union literals are supported by the typechecker), and the walk
   still applies full runtime type-guarding. Ported from the verified
   config_walk.c artifact with the name-canonicalization completion (trailing
   dot on zone + rdata target names, lowercase, full owner FQDN built from
   relative name + zone). */
#include "dhall.h"
#include "dnsd.h"
#include <ctype.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <netinet/in.h>

static char cfg_err[256];
static void cfg_error(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(cfg_err, sizeof cfg_err, fmt, ap); va_end(ap);
}

/* Look up a record-literal field BY LABEL. normalize() sorts fields
   alphabetically, so index-based access is wrong. */
static Term *rec_get(Term *t, const char *label) {
    if (!t || t->tag != TmRecordLit) return NULL;
    for (int i = 0; i < t->as.rec.n; i++)
        if (!strcmp(t->as.rec.fs[i].label, label)) return t->as.rec.fs[i].value;
    return NULL;
}

static bool text_flat(Term *t, char *out, size_t cap) {
    if (!t || t->tag != TmText || !t->as.text) { cfg_error("expected Text"); return false; }
    size_t n = 0;
    for (TextPart *p = t->as.text; p; p = p->next) {
        if (p->expr) { cfg_error("text interpolation not normalized"); return false; }
        if (p->lit) n += strlen(p->lit);
    }
    if (n + 1 > cap) { cfg_error("text too long"); return false; }
    out[0] = '\0';
    for (TextPart *p = t->as.text; p; p = p->next) if (p->lit) strcat(out, p->lit);
    return true;
}

static bool nat_u64(Term *t, uint64_t *out) {
    if (!t || t->tag != TmConst || t->as.c.kind != C_NAT) { cfg_error("expected Natural"); return false; }
    if (t->as.c.bnat) { cfg_error("Natural exceeds uint64"); return false; }
    *out = t->as.c.nat;
    return true;
}

static int list_elems(Term *t, Term **elems, int cap) {
    int n = 0;
    for (Term *p = t; p->tag == TmCons; p = p->as.cons.tail) {
        if (n == cap) { cfg_error("list too long (cap %d)", cap); return -1; }
        elems[n++] = p->as.cons.head;
    }
    if (t->tag != TmNil && n == 0) { cfg_error("expected a list"); return -1; }
    return n;
}

static void lower_inplace(char *s) {
    for (char *p = s; *p; p++) *p = (char)tolower((unsigned char)*p);
}

/* Validate a (lowercased) relative owner name: each label 1..63 chars, total
   <= MAX_NAME_LEN, no empty labels, no leading/trailing dot (except the single
   "@" apex marker). Returns true if OK. */
static bool valid_owner(const char *s) {
    if (!s || !*s) return true;               /* empty = apex */
    if (!strcmp(s, "@")) return true;
    size_t tot = 0, lab = 0;
    for (const char *p = s;; p++) {
        if (*p == '\0' || *p == '.') {
            if (lab == 0 || lab > 63) { cfg_error("invalid owner name label (empty or >63)"); return false; }
            lab = 0;
            if (*p == '\0') break;
        } else {
            lab++;
            tot++;
            if (tot > MAX_NAME_LEN) { cfg_error("owner name too long"); return false; }
        }
    }
    return true;
}

/* Canonicalize a domain name in place: lowercase, and ensure a trailing dot.
   (This is the Stage-1 completion: artifact-1 left trailing-dot handling as a
   stub; zone names and rdata target names must end with '.'.) Returns false and
   sets cfg_error if the buffer has no room for the trailing dot or the name has
   a label that is empty or >63 chars (RFC 1035) or exceeds MAX_NAME_LEN. */
static bool canon_fqdn(char *s, size_t cap) {
    size_t n = strlen(s);
    lower_inplace(s);
    /* Validate labels: 1..63 chars each, total <= MAX_NAME_LEN. A trailing dot
       is the root terminator, not an empty label. */
    size_t tot = 0, lab = 0;
    for (size_t i = 0; i <= n; i++) {
        char c = s[i];
        if (c == '.' && i + 1 == n) break;      /* trailing dot = root terminator */
        if (c == '\0' || c == '.') {
            if (lab == 0 || lab > 63) { cfg_error("invalid FQDN label (empty or >63)"); return false; }
            lab = 0;
            if (c == '\0') break;
        } else {
            lab++;
            tot++;
            if (tot > MAX_NAME_LEN) { cfg_error("FQDN too long"); return false; }
        }
    }
    if (n > 0 && s[n - 1] != '.') {
        if (n + 1 >= cap) { cfg_error("name too long"); return false; }
        s[n] = '.'; s[n + 1] = '\0';
    }
    return true;
}

/* Build the full owner FQDN for a record: relative name (@ or "" = apex)
   joined to the zone. out must hold zone + relative + separator. */
static void build_owner(char *out, size_t cap, const char *relative, const char *zone) {
    if (!relative || relative[0] == '\0') {
        snprintf(out, cap, "%s", zone);
    } else {
        snprintf(out, cap, "%s.%s", relative, zone);
    }
}

static bool walk_record(DnsRecord *out, Term *t, const char *zone) {
    if (t->tag != TmUnionLit) { cfg_error("record must be a union literal"); return false; }
    const char *rtype = NULL; Term *p = NULL;
    for (int i = 0; i < t->as.uni.n; i++)
        if (t->as.uni.fs[i].value) { rtype = t->as.uni.fs[i].label; p = t->as.uni.fs[i].value; break; }
    if (!rtype || !p) { cfg_error("record union has no selected alternative"); return false; }
    memset(out, 0, sizeof *out);
    if      (!strcmp(rtype, "A"))     out->type = RT_A;
    else if (!strcmp(rtype, "AAAA"))  out->type = RT_AAAA;
    else if (!strcmp(rtype, "CNAME")) out->type = RT_CNAME;
    else if (!strcmp(rtype, "TXT"))   out->type = RT_TXT;
    else if (!strcmp(rtype, "MX"))    out->type = RT_MX;
    else if (!strcmp(rtype, "NS"))    out->type = RT_NS;
    else if (!strcmp(rtype, "SOA"))   out->type = RT_SOA;
    else if (!strcmp(rtype, "CAA"))   out->type = RT_CAA;
    else { cfg_error("unknown record type '%s'", rtype); return false; }
    static char name[MAX_NAME_LEN + 1];
    uint64_t ttl = 0;
    if (!text_flat(rec_get(p, "name"), name, sizeof name)) return false;
    if (!nat_u64(rec_get(p, "ttl"), &ttl)) return false;
    if (ttl > UINT32_MAX) { cfg_error("ttl out of range"); return false; }
    out->ttl = (uint32_t)ttl;
    lower_inplace(name);
    if (!strcmp(name, "@")) name[0] = '\0';
    if (!valid_owner(name)) return false;
    out->name = arena_strdup(dhall_arena, name);
    static char ownerbuf[MAX_NAME_LEN * 2 + 2];
    build_owner(ownerbuf, sizeof ownerbuf, name, zone);
    out->owner = arena_strdup(dhall_arena, ownerbuf);
    static char v[MAX_TEXT_LEN + 1], v2[MAX_TEXT_LEN + 1];
    switch (out->type) {
    case RT_A: case RT_AAAA: case RT_TXT:
        if (!text_flat(rec_get(p, "value"), v, sizeof v)) return false;
        /* Parse and validate A/AAAA rdata ONCE here; build_answer copies the
           cached bytes per query (no per-query inet_pton). Invalid addresses
           now fail config load instead of silently dropping the RR (with TC)
           on every response — fail-fast is the stricter, operator-friendly
           behavior. */
        if (out->type == RT_A) {
            struct in_addr a;
            if (inet_pton(AF_INET, v, &a) != 1) { cfg_error("invalid IPv4 address '%s'", v); return false; }
            memcpy(&out->addr4, &a, 4);
        } else if (out->type == RT_AAAA) {
            struct in6_addr a6;
            if (inet_pton(AF_INET6, v, &a6) != 1) { cfg_error("invalid IPv6 address '%s'", v); return false; }
            memcpy(out->addr6, &a6, 16);
        }
        out->value = arena_strdup(dhall_arena, v); break;
    case RT_CNAME: case RT_NS:
        if (!text_flat(rec_get(p, "value"), v, sizeof v)) return false;
        if (!canon_fqdn(v, sizeof v)) return false;   /* rdata target name: trailing dot + lowercase */
        out->value = arena_strdup(dhall_arena, v); break;
    case RT_MX: {
        uint64_t pri = 0;
        if (!nat_u64(rec_get(p, "priority"), &pri)) return false;
        if (pri > UINT16_MAX) { cfg_error("MX priority out of range"); return false; }
        out->priority = (uint16_t)pri;
        if (!text_flat(rec_get(p, "exchange"), v, sizeof v)) return false;
        if (!canon_fqdn(v, sizeof v)) return false;   /* rdata target name */
        out->exchange = arena_strdup(dhall_arena, v); break;
    }
    case RT_SOA: {
        uint64_t serial=0, refresh=0, retry=0, expire=0, minimum=0;
        if (!text_flat(rec_get(p, "mname"), v, sizeof v)) return false;
        if (!text_flat(rec_get(p, "rname"), v2, sizeof v2)) return false;
        if (!nat_u64(rec_get(p, "serial"), &serial)) return false;
        if (!nat_u64(rec_get(p, "refresh"), &refresh)) return false;
        if (!nat_u64(rec_get(p, "retry"), &retry)) return false;
        if (!nat_u64(rec_get(p, "expire"), &expire)) return false;
        if (!nat_u64(rec_get(p, "minimum"), &minimum)) return false;
        if (serial > UINT32_MAX || refresh > UINT32_MAX || retry > UINT32_MAX ||
            expire > UINT32_MAX || minimum > UINT32_MAX) { cfg_error("SOA field out of range"); return false; }
        if (!canon_fqdn(v, sizeof v)) return false;   /* rdata target names */
        if (!canon_fqdn(v2, sizeof v2)) return false;
        out->mname = arena_strdup(dhall_arena, v); out->rname = arena_strdup(dhall_arena, v2);
        out->serial=(uint32_t)serial; out->refresh=(uint32_t)refresh;
        out->retry=(uint32_t)retry;   out->expire=(uint32_t)expire;
        out->minimum=(uint32_t)minimum;
        break;
    }
    case RT_CAA: {
        uint64_t flags = 0;
        if (!nat_u64(rec_get(p, "flags"), &flags)) return false;
        if (flags > UINT8_MAX) { cfg_error("CAA flags out of range"); return false; }
        /* RFC 8659 §4.1: only the MSB (0x80, issuer-critical) is defined; all
           other bits are reserved and MUST be zero. Reject misconfigured flags. */
        if (flags & ~0x80u) { cfg_error("CAA flags has reserved bits set"); return false; }
        out->flags = (uint8_t)flags;
        if (!text_flat(rec_get(p, "tag"), v, sizeof v)) return false;
        if (!text_flat(rec_get(p, "value"), v2, sizeof v2)) return false;
        size_t tlen = strlen(v);
        if (tlen < 1 || tlen > 15) { cfg_error("CAA tag must be 1-15 chars"); return false; }
        for (size_t i = 0; i < tlen; i++) {
            char c = v[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-')) {
                cfg_error("CAA tag has invalid character"); return false;
            }
        }
        out->tag = arena_strdup(dhall_arena, v);
        out->value = arena_strdup(dhall_arena, v2);   /* CAA value (raw octets) */
        break;
    }
    }
    return true;
}

static bool build_config(DnsConfig *cfg, Term *nf) {
    memset(cfg, 0, sizeof *cfg);
    if (!nf || nf->tag != TmRecordLit) { cfg_error("config must be a record"); return false; }
    Term *zones = rec_get(nf, "zones");
    if (!zones) { cfg_error("config missing 'zones'"); return false; }
    static Term *zelems[MAX_ZONES];
    int nz = list_elems(zones, zelems, MAX_ZONES);
    if (nz < 0) return false;
    for (int i = 0; i < nz; i++) {
        DnsZone *z = &cfg->zones[cfg->nzones];
        static char zname[MAX_NAME_LEN + 1];
        if (!text_flat(rec_get(zelems[i], "name"), zname, sizeof zname)) return false;
        if (!canon_fqdn(zname, sizeof zname)) return false;   /* zone name: lowercase + trailing dot */
        z->name = arena_strdup(dhall_arena, zname);
        Term *records = rec_get(zelems[i], "records");
        if (!records) { cfg_error("zone missing 'records'"); return false; }
        static Term *relems[MAX_RECORDS];
        int nr = list_elems(records, relems, MAX_RECORDS);
        if (nr < 0) return false;
        for (int j = 0; j < nr; j++) {
            if (!walk_record(&z->records[z->nrecords], relems[j], z->name)) return false;
            z->nrecords++;
        }
        cfg->nzones++;
    }
    return true;
}

static char *read_all(FILE *f, size_t *len_out) {
    size_t cap = 65536, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        if (len == cap) { cap *= 2; char *nb = realloc(buf, cap); if (!nb) { free(buf); return NULL; } buf = nb; }
        size_t n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (n == 0) break;
    }
    buf[len] = '\0';
    if (len_out) *len_out = len;
    return buf;
}

int config_load(DnsConfig *cfg, const char *path, char *errbuf, size_t errcap) {
    if (errbuf && errcap > 0) errbuf[0] = '\0';
    FILE *in = fopen(path, "rb");
    if (!in) { snprintf(errbuf, errcap, "cannot open config file '%s'", path); return -1; }
    size_t src_len = 0;
    char *src = read_all(in, &src_len);
    fclose(in);
    if (!src) { snprintf(errbuf, errcap, "out of memory reading '%s'", path); return -1; }

    if (!dhall_arena) dhall_arena = arena_new();
    arena_reset(dhall_arena);

    ImportLoader *loader = import_loader_new();
    import_loader_push_root(loader, path);

    Parser p;
    memset(&p, 0, sizeof p);
    p.loader = loader;
    DhallError err;
    dhall_error_clear(&err);

    Term *t = parse_source(&p, src, path, &err);
    free(src);
    if (!t) {
        snprintf(errbuf, errcap, "config parse error: %s", err.msg);
        import_loader_free(loader);
        return -1;
    }
    /* Typecheck the config against its own schema before walking it. */
    Term *ty = infer_type(&p, t, &err);
    if (!ty) {
        snprintf(errbuf, errcap, "config type error: %s", err.msg);
        import_loader_free(loader);
        return -1;
    }
    normalize_clear_error();
    Term *nf = normalize(t);
    if (normalize_has_error()) {
        err = *normalize_get_error();
        snprintf(errbuf, errcap, "config normalize error: %s", err.msg);
        import_loader_free(loader);
        return -1;
    }
    import_loader_free(loader);

    bool ok = build_config(cfg, nf);
    if (!ok) {
        snprintf(errbuf, errcap, "config error: %s", cfg_err);
        return -1;
    }
    return 0;
}
