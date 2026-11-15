/* SPDX-License-Identifier: Apache-2.0 */
#ifndef IPHETH_TRAFFIC_H
#define IPHETH_TRAFFIC_H
#include <stddef.h>
#include <stdint.h>
/* Header counters only; no payload logging, allocation or network policy.
 * Extension headers are counted as IPv6, but not guessed to be TCP/ICMP. */
struct ipheth_traffic {
    uint32_t ipv6, ns, na, syn, syn_ack, rst;
    uint16_t source_port, destination_port;
};
static inline void ipheth_traffic_observe(struct ipheth_traffic *s,
                                        const uint8_t *p, size_t n)
{
    if (!p || n < 54 || p[12] != 0x86 || p[13] != 0xdd || p[14] >> 4 != 6) return;
    ++s->ipv6;
    size_t payload = ((size_t)p[18] << 8) | p[19];
    if (payload > n - 54) return;
    if (p[20] == 58 && payload >= 24) {
        s->ns += p[54] == 135;
        s->na += p[54] == 136;
    } else if (p[20] == 6 && payload >= 20) {
        s->source_port = (uint16_t)((p[54] << 8) | p[55]);
        s->destination_port = (uint16_t)((p[56] << 8) | p[57]);
        if ((p[66] >> 4) < 5 || (size_t)(p[66] >> 4) * 4 > payload) return;
        unsigned flags = p[67];
        s->syn += (flags & 0x12) == 0x02;
        s->syn_ack += (flags & 0x12) == 0x12;
        s->rst += !!(flags & 0x04);
    }
}
#endif
