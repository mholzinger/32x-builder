/* SH2ZAM quarter-wave sine lookup: Q16.16, 257 samples, 1,028-byte table.
 * Values generated from the mathematical sin() definition; MIT, root LICENSE.
 * Copyright (c) 2026 SH2ZAM Contributors.
 */
#include "sh2zam_internal.h"

/* Quarter turn = 16384 units. Linear interpolation between 64-unit steps. */
static const int32_t sin_quarter[257] = {
    0, 402, 804, 1206, 1608, 2010, 2412, 2814,
    3216, 3617, 4019, 4420, 4821, 5222, 5623, 6023,
    6424, 6824, 7224, 7623, 8022, 8421, 8820, 9218,
    9616, 10014, 10411, 10808, 11204, 11600, 11996, 12391,
    12785, 13180, 13573, 13966, 14359, 14751, 15143, 15534,
    15924, 16314, 16703, 17091, 17479, 17867, 18253, 18639,
    19024, 19409, 19792, 20175, 20557, 20939, 21320, 21699,
    22078, 22457, 22834, 23210, 23586, 23961, 24335, 24708,
    25080, 25451, 25821, 26190, 26558, 26925, 27291, 27656,
    28020, 28383, 28745, 29106, 29466, 29824, 30182, 30538,
    30893, 31248, 31600, 31952, 32303, 32652, 33000, 33347,
    33692, 34037, 34380, 34721, 35062, 35401, 35738, 36075,
    36410, 36744, 37076, 37407, 37736, 38064, 38391, 38716,
    39040, 39362, 39683, 40002, 40320, 40636, 40951, 41264,
    41576, 41886, 42194, 42501, 42806, 43110, 43412, 43713,
    44011, 44308, 44604, 44898, 45190, 45480, 45769, 46056,
    46341, 46624, 46906, 47186, 47464, 47741, 48015, 48288,
    48559, 48828, 49095, 49361, 49624, 49886, 50146, 50404,
    50660, 50914, 51166, 51417, 51665, 51911, 52156, 52398,
    52639, 52878, 53114, 53349, 53581, 53812, 54040, 54267,
    54491, 54714, 54934, 55152, 55368, 55582, 55794, 56004,
    56212, 56418, 56621, 56823, 57022, 57219, 57414, 57607,
    57798, 57986, 58172, 58356, 58538, 58718, 58896, 59071,
    59244, 59415, 59583, 59750, 59914, 60075, 60235, 60392,
    60547, 60700, 60851, 60999, 61145, 61288, 61429, 61568,
    61705, 61839, 61971, 62101, 62228, 62353, 62476, 62596,
    62714, 62830, 62943, 63054, 63162, 63268, 63372, 63473,
    63572, 63668, 63763, 63854, 63944, 64031, 64115, 64197,
    64277, 64354, 64429, 64501, 64571, 64639, 64704, 64766,
    64827, 64884, 64940, 64993, 65043, 65091, 65137, 65180,
    65220, 65259, 65294, 65328, 65358, 65387, 65413, 65436,
    65457, 65476, 65492, 65505, 65516, 65525, 65531, 65535,
    65536
};

static sh2z_fx_t lookup_quarter(uint16_t offset) {
    if (offset >= 16384) return SH2Z_ONE;
    const uint16_t index = (uint16_t)(offset >> 6);
    const uint16_t frac = (uint16_t)(offset & 63u);
    const int32_t a = sin_quarter[index], b = sin_quarter[index+1];
    /* The unchanged table is nonnegative and monotonic. Pulling a*64
     * outside the shift removes one multiply without changing rounding. */
    return (sh2z_fx_t)(a + (((b-a)*(int32_t)frac + 32) >> 6));
}

/* Offset is in [0,16383]. A cosine uses the reflected
 * table cell; +31 preserves the original +32 rounding at its opposite end. */
