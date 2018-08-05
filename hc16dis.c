/*
 * hc16.c
 * Copyright 2018 Peter Jones <pjones@redhat.com>
 *
 */

#include <err.h>
#include <errno.h>
#include <error.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util.h"

static int dbg = 0;

typedef struct operand_s {
        char *name;
        int bits;
        int sext;
        int zext;
} operand;

operand op_b = { "b", 4};               // 4-bit address extension
operand op_ff = { "ff", 8};             // off8
operand op_gggg = { "gggg", 16, 1};     // soff16
operand op_zg = { "zg", 4, 1};          // soff20 [20:17]
operand op_hh = { "hh", 8};             // addr imm16 [15:8]
operand op_ii = { "ii", 8, 1};          // simm8
operand op_jj = { "jj", 8};             // imm16 [15:8]
operand op_kk = { "kk", 8};             // imm16 [7:0]
operand op_ll = { "ll", 8};             // addr imm16 [7:0]
operand op_mm = { "mm", 8};             // mask8
operand op_mmmm = { "mmmm", 16};        // mask16
operand op_rr = { "rr", 8};             // roff8
operand op_rrrr = { "rrrr", 16, 1};     // sroff16
operand op_xo = { "xo", 8};             // MAC index X offset
operand op_yo = { "yo", 8};             // MAC index Y offset
operand op_z = { "z", 4, 0, 1};         // 4-bit zero extension

typedef struct operands_s {
        char *name;
        int num;
        operand *elements[4];
} operands;

operands ff = { "ff", 1, { &op_ff }};
operands ii = { "ii", 1, { &op_ii }};
operands rr = { "rr", 1, { &op_rr }};
operands gggg = { "gggg", 1, { &op_gggg }};
operands rrrr = { "rrrr", 1, { &op_rrrr }};

operands ggggmmmm = { "gggg mmmm", 2, { &op_gggg, &op_mmmm }};
operands hhll = { "hhll", 2, { &op_hh, &op_ll }};
operands jjkk = { "jjkk", 2, { &op_jj, &op_kk }};
operands mmff = { "mm ff", 2, { &op_mm, &op_ff }};
operands mmgggg = { "mm gggg", 2, { &op_mm, &op_gggg }};
operands xoyo = { "xoyo", 2, { &op_xo, &op_yo }};
operands zggggg = { "zg gggg", 2, { &op_zg, &op_gggg }};

operands ffhhll = { "ff hhll", 3, { &op_ff, &op_hh, &op_ll }};
operands hhllmmmm = { "hhll mmmm", 3, { &op_hh, &op_ll, &op_mmmm }};
operands mmffrr = { "mm ff rr", 3, { &op_mm, &op_ff, &op_rr }};
operands mmggggrrrr = { "mm gggg rrrr", 3, { &op_mm, &op_gggg, &op_rrrr }};
operands mmhhll = { "mm hhll", 3, { &op_mm, &op_hh, &op_ll }};

operands mmhhllrr = { "mm hhll rr", 4, { &op_mm, &op_hh, &op_ll, &op_rr }};
operands mmhhllrrrr = { "mm hhll rrrr", 4, { &op_mm, &op_hh, &op_ll, &op_rrrr }};
operands zbhhll = { "z b hhll", 4, { &op_z, &op_b, &op_hh, &op_ll }};

typedef enum {
        PAGE0,
        PAGE1,
        PAGE2,
        PAGE3,
        IND8X,
        ind8x = 4,
        IND8Y,
        ind8y = 5,
        IND8Z,
        ind8z = 6,
        IND16X,
        ind16x = 7,
        IND16Y,
        ind16y = 8,
        IND16Z,
        ind16z = 9,
        IXP_to_EXT,
        ixp_to_ext = 10,
        IXP2EXT = 10,
        ixp2ext = 10,
        EXT_to_IXP,
        ext_to_ixp = 11,
        EXT2IXP = 11,
        ext2ixp = 11,
        INH,
        inh = 12,
        REL8,
        rel8 = 13,
        REL16,
        rel16 = 14,
        IMM8,
        imm8 = 15,
        IMM16,
        imm16 = 16,
        IND20X,
        ind20x = 17,
        IND20Y,
        ind20y = 18,
        IND20Z,
        ind20z = 19,
        EXT,
        ext = 20,
        EXT20 = 21,
        ext20 = 21,
        EX,
        ex = 22,
        EY,
        ey = 23,
        EZ,
        ez = 24,
        ext2ext,
        EXT2EXT = 25,
        ext_to_ext = 25,
        EXT_to_EXT = 25,
} mode;

char *modenames[] = {
        [PAGE0] = "PAGE0",
        [PAGE1] = "PAGE1",
        [PAGE2] = "PAGE2",
        [PAGE3] = "PAGE3",
        [ind8x] = "X",
        [ind8y] = "Y",
        [ind8z] = "Z",
        [ind16x] = "X",
        [ind16y] = "Y",
        [ind16z] = "Z",
        [ixp2ext] = "IXP->EXT",
        [ext2ixp] = "EXT->IXP",
        [inh] = "INH",
        [rel8] = "rel8",
        [rel16] = "rel16",
        [imm8] = "imm8",
        [imm16] = "imm16",
        [ind20x] = "X",
        [ind20y] = "Y",
        [ind20z] = "Z",
        [ext] = "EXT",
        [ext20] = "EXT20",
        [ex] = "E_X",
        [ey] = "E_Y",
        [ez] = "E_Z",
        [ext2ext] = "EXT->EXT",
};

typedef struct optable_s {
        uint8_t opcode;
        char *mnemonic;
        mode mode;
        operands *operands;
} op;

