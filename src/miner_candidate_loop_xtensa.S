/*
 * Clean-room ESP32 SHA-256 candidate scan loop.
 *
 * uint32_t miner_scan_until_candidate_xtensa(
 *     const uint32_t* block0,
 *     const uint32_t* block1,
 *     uint32_t swapped_nonce,
 *     uint32_t raw_zero_mask);
 *
 * Return value:
 *   bits 0..30: hashes scanned
 *   bit 31:     set when the last scanned hash meets raw_zero_mask
 */

  .section .iram1, "ax"
  .align 4

.macro DPORT_SAFE_READ out, base, offset
  rsil    a15, 5
  l32i    \out, a5, 0
  l32i    \out, \base, \offset
  wsr     a15, ps
  rsync
.endm

.macro DPORT_SEQUENCE_READ8 out, base, offset
  l32i    \out, a5, 0
  l8ui    \out, \base, \offset
.endm

.Lsha_text_base:
  .word 0x3ff03000
.Lsha_cmd_base:
  .word 0x3ff03090
.Lapb_ctrl:
  .word 0x3ff40078
.Lnonce_step0:
  .word 0x01000000
.Lnonce_step1:
  .word 0x00010000
.Lsha_pad:
  .word 0x80000000
.Lfound_bit:
  .word 0x80000000

  .align 4
  .global miner_scan_until_candidate_xtensa
  .type miner_scan_until_candidate_xtensa, @function

miner_scan_until_candidate_xtensa:
  entry   a1, 16

  extui   a14, a5, 8, 8
  l32r    a6, .Lsha_text_base
  l32r    a7, .Lsha_cmd_base
  l32r    a5, .Lapb_ctrl
  movi.n  a8, 0
  movi.n  a9, 1
  movi.n  a10, 0
  l32r    a12, .Lnonce_step0
  l32r    a13, .Lsha_pad

.Lscan_next:
  l32i    a11, a2, 0
  s32i    a11, a6, 0
  l32i    a11, a2, 4
  s32i    a11, a6, 4
  l32i    a11, a2, 8
  s32i    a11, a6, 8
  l32i    a11, a2, 12
  s32i    a11, a6, 12
  l32i    a11, a2, 16
  s32i    a11, a6, 16
  l32i    a11, a2, 20
  s32i    a11, a6, 20
  l32i    a11, a2, 24
  s32i    a11, a6, 24
  l32i    a11, a2, 28
  s32i    a11, a6, 28
  l32i    a11, a2, 32
  s32i    a11, a6, 32
  l32i    a11, a2, 36
  s32i    a11, a6, 36
  l32i    a11, a2, 40
  s32i    a11, a6, 40
  l32i    a11, a2, 44
  s32i    a11, a6, 44
  l32i    a11, a2, 48
  s32i    a11, a6, 48
  l32i    a11, a2, 52
  s32i    a11, a6, 52
  l32i    a11, a2, 56
  s32i    a11, a6, 56
  l32i    a11, a2, 60
  s32i    a11, a6, 60
  memw
  s32i.n  a9, a7, 0

  l32i    a11, a3, 0
  s32i    a11, a6, 0
  l32i    a11, a3, 4
  s32i    a11, a6, 4
  l32i    a11, a3, 8
  s32i    a11, a6, 8
  s32i    a4, a6, 12
  s32i    a13, a6, 16
  s32i    a8, a6, 20
  s32i    a8, a6, 24
  s32i    a8, a6, 28
  s32i    a8, a6, 32
  s32i    a8, a6, 36
  s32i    a8, a6, 40
  s32i    a8, a6, 44
  s32i    a8, a6, 48
  s32i    a8, a6, 52
  s32i    a8, a6, 56
  movi    a11, 0x280
  s32i    a11, a6, 60
  memw

.Lwait_start:
  DPORT_SAFE_READ a11, a7, 12
  bnez.n  a11, .Lwait_start
  s32i.n  a9, a7, 4
  memw
  add     a4, a4, a12
  extui   a11, a4, 24, 8
  bnez    a11, .Lwait_continue
  l32r    a11, .Lnonce_step1
  add     a4, a4, a11
  extui   a11, a4, 16, 8
  bnez    a11, .Lwait_continue
  movi    a11, 0x100
  add     a4, a4, a11
  extui   a11, a4, 8, 8
  bnez    a11, .Lwait_continue
  addi.n  a4, a4, 1

.Lwait_continue:
  DPORT_SAFE_READ a11, a7, 12
  bnez.n  a11, .Lwait_continue
  s32i.n  a9, a7, 8
  memw

.Lwait_load_first:
  DPORT_SAFE_READ a11, a7, 12
  bnez.n  a11, .Lwait_load_first

  s32i.n  a13, a6, 32
  movi    a11, 0x100
  s32i    a11, a6, 60
  memw
  s32i.n  a9, a7, 0
  memw

.Lwait_second:
  DPORT_SAFE_READ a11, a7, 12
  bnez.n  a11, .Lwait_second
  s32i.n  a9, a7, 8
  memw

.Lwait_load_second:
  DPORT_SAFE_READ a11, a7, 12
  bnez.n  a11, .Lwait_load_second

  addi.n  a10, a10, 1
  DPORT_SEQUENCE_READ8 a11, a6, 28
  bnez.n  a11, .Lscan_next
  DPORT_SEQUENCE_READ8 a11, a6, 29
  and     a11, a11, a14
  beqz.n  a11, .Ldone_found

  j       .Lscan_next

.Ldone_found:
  l32r    a11, .Lfound_bit
  or      a2, a10, a11
  retw.n