static void lookup_pair(uint16_t offset,sh2z_fx_t *s,sh2z_fx_t *c) {
    const unsigned index=(unsigned)(offset>>6);
    const int32_t frac=(int32_t)(offset&63u);
    const int32_t s0=sin_quarter[index],c0=sin_quarter[256u-index];
    *s=s0+(((sin_quarter[index+1u]-s0)*frac+32)>>6);
    *c=c0-(((c0-sin_quarter[255u-index])*frac+31)>>6);
}

sh2z_angle_t sh2z_angle_from_degrees(int32_t degrees) {
    int32_t normalized = degrees % 360;
    if (normalized < 0) normalized += 360;
    return (sh2z_angle_t)((normalized * 65536 + 180) / 360);
}
sh2z_fx_t sh2z_sin(sh2z_angle_t angle) {
    const unsigned quadrant = (unsigned)(angle >> 14);
    const uint16_t offset = (uint16_t)(angle & 16383u);
    if (quadrant == 0) return lookup_quarter(offset);
    if (quadrant == 1) return lookup_quarter((uint16_t)(16384u-offset));
    if (quadrant == 2) return -lookup_quarter(offset);
    return -lookup_quarter((uint16_t)(16384u-offset));
}
sh2z_fx_t sh2z_cos(sh2z_angle_t angle) {
    return sh2z_sin((sh2z_angle_t)(angle + SH2Z_ANGLE_QUARTER));
}

void sh2z_sincos(sh2z_angle_t angle,sh2z_fx_t *s,sh2z_fx_t *c) {
    /* Keep single-output calls cheap, and retain the public null semantics. */
    if(!s) { if(c) *c=sh2z_cos(angle); return; }
    if(!c) { *s=sh2z_sin(angle); return; }
    sh2z_fx_t sv,cv;
    lookup_pair((uint16_t)(angle&16383u),&sv,&cv);
    if(angle&SH2Z_ANGLE_QUARTER) {
        const sh2z_fx_t t=sv;
        sv=cv; cv=-t;
    }
    if(angle&SH2Z_ANGLE_HALF) { sv=-sv; cv=-cv; }
    /* Store in the same order as the separate calls, including s == c. */
    *s=sv; *c=cv;
}

/* First-octant estimates from the existing exact table decisions at i/256.
 * This 514-byte table only nominates an angle. The two neighboring decision
 * boundaries below restore the original answer, including lower-angle ties.
 * Exhaustive ratio-bin tests prove the estimate is within one angle unit. */
static const uint16_t atan_octant[257] = {
    0, 41, 81, 122, 163, 204, 244, 285, 326, 367, 407, 448,
    489, 529, 570, 610, 651, 692, 732, 773, 813, 854, 894, 935,
    975, 1015, 1056, 1096, 1136, 1176, 1217, 1257, 1297, 1337, 1377, 1417,
    1457, 1497, 1537, 1577, 1617, 1656, 1696, 1736, 1775, 1815, 1854, 1894,
    1933, 1973, 2012, 2051, 2090, 2129, 2168, 2207, 2246, 2285, 2324, 2363,
    2401, 2440, 2478, 2517, 2555, 2594, 2632, 2670, 2708, 2746, 2784, 2822,
    2860, 2897, 2935, 2973, 3010, 3047, 3085, 3122, 3159, 3196, 3233, 3270,
    3307, 3344, 3380, 3417, 3453, 3490, 3526, 3562, 3599, 3635, 3670, 3706,
    3742, 3778, 3813, 3849, 3884, 3919, 3955, 3990, 4025, 4060, 4095, 4129,
    4164, 4199, 4233, 4267, 4301, 4336, 4370, 4404, 4438, 4471, 4505, 4539,
    4572, 4605, 4639, 4672, 4705, 4738, 4771, 4803, 4836, 4869, 4901, 4933,
    4966, 4998, 5030, 5062, 5094, 5125, 5157, 5188, 5220, 5251, 5282, 5313,
    5344, 5375, 5406, 5437, 5467, 5498, 5528, 5559, 5589, 5619, 5649, 5679,
    5708, 5738, 5768, 5797, 5826, 5856, 5885, 5914, 5943, 5972, 6000, 6029,
    6058, 6086, 6114, 6142, 6171, 6199, 6227, 6254, 6282, 6310, 6337, 6365,
    6392, 6419, 6446, 6473, 6500, 6527, 6554, 6580, 6607, 6633, 6660, 6686,
    6712, 6738, 6764, 6790, 6815, 6841, 6867, 6892, 6917, 6943, 6968, 6993,
    7018, 7043, 7068, 7092, 7117, 7141, 7166, 7190, 7214, 7238, 7262, 7286,
    7310, 7334, 7358, 7381, 7405, 7428, 7451, 7475, 7498, 7521, 7544, 7566,
    7589, 7612, 7634, 7657, 7679, 7702, 7724, 7746, 7768, 7790, 7812, 7834,
    7856, 7877, 7899, 7920, 7942, 7963, 7984, 8005, 8026, 8047, 8068, 8089,
    8110, 8130, 8151, 8172, 8192,
};

