/* Private implementation fragment included by stratum_miner.c. */

static inline __attribute__((always_inline)) uint32_t IRAM_ATTR rotr32(uint32_t value, uint8_t bits) {
  return (value >> bits) | (value << (32U - bits));
}

static inline __attribute__((always_inline)) uint32_t IRAM_ATTR byteswap32(uint32_t value) {
#if defined(__GNUC__)
  return __builtin_bswap32(value);
#else
  return ((value & 0x000000ffUL) << 24) | ((value & 0x0000ff00UL) << 8) |
         ((value & 0x00ff0000UL) >> 8) | ((value & 0xff000000UL) >> 24);
#endif
}

static inline __attribute__((always_inline)) uint32_t read_u32_be(const uint8_t* bytes) {
  return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
         ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static inline __attribute__((always_inline)) void write_u32_be(uint8_t* bytes, uint32_t value) {
  bytes[0] = (uint8_t)(value >> 24);
  bytes[1] = (uint8_t)(value >> 16);
  bytes[2] = (uint8_t)(value >> 8);
  bytes[3] = (uint8_t)value;
}

static inline __attribute__((always_inline)) uint32_t IRAM_ATTR sha256_compress_words(
    const uint32_t* state,
    uint32_t* output,
    uint32_t* output_word7,
    uint32_t w0,
    uint32_t w1,
    uint32_t w2,
    uint32_t w3,
    uint32_t w4,
    uint32_t w5,
    uint32_t w6,
    uint32_t w7,
    uint32_t w8,
    uint32_t w9,
    uint32_t w10,
    uint32_t w11,
    uint32_t w12,
    uint32_t w13,
    uint32_t w14,
    uint32_t w15) {
  uint32_t a = state[0];
  uint32_t b = state[1];
  uint32_t c = state[2];
  uint32_t d = state[3];
  uint32_t e = state[4];
  uint32_t f = state[5];
  uint32_t g = state[6];
  uint32_t h = state[7];

#define SHA256_BIG0(x) (rotr32((x), 2) ^ rotr32((x), 13) ^ rotr32((x), 22))
#define SHA256_BIG1(x) (rotr32((x), 6) ^ rotr32((x), 11) ^ rotr32((x), 25))
#define SHA256_SMALL0(x) (rotr32((x), 7) ^ rotr32((x), 18) ^ ((x) >> 3))
#define SHA256_SMALL1(x) (rotr32((x), 17) ^ rotr32((x), 19) ^ ((x) >> 10))
#define SHA256_CH(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((z) & ((x) ^ (y))))
#define SHA256_SCHED(w, w1, w9, w14) ((w) += SHA256_SMALL1(w14) + (w9) + SHA256_SMALL0(w1))
#define SHA256_ROUND(a, b, c, d, e, f, g, h, k, w) \
  do { \
    const uint32_t temp1 = (h) + SHA256_BIG1(e) + SHA256_CH((e), (f), (g)) + (k) + (w); \
    const uint32_t temp2 = SHA256_BIG0(a) + SHA256_MAJ((a), (b), (c)); \
    (d) += temp1; \
    (h) = temp1 + temp2; \
  } while (false)

  SHA256_ROUND(a, b, c, d, e, f, g, h, 0x428a2f98UL, w0);
  SHA256_ROUND(h, a, b, c, d, e, f, g, 0x71374491UL, w1);
  SHA256_ROUND(g, h, a, b, c, d, e, f, 0xb5c0fbcfUL, w2);
  SHA256_ROUND(f, g, h, a, b, c, d, e, 0xe9b5dba5UL, w3);
  SHA256_ROUND(e, f, g, h, a, b, c, d, 0x3956c25bUL, w4);
  SHA256_ROUND(d, e, f, g, h, a, b, c, 0x59f111f1UL, w5);
  SHA256_ROUND(c, d, e, f, g, h, a, b, 0x923f82a4UL, w6);
  SHA256_ROUND(b, c, d, e, f, g, h, a, 0xab1c5ed5UL, w7);
  SHA256_ROUND(a, b, c, d, e, f, g, h, 0xd807aa98UL, w8);
  SHA256_ROUND(h, a, b, c, d, e, f, g, 0x12835b01UL, w9);
  SHA256_ROUND(g, h, a, b, c, d, e, f, 0x243185beUL, w10);
  SHA256_ROUND(f, g, h, a, b, c, d, e, 0x550c7dc3UL, w11);
  SHA256_ROUND(e, f, g, h, a, b, c, d, 0x72be5d74UL, w12);
  SHA256_ROUND(d, e, f, g, h, a, b, c, 0x80deb1feUL, w13);
  SHA256_ROUND(c, d, e, f, g, h, a, b, 0x9bdc06a7UL, w14);
  SHA256_ROUND(b, c, d, e, f, g, h, a, 0xc19bf174UL, w15);
  SHA256_ROUND(a, b, c, d, e, f, g, h, 0xe49b69c1UL, SHA256_SCHED(w0, w1, w9, w14));
  SHA256_ROUND(h, a, b, c, d, e, f, g, 0xefbe4786UL, SHA256_SCHED(w1, w2, w10, w15));
  SHA256_ROUND(g, h, a, b, c, d, e, f, 0x0fc19dc6UL, SHA256_SCHED(w2, w3, w11, w0));
  SHA256_ROUND(f, g, h, a, b, c, d, e, 0x240ca1ccUL, SHA256_SCHED(w3, w4, w12, w1));
  SHA256_ROUND(e, f, g, h, a, b, c, d, 0x2de92c6fUL, SHA256_SCHED(w4, w5, w13, w2));
  SHA256_ROUND(d, e, f, g, h, a, b, c, 0x4a7484aaUL, SHA256_SCHED(w5, w6, w14, w3));
  SHA256_ROUND(c, d, e, f, g, h, a, b, 0x5cb0a9dcUL, SHA256_SCHED(w6, w7, w15, w4));
  SHA256_ROUND(b, c, d, e, f, g, h, a, 0x76f988daUL, SHA256_SCHED(w7, w8, w0, w5));
  SHA256_ROUND(a, b, c, d, e, f, g, h, 0x983e5152UL, SHA256_SCHED(w8, w9, w1, w6));
  SHA256_ROUND(h, a, b, c, d, e, f, g, 0xa831c66dUL, SHA256_SCHED(w9, w10, w2, w7));
  SHA256_ROUND(g, h, a, b, c, d, e, f, 0xb00327c8UL, SHA256_SCHED(w10, w11, w3, w8));
  SHA256_ROUND(f, g, h, a, b, c, d, e, 0xbf597fc7UL, SHA256_SCHED(w11, w12, w4, w9));
  SHA256_ROUND(e, f, g, h, a, b, c, d, 0xc6e00bf3UL, SHA256_SCHED(w12, w13, w5, w10));
  SHA256_ROUND(d, e, f, g, h, a, b, c, 0xd5a79147UL, SHA256_SCHED(w13, w14, w6, w11));
  SHA256_ROUND(c, d, e, f, g, h, a, b, 0x06ca6351UL, SHA256_SCHED(w14, w15, w7, w12));
  SHA256_ROUND(b, c, d, e, f, g, h, a, 0x14292967UL, SHA256_SCHED(w15, w0, w8, w13));
  SHA256_ROUND(a, b, c, d, e, f, g, h, 0x27b70a85UL, SHA256_SCHED(w0, w1, w9, w14));
  SHA256_ROUND(h, a, b, c, d, e, f, g, 0x2e1b2138UL, SHA256_SCHED(w1, w2, w10, w15));
  SHA256_ROUND(g, h, a, b, c, d, e, f, 0x4d2c6dfcUL, SHA256_SCHED(w2, w3, w11, w0));
  SHA256_ROUND(f, g, h, a, b, c, d, e, 0x53380d13UL, SHA256_SCHED(w3, w4, w12, w1));
  SHA256_ROUND(e, f, g, h, a, b, c, d, 0x650a7354UL, SHA256_SCHED(w4, w5, w13, w2));
  SHA256_ROUND(d, e, f, g, h, a, b, c, 0x766a0abbUL, SHA256_SCHED(w5, w6, w14, w3));
  SHA256_ROUND(c, d, e, f, g, h, a, b, 0x81c2c92eUL, SHA256_SCHED(w6, w7, w15, w4));
  SHA256_ROUND(b, c, d, e, f, g, h, a, 0x92722c85UL, SHA256_SCHED(w7, w8, w0, w5));
  SHA256_ROUND(a, b, c, d, e, f, g, h, 0xa2bfe8a1UL, SHA256_SCHED(w8, w9, w1, w6));
  SHA256_ROUND(h, a, b, c, d, e, f, g, 0xa81a664bUL, SHA256_SCHED(w9, w10, w2, w7));
  SHA256_ROUND(g, h, a, b, c, d, e, f, 0xc24b8b70UL, SHA256_SCHED(w10, w11, w3, w8));
  SHA256_ROUND(f, g, h, a, b, c, d, e, 0xc76c51a3UL, SHA256_SCHED(w11, w12, w4, w9));
  SHA256_ROUND(e, f, g, h, a, b, c, d, 0xd192e819UL, SHA256_SCHED(w12, w13, w5, w10));
  SHA256_ROUND(d, e, f, g, h, a, b, c, 0xd6990624UL, SHA256_SCHED(w13, w14, w6, w11));
  SHA256_ROUND(c, d, e, f, g, h, a, b, 0xf40e3585UL, SHA256_SCHED(w14, w15, w7, w12));
  SHA256_ROUND(b, c, d, e, f, g, h, a, 0x106aa070UL, SHA256_SCHED(w15, w0, w8, w13));
  SHA256_ROUND(a, b, c, d, e, f, g, h, 0x19a4c116UL, SHA256_SCHED(w0, w1, w9, w14));
  SHA256_ROUND(h, a, b, c, d, e, f, g, 0x1e376c08UL, SHA256_SCHED(w1, w2, w10, w15));
  SHA256_ROUND(g, h, a, b, c, d, e, f, 0x2748774cUL, SHA256_SCHED(w2, w3, w11, w0));
  SHA256_ROUND(f, g, h, a, b, c, d, e, 0x34b0bcb5UL, SHA256_SCHED(w3, w4, w12, w1));
  SHA256_ROUND(e, f, g, h, a, b, c, d, 0x391c0cb3UL, SHA256_SCHED(w4, w5, w13, w2));
  SHA256_ROUND(d, e, f, g, h, a, b, c, 0x4ed8aa4aUL, SHA256_SCHED(w5, w6, w14, w3));
  SHA256_ROUND(c, d, e, f, g, h, a, b, 0x5b9cca4fUL, SHA256_SCHED(w6, w7, w15, w4));
  SHA256_ROUND(b, c, d, e, f, g, h, a, 0x682e6ff3UL, SHA256_SCHED(w7, w8, w0, w5));
  SHA256_ROUND(a, b, c, d, e, f, g, h, 0x748f82eeUL, SHA256_SCHED(w8, w9, w1, w6));
  SHA256_ROUND(h, a, b, c, d, e, f, g, 0x78a5636fUL, SHA256_SCHED(w9, w10, w2, w7));
  SHA256_ROUND(g, h, a, b, c, d, e, f, 0x84c87814UL, SHA256_SCHED(w10, w11, w3, w8));
  SHA256_ROUND(f, g, h, a, b, c, d, e, 0x8cc70208UL, SHA256_SCHED(w11, w12, w4, w9));
  SHA256_ROUND(e, f, g, h, a, b, c, d, 0x90befffaUL, SHA256_SCHED(w12, w13, w5, w10));
  SHA256_ROUND(d, e, f, g, h, a, b, c, 0xa4506cebUL, SHA256_SCHED(w13, w14, w6, w11));
  SHA256_ROUND(c, d, e, f, g, h, a, b, 0xbef9a3f7UL, SHA256_SCHED(w14, w15, w7, w12));
  SHA256_ROUND(b, c, d, e, f, g, h, a, 0xc67178f2UL, SHA256_SCHED(w15, w0, w8, w13));

#undef SHA256_ROUND
#undef SHA256_SCHED
#undef SHA256_MAJ
#undef SHA256_CH
#undef SHA256_SMALL1
#undef SHA256_SMALL0
#undef SHA256_BIG1
#undef SHA256_BIG0

  const uint32_t out0 = state[0] + a;
  const uint32_t out7 = state[7] + h;
  if (output != NULL) {
    output[0] = out0;
    output[1] = state[1] + b;
    output[2] = state[2] + c;
    output[3] = state[3] + d;
    output[4] = state[4] + e;
    output[5] = state[5] + f;
    output[6] = state[6] + g;
    output[7] = out7;
  }
  if (output_word7 != NULL) {
    *output_word7 = out7;
  }
  return out0;
}

static void sha256_midstate_first_block(const uint8_t block[64], uint32_t midstate[8]) {
  sha256_compress_words(SHA256_INITIAL_STATE,
                        midstate,
                        NULL,
                        read_u32_be(block),
                        read_u32_be(block + 4),
                        read_u32_be(block + 8),
                        read_u32_be(block + 12),
                        read_u32_be(block + 16),
                        read_u32_be(block + 20),
                        read_u32_be(block + 24),
                        read_u32_be(block + 28),
                        read_u32_be(block + 32),
                        read_u32_be(block + 36),
                        read_u32_be(block + 40),
                        read_u32_be(block + 44),
                        read_u32_be(block + 48),
                        read_u32_be(block + 52),
                        read_u32_be(block + 56),
                        read_u32_be(block + 60));
}

static void prepare_job_hashing(mining_job_t* job) {
  sha256_midstate_first_block(job->header, job->header_midstate);
  job->header_tail_words[0] = read_u32_be(job->header + 64);
  job->header_tail_words[1] = read_u32_be(job->header + 68);
  job->header_tail_words[2] = read_u32_be(job->header + 72);
}

static void IRAM_ATTR __attribute__((noinline)) sha256d_midstate_second_state_swapped(
    const uint32_t midstate[8],
    const uint32_t tail_words[3],
    uint32_t swapped_nonce,
    uint32_t second_state[8]) {
  uint32_t first_state[8] __attribute__((aligned(16)));
  sha256_compress_words(midstate,
                        first_state,
                        NULL,
                        tail_words[0],
                        tail_words[1],
                        tail_words[2],
                        swapped_nonce,
                        0x80000000UL,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        80UL * 8UL);

  sha256_compress_words(SHA256_INITIAL_STATE,
                        second_state,
                        NULL,
                        first_state[0],
                        first_state[1],
                        first_state[2],
                        first_state[3],
                        first_state[4],
                        first_state[5],
                        first_state[6],
                        first_state[7],
                        0x80000000UL,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        32UL * 8UL);
}

static inline __attribute__((always_inline)) void IRAM_ATTR sha256d_midstate_second_state(
    const uint32_t midstate[8],
    const uint32_t tail_words[3],
    uint32_t nonce,
    uint32_t second_state[8]) {
  sha256d_midstate_second_state_swapped(midstate, tail_words, byteswap32(nonce), second_state);
}

static inline __attribute__((always_inline)) uint32_t IRAM_ATTR sha256d_midstate_second_word0_swapped(
    const uint32_t midstate[8],
    const uint32_t tail_words[3],
    uint32_t swapped_nonce) {
  uint32_t first_state[8] __attribute__((aligned(16)));
  sha256_compress_words(midstate,
                        first_state,
                        NULL,
                        tail_words[0],
                        tail_words[1],
                        tail_words[2],
                        swapped_nonce,
                        0x80000000UL,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        80UL * 8UL);

  return sha256_compress_words(SHA256_INITIAL_STATE,
                               NULL,
                               NULL,
                               first_state[0],
                               first_state[1],
                               first_state[2],
                               first_state[3],
                               first_state[4],
                               first_state[5],
                               first_state[6],
                               first_state[7],
                               0x80000000UL,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               32UL * 8UL);
}

static inline __attribute__((always_inline)) uint32_t IRAM_ATTR
sha256d_midstate_second_target_word0_raw_swapped(
    const uint32_t midstate[8],
    uint32_t tail0,
    uint32_t tail1,
    uint32_t tail2,
    uint32_t swapped_nonce) {
  uint32_t first_state[8] __attribute__((aligned(16)));
  sha256_compress_words(midstate,
                        first_state,
                        NULL,
                        tail0,
                        tail1,
                        tail2,
                        swapped_nonce,
                        0x80000000UL,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        80UL * 8UL);

  uint32_t raw_target_word0 = 0;
  sha256_compress_words(SHA256_INITIAL_STATE,
                        NULL,
                        &raw_target_word0,
                        first_state[0],
                        first_state[1],
                        first_state[2],
                        first_state[3],
                        first_state[4],
                        first_state[5],
                        first_state[6],
                        first_state[7],
                        0x80000000UL,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        32UL * 8UL);
  return raw_target_word0;
}

static void IRAM_ATTR sha256d_miner_second_state(
    const mining_job_t* job, uint32_t nonce, uint32_t second_state[8]) {
  sha256d_midstate_second_state(job->header_midstate, job->header_tail_words, nonce, second_state);
}

static void sha256d_miner_header(const mining_job_t* job, uint32_t nonce, uint8_t out[32]) {
  uint32_t second_state[8];
  sha256d_miner_second_state(job, nonce, second_state);
  for (size_t i = 0; i < 8; ++i) {
    write_u32_be(out + i * 4, second_state[i]);
  }
}

