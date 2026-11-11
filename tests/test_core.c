/* SPDX-License-Identifier: Apache-2.0 */
#include "ipheth.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned frames;
static int consume(void *arg, const uint8_t *p, size_t n)
{ assert(arg == &frames); assert(p); assert(n >= 14 && n <= 1514); frames++; return 0; }
static int reject(void *arg, const uint8_t *p, size_t n)
{ (void)arg; (void)p; (void)n; return -19; }
static void put16(uint8_t *p, unsigned n) { p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); }
static uint8_t descriptor[] = {
    9,2, 0,0, 2,4,0,0x80,50,
    9,4,1,0,2,0xff,0xfe,2,0, /* USBMUX must remain untouched */
    7,5,0x81,2,0,2,0, 7,5,2,2,0,2,0,
    9,4,7,0,0,0xff,0xfd,1,0,
    9,4,7,2,2,0xff,0xfd,1,0,
    7,5,0x89,2,0,2,0, 7,5,3,2,0,2,0,
    9,4,7,1,2,0xff,0xfd,1,0,
    7,5,0x86,2,0,2,0, 7,5,5,2,0,2,0
};
static void descriptors(void)
{
    struct ipheth_interface r;
    put16(descriptor + 2, sizeof(descriptor));
    assert(ipheth_find_interface(0x05ac, descriptor, sizeof(descriptor), 7, &r) == 0);
    assert(r.number == 7 && r.alternate == 1 && r.in == 0x86 && r.out == 5);
    assert(r.in_mps == 512 && r.out_mps == 512);
    assert(ipheth_find_interface(0x05ac, descriptor, sizeof(descriptor), 1, &r) < 0);
    assert(ipheth_find_interface(0x1234, descriptor, sizeof(descriptor), 7, &r) < 0);
    for (size_t i = 0; i < sizeof(descriptor); i++)
        assert(ipheth_find_interface(0x05ac, descriptor, i, 7, &r) < 0);
    uint8_t copy[sizeof(descriptor)];
    for (size_t i = 0; i < sizeof(descriptor); i++) {
        memcpy(copy, descriptor, sizeof(copy)); copy[i] = 0;
        (void)ipheth_find_interface(0x05ac, copy, sizeof(copy), 7, &r);
    }
    memcpy(copy, descriptor, sizeof(copy)); copy[0] = 0;
    assert(ipheth_find_interface(0x05ac, copy, sizeof(copy), 7, &r) < 0);
}
static void controls(void)
{
    struct ipheth_request r;
    assert(!ipheth_request_init(&r, IPHETH_CMD_MAC, 7));
    assert(r.type == 0xc0 && r.index == 7 && r.length == 64 && !r.value);
    assert(!ipheth_request_init(&r, IPHETH_CMD_NCM, 9));
    assert(r.type == 0x41 && r.index == 9 && !r.length);
    assert(ipheth_request_init(&r, 99, 2) < 0);
    uint8_t p[64] = {2,1,2,3,4,5}, mac[6]; bool up;
    assert(!ipheth_mac(p, 6, mac) && !memcmp(p, mac, 6));
    assert(ipheth_mac(p, 5, mac) < 0);
    p[0] = 1; assert(ipheth_mac(p, 6, mac) < 0);
    memset(p, 0, sizeof(p)); assert(ipheth_mac(p, 64, mac) < 0);
    p[0] = 4; assert(!ipheth_carrier(p, 1, &up) && up);
    p[1] = 0; assert(!ipheth_carrier(p, 2, &up) && !up);
    p[1] = 4; assert(!ipheth_carrier(p, 2, &up) && up);
    assert(ipheth_carrier(p, 0, &up) < 0);
}
static void framing(void)
{
    uint8_t legacy[1517] = {0}, out[1514];
    frames = 0;
    assert(ipheth_receive(IPHETH_LEGACY, legacy, 16, consume, &frames) == 1);
    assert(frames == 1);
    assert(ipheth_receive(IPHETH_LEGACY, legacy, 1516, consume, &frames) == 1);
    assert(ipheth_receive(IPHETH_LEGACY, legacy, 1517, consume, &frames) < 0);
    for (size_t i = 0; i < 16; i++) assert(ipheth_receive(IPHETH_LEGACY, legacy, i, consume, &frames) < 0);
    assert(ipheth_receive(IPHETH_LEGACY, legacy, 16, reject, NULL) == -19);
    const size_t sizes[] = {14, 42, 60, 64, 70, 512, 1514};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        size_t n = sizes[i];
        memset(out, 0xa5, sizeof(out));
        assert(ipheth_transmit(legacy, n, out, n) == (int)n);
        assert(!memcmp(legacy, out, n));
        if (n < sizeof(out)) assert(out[n] == 0xa5);
    }
    assert(ipheth_transmit(legacy, 1514, out, sizeof(out)) == 1514);
    assert(ipheth_transmit(legacy, 1515, out, sizeof(out)) < 0);
    assert(ipheth_transmit(legacy, 14, out, 13) < 0);
    uint8_t ncm[200] = {0};
    memcpy(ncm, "NCMH", 4); put16(ncm+4, 12); put16(ncm+8, sizeof(ncm)); put16(ncm+10, 12);
    memcpy(ncm+12, "NCM0", 4); put16(ncm+16, 96);
    put16(ncm+20, 108); put16(ncm+22, 14);
    put16(ncm+24, 122); put16(ncm+26, 60);
    frames = 0;
    assert(ipheth_receive(IPHETH_NCM, ncm, sizeof(ncm), consume, &frames) == 2 && frames == 2);
    for (size_t i = 0; i < sizeof(ncm); i++)
        assert(ipheth_receive(IPHETH_NCM, ncm, i, consume, &frames) < 0);
    put16(ncm+24, 121); frames = 0;
    assert(ipheth_receive(IPHETH_NCM, ncm, sizeof(ncm), consume, &frames) < 0 && !frames);
    put16(ncm+24, 122); put16(ncm+26, 200);
    assert(ipheth_receive(IPHETH_NCM, ncm, sizeof(ncm), consume, &frames) < 0 && !frames);
    put16(ncm+26, 60); put16(ncm+28, 190); /* half-null DPE */
    assert(ipheth_receive(IPHETH_NCM, ncm, sizeof(ncm), consume, &frames) < 0 && !frames);
    put16(ncm+28, 0); put16(ncm+18, 12); /* reject chained NDP */
    assert(ipheth_receive(IPHETH_NCM, ncm, sizeof(ncm), consume, &frames) < 0);
    put16(ncm+18, 0); put16(ncm+16, 16); /* no terminator */
    assert(ipheth_receive(IPHETH_NCM, ncm, sizeof(ncm), consume, &frames) < 0);
    assert(ipheth_receive(IPHETH_NCM, ncm, sizeof(ncm), NULL, &frames) < 0);
    static uint8_t full[65536];
    memcpy(full, "NCMH", 4); put16(full+4, 12); put16(full+10, 12);
    memcpy(full+12, "NCM0", 4); put16(full+16, 96);
    put16(full+20, 65522); put16(full+22, 14); frames = 0;
    assert(ipheth_receive(IPHETH_NCM, full, sizeof(full), consume, &frames) == 1 && frames == 1);
    put16(full+22, 15); frames = 0;
    assert(ipheth_receive(IPHETH_NCM, full, sizeof(full), consume, &frames) < 0 && !frames);
    /* Structured mutations exercise header/table paths random bytes rarely reach. */
    for (unsigned i = 0; i < 108; i++) {
        uint8_t copy[sizeof(ncm)]; memcpy(copy, ncm, sizeof(copy));
        copy[i] ^= 0xff;
        (void)ipheth_receive(IPHETH_NCM, copy, sizeof(copy), consume, &frames);
    }
}
static uint32_t random_state = 1;
static uint32_t next(void) { random_state = random_state * 1664525 + 1013904223; return random_state; }
static void malformed(void)
{
    uint8_t data[2048]; struct ipheth_interface result;
    for (unsigned iteration = 0; iteration < 50000; iteration++) {
        size_t n = next() % sizeof(data);
        for (size_t i = 0; i < n; i++) data[i] = (uint8_t)(next() >> 24);
        (void)ipheth_receive(IPHETH_NCM, data, n, consume, &frames);
        (void)ipheth_receive(IPHETH_LEGACY, data, n, consume, &frames);
        (void)ipheth_find_interface(0x05ac, data, n, 2, &result);
    }
}
int main(void) { descriptors(); controls(); framing(); malformed(); puts("protocol tests passed (50,000 malformed inputs)"); }