sh2z_angle_t sh2z_atan2(sh2z_fx_t y,sh2z_fx_t x) {
    if(!y) return (sh2z_angle_t)(x<0 ? SH2Z_ANGLE_HALF : 0);
    if(!x) return y>0 ? SH2Z_ANGLE_QUARTER : (sh2z_angle_t)49152u;
    const uint32_t ax=sh2zi_magnitude(x),ay=sh2zi_magnitude(y);
    unsigned a;
    if(ax==ay) a=8192;
    else {
        const uint32_t ratio=ay<ax ? sh2zi_fraction16(ay,ax) : sh2zi_fraction16(ax,ay);
        const unsigned index=ratio>>8,frac=ratio&255u;
        const unsigned low=atan_octant[index],delta=(unsigned)atan_octant[index+1u]-low;
        a=low+((delta*frac+128u)>>8);
        if(ay>ax) a=16384u-a;
        /* A result k changes to k+1 exactly when
         * ay*(cos(k)+cos(k+1)) > ax*(sin(k)+sin(k+1)).
         * The first-quadrant decision is monotonic. For every Q16 ratio bin,
         * testing both inclusive endpoints proves that a is within one unit
         * of the exact result; both original boundaries therefore suffice. */
        if(!a) {
            if((uint64_t)ay*131072u>(uint64_t)ax*6u) ++a;
        } else if(a==16384u) {
            if((uint64_t)ay*6u<=(uint64_t)ax*131072u) --a;
        } else {
            const unsigned cell=a>>6;
            const int32_t f=(int32_t)(a&63u);
            const int32_t s0=sin_quarter[cell],c0=sin_quarter[256u-cell];
            const int32_t ds=sin_quarter[cell+1u]-s0;
            const int32_t dc=c0-sin_quarter[255u-cell];
            const int32_t ps=ds*f,pc=dc*f;
            const uint32_t s=(uint32_t)(s0+((ps+32)>>6));
            const uint32_t c=(uint32_t)(c0-((pc+31)>>6));
            uint32_t previous_s,previous_c;
            if(f) {
                previous_s=(uint32_t)(s0+((ps-ds+32)>>6));
                previous_c=(uint32_t)(c0-((pc-dc+31)>>6));
            } else {
                previous_s=(uint32_t)(s0-((s0-sin_quarter[cell-1u]+31)>>6));
                previous_c=(uint32_t)(c0+((sin_quarter[257u-cell]-c0+32)>>6));
            }
            if((uint64_t)ay*(previous_c+c)<=(uint64_t)ax*(previous_s+s)) --a;
            else {
                const uint32_t next_s=(uint32_t)(s0+((ps+ds+32)>>6));
                const uint32_t next_c=(uint32_t)(c0-((pc+dc+31)>>6));
                if((uint64_t)ay*(c+next_c)>(uint64_t)ax*(s+next_s)) ++a;
            }
        }
    }
    if(x<0) a=32768u-a;
    if(y<0) a=65536u-a;
    return (sh2z_angle_t)a;
}
