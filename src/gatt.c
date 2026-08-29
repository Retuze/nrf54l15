#include "gatt.h"

/* ATT opcodes */
#define OP_ERROR              0x01u
#define OP_MTU_REQ            0x02u
#define OP_MTU_RSP            0x03u
#define OP_FIND_INFO_REQ      0x04u
#define OP_FIND_INFO_RSP      0x05u
#define OP_READ_BY_TYPE_REQ   0x08u
#define OP_READ_BY_TYPE_RSP   0x09u
#define OP_READ_REQ           0x0Au
#define OP_READ_RSP           0x0Bu
#define OP_READ_BLOB_REQ      0x0Cu
#define OP_READ_BLOB_RSP      0x0Du
#define OP_READ_BY_GROUP_REQ  0x10u
#define OP_READ_BY_GROUP_RSP  0x11u
#define OP_WRITE_REQ          0x12u
#define OP_WRITE_CMD          0x52u

/* ATT error codes */
#define E_INVALID_HANDLE      0x01u
#define E_READ_NOT_PERM       0x02u
#define E_WRITE_NOT_PERM      0x03u
#define E_INVALID_OFFSET      0x07u
#define E_REQ_NOT_SUP         0x06u
#define E_ATTR_NOT_FOUND      0x0Au

/* Our maximums (no L2CAP fragmentation): a data PDU carries 251 octets, of
 * which 4 are the L2CAP header, leaving 247 for ATT -> server MTU 247. */
#define ATT_SERVER_MTU 247u
#define OUR_MAX_OCTETS 251u

static uint32_t g_mtu       = 23u;   /* effective ATT MTU  (min of the two)   */
static uint32_t g_tx_octets = 27u;   /* effective data-PDU payload we may send */

void gatt_on_connect(void)
{
    g_mtu = 23u;
    g_tx_octets = 27u;
}

void gatt_set_tx_octets(uint32_t octets)
{
    if (octets < 27u)  octets = 27u;
    if (octets > OUR_MAX_OCTETS) octets = OUR_MAX_OCTETS;
    g_tx_octets = octets;
}

uint32_t gatt_dbg_mtu(void)   { return g_mtu; }
uint32_t gatt_dbg_txoct(void) { return g_tx_octets; }

typedef struct {
    uint16_t       handle;
    uint16_t       type;
    const uint8_t *val;
    uint8_t        vlen;
} attr_t;

/* A 180-byte value to exercise DLE + large MTU; filled by gatt_init(). */
static uint8_t v_big[180];

void gatt_init(void)
{
    for (uint32_t i = 0; i < sizeof(v_big); i++) v_big[i] = (uint8_t)i;
}

static const uint8_t v_gap_svc[]   = { 0x00, 0x18 };
static const uint8_t v_name_decl[] = { 0x02, 0x03, 0x00, 0x00, 0x2A };
static const uint8_t v_name[]      = { '5', '4', 'L', '-', 'G', 'A', 'T', 'T' };
static const uint8_t v_appr_decl[] = { 0x02, 0x05, 0x00, 0x01, 0x2A };
static const uint8_t v_appr[]      = { 0x00, 0x00 };
static const uint8_t v_ppcp_decl[] = { 0x02, 0x07, 0x00, 0x04, 0x2A };
static const uint8_t v_ppcp[]      = { 0x18, 0x00, 0x28, 0x00, 0x00, 0x00, 0xF4, 0x01 };
static const uint8_t v_bat_svc[]   = { 0x0F, 0x18 };
static const uint8_t v_batl_decl[] = { 0x02, 0x0A, 0x00, 0x19, 0x2A };
static const uint8_t v_batl[]      = { 0x64 };
/* Custom service 0xFFF0 with a big read characteristic 0xFFF1. */
static const uint8_t v_cust_svc[]  = { 0xF0, 0xFF };
static const uint8_t v_big_decl[]  = { 0x02, 0x0D, 0x00, 0xF1, 0xFF };

static const attr_t db[] = {
    { 0x0001, 0x2800, v_gap_svc,   sizeof(v_gap_svc)   },
    { 0x0002, 0x2803, v_name_decl, sizeof(v_name_decl) },
    { 0x0003, 0x2A00, v_name,      sizeof(v_name)      },
    { 0x0004, 0x2803, v_appr_decl, sizeof(v_appr_decl) },
    { 0x0005, 0x2A01, v_appr,      sizeof(v_appr)      },
    { 0x0006, 0x2803, v_ppcp_decl, sizeof(v_ppcp_decl) },
    { 0x0007, 0x2A04, v_ppcp,      sizeof(v_ppcp)      },
    { 0x0008, 0x2800, v_bat_svc,   sizeof(v_bat_svc)   },
    { 0x0009, 0x2803, v_batl_decl, sizeof(v_batl_decl) },
    { 0x000A, 0x2A19, v_batl,      sizeof(v_batl)      },
    { 0x000B, 0x2800, v_cust_svc,  sizeof(v_cust_svc)  },
    { 0x000C, 0x2803, v_big_decl,  sizeof(v_big_decl)  },
    { 0x000D, 0xFFF1, v_big,       sizeof(v_big)       },
};
#define NDB (sizeof(db) / sizeof(db[0]))

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t att_err(uint8_t *o, uint8_t op, uint16_t h, uint8_t e)
{
    o[0] = OP_ERROR; o[1] = op;
    o[2] = (uint8_t)h; o[3] = (uint8_t)(h >> 8); o[4] = e;
    return 5;
}

