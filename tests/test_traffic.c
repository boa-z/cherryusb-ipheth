/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <string.h>
#include "../port/rtthread/ipheth_traffic.h"
int main(void)
{
    struct ipheth_traffic s = {0};
    uint8_t frame[128] = {0};
    frame[12] = 0x86; frame[13] = 0xdd; frame[14] = 0x60;
    frame[19] = 24; frame[20] = 58; frame[21] = 255; frame[54] = 135;
    frame[6] = 0x9e; frame[22] = 0xfe; frame[38] = 0xff; frame[62] = 0xfe;
    ipheth_traffic_observe(&s, NULL, 0);
    for (size_t n = 0; n < 78; ++n) ipheth_traffic_observe(&s, frame, n);
    assert(!s.ns && !s.na && !s.neighbor_next);
    for (unsigned i = 0; i < 9; ++i) {
        frame[77] = i;
        ipheth_traffic_observe(&s, frame, 78);
    }
    assert(s.ns == 9 && s.na == 0 && s.neighbor_next == 1);
    for (unsigned j = 0; j < 4; ++j) {
        const struct ipheth_neighbor *r = &s.neighbors[(s.neighbor_next + j) % 4];
        assert(r->ordinal == 6+j && r->target[15] == 5+j);
        assert(r->source[0] == 0xfe && r->destination[0] == 0xff);
        assert(r->source_mac[0] == 0x9e && r->type == 135 && r->hop_limit == 255);
    }
    frame[54] = 136;
    ipheth_traffic_observe(&s, frame, 78);
    assert(s.na == 1 && s.neighbors[1].ordinal == 10 && s.neighbors[1].type == 136);
    struct ipheth_neighbor saved = s.neighbors[1];
    memset(frame, 0xa5, sizeof(frame));
    assert(s.neighbors[1].target[15] == saved.target[15]); /* Borrowed RX buffer can retire. */
    frame[12] = 0x86; frame[13] = 0xdd; frame[14] = 0x60;
    frame[19] = 64; frame[18] = 0; frame[20] = 0; /* Extension not guessed as ICMP. */
    ipheth_traffic_observe(&s, frame, 118);
    assert(s.na == 1 && s.ns == 9);
    frame[20] = 6; frame[66] = 0x50; frame[67] = 0x12;
    frame[54] = 0x1b; frame[55] = 0x58; frame[56] = 0xc0; frame[57] = 0x00;
    ipheth_traffic_observe(&s, frame, 118);
    assert(s.syn_ack == 1 && s.source_port == 7000 && s.destination_port == 49152);
    assert(s.na == 1 && s.ns == 9);
    memset(&s, 0, sizeof(s));
    for (unsigned j = 0; j < 4; ++j) assert(!s.neighbors[j].ordinal);
    return 0;
}
