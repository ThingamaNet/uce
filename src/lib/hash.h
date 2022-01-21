/* ================ sha1.h ================ */
/*
SHA-1 in C
By Steve Reid <steve@edmweb.com>
100% Public Domain
*/


String gen_sha1(String s, bool as_binary = false);

u32 gen_noise32(u32 index, u32 seed = 0);
u64 gen_noise64(u64 index, u64 seed = 0);
f64 gen_noise01(u64 index, u64 seed = 0);

u64 gen_int(u64 from, u64 to, u64 index, u64 seed = 0);
f64 gen_float(f64 from, f64 to, u64 index, u64 seed = 0, f64 decimal_precision = 0.000000000001);

u64 draw_int(u64 from, u64 to);
f64 draw_float(f64 from, f64 to, f64 decimal_precision = 0.000000000001);