op opcodes[4][0x100] = {
        { // no prefix
                { 0x00, "com", ind8x, &ff },
                { 0x01, "dec", ind8x, &ff },
                { 0x02, "neg", ind8x, &ff },
                { 0x03, "inc", ind8x, &ff },
                { 0x04, "asl", ind8x, &ff },
                { 0x05, "clr", ind8x, &ff },
                { 0x06, "tst", ind8x, &ff },
                { 0x07, "unrecognized", PAGE0, &ii },
                { 0x08, "bclr", ind16x, &mmggggrrrr },
                { 0x09, "bset", ind16x, &mmggggrrrr },
                { 0x0a, "brclr", ind16x, &mmggggrrrr },
                { 0x0b, "brset", ind16x, &mmggggrrrr },
                { 0x0c, "rol", ind8x, &ff },
                { 0x0d, "asr", ind8x, &ff },
                { 0x0e, "ror", ind8x, &ff },
                { 0x0f, "lsr", ind8x, &ff },
                { 0x10, "com", ind8y, &ff },
                { 0x11, "dec", ind8y, &ff },
                { 0x12, "neg", ind8y, &ff },
                { 0x13, "inc", ind8y, &ff },
                { 0x14, "asl", ind8y, &ff },
                { 0x15, "clr", ind8y, &ff },
                { 0x16, "tst", ind8y, &ff },
                { 0x17, "PREBYTE", PAGE1 },
                { 0x18, "bclr", ind16y, &mmggggrrrr },
                { 0x19, "bset", ind16y, &mmggggrrrr },
                { 0x1a, "brclr", ind16y, &mmggggrrrr },
                { 0x1b, "brset", ind16y, &mmggggrrrr },
                { 0x1c, "rol", ind8y, &ff },
                { 0x1d, "asr", ind8y, &ff },
                { 0x1e, "ror", ind8y, &ff },
                { 0x1f, "lsr", ind8y, &ff },
                { 0x20, "com", ind8z, &ff },
                { 0x21, "dec", ind8z, &ff },
                { 0x22, "neg", ind8z, &ff },
                { 0x23, "inc", ind8z, &ff },
                { 0x24, "asl", ind8z, &ff },
                { 0x25, "clr", ind8z, &ff },
                { 0x26, "tst", ind8z, &ff },
                { 0x27, "PREBYTE", PAGE2 },
                { 0x28, "bclr", ind16z, &mmggggrrrr },
                { 0x29, "bset", ind16z, &mmggggrrrr },
                { 0x2a, "brclr", ind16z, &mmggggrrrr },
                { 0x2b, "brset", ind16z, &mmggggrrrr },
                { 0x2c, "rol", ind8z, &ff },
                { 0x2d, "asr", ind8z, &ff },
                { 0x2e, "ror", ind8z, &ff },
                { 0x2f, "lsr", ind8z, &ff },
                { 0x30, "movb", ixp2ext, &ffhhll },
                { 0x31, "movw", ixp2ext, &ffhhll },
                { 0x32, "movb", ext2ixp, &ffhhll },
                { 0x33, "movw", ext2ixp, &ffhhll },
                { 0x34, "pshm", inh, &ii },
                { 0x35, "pulm", inh, &ii },
                { 0x36, "bsr", rel8, &rr },
                { 0x37, "PREBYTE", PAGE3 },
                { 0x38, "bclr", ext, &mmhhll },
                { 0x39, "bset", ext, &mmhhll },
                { 0x3a, "brclr", ext, &mmhhllrrrr },
                { 0x3b, "brset", ext, &mmhhllrrrr },
                { 0x3c, "aix", imm8, &ii },
                { 0x3d, "aiy", imm8, &ii },
                { 0x3e, "aiz", imm8, &ii },
                { 0x3f, "ais", imm8, &ii },
                { 0x40, "suba", ind8x, &ff },
                { 0x41, "adda", ind8x, &ff },
                { 0x42, "sbca", ind8x, &ff },
                { 0x43, "adca", ind8x, &ff },
                { 0x44, "eora", ind8x, &ff },
                { 0x45, "ldaa", ind8x, &ff },
                { 0x46, "anda", ind8x, &ff },
                { 0x47, "oraa", ind8x, &ff },
                { 0x48, "cmpa", ind8x, &ff },
                { 0x49, "bita", ind8x, &ff },
                { 0x4a, "staa", ind8x, &ff },
                { 0x4b, "jmp", ind20x, &zggggg },
                { 0x4c, "cpx", ind8x, &ff },
                { 0x4d, "cpy", ind8x, &ff },
                { 0x4e, "cpz", ind8x, &ff },
                { 0x4f, "cps", ind8x, &ff },
                { 0x50, "suba", ind8y, &ff },
                { 0x51, "adda", ind8y, &ff },
                { 0x52, "sbca", ind8y, &ff },
                { 0x53, "adca", ind8y, &ff },
                { 0x54, "eora", ind8y, &ff },
                { 0x55, "ldaa", ind8y, &ff },
                { 0x56, "anda", ind8y, &ff },
                { 0x57, "oraa", ind8y, &ff },
                { 0x58, "cmpa", ind8y, &ff },
                { 0x59, "bita", ind8y, &ff },
                { 0x5a, "staa", ind8y, &ff },
                { 0x5b, "jmp", ind20y, &zggggg },
                { 0x5c, "cpx", ind8y, &ff },
                { 0x5d, "cpy", ind8y, &ff },
                { 0x5e, "cpz", ind8y, &ff },
                { 0x5f, "cps", ind8y, &ff },
                { 0x60, "suba", ind8z, &ff },
                { 0x61, "adda", ind8z, &ff },
                { 0x62, "sbca", ind8z, &ff },
                { 0x63, "adca", ind8z, &ff },
                { 0x64, "eora", ind8z, &ff },
                { 0x65, "ldaa", ind8z, &ff },
                { 0x66, "anda", ind8z, &ff },
                { 0x67, "oraa", ind8z, &ff },
                { 0x68, "cmpa", ind8z, &ff },
                { 0x69, "bita", ind8z, &ff },
                { 0x6a, "staa", ind8z, &ff },
                { 0x6b, "jmp", ind20z, &zggggg },
                { 0x6c, "cpx", ind8z, &ff },
                { 0x6d, "cpy", ind8z, &ff },
                { 0x6e, "cpz", ind8z, &ff },
                { 0x6f, "cps", ind8z, &ff },
                { 0x70, "suba", imm8, &ff },
                { 0x71, "adda", imm8, &ff },
                { 0x72, "sbca", imm8, &ff },
                { 0x73, "adca", imm8, &ff },
                { 0x74, "eora", imm8, &ff },
                { 0x75, "ldaa", imm8, &ff },
                { 0x76, "anda", imm8, &ff },
                { 0x77, "oraa", imm8, &ff },
                { 0x78, "cmpa", imm8, &ff },
                { 0x79, "bita", imm8, &ff },
                { 0x7a, "jmp", ext, &zbhhll },
                { 0x7b, "mac", imm8, &ff },
                { 0x7c, "adde", imm8, &ff },
                { 0x7d, "unrecognized", imm8, &ii },
                { 0x7e, "unrecognized", imm8, &ii },
                { 0x7f, "unrecognized", imm8, &ii },
                { 0x80, "subd", ind8x, &ff },
                { 0x81, "addd", ind8x, &ff },
                { 0x82, "sbcd", ind8x, &ff },
                { 0x83, "adcd", ind8x, &ff },
                { 0x84, "eord", ind8x, &ff },
                { 0x85, "ldd", ind8x, &ff },
                { 0x86, "andd", ind8x, &ff },
                { 0x87, "ord", ind8x, &ff },
                { 0x88, "cmpd", ind8x, &ff },
                { 0x89, "jsr", ind20x, &zggggg },
                { 0x8a, "std", ind8x, &ff },
                { 0x8b, "brset", ind8x, &ff },
                { 0x8c, "stx", ind8x, &ff },
                { 0x8d, "sty", ind8x, &ff },
                { 0x8e, "stz", ind8x, &ff },
                { 0x8f, "sts", ind8x, &ff },
                { 0x90, "subd", ind8y, &ff },
                { 0x91, "addd", ind8y, &ff },
                { 0x92, "sbcd", ind8y, &ff },
                { 0x93, "adcd", ind8y, &ff },
                { 0x94, "eord", ind8y, &ff },
                { 0x95, "ldd", ind8y, &ff },
                { 0x96, "andd", ind8y, &ff },
                { 0x97, "ord", ind8y, &ff },
                { 0x98, "cmpd", ind8y, &ff },
                { 0x99, "jsr", ind20x, &zggggg },
                { 0x9a, "std", ind8y, &ff },
                { 0x9b, "brset", ind8y, &ff },
                { 0x9c, "stx", ind8y, &ff },
                { 0x9d, "sty", ind8y, &ff },
                { 0x9e, "stz", ind8y, &ff },
                { 0x9f, "sts", ind8y, &ff },
                { 0xa0, "subd", ind8z, &ff },
                { 0xa1, "addd", ind8z, &ff },
                { 0xa2, "sbcd", ind8z, &ff },
                { 0xa3, "adcd", ind8z, &ff },
                { 0xa4, "eord", ind8z, &ff },
                { 0xa5, "ldd", ind8z, &ff },
                { 0xa6, "andd", ind8z, &ff },
                { 0xa7, "ord", ind8z, &ff },
                { 0xa8, "cmpd", ind8z, &ff },
                { 0xa9, "jsr", ind20x, &zggggg },
                { 0xaa, "std", ind8z, &ff },
                { 0xab, "brset", ind8z, &ff },
                { 0xac, "stx", ind8z, &ff },
                { 0xad, "sty", ind8z, &ff },
                { 0xae, "stz", ind8z, &ff },
                { 0xaf, "sts", ind8z, &ff },
                { 0xb0, "bra", rel8, &rr },
                { 0xb1, "brn", rel8, &rr },
                { 0xb2, "bhi", rel8, &rr },
                { 0xb3, "bls", rel8, &rr },
                { 0xb4, "bcc", rel8, &rr },
                { 0xb5, "bcs", rel8, &rr },
                { 0xb6, "bne", rel8, &rr },
                { 0xb7, "beq", rel8, &rr },
                { 0xb8, "bvc", rel8, &rr },
                { 0xb9, "bvs", rel8, &rr},
                { 0xba, "bpl", rel8, &rr },
                { 0xbb, "bmi", rel8, &rr },
                { 0xbc, "bge", rel8, &rr },
                { 0xbd, "blt", rel8, &rr },
                { 0xbe, "bgt", rel8, &rr },
                { 0xbf, "ble", rel8, &rr },
                { 0xc0, "subb", ind8x, &ff },
                { 0xc1, "addb", ind8x, &ff },
                { 0xc2, "sbcb", ind8x, &ff },
                { 0xc3, "adcb", ind8x, &ff },
                { 0xc4, "eorb", ind8x, &ff },
                { 0xc5, "ldab", ind8x, &ff },
                { 0xc6, "andb", ind8x, &ff },
                { 0xc7, "orab", ind8x, &ff },
                { 0xc8, "cmpb", ind8x, &ff },
                { 0xc9, "bitb", ind8x, &ff },
                { 0xca, "stab", ind8x, &ff },
                { 0xcb, "brclr", ind8x, &ff },
                { 0xcc, "ldx", ind8x, &ff },
                { 0xcd, "ldy", ind8x, &ff },
                { 0xce, "ldz", ind8x, &ff },
                { 0xcf, "lds", ind8x, &ff },
                { 0xd0, "subb", ind8y, &ff },
                { 0xd1, "addb", ind8y, &ff },
                { 0xd2, "sbcb", ind8y, &ff },
                { 0xd3, "adcb", ind8y, &ff },
                { 0xd4, "eorb", ind8y, &ff },
                { 0xd5, "ldab", ind8y, &ff },
                { 0xd6, "andb", ind8y, &ff },
                { 0xd7, "orab", ind8y, &ff },
                { 0xd8, "cmpb", ind8y, &ff },
                { 0xd9, "bitb", ind8y, &ff },
                { 0xda, "ldab", ind8y, &ff },
                { 0xdb, "brclr", ind8y, &ff },
                { 0xdc, "ldx", ind8y, &ff },
                { 0xdd, "ldy", ind8y, &ff },
                { 0xde, "ldz", ind8y, &ff },
                { 0xdf, "lds", ind8y, &ff },
                { 0xe0, "subb", ind8z, &ff },
                { 0xe1, "addb", ind8z, &ff },
                { 0xe2, "sbcb", ind8z, &ff },
                { 0xe3, "adcb", ind8z, &ff },
                { 0xe4, "eorb", ind8z, &ff },
                { 0xe5, "ldab", ind8z, &ff },
                { 0xe6, "andb", ind8z, &ff },
                { 0xe7, "orab", ind8z, &ff },
                { 0xe8, "cmpb", ind8z, &ff },
                { 0xe9, "bitb", ind8z, &ff },
                { 0xea, "ldab", ind8z, &ff },
                { 0xeb, "brclr", ind8z, &ff },
                { 0xec, "ldx", ind8z, &ff },
                { 0xed, "ldy", ind8z, &ff },
                { 0xee, "ldz", ind8z, &ff },
                { 0xef, "lds", ind8z, &ff },
                { 0xf0, "subb", imm8, &ii },
                { 0xf1, "addb", imm8, &ii },
                { 0xf2, "sbcb", imm8, &ii },
                { 0xf3, "adcb", imm8, &ii },
                { 0xf4, "eorb", imm8, &ii },
                { 0xf5, "ldab", imm8, &ii },
                { 0xf6, "andb", imm8, &ii },
                { 0xf7, "orab", imm8, &ii },
                { 0xf8, "cmpb", imm8, &ii },
                { 0xf9, "bitb", imm8, &ii },
                { 0xfa, "jsr", ext20, &zbhhll },
                { 0xfb, "rmac", imm8, &xoyo },
                { 0xfc, "addd", imm8, &ii },
                { 0xfd, "unrecognized", imm8, &ii },
                { 0xfe, "unrecognized", imm8, &ii },
                { 0xff, "unrecognized", imm8, &ii },
        },
        { // prefix 0x17
                { 0x00, "com", ind16x, &gggg },
                { 0x01, "dec", ind16x, &gggg },
                { 0x02, "neg", ind16x, &gggg },
                { 0x03, "inc", ind16x, &gggg },
                { 0x04, "asl", ind16x, &gggg },
                { 0x05, "clr", ind16x, &gggg },
                { 0x06, "tst", ind16x, &gggg },
                { 0x07, "unrecognized", PAGE1, &ii },
                { 0x08, "bclr", ind8x, &mmgggg },
                { 0x09, "bset", ind8x, &mmgggg },
                { 0x0a, "unrecognized", PAGE1, &ii },
                { 0x0b, "unrecognized", PAGE1, &ii },
                { 0x0c, "rol", ind16x, &gggg },
                { 0x0d, "asr", ind16x, &gggg },
                { 0x0e, "ror", ind16x, &gggg },
                { 0x0f, "lsr", ind16x, &gggg },
                { 0x10, "com", ind16y, &gggg },
                { 0x11, "dec", ind16y, &gggg },
                { 0x12, "neg", ind16y, &gggg },
                { 0x13, "inc", ind16y, &gggg },
                { 0x14, "asl", ind16y, &gggg },
                { 0x15, "clr", ind16y, &gggg },
                { 0x16, "tst", ind16y, &gggg },
                { 0x17, "unrecognized", PAGE1, &ii },
                { 0x18, "bclr", ind8x, &mmgggg },
                { 0x19, "bset", ind8x, &mmgggg },
                { 0x1a, "unrecognized", PAGE1, &ii },
                { 0x1b, "unrecognized", PAGE1, &ii },
                { 0x1c, "rol", ind16y, &gggg },
                { 0x1d, "asr", ind16y, &gggg },
                { 0x1e, "ror", ind16y, &gggg },
                { 0x1f, "lsr", ind16y, &gggg },
                { 0x20, "com", ind16z, &gggg },
                { 0x21, "dec", ind16z, &gggg },
                { 0x22, "neg", ind16z, &gggg },
                { 0x23, "inc", ind16z, &gggg },
                { 0x24, "asl", ind16z, &gggg },
                { 0x25, "clr", ind16z, &gggg },
                { 0x26, "tst", ind16z, &gggg },
                { 0x27, "unrecognized", imm8, &ii },
                { 0x28, "bclr", ind8x, &mmgggg },
                { 0x29, "bset", ind8x, &mmgggg },
                { 0x2a, "unrecognized", imm8, &ii },
                { 0x2b, "unrecognized", imm8, &ii },
                { 0x2c, "rol", ind16z, &gggg },
                { 0x2d, "asr", ind16z, &gggg },
                { 0x2e, "ror", ind16z, &gggg },
                { 0x2f, "lsr", ind16z, &gggg },
                { 0x30, "com", ext, &hhll },
                { 0x31, "dec", ext, &hhll },
                { 0x32, "neg", ext, &hhll },
                { 0x33, "inc", ext, &hhll },
                { 0x34, "asl", ext, &hhll },
                { 0x35, "clr", ext, &hhll },
                { 0x36, "tst", ext, &hhll },
                { 0x37, "unrecognized", imm8, &ii },
                { 0x38, "unrecognized", imm8, &ii },
                { 0x39, "unrecognized", imm8, &ii },
                { 0x3a, "unrecognized", imm8, &ii },
                { 0x3b, "unrecognized", imm8, &ii },
                { 0x3c, "rol", ext, &hhll },
                { 0x3d, "asr", ext, &hhll },
                { 0x3e, "ror", ext, &hhll },
                { 0x3f, "lsr", ext, &hhll },
                { 0x40, "suba", ind16x, &gggg },
                { 0x41, "adda", ind16x, &gggg },
                { 0x42, "sbca", ind16x, &gggg },
                { 0x43, "adca", ind16x, &gggg },
                { 0x44, "eora", ind16x, &gggg },
                { 0x45, "ldaa", ind16x, &gggg },
                { 0x46, "anda", ind16x, &gggg },
                { 0x47, "oraa", ind16x, &gggg },
                { 0x48, "cmpa", ind16x, &gggg },
                { 0x49, "bita", ind16x, &gggg },
                { 0x4a, "staa", ind16x, &gggg },
                { 0x4b, "unrecognized", imm8, &ii },
                { 0x4c, "cpx", ind16x, &gggg },
                { 0x4d, "cpy", ind16x, &gggg },
                { 0x4e, "cpz", ind16x, &gggg },
                { 0x4f, "cps", ind16x, &gggg },
                { 0x50, "suba", ind16y, &gggg },
                { 0x51, "adda", ind16y, &gggg },
                { 0x52, "sbca", ind16y, &gggg },
                { 0x53, "adca", ind16y, &gggg },
                { 0x54, "eora", ind16y, &gggg },
                { 0x55, "ldaa", ind16y, &gggg },
                { 0x56, "anda", ind16y, &gggg },
                { 0x57, "oraa", ind16y, &gggg },
                { 0x58, "cmpa", ind16y, &gggg },
                { 0x59, "bita", ind16y, &gggg },
                { 0x5a, "staa", ind16y, &gggg },
                { 0x5b, "unrecognized", imm8, &ii },
                { 0x5c, "cpx", ind16y, &gggg },
                { 0x5d, "cpy", ind16y, &gggg },
                { 0x5e, "cpz", ind16y, &gggg },
                { 0x5f, "cps", ind16y, &gggg },
                { 0x60, "suba", ind16z, &gggg },
                { 0x61, "adda", ind16z, &gggg },
                { 0x62, "sbca", ind16z, &gggg },
                { 0x63, "adca", ind16z, &gggg },
                { 0x64, "eora", ind16z, &gggg },
                { 0x65, "ldaa", ind16z, &gggg },
                { 0x66, "anda", ind16z, &gggg },
                { 0x67, "oraa", ind16z, &gggg },
                { 0x68, "cmpa", ind16z, &gggg },
                { 0x69, "bita", ind16z, &gggg },
                { 0x6a, "staa", ind16z, &gggg },
                { 0x6b, "unrecognized", imm8, &ii },
                { 0x6c, "cpx", ind16z, &gggg },
                { 0x6d, "cpy", ind16z, &gggg },
                { 0x6e, "cpz", ind16z, &gggg },
                { 0x6f, "cps", ind16z, &gggg },
                { 0x70, "suba", ext, &hhll },
                { 0x71, "adda", ext, &hhll },
                { 0x72, "sbca", ext, &hhll },
                { 0x73, "adca", ext, &hhll },
                { 0x74, "eora", ext, &hhll },
                { 0x75, "ldaa", ext, &hhll },
                { 0x76, "anda", ext, &hhll },
                { 0x77, "oraa", ext, &hhll },
                { 0x78, "cmpa", ext, &hhll },
                { 0x79, "bita", ext, &hhll },
                { 0x7a, "staa", ext, &hhll },
                { 0x7b, "unrecognized", imm8, &ii },
                { 0x7c, "cpx", ext, &hhll },
                { 0x7d, "cpy", ext, &hhll },
                { 0x7e, "cpz", ext, &hhll },
                { 0x7f, "cps", ext, &hhll },
                { 0x80, "unrecognized", imm8, &ii },
                { 0x81, "unrecognized", imm8, &ii },
                { 0x82, "unrecognized", imm8, &ii },
                { 0x83, "unrecognized", imm8, &ii },
                { 0x84, "unrecognized", imm8, &ii },
                { 0x85, "unrecognized", imm8, &ii },
                { 0x86, "unrecognized", imm8, &ii },
                { 0x87, "unrecognized", imm8, &ii },
                { 0x88, "unrecognized", imm8, &ii },
                { 0x89, "unrecognized", imm8, &ii },
                { 0x8a, "unrecognized", imm8, &ii },
                { 0x8b, "unrecognized", imm8, &ii },
                { 0x8c, "stx", ind16x, &gggg },
                { 0x8d, "sty", ind16x, &gggg },
                { 0x8e, "stz", ind16x, &gggg },
                { 0x8f, "sts", ind16x, &gggg },
                { 0x90, "unrecognized", imm8, &ii },
                { 0x91, "unrecognized", imm8, &ii },
                { 0x92, "unrecognized", imm8, &ii },
                { 0x93, "unrecognized", imm8, &ii },
                { 0x94, "unrecognized", imm8, &ii },
                { 0x95, "unrecognized", imm8, &ii },
                { 0x96, "unrecognized", imm8, &ii },
                { 0x97, "unrecognized", imm8, &ii },
                { 0x98, "unrecognized", imm8, &ii },
                { 0x99, "unrecognized", imm8, &ii },
                { 0x9a, "unrecognized", imm8, &ii },
                { 0x9b, "unrecognized", imm8, &ii },
                { 0x9c, "stx", ind16y, &gggg },
                { 0x9d, "sty", ind16y, &gggg },
                { 0x9e, "stz", ind16y, &gggg },
                { 0x9f, "sts", ind16y, &gggg },
                { 0xa0, "unrecognized", imm8, &ii },
                { 0xa1, "unrecognized", imm8, &ii },
                { 0xa2, "unrecognized", imm8, &ii },
                { 0xa3, "unrecognized", imm8, &ii },
                { 0xa4, "unrecognized", imm8, &ii },
                { 0xa5, "unrecognized", imm8, &ii },
                { 0xa6, "unrecognized", imm8, &ii },
                { 0xa7, "unrecognized", imm8, &ii },
                { 0xa8, "unrecognized", imm8, &ii },
                { 0xa9, "unrecognized", imm8, &ii },
                { 0xaa, "unrecognized", imm8, &ii },
                { 0xab, "unrecognized", imm8, &ii },
                { 0xac, "stx", ind16z, &gggg },
                { 0xad, "sty", ind16z, &gggg },
                { 0xae, "stz", ind16z, &gggg },
                { 0xaf, "sts", ind16z, &gggg },
                { 0xb0, "unrecognized", imm8, &ii },
                { 0xb1, "unrecognized", imm8, &ii },
                { 0xb2, "unrecognized", imm8, &ii },
                { 0xb3, "unrecognized", imm8, &ii },
                { 0xb4, "unrecognized", imm8, &ii },
                { 0xb5, "unrecognized", imm8, &ii },
                { 0xb6, "unrecognized", imm8, &ii },
                { 0xb7, "unrecognized", imm8, &ii },
                { 0xb8, "unrecognized", imm8, &ii },
                { 0xb9, "unrecognized", imm8, &ii },
                { 0xba, "unrecognized", imm8, &ii },
                { 0xbb, "unrecognized", imm8, &ii },
                { 0xbc, "stx", ext, &hhll },
                { 0xbd, "sty", ext, &hhll },
                { 0xbe, "stz", ext, &hhll },
                { 0xbf, "sts", ext, &hhll },
                { 0xc0, "subb", ind16x, &gggg },
                { 0xc1, "addb", ind16x, &gggg },
                { 0xc2, "sbcb", ind16x, &gggg },
                { 0xc3, "adcb", ind16x, &gggg },
                { 0xc4, "eorb", ind16x, &gggg },
                { 0xc5, "ldab", ind16x, &gggg },
                { 0xc6, "andb", ind16x, &gggg },
                { 0xc7, "orab", ind16x, &gggg },
                { 0xc8, "cmpb", ind16x, &gggg },
                { 0xc9, "bitb", ind16x, &gggg },
                { 0xca, "stab", ind16x, &gggg },
                { 0xcb, "unrecognized", imm8, &ii },
                { 0xcc, "ldx", ind16x, &gggg },
                { 0xcd, "ldy", ind16x, &gggg },
                { 0xce, "ldz", ind16x, &gggg },
                { 0xcf, "lds", ind16x, &gggg },
                { 0xd0, "subb", ind16y, &gggg },
                { 0xd1, "addb", ind16y, &gggg },
                { 0xd2, "sbcb", ind16y, &gggg },
                { 0xd3, "adcb", ind16y, &gggg },
                { 0xd4, "eorb", ind16y, &gggg },
                { 0xd5, "ldab", ind16y, &gggg },
                { 0xd6, "andb", ind16y, &gggg },
                { 0xd7, "orab", ind16y, &gggg },
                { 0xd8, "cmpb", ind16y, &gggg },
                { 0xd9, "bitb", ind16y, &gggg },
                { 0xda, "stab", ind16y, &gggg },
                { 0xdb, "unrecognized", imm8, &ii },
                { 0xdc, "ldx", ind16y, &gggg },
                { 0xdd, "ldy", ind16y, &gggg },
                { 0xde, "ldz", ind16y, &gggg },
                { 0xdf, "lds", ind16y, &gggg },
                { 0xe0, "subb", ind16z, &gggg },
                { 0xe1, "addb", ind16z, &gggg },
                { 0xe2, "sbcb", ind16z, &gggg },
                { 0xe3, "adcb", ind16z, &gggg },
                { 0xe4, "eorb", ind16z, &gggg },
                { 0xe5, "ldab", ind16z, &gggg },
                { 0xe6, "andb", ind16z, &gggg },
                { 0xe7, "orab", ind16z, &gggg },
                { 0xe8, "cmpb", ind16z, &gggg },
                { 0xe9, "bitb", ind16z, &gggg },
                { 0xea, "stab", ind16z, &gggg },
                { 0xeb, "unrecognized", imm8, &ii },
                { 0xec, "ldx", ind16z, &gggg },
                { 0xed, "ldy", ind16z, &gggg },
                { 0xee, "ldz", ind16z, &gggg },
                { 0xef, "lds", ind16z, &gggg },
                { 0xf0, "subb", ext, &hhll },
                { 0xf1, "addb", ext, &hhll },
                { 0xf2, "sbcb", ext, &hhll },
                { 0xf3, "adcb", ext, &hhll },
                { 0xf4, "eorb", ext, &hhll },
                { 0xf5, "ldab", ext, &hhll },
                { 0xf6, "andb", ext, &hhll },
                { 0xf7, "orab", ext, &hhll },
                { 0xf8, "cmpb", ext, &hhll },
                { 0xf9, "bitb", ext, &hhll },
                { 0xfa, "stab", ext, &hhll },
                { 0xfb, "unrecognized", imm8, &ii },
                { 0xfc, "ldx", ext, &hhll },
                { 0xfd, "ldy", ext, &hhll },
                { 0xfe, "ldz", ext, &hhll },
                { 0xff, "lds", ext, &hhll },
        },
        { // prefix 0x27
                { 0x00, "comw", ind16x, &ggggmmmm },
                { 0x01, "decw", ind16x, &ggggmmmm },
                { 0x02, "negw", ind16x, &ggggmmmm },
                { 0x03, "incw", ind16x, &ggggmmmm },
                { 0x04, "aslw", ind16x, &ggggmmmm },
                { 0x05, "clrw", ind16x, &ggggmmmm },
                { 0x06, "tstw", ind16x, &ggggmmmm },
                { 0x07, "unrecognized", PAGE2 },
                { 0x08, "bclrw", ind16x, &ggggmmmm },
                { 0x09, "bsetw", ind16x, &ggggmmmm },
                { 0x0a, "unrecognized", PAGE2 },
                { 0x0b, "unrecognized", PAGE2 },
                { 0x0c, "rolw", ind16x, &ggggmmmm },
                { 0x0d, "asrw", ind16x, &ggggmmmm },
                { 0x0e, "rorw", ind16x, &ggggmmmm },
                { 0x0f, "lsrw", ind16x, &ggggmmmm },
                { 0x10, "comw", ind16y, &ggggmmmm },
                { 0x11, "decw", ind16y, &ggggmmmm },
                { 0x12, "negw", ind16y, &ggggmmmm },
                { 0x13, "incw", ind16y, &ggggmmmm },
                { 0x14, "aslw", ind16y, &ggggmmmm },
                { 0x15, "clrw", ind16y, &ggggmmmm },
                { 0x16, "tstw", ind16y, &ggggmmmm },
                { 0x17, "unrecognized", PAGE2 },
                { 0x18, "bclrw", ind16y, &ggggmmmm },
                { 0x19, "bsetw", ind16y, &ggggmmmm },
                { 0x1a, "unrecognized", PAGE2 },
                { 0x1b, "unrecognized", PAGE2 },
                { 0x1c, "rolw", ind16y, &ggggmmmm },
                { 0x1d, "asrw", ind16y, &ggggmmmm },
                { 0x1e, "rorw", ind16y, &ggggmmmm },
                { 0x1f, "lsrw", ind16y, &ggggmmmm },
                { 0x20, "comw", ind16z, &ggggmmmm },
                { 0x21, "decw", ind16z, &ggggmmmm },
                { 0x22, "negw", ind16z, &ggggmmmm },
                { 0x23, "incw", ind16z, &ggggmmmm },
                { 0x24, "aslw", ind16z, &ggggmmmm },
                { 0x25, "clrw", ind16z, &ggggmmmm },
                { 0x26, "tstw", ind16z, &ggggmmmm },
                { 0x27, "unrecognized", PAGE2 },
                { 0x28, "bclrw", ind16z, &ggggmmmm },
                { 0x29, "bsetw", ind16z, &ggggmmmm },
                { 0x2a, "unrecognized", PAGE2 },
                { 0x2b, "unrecognized", PAGE2 },
                { 0x2c, "rolw", ind16z, &ggggmmmm },
                { 0x2d, "asrw", ind16z, &ggggmmmm },
                { 0x2e, "rorw", ind16z, &ggggmmmm },
                { 0x2f, "lsrw", ind16z, &ggggmmmm },
                { 0x30, "comw", ext, &hhllmmmm },
                { 0x31, "decw", ext, &hhllmmmm },
                { 0x32, "negw", ext, &hhllmmmm },
                { 0x33, "incw", ext, &hhllmmmm },
                { 0x34, "aslw", ext, &hhllmmmm },
                { 0x35, "clrw", ext, &hhllmmmm },
                { 0x36, "tstw", ext, &hhllmmmm },
                { 0x37, "unrecognized", PAGE2 },
                { 0x38, "bclrw", ext, &hhllmmmm },
                { 0x39, "bsetw", ext, &hhllmmmm },
                { 0x3a, "unrecognized", PAGE2 },
                { 0x3b, "unrecognized", PAGE2 },
                { 0x3c, "rolw", ext, &hhllmmmm },
                { 0x3d, "asrw", ext, &hhllmmmm },
                { 0x3e, "rorw", ext, &hhllmmmm },
                { 0x3f, "lsrw", ext, &hhllmmmm },
                { 0x40, "suba", ex },
                { 0x41, "adda", ex },
                { 0x42, "sbca", ex },
                { 0x43, "adca", ex },
                { 0x44, "eora", ex },
                { 0x45, "ldaa", ex },
                { 0x46, "anda", ex },
                { 0x47, "oraa", ex },
                { 0x48, "cmpa", ex },
                { 0x49, "bita", ex },
                { 0x4a, "staa", ex },
                { 0x4b, "unrecognized", PAGE2 },
                { 0x4c, "nop", ex },
                { 0x4d, "tyx", ex },
                { 0x4e, "tzx", ex },
                { 0x4f, "tsx", ex },
                { 0x50, "suba", ey },
                { 0x51, "adda", ey },
                { 0x52, "sbca", ey },
                { 0x53, "adca", ey },
                { 0x54, "eora", ey },
                { 0x55, "ldaa", ey },
                { 0x56, "anda", ey },
                { 0x57, "oraa", ey },
                { 0x58, "cmpa", ey },
                { 0x59, "bita", ey },
                { 0x5a, "staa", ey },
                { 0x5b, "unrecognized", PAGE2 },
                { 0x5c, "txy", ey },
                { 0x5d, "unrecognized", PAGE2 },
                { 0x5e, "tzy", ey },
                { 0x5f, "tsy", ey },
                { 0x60, "suba", ez },
                { 0x61, "adda", ez },
                { 0x62, "sbca", ez },
                { 0x63, "adca", ez },
                { 0x64, "eora", ez },
                { 0x65, "ldaa", ez },
                { 0x66, "anda", ez },
                { 0x67, "oraa", ez },
                { 0x68, "cmpa", ez },
                { 0x69, "bita", ez },
                { 0x6a, "staa", ez },
                { 0x6b, "unrecognized", PAGE2 },
                { 0x6c, "nxz", ez },
                { 0x6d, "tyz", ez },
                { 0x6e, "unrecognized", PAGE2 },
                { 0x6f, "tsx", ez },
                { 0x70, "come", inh },
                { 0x71, "lded", ext },
                { 0x72, "nege", inh },
                { 0x73, "sted", ext },
                { 0x74, "asle", inh },
                { 0x75, "clre", inh },
                { 0x76, "tste", inh },
                { 0x77, "rti", inh },
                { 0x78, "ade", inh },
                { 0x79, "sde", inh },
                { 0x7a, "xgde", inh },
                { 0x7b, "tde", inh },
                { 0x7c, "role", inh },
                { 0x7d, "asre", inh },
                { 0x7e, "rore", inh },
                { 0x7f, "lsre", inh },
                { 0x80, "subd", ex },
                { 0x81, "addd", ex },
                { 0x82, "sbcd", ex },
                { 0x83, "adcd", ex },
                { 0x84, "eord", ex },
                { 0x85, "ldd", ex },
                { 0x86, "andd", ex },
                { 0x87, "ord", ex },
                { 0x88, "cpd", ex },
                { 0x89, "unrecognized", PAGE2 },
                { 0x8a, "std", ex },
                { 0x8b, "unrecognized", PAGE2 },
                { 0x8c, "unrecognized", PAGE2 },
                { 0x8d, "unrecognized", PAGE2 },
                { 0x8e, "unrecognized", PAGE2 },
                { 0x8f, "unrecognized", PAGE2 },
                { 0x90, "subd", ey },
                { 0x91, "addd", ey },
                { 0x92, "sbcd", ey },
                { 0x93, "adcd", ey },
                { 0x94, "eord", ey },
                { 0x95, "ldd", ey },
                { 0x96, "andd", ey },
                { 0x97, "ord", ey },
                { 0x98, "cpd", ey },
                { 0x99, "unrecognized", PAGE2 },
                { 0x9a, "std", ey },
                { 0x9b, "unrecognized", PAGE2 },
                { 0x9c, "unrecognized", PAGE2 },
                { 0x9d, "unrecognized", PAGE2 },
                { 0x9e, "unrecognized", PAGE2 },
                { 0x9f, "unrecognized", PAGE2 },
                { 0xa0, "subd", ez },
                { 0xa1, "addd", ez },
                { 0xa2, "sbcd", ez },
                { 0xa3, "adcd", ez },
                { 0xa4, "eord", ez },
                { 0xa5, "ldd", ez },
                { 0xa6, "andd", ez },
                { 0xa7, "ord", ez },
                { 0xa8, "cpd", ez },
                { 0xa9, "unrecognized", PAGE2 },
                { 0xaa, "std", ez },
                { 0xab, "unrecognized", PAGE2 },
                { 0xac, "unrecognized", PAGE2 },
                { 0xad, "unrecognized", PAGE2 },
                { 0xae, "unrecognized", PAGE2 },
                { 0xaf, "unrecognized", PAGE2 },
                { 0xb0, "ldhi", ext },
                { 0xb1, "tedm", ext },
                { 0xb2, "tem", ext },
                { 0xb3, "tmxed", ext },
                { 0xb4, "tmer", ext },
                { 0xb5, "tmet", ext },
                { 0xb6, "aslm", ext },
                { 0xb7, "pshmac", ext },
                { 0xb8, "pulmac", ext },
                { 0xb9, "asrm", ext },
                { 0xba, "tekb", ext },
                { 0xbc, "unrecognized", PAGE2 },
                { 0xbd, "unrecognized", PAGE2 },
                { 0xbe, "unrecognized", PAGE2 },
                { 0xbf, "unrecognized", PAGE2 },
                { 0xc0, "subb", ex },
                { 0xc1, "addb", ex },
                { 0xc2, "sbcb", ex },
                { 0xc3, "adcb", ex },
                { 0xc4, "eorb", ex },
                { 0xc5, "ldab", ex },
                { 0xc6, "andb", ex },
                { 0xc7, "orab", ex },
                { 0xc8, "cmpb", ex },
                { 0xc9, "bitb", ex },
                { 0xca, "stab", ex },
                { 0xcb, "unrecognized", PAGE2 },
                { 0xcc, "unrecognized", PAGE2 },
                { 0xcd, "unrecognized", PAGE2 },
                { 0xce, "unrecognized", PAGE2 },
                { 0xcf, "unrecognized", PAGE2 },
                { 0xd0, "subb", ey },
                { 0xd1, "addb", ey },
                { 0xd2, "sbcb", ey },
                { 0xd3, "adcb", ey },
                { 0xd4, "eorb", ey },
                { 0xd5, "ldab", ey },
                { 0xd6, "andb", ey },
                { 0xd7, "orab", ey },
                { 0xd8, "cmpb", ey },
                { 0xd9, "bitb", ey },
                { 0xda, "stab", ey },
                { 0xdb, "unrecognized", PAGE2 },
                { 0xdc, "unrecognized", PAGE2 },
                { 0xdd, "unrecognized", PAGE2 },
                { 0xde, "unrecognized", PAGE2 },
                { 0xdf, "unrecognized", PAGE2 },
                { 0xe0, "subb", ez },
                { 0xe1, "addb", ez },
                { 0xe2, "sbcb", ez },
                { 0xe3, "adcb", ez },
                { 0xe4, "eorb", ez },
                { 0xe5, "ldab", ez },
                { 0xe6, "andb", ez },
                { 0xe7, "orab", ez },
                { 0xe8, "cmpb", ez },
                { 0xe9, "bitb", ez },
                { 0xea, "stab", ez },
                { 0xeb, "unrecognized", PAGE2 },
                { 0xec, "unrecognized", PAGE2 },
                { 0xed, "unrecognized", PAGE2 },
                { 0xee, "unrecognized", PAGE2 },
                { 0xef, "unrecognized", PAGE2 },
                { 0xf0, "comd", inh },
                { 0xf1, "ldstop", ext },
                { 0xf2, "negd", inh },
                { 0xf3, "wai", ext },
                { 0xf4, "asld", inh },
                { 0xf5, "clrd", inh },
                { 0xf6, "tstd", inh },
                { 0xf7, "rts", inh },
                { 0xf8, "sxt", inh },
                { 0xf9, "lbsr", rel16, &rrrr },
                { 0xfa, "tbek", inh },
                { 0xfb, "ted", inh },
                { 0xfc, "rold", inh },
                { 0xfd, "asrd", inh },
                { 0xfe, "rord", inh },
                { 0xff, "lsrd", inh },
        },
        { // prefix 0x37
                { 0x00, "coma", inh },
                { 0x01, "deca", inh },
                { 0x02, "nega", inh },
                { 0x03, "inca", inh },
                { 0x04, "asla", inh },
                { 0x05, "clra", inh },
                { 0x06, "tsta", inh },
                { 0x07, "tba", inh },
                { 0x08, "psha", inh },
                { 0x09, "pula", inh },
                { 0x0a, "sba", inh },
                { 0x0b, "aba", inh },
                { 0x0c, "rola", inh },
                { 0x0d, "asra", inh },
                { 0x0e, "rora", inh },
                { 0x0f, "lsra", inh },
                { 0x10, "comb", inh },
                { 0x11, "decb", inh },
                { 0x12, "negb", inh },
                { 0x13, "incb", inh },
                { 0x14, "aslb", inh },
                { 0x15, "clrb", inh },
                { 0x16, "tstb", inh },
                { 0x17, "tbb", inh },
                { 0x18, "pshb", inh },
                { 0x19, "pulb", inh },
                { 0x1a, "sbb", inh },
                { 0x1b, "abb", inh },
                { 0x1c, "rolb", inh },
                { 0x1d, "asrb", inh },
                { 0x1e, "rorb", inh },
                { 0x1f, "lsrb", inh },
                { 0x20, "swi", inh },
                { 0x21, "daa", inh },
                { 0x22, "ace", inh },
                { 0x23, "aced", inh },
                { 0x24, "mul", inh },
                { 0x25, "emul", inh },
                { 0x26, "emuls", inh },
                { 0x27, "fmuls", inh },
                { 0x28, "ediv", inh },
                { 0x29, "edivs", inh },
                { 0x2a, "idiv", inh },
                { 0x2b, "fdiv", inh },
                { 0x2c, "tpd", inh },
                { 0x2d, "tdp", inh },
                { 0x2e, "unrecognized", PAGE3 },
                { 0x2f, "tdmsk", inh },
                { 0x30, "sube", imm16, &jjkk },
                { 0x31, "adde", imm16, &jjkk },
                { 0x32, "sbce", imm16, &jjkk },
                { 0x33, "adce", imm16, &jjkk },
                { 0x34, "eore", imm16, &jjkk },
                { 0x35, "lde", imm16, &jjkk },
                { 0x36, "ande", imm16, &jjkk },
                { 0x37, "ore", imm16, &jjkk },
                { 0x38, "cpe", imm16, &jjkk },
                { 0x39, "unrecognized", PAGE3 },
                { 0x3a, "andp", imm16, &jjkk },
                { 0x3b, "orp", imm16, &jjkk },
                { 0x3c, "aix", imm16, &jjkk },
                { 0x3d, "aiy", imm16, &jjkk },
                { 0x3e, "aiz", imm16, &jjkk },
                { 0x3f, "ais", imm16, &jjkk },
                { 0x40, "sube", ind16x, &gggg },
                { 0x41, "adde", ind16x, &gggg },
                { 0x42, "sbce", ind16x, &gggg },
                { 0x43, "adce", ind16x, &gggg },
                { 0x44, "eore", ind16x, &gggg },
                { 0x45, "lde", ind16x, &gggg },
                { 0x46, "ande", ind16x, &gggg },
                { 0x47, "ore", ind16x, &gggg },
                { 0x48, "cpe", ind16x, &gggg },
                { 0x49, "unrecognized", PAGE3 },
                { 0x4a, "ste", ind16x, &gggg },
                { 0x4b, "unrecognized", PAGE3 },
                { 0x4c, "xgex", inh },
                { 0x4d, "aex", inh },
                { 0x4e, "txs", inh },
                { 0x4f, "abx", inh },
                { 0x50, "sube", ind16y, &gggg },
                { 0x51, "adde", ind16y, &gggg },
                { 0x52, "sbce", ind16y, &gggg },
                { 0x53, "adce", ind16y, &gggg },
                { 0x54, "eore", ind16y, &gggg },
                { 0x55, "lde", ind16y, &gggg },
                { 0x56, "ande", ind16y, &gggg },
                { 0x57, "ore", ind16y, &gggg },
                { 0x58, "cpe", ind16y, &gggg },
                { 0x59, "unrecognized", PAGE3 },
                { 0x5a, "ste", ind16y, &gggg },
                { 0x5b, "unrecognized", PAGE3 },
                { 0x5c, "xgey", inh },
                { 0x5d, "aey", inh },
                { 0x5e, "tys", inh },
                { 0x5f, "aby", inh },
                { 0x60, "sube", ind16z, &gggg },
                { 0x61, "adde", ind16z, &gggg },
                { 0x62, "sbce", ind16z, &gggg },
                { 0x63, "adce", ind16z, &gggg },
                { 0x64, "eore", ind16z, &gggg },
                { 0x65, "lde", ind16z, &gggg },
                { 0x66, "ande", ind16z, &gggg },
                { 0x67, "ore", ind16z, &gggg },
                { 0x68, "cpe", ind16z, &gggg },
                { 0x69, "unrecognized", PAGE3 },
                { 0x6a, "ste", ind16z, &gggg },
                { 0x6b, "unrecognized", PAGE3 },
                { 0x6c, "xgez", inh },
                { 0x6d, "aez", inh },
                { 0x6e, "tzs", inh },
                { 0x6f, "abz", inh },
                { 0x70, "sube", ext, &hhll },
                { 0x71, "adde", ext, &hhll },
                { 0x72, "sbce", ext, &hhll },
                { 0x73, "adce", ext, &hhll },
                { 0x74, "eore", ext, &hhll },
                { 0x75, "lde", ext, &hhll },
                { 0x76, "ande", ext, &hhll },
                { 0x77, "ore", ext, &hhll },
                { 0x78, "cpe", ext, &hhll },
                { 0x79, "unrecognized", PAGE3 },
                { 0x7a, "ste", ext, &hhll },
                { 0x7b, "unrecognized", PAGE3 },
                { 0x7c, "cpx", imm16, &jjkk },
                { 0x7d, "cpx", imm16, &jjkk },
                { 0x7e, "cpz", imm16, &jjkk },
                { 0x7f, "cps", imm16, &jjkk },
                { 0x80, "lbra", rel16, &rrrr },
                { 0x81, "lbrn", rel16, &rrrr },
                { 0x82, "lbhi", rel16, &rrrr },
                { 0x83, "lbls", rel16, &rrrr },
                { 0x84, "lbcc", rel16, &rrrr },
                { 0x85, "lbcs", rel16, &rrrr },
                { 0x86, "lbne", rel16, &rrrr },
                { 0x87, "lbeq", rel16, &rrrr },
                { 0x88, "lbvc", rel16, &rrrr },
                { 0x89, "lbvs", rel16, &rrrr },
                { 0x8a, "lbpl", rel16, &rrrr },
                { 0x8b, "lbmi", rel16, &rrrr },
                { 0x8c, "lbge", rel16, &rrrr },
                { 0x8d, "lblt", rel16, &rrrr },
                { 0x8e, "lbgt", rel16, &rrrr },
                { 0x8f, "lble", rel16, &rrrr },
                { 0x90, "lbmv", rel16, &rrrr },
                { 0x91, "lbev", rel16, &rrrr },
                { 0x92, "unrecognized", PAGE3 },
                { 0x93, "unrecognized", PAGE3 },
                { 0x94, "unrecognized", PAGE3 },
                { 0x95, "unrecognized", PAGE3 },
                { 0x96, "unrecognized", PAGE3 },
                { 0x97, "unrecognized", PAGE3 },
                { 0x98, "unrecognized", PAGE3 },
                { 0x99, "unrecognized", PAGE3 },
                { 0x9a, "unrecognized", PAGE3 },
                { 0x9b, "unrecognized", PAGE3 },
                { 0x9c, "tbxk", inh },
                { 0x9d, "tbyk", inh },
                { 0x9e, "tbzk", inh },
                { 0x9f, "tbsk", inh },
                { 0xa0, "unrecognized", PAGE3 },
                { 0xa1, "unrecognized", PAGE3 },
                { 0xa2, "unrecognized", PAGE3 },
                { 0xa3, "unrecognized", PAGE3 },
                { 0xa4, "unrecognized", PAGE3 },
                { 0xa5, "unrecognized", PAGE3 },
                { 0xa6, "bgnd", inh },
                { 0xa7, "unrecognized", PAGE3 },
                { 0xa8, "unrecognized", PAGE3 },
                { 0xa9, "unrecognized", PAGE3 },
                { 0xaa, "unrecognized", PAGE3 },
                { 0xab, "unrecognized", PAGE3 },
                { 0xac, "txkb", inh },
                { 0xad, "tykb", inh },
                { 0xae, "tzkb", inh },
                { 0xaf, "tskb", inh },
                { 0xb0, "subd", imm16, &jjkk },
                { 0xb1, "addd", imm16, &jjkk },
                { 0xb2, "sbcd", imm16, &jjkk },
                { 0xb3, "adcd", imm16, &jjkk },
                { 0xb4, "eord", imm16, &jjkk },
                { 0xb5, "ldd", imm16, &jjkk },
                { 0xb6, "andd", imm16, &jjkk },
                { 0xb7, "ord", imm16, &jjkk },
                { 0xb8, "cpd", imm16, &jjkk },
                { 0xb9, "unrecognized", PAGE3 },
                { 0xba, "unrecognized", PAGE3 },
                { 0xbb, "unrecognized", PAGE3 },
                { 0xbc, "ldx", imm16, &jjkk },
                { 0xbd, "ldy", imm16, &jjkk },
                { 0xbe, "ldz", imm16, &jjkk },
                { 0xbf, "lds", imm16, &jjkk },
                { 0xc0, "subd", ind16x, &gggg },
                { 0xc1, "addd", ind16x, &gggg },
                { 0xc2, "sbcd", ind16x, &gggg },
                { 0xc3, "adcd", ind16x, &gggg },
                { 0xc4, "eord", ind16x, &gggg },
                { 0xc5, "ldd", ind16x, &gggg },
                { 0xc6, "andd", ind16x, &gggg },
                { 0xc7, "ord", ind16x, &gggg },
                { 0xc8, "cpd", ind16x, &gggg },
                { 0xc9, "unrecognized", PAGE3 },
                { 0xca, "std", ind16x, &gggg },
                { 0xcb, "unrecognized", PAGE3 },
                { 0xcc, "xgdx", inh },
                { 0xcd, "adx", inh },
                { 0xce, "unrecognized", PAGE3 },
                { 0xcf, "unrecognized", PAGE3 },
                { 0xd0, "subd", ind16y, &gggg },
                { 0xd1, "addd", ind16y, &gggg },
                { 0xd2, "sbcd", ind16y, &gggg },
                { 0xd3, "adcd", ind16y, &gggg },
                { 0xd4, "eord", ind16y, &gggg },
                { 0xd5, "ldd", ind16y, &gggg },
                { 0xd6, "andd", ind16y, &gggg },
                { 0xd7, "ord", ind16y, &gggg },
                { 0xd8, "cpd", ind16y, &gggg },
                { 0xd9, "unrecognized", PAGE3 },
                { 0xda, "std", ind16y, &gggg },
                { 0xdb, "unrecognized", PAGE3 },
                { 0xdc, "xgdy", inh },
                { 0xdd, "ady", inh },
                { 0xde, "unrecognized", PAGE3 },
                { 0xdf, "unrecognized", PAGE3 },
                { 0xe0, "subd", ind16z, &gggg },
                { 0xe1, "addd", ind16z, &gggg },
                { 0xe2, "sbcd", ind16z, &gggg },
                { 0xe3, "adcd", ind16z, &gggg },
                { 0xe4, "eord", ind16z, &gggg },
                { 0xe5, "ldd", ind16z, &gggg },
                { 0xe6, "andd", ind16z, &gggg },
                { 0xe7, "ord", ind16z, &gggg },
                { 0xe8, "cpd", ind16z, &gggg },
                { 0xe9, "unrecognized", PAGE3 },
                { 0xea, "std", ind16z, &gggg },
                { 0xeb, "unrecognized", PAGE3 },
                { 0xec, "xgdz", inh },
                { 0xed, "adz", inh },
                { 0xee, "unrecognized", PAGE3 },
                { 0xef, "unrecognized", PAGE3 },
                { 0xf0, "subd", ext },
                { 0xf1, "addd", ext },
                { 0xf2, "sbcd", ext },
                { 0xf3, "adcd", ext },
                { 0xf4, "eord", ext },
                { 0xf5, "ldd", ext },
                { 0xf6, "andd", ext },
                { 0xf7, "ord", ext },
                { 0xf8, "cpd", ext },
                { 0xf9, "unrecognized", PAGE3 },
                { 0xfa, "std", ext },
                { 0xfb, "unrecognized", PAGE3 },
                { 0xfc, "tpa", inh },
                { 0xfd, "tap", inh },
                { 0xfe, "movb", ext2ext },
                { 0xff, "movw", ext2ext },

        }
};

