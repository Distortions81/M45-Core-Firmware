/* Private implementation fragment included by stratum_miner.c. */

#define SHA_TEXT_MEMW() asm volatile("memw" ::: "memory")
#define SHA_TEXT_STORE(base, byte_offset, value) \
  asm volatile("s32i %0, %1, " #byte_offset : : "r"(value), "r"(base))
#define SHA_CMD_STORE(base, byte_offset, value) \
  asm volatile("s32i %0, %1, " #byte_offset : : "r"(value), "r"(base))
#define SHA_TEXT_READ(base, apb_base, byte_offset) \
  __extension__({ \
    uint32_t value; \
    asm volatile("l32i %0, %1, 0\n" \
                 "l32i %0, %2, " #byte_offset \
                 : "=&a"(value) \
                 : "r"(apb_base), "r"(base) \
                 : "memory"); \
    value; \
  })

static inline __attribute__((always_inline)) uint32_t IRAM_ATTR sha_dport_reg_read(uint32_t reg) {
#if defined(BOOTLOADER_BUILD) || defined(CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE) || !SOC_DPORT_WORKAROUND
  return _DPORT_REG_READ(reg);
#else
  uint32_t apb;
  unsigned int int_level;
  asm volatile("rsil %[level], " XTSTR(SOC_DPORT_WORKAROUND_DIS_INTERRUPT_LVL) "\n"
               "movi %[apb], " XTSTR(0x3ff40078) "\n"
               "l32i %[apb], %[apb], 0\n"
               "l32i %[reg], %[reg], 0\n"
               "wsr %[level], " XTSTR(PS) "\n"
               "rsync\n"
               : [apb] "=a"(apb), [reg] "+a"(reg), [level] "=a"(int_level)
               :
               : "memory");
  return reg;
#endif
}

static inline __attribute__((always_inline)) void IRAM_ATTR synthetic_hw_sha_wait_idle(void) {
  while (sha_dport_reg_read(SHA_256_BUSY_REG) != 0) {
  }
}

static inline __attribute__((always_inline)) void IRAM_ATTR synthetic_hw_sha_write_block(
    const uint32_t block[16]) {
  uint32_t* reg = (uint32_t*)SHA_TEXT_BASE;
  SHA_TEXT_STORE(reg, 0, block[0]);
  SHA_TEXT_STORE(reg, 4, block[1]);
  SHA_TEXT_STORE(reg, 8, block[2]);
  SHA_TEXT_STORE(reg, 12, block[3]);
  SHA_TEXT_STORE(reg, 16, block[4]);
  SHA_TEXT_STORE(reg, 20, block[5]);
  SHA_TEXT_STORE(reg, 24, block[6]);
  SHA_TEXT_STORE(reg, 28, block[7]);
  SHA_TEXT_STORE(reg, 32, block[8]);
  SHA_TEXT_STORE(reg, 36, block[9]);
  SHA_TEXT_STORE(reg, 40, block[10]);
  SHA_TEXT_STORE(reg, 44, block[11]);
  SHA_TEXT_STORE(reg, 48, block[12]);
  SHA_TEXT_STORE(reg, 52, block[13]);
  SHA_TEXT_STORE(reg, 56, block[14]);
  SHA_TEXT_STORE(reg, 60, block[15]);
  SHA_TEXT_MEMW();
}

static inline __attribute__((always_inline)) void IRAM_ATTR synthetic_hw_sha_read_state(
    uint32_t state[8]) {
  DPORT_REG_WRITE(SHA_256_LOAD_REG, 1);
  synthetic_hw_sha_wait_idle();
  DPORT_INTERRUPT_DISABLE();
  state[0] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 0 * 4);
  state[1] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 1 * 4);
  state[2] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 2 * 4);
  state[3] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 3 * 4);
  state[4] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 4 * 4);
  state[5] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 5 * 4);
  state[6] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 6 * 4);
  state[7] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 7 * 4);
  DPORT_INTERRUPT_RESTORE();
}

static void synthetic_hw_prepare_blocks(
    uint32_t block0[16],
    uint32_t block1[16],
    const uint8_t header[80]) {
  for (size_t i = 0; i < 16; ++i) {
    block0[i] = read_u32_be(header + i * 4);
  }
  block1[0] = read_u32_be(header + 64);
  block1[1] = read_u32_be(header + 68);
  block1[2] = read_u32_be(header + 72);
  block1[3] = 0;
  block1[4] = 0x80000000UL;
  for (size_t i = 5; i < 15; ++i) {
    block1[i] = 0;
  }
  block1[15] = 80UL * 8UL;
}

