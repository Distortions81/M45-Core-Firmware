/*
 * ESP32-S3 SHA-256 candidate scan loop.
 *
 * uint32_t s3_miner_scan_until_candidate_xtensa(
 *     const uint32_t* midstate_s3,
 *     const uint32_t* tail_s3,
 *     uint32_t nonce,
 *     uint32_t candidate_word0,
 *     uint32_t max_hashes);
 *
 * midstate_s3 and tail_s3 contain the values in the byte order expected by the
 * ESP32-S3 SHA H/TEXT registers.
 *
 * Return value:
 *   bits 0..30: hashes scanned
 *   bit 31: set when the last scanned hash meets candidate_word0
 */

  .section .iram1, "ax"
  .align 4

.Ls3_sha_h_base:
  .word 0x6003b040
.Ls3_sha_text_base:
  .word 0x6003b080
.Ls3_sha_cmd_base:
  .word 0x6003b010
.Ls3_first_len:
  .word 0x80020000
.Ls3_second_len:
  .word 0x00010000
.Ls3_candidate_found_flag:
  .word 0x80000000

  .align 4
  .global s3_miner_scan_until_candidate_xtensa
  .type s3_miner_scan_until_candidate_xtensa, @function

s3_miner_scan_until_candidate_xtensa:
  entry   a1, 32

  l32r    a7, .Ls3_sha_h_base
  l32r    a8, .Ls3_sha_text_base
  l32r    a9, .Ls3_sha_cmd_base
  movi.n  a10, 0
  movi.n  a11, 0
  movi.n  a12, 1

  l32i    a14, a3, 0
  l32i    a15, a3, 4
  l32i    a13, a3, 8
  s32i    a13, a1, 0

  beqz    a6, .Ls3_done

  /* Prime TEXT with the first nonce block. Offsets 36..56 stay zero for the
   * lifetime of the loop because neither SHA block writes non-zero values
   * there.
   */
  s32i    a14, a8, 0
  s32i    a15, a8, 4
  l32i    a13, a1, 0
  s32i    a13, a8, 8
  s32i    a4, a8, 12
  movi    a13, 128
  s32i    a13, a8, 16
  s32i    a11, a8, 20
  s32i    a11, a8, 24
  s32i    a11, a8, 28
  s32i    a11, a8, 32
  s32i    a11, a8, 36
  s32i    a11, a8, 40
  s32i    a11, a8, 44
  s32i    a11, a8, 48
  s32i    a11, a8, 52
  s32i    a11, a8, 56
  l32r    a13, .Ls3_first_len
  s32i    a13, a8, 60

.Ls3_scan_next:
  l32i    a13, a2, 0
  s32i    a13, a7, 0
  l32i    a13, a2, 4
  s32i    a13, a7, 4
  l32i    a13, a2, 8
  s32i    a13, a7, 8
  l32i    a13, a2, 12
  s32i    a13, a7, 12
  l32i    a13, a2, 16
  s32i    a13, a7, 16
  l32i    a13, a2, 20
  s32i    a13, a7, 20
  l32i    a13, a2, 24
  s32i    a13, a7, 24
  l32i    a13, a2, 28
  s32i    a13, a7, 28
  memw
  s32i    a12, a9, 4

  movi    a13, 128
  s32i    a13, a8, 32
  l32r    a13, .Ls3_second_len
  s32i    a13, a8, 60

  .rept 64
  nop
  .endr

.Ls3_wait_first:
  l32i    a13, a9, 8
  bnez    a13, .Ls3_wait_first

  l32i    a13, a7, 0
  s32i    a13, a8, 0
  l32i    a13, a7, 4
  s32i    a13, a8, 4
  l32i    a13, a7, 8
  s32i    a13, a8, 8
  l32i    a13, a7, 12
  s32i    a13, a8, 12
  l32i    a13, a7, 16
  s32i    a13, a8, 16
  l32i    a13, a7, 20
  s32i    a13, a8, 20
  l32i    a13, a7, 24
  s32i    a13, a8, 24
  l32i    a13, a7, 28
  s32i    a13, a8, 28
  memw
  s32i    a12, a9, 0

  s32i    a14, a8, 0
  s32i    a15, a8, 4
  l32i    a13, a1, 0
  s32i    a13, a8, 8
  addi    a13, a4, 1
  s32i    a13, a8, 12
  movi    a13, 128
  s32i    a13, a8, 16
  s32i    a11, a8, 20
  s32i    a11, a8, 24
  s32i    a11, a8, 28
  s32i    a11, a8, 32
  l32r    a13, .Ls3_first_len
  s32i    a13, a8, 60

.Ls3_wait_second:
  l32i    a13, a9, 8
  bnez    a13, .Ls3_wait_second

  l32i    a13, a7, 28
  addi.n  a10, a10, 1
  bltu    a5, a13, .Ls3_no_candidate

  l32r    a13, .Ls3_candidate_found_flag
  or      a2, a10, a13
  retw.n

.Ls3_no_candidate:
  addi.n  a4, a4, 1
  bltu    a10, a6, .Ls3_scan_next

.Ls3_done:
  mov.n   a2, a10
  retw.n