#define getb(p)                                         \
        ({                                              \
                uint64_t _p = p;                        \
                if (_p > size || pos > size - _p) {     \
                        warnx("Invalid buffer access"); \
                        errno = EINVAL;                 \
                        return -1;                      \
                }                                       \
                in[pos + p];                            \
        })

static void
print_operand(operand *arg, mode mode UNUSED, uint8_t value[4])
{
        if (arg == &op_b) {
        } else if (arg == &op_ff) {
                printf("0x%02hhx", value[0]);
        } else if (arg == &op_gggg) {
                printf("0x%04hx%04hx", value[0], value[1]);
        } else if (arg == &op_hh) {
        } else if (arg == &op_ii) {
                printf("0x%02hhx", value[0]);
        } else if (arg == &op_jj) {
        } else if (arg == &op_kk) {
        } else if (arg == &op_ll) {
        } else if (arg == &op_mm) {
                printf("(0x%02hhx & ", value[0]);
        } else if (arg == &op_mmmm) {
        } else if (arg == &op_rr) {
        } else if (arg == &op_rrrr) {
        } else if (arg == &op_xo) {
        } else if (arg == &op_yo) {
        } else if (arg == &op_z) {
        } else {
                warnx("Unknown operand %p?!?!?", arg);
                warnx("op_b: %p\n", &op_b);
                warnx("op_ff: %p\n", &op_ff);
                warnx("op_zg: %p\n", &op_zg);
                warnx("op_gggg: %p\n", &op_gggg);
                warnx("op_hh: %p\n", &op_hh);
                warnx("op_ii: %p\n", &op_ii);
                warnx("op_jj: %p\n", &op_jj);
                warnx("op_kk: %p\n", &op_kk);
                warnx("op_ll: %p\n", &op_ll);
                warnx("op_mm: %p\n", &op_mm);
                warnx("op_mmmm: %p\n", &op_mmmm);
                warnx("op_rr: %p\n", &op_rr);
                warnx("op_rrrr: %p\n", &op_rrrr);
                warnx("op_xo: %p\n", &op_xo);
                warnx("op_yo: %p\n", &op_yo);
                warnx("op_z: %p\n", &op_z);
                exit(6);
        }
}