static void IRAM_ATTR synthetic_hw_sha256d_second_state(
    const uint32_t block0[16],
    uint32_t block1[16],
    uint32_t nonce,
    uint32_t second_state[8]) {
  uint32_t* reg = (uint32_t*)SHA_TEXT_BASE;

  synthetic_hw_sha_write_block(block0);
  DPORT_REG_WRITE(SHA_256_START_REG, 1);

  block1[3] = byteswap32(nonce);
  synthetic_hw_sha_write_block(block1);
  synthetic_hw_sha_wait_idle();
  DPORT_REG_WRITE(SHA_256_CONTINUE_REG, 1);
  synthetic_hw_sha_wait_idle();

  DPORT_REG_WRITE(SHA_256_LOAD_REG, 1);
  synthetic_hw_sha_wait_idle();

  SHA_TEXT_STORE(reg, 32, 0x80000000UL);
  SHA_TEXT_STORE(reg, 60, 32UL * 8UL);
  SHA_TEXT_MEMW();
  DPORT_REG_WRITE(SHA_256_START_REG, 1);
  synthetic_hw_sha_wait_idle();
  synthetic_hw_sha_read_state(second_state);
}

static bool IRAM_ATTR synthetic_hw_sha256d_meets_target(
    const uint32_t block0[16],
    uint32_t block1[16],
    uint32_t nonce,
    const uint32_t target_words[8],
    uint32_t target_word0) {
  uint32_t* reg = (uint32_t*)SHA_TEXT_BASE;

  synthetic_hw_sha_write_block(block0);
  DPORT_REG_WRITE(SHA_256_START_REG, 1);

  block1[3] = byteswap32(nonce);
  synthetic_hw_sha_write_block(block1);
  synthetic_hw_sha_wait_idle();
  DPORT_REG_WRITE(SHA_256_CONTINUE_REG, 1);
  synthetic_hw_sha_wait_idle();

  DPORT_REG_WRITE(SHA_256_LOAD_REG, 1);
  synthetic_hw_sha_wait_idle();

  SHA_TEXT_STORE(reg, 32, 0x80000000UL);
  SHA_TEXT_STORE(reg, 60, 32UL * 8UL);
  SHA_TEXT_MEMW();
  DPORT_REG_WRITE(SHA_256_START_REG, 1);
  synthetic_hw_sha_wait_idle();

  DPORT_REG_WRITE(SHA_256_LOAD_REG, 1);
  synthetic_hw_sha_wait_idle();
  const uint32_t compare_word0 = byteswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 7 * 4));
  if (LIKELY(compare_word0 != target_word0)) {
    return compare_word0 < target_word0;
  }
  DPORT_INTERRUPT_DISABLE();
  for (size_t i = 1; i < 8; ++i) {
    const uint32_t hash_word =
        byteswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + (7 - i) * 4));
    if (UNLIKELY(hash_word != target_words[i])) {
      DPORT_INTERRUPT_RESTORE();
      return hash_word < target_words[i];
    }
  }
  DPORT_INTERRUPT_RESTORE();
  return true;
}

static bool IRAM_ATTR synthetic_hw_nonce_meets_target(
    synthetic_task_context_t* context,
    uint32_t nonce) {
  return synthetic_hw_sha256d_meets_target(context->hw_block0,
                                          context->hw_block1,
                                          nonce,
                                          context->work.target_words,
                                          context->work.target_word0);
}

static bool synthetic_hw_self_check(const synthetic_task_context_t* context) {
  uint32_t hw_block1[16];
  uint32_t software_state[8];
  uint32_t hardware_state[8];
  static const uint32_t test_nonces[] = {0, 1, 0x12345678UL, 0xffffffffUL};

  memcpy(hw_block1, context->hw_block1, sizeof(hw_block1));
  esp_sha_lock_engine(SHA2_256);
  for (size_t i = 0; i < sizeof(test_nonces) / sizeof(test_nonces[0]); ++i) {
    const uint32_t nonce = test_nonces[i];
    sha256d_midstate_second_state(context->work.midstate,
                                  context->work.tail_words,
                                  nonce,
                                  software_state);
    synthetic_hw_sha256d_second_state(context->hw_block0, hw_block1, nonce, hardware_state);
    if (memcmp(software_state, hardware_state, sizeof(software_state)) != 0) {
      APP_SERIAL_LOGF("synthetic: hardware sha self-check failed nonce=%08x\n", (unsigned)nonce);
      esp_sha_unlock_engine(SHA2_256);
      return false;
    }
  }
  esp_sha_unlock_engine(SHA2_256);
  return true;
}
