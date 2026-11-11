/* SPDX-License-Identifier: Apache-2.0 */
#include "ipheth.h"
#include <string.h>
static unsigned le16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static bool frame_size(size_t n) { return n >= 14 && n <= IPHETH_FRAME_MAX; }
int ipheth_find_interface(uint16_t vid, const uint8_t *d, size_t n,
                          uint8_t number, struct ipheth_interface *r)
{
    struct ipheth_interface cur = {0}, best = {0};
    bool match = false, found = false;
    unsigned expected = 0, seen = 0;
    if (!d || !r || vid != IPHETH_APPLE_VID || n < 9 || d[0] < 9 || d[1] != 2)
        return IPHETH_EINVAL;
    size_t total = le16(d + 2);
    if (total > n || total < 9) return IPHETH_EINVAL;
    for (size_t off = 0; off < total;) {
        unsigned len = d[off];
        if (len < 2 || len > total - off) return IPHETH_EINVAL;
        const uint8_t *p = d + off;
        if (p[1] == 4) {
            if (match && seen != expected) return IPHETH_EINVAL;
            if (match && cur.in && cur.out && (!found || cur.alternate < best.alternate)) {
                best = cur; found = true;
            }
            if (len < 9) return IPHETH_EINVAL;
            memset(&cur, 0, sizeof(cur));
            match = p[2] == number && p[5] == 0xff && p[6] == 0xfd && p[7] == 1;
            cur.number = p[2]; cur.alternate = p[3]; expected = p[4]; seen = 0;
        } else if (p[1] == 5 && match) {
            if (len < 7 || ++seen > expected) return IPHETH_EINVAL;
            if ((p[3] & 3) == 2) {
                unsigned mps = le16(p + 4) & 0x7ff;
                if (!(p[2] & 15) || (p[2] & 0x70) || !mps || mps > 512)
                    return IPHETH_EINVAL;
                if (p[2] & 0x80) {
                    if (cur.in) return IPHETH_EINVAL;
                    cur.in = p[2]; cur.in_mps = (uint16_t)mps;
                } else {
                    if (cur.out) return IPHETH_EINVAL;
                    cur.out = p[2]; cur.out_mps = (uint16_t)mps;
                }
            }
        }
        off += len;
    }
    if (match && seen != expected) return IPHETH_EINVAL;
    if (match && cur.in && cur.out && (!found || cur.alternate < best.alternate)) {
        best = cur; found = true;
    }
    if (!found) return IPHETH_ENODEV;
    *r = best;
    return 0;
}
int ipheth_request_init(struct ipheth_request *r, uint8_t cmd, uint8_t intf)
{
    if (!r || (cmd != IPHETH_CMD_MAC && cmd != IPHETH_CMD_NCM && cmd != IPHETH_CMD_CARRIER))
        return IPHETH_EINVAL;
    r->type = cmd == IPHETH_CMD_NCM ? 0x41 : 0xc0;
    r->request = cmd; r->value = 0; r->index = intf;
    r->length = cmd == IPHETH_CMD_NCM ? 0 : IPHETH_CTRL_SIZE;
    return 0;
}
int ipheth_mac(const uint8_t *p, size_t n, uint8_t mac[6])
{
    uint8_t any = 0;
    if (!p || !mac || n < 6 || n > IPHETH_CTRL_SIZE || (p[0] & 1)) return IPHETH_EINVAL;
    for (unsigned i = 0; i < 6; i++) any |= p[i];
    if (!any) return IPHETH_EINVAL;
    memcpy(mac, p, 6); return 0;
}
int ipheth_carrier(const uint8_t *p, size_t n, bool *up)
{
    if (!p || !up || !n || n > IPHETH_CTRL_SIZE) return IPHETH_EINVAL;
    *up = p[n == 1 ? 0 : 1] == 4; return 0;
}
static int ncm(const uint8_t *p, size_t n, ipheth_frame_fn fn, void *arg)
{
    if (n < 108 || n > IPHETH_RX_NCM_MAX || memcmp(p, "NCMH", 4) ||
        le16(p + 4) != 12 || le16(p + 10) != 12 || memcmp(p + 12, "NCM0", 4))
        return IPHETH_EINVAL;
    /* wBlockLength may wrap to zero for a full 64 KiB transfer. */
    size_t block = le16(p + 8);
    if (!block && n == 65536) block = n;
    unsigned table = le16(p + 16);
    if (block != n || table < 12 || table > 96 || (table & 3) || le16(p + 18))
        return IPHETH_EINVAL;
    unsigned count = 0;
    bool end = false;
    for (unsigned i = 20; i + 4 <= 12 + table; i += 4) {
        unsigned start = le16(p + i), len = le16(p + i + 2);
        if (!start && !len) { end = true; break; }
        if (start < 108 || start > n || len > n - start || !frame_size(len))
            return IPHETH_EINVAL;
        for (unsigned j = 0; j < count; j++) {
            unsigned a = le16(p + 20 + j * 4), b = le16(p + 22 + j * 4);
            if (start < a + b && a < start + len) return IPHETH_EINVAL;
        }
        count++;
    }
    if (!end) return IPHETH_EINVAL;
    for (unsigned i = 0; i < count; i++) {
        int rc = fn(arg, p + le16(p + 20 + i * 4), le16(p + 22 + i * 4));
        if (rc) return rc;
    }
    return (int)count;
}
int ipheth_receive(enum ipheth_rx_mode mode, const uint8_t *p, size_t n,
                   ipheth_frame_fn fn, void *arg)
{
    if (!p || !fn) return IPHETH_EINVAL;
    if (mode == IPHETH_NCM) return ncm(p, n, fn, arg);
    if (mode != IPHETH_LEGACY || n < 2 || !frame_size(n - 2)) return IPHETH_EINVAL;
    int rc = fn(arg, p + 2, n - 2);
    return rc ? rc : 1;
}
int ipheth_transmit(const uint8_t *p, size_t n, uint8_t *out, size_t cap)
{
    size_t size = n < 60 ? 60 : n;
    if (!p || !out || !frame_size(n) || cap < size) return IPHETH_EINVAL;
    memmove(out, p, n);
    memset(out + n, 0, size - n);
    return (int)size;
}