static int
disass(const char * const in, const size_t size)
{
        size_t pos;

        for (pos = 0; pos < size - 2; ) {
                uint8_t prefix;
                uint8_t opcode;
                op op;
                operands *operands;
                int bits;
                char buf[] = "00000000: 00 00 00 0000 0000 0000 0000 0000\t";
                char *next;
                size_t sz;
                int bytes = 0;

                switch (in[pos]) {
                case 0x17:
                case 0x27:
                case 0x37:
                        prefix = getb(0);
                        opcode = getb(1);
                        break;
                default:
                        prefix = 0;
                        opcode = getb(0);
                        break;
                }

                op = opcodes[(prefix >> 4) & 3][opcode];
                bytes = prefix ? 2 : 1;

                bits = 0;
                operands = op.operands;
                for (int i = 0; operands && i < operands->num; i++)
                        bits += operands->elements[i]->bits;

                sz = sizeof(buf);
                next = spnprintf(buf, sz, "%08zx: %02hhx", pos, prefix ? prefix : opcode);
                if (prefix)
                        next = spnprintf(next, sz, "%02hhx", opcode);
                //printf("prefix:%02hhx bits:%d ", prefix, bits);
                for (int i = bits / 8 + (bits % 8 ? 1 : 0);
                     i > 0;
                     i--) {
                        uint8_t byte = getb(i);
                        uint8_t mask = 0xff;
                        if (i == (prefix ? 2 : 1) && bits % 8)
                                mask = (1 << bits % 8) - 1;
                        //printf("i:%d byte:%02hhx mask:%02hhx: ", i, byte, mask);
                        next = spnprintf(next, sz, "%02hhx", byte & mask);
                        bytes += 1;
                }
                memset(next, ' ', sz);
                buf[sizeof(buf)-1] = 0;
                sz = sizeof(buf) + 25;
                next = buf + 25;
                next = spnprintf(next, sz, " ");
                printf("%s ", buf);

                printf("%s%c", op.mnemonic, ' ');

                if (op.operands) {
                        int localbits = 0;
                        operands = op.operands;
                        bytes = prefix ? 2 : 1;
                        for (int j = 0; j < operands->num; j++) {
                                uint8_t value[4];

                                operand *operand = operands->elements[j];

                                value[0] = getb(bytes);
                                localbits = operand->bits;
                                for (int k = 1; localbits > 8; k++) {
                                        value[k] = getb(bytes+k);
                                        localbits -= localbits >= 8 ? 8 : localbits;
                                }
                                value[1] = getb(bytes+1);
                                switch(op.mode) {
                                case ind8x:
                                case ind16x:
                                        printf("[%%x]+");
                                        break;
                                case ind8y:
                                case ind16y:
                                        printf("[%%y]+");
                                        break;
                                case ind8z:
                                case ind16z:
                                        printf("[%%z]+");
                                        break;
                                default:
                                        break;
                                }
                                print_operand(operand, op.mode, value);
                                if (j + 1 < operands->num)
                                        printf(", ");
                                localbits += operand->bits;
                                while (localbits > 8) {
                                        pos += 1;
                                        localbits -= 8;
                                }
                        }
                        //printf("%s\n", operands->name);
                        pos += 1;
                } else {
                        pos += bytes;
                }
                puts("\n");
        }

        return 0;
}