/* Longest ATT value chunk we may put in one response. */
static uint32_t max_chunk(void)
{
    uint32_t by_mtu = g_mtu - 1u;              /* opcode uses 1 byte */
    uint32_t by_pdu = g_tx_octets - 4u - 1u;   /* L2CAP(4) + opcode(1) */
    return (by_mtu < by_pdu) ? by_mtu : by_pdu;
}

uint32_t gatt_handle_att(const uint8_t *req, uint32_t len, uint8_t *o)
{
    if (len < 1) return 0;
    uint8_t op = req[0];

    switch (op) {
    case OP_MTU_REQ: {
        uint32_t client = (len >= 3) ? u16(req + 1) : 23u;
        g_mtu = client < ATT_SERVER_MTU ? client : ATT_SERVER_MTU;
        if (g_mtu < 23u) g_mtu = 23u;
        o[0] = OP_MTU_RSP; o[1] = (uint8_t)ATT_SERVER_MTU; o[2] = (uint8_t)(ATT_SERVER_MTU >> 8);
        return 3;
    }

    case OP_READ_BY_GROUP_REQ: {
        if (len < 7) return att_err(o, op, 0, E_ATTR_NOT_FOUND);
        uint16_t s = u16(req + 1), e = u16(req + 3), t = u16(req + 5);
        for (uint32_t i = 0; i < NDB; i++) {
            if (db[i].type == t && db[i].handle >= s && db[i].handle <= e) {
                uint16_t eg = db[i].handle;
                for (uint32_t j = i + 1; j < NDB; j++) {
                    if (db[j].type == 0x2800) break;
                    eg = db[j].handle;
                }
                o[0] = OP_READ_BY_GROUP_RSP; o[1] = (uint8_t)(4 + db[i].vlen);
                o[2] = (uint8_t)db[i].handle; o[3] = (uint8_t)(db[i].handle >> 8);
                o[4] = (uint8_t)eg;           o[5] = (uint8_t)(eg >> 8);
                for (uint8_t k = 0; k < db[i].vlen; k++) o[6 + k] = db[i].val[k];
                return 6u + db[i].vlen;
            }
        }
        return att_err(o, op, s, E_ATTR_NOT_FOUND);
    }

    case OP_READ_BY_TYPE_REQ: {
        if (len < 7) return att_err(o, op, 0, E_ATTR_NOT_FOUND);
        uint16_t s = u16(req + 1), e = u16(req + 3), t = u16(req + 5);
        for (uint32_t i = 0; i < NDB; i++) {
            if (db[i].type == t && db[i].handle >= s && db[i].handle <= e) {
                o[0] = OP_READ_BY_TYPE_RSP; o[1] = (uint8_t)(2 + db[i].vlen);
                o[2] = (uint8_t)db[i].handle; o[3] = (uint8_t)(db[i].handle >> 8);
                for (uint8_t k = 0; k < db[i].vlen; k++) o[4 + k] = db[i].val[k];
                return 4u + db[i].vlen;
            }
        }
        return att_err(o, op, s, E_ATTR_NOT_FOUND);
    }

    case OP_FIND_INFO_REQ: {
        if (len < 5) return att_err(o, op, 0, E_ATTR_NOT_FOUND);
        uint16_t s = u16(req + 1), e = u16(req + 3);
        for (uint32_t i = 0; i < NDB; i++) {
            if (db[i].handle >= s && db[i].handle <= e) {
                o[0] = OP_FIND_INFO_RSP; o[1] = 0x01;
                o[2] = (uint8_t)db[i].handle; o[3] = (uint8_t)(db[i].handle >> 8);
                o[4] = (uint8_t)db[i].type;   o[5] = (uint8_t)(db[i].type >> 8);
                return 6;
            }
        }
        return att_err(o, op, s, E_ATTR_NOT_FOUND);
    }

    case OP_READ_REQ: {
        if (len < 3) return att_err(o, op, 0, E_INVALID_HANDLE);
        uint16_t h = u16(req + 1);
        for (uint32_t i = 0; i < NDB; i++) {
            if (db[i].handle == h) {
                uint32_t n = db[i].vlen;
                uint32_t cap = max_chunk();
                if (n > cap) n = cap;
                o[0] = OP_READ_RSP;
                for (uint32_t k = 0; k < n; k++) o[1 + k] = db[i].val[k];
                return 1u + n;
            }
        }
        return att_err(o, op, h, E_INVALID_HANDLE);
    }

    case OP_READ_BLOB_REQ: {
        if (len < 5) return att_err(o, op, 0, E_INVALID_HANDLE);
        uint16_t h = u16(req + 1), off = u16(req + 3);
        for (uint32_t i = 0; i < NDB; i++) {
            if (db[i].handle == h) {
                if (off > db[i].vlen) return att_err(o, op, h, E_INVALID_OFFSET);
                uint32_t n = db[i].vlen - off;
                uint32_t cap = max_chunk();
                if (n > cap) n = cap;
                o[0] = OP_READ_BLOB_RSP;
                for (uint32_t k = 0; k < n; k++) o[1 + k] = db[i].val[off + k];
                return 1u + n;
            }
        }
        return att_err(o, op, h, E_INVALID_HANDLE);
    }

    case OP_WRITE_CMD:
        return 0;

    case OP_WRITE_REQ: {
        uint16_t h = (len >= 3) ? u16(req + 1) : 0;
        return att_err(o, op, h, E_WRITE_NOT_PERM);
    }

    default:
        return att_err(o, op, 0, E_REQ_NOT_SUP);
    }
}