static void NORETURN
usage(int status)
{
        FILE *out = status == 0 ? stdout : stderr;

        putsf(out, "usage: hc16 <INFILE>\n");
        exit(1);
}

static void
process_file(const char * const filename)
{
        FILE *in = NULL;
        struct stat sb;
        int rc;
        char *buf;
        size_t sz, bufsize;

        in = fopen(filename, "r");
        if (!filename)
                err(2, "Could not open \"%s\"", filename);

        rc = fstat(fileno(in), &sb);
        if (rc < 0)
                err(3, "Could not stat input");

        bufsize = sb.st_size;
        bufsize += bufsize % 4 ? bufsize % 4 : 0;

        buf = calloc(1, bufsize);
        if (!buf)
                err(4, "Could not allocate memory");

        sz = fread(buf, 1, sb.st_size, in);
        errno = ferror(in);
        if (sz <= 0 || sz != (uint64_t)sb.st_size)
                err(5, "Could not read file");

        fclose(in);

        disass(buf, sb.st_size);

        free(buf);
}

int main(int argc, char *argv[])
{
        if (argc < 2)
                usage(1);

        for (int i = 1; i < argc; i++) {
                if (!strcmp(argv[i], "--help") ||
                    !strcmp(argv[i], "-h") ||
                    !strcmp(argv[i], "-?") ||
                    !strcmp(argv[i], "--usage"))
                        usage(0);

                if (!strcmp(argv[i], "-d") ||
                    !strcmp(argv[i], "--debug")) {
                        dbg += 1;
                        continue;
                }

                process_file(argv[i]);
        }

        exit(0);
}

// vim:fenc=utf-8:tw=75:et
