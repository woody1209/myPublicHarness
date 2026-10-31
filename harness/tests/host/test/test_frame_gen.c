//#############################################################################
//
// FILE:   test_frame_gen.c
//
// TITLE:  Host unit tests for the frame_gen module (spec 0002 T-015).
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include "frame_gen.h"

void frame_gen_tests(void)
{
    uint16_t buf[FRAME_GEN_WORDS];
    uint16_t buf2[FRAME_GEN_WORDS];

    fw_test_suite_begin("frame_gen");

    fw_test_case_begin("lfsr16 determinism");
    FW_ASSERT_EQ_U32(0x899BU, (uint32_t)frame_gen_lfsr16(0x1337U));
    FW_ASSERT_EQ_U32((uint32_t)frame_gen_lfsr16(0x1337U),
                     (uint32_t)frame_gen_lfsr16(0x1337U));

    fw_test_case_begin("fill produces the documented reference frame");
    frame_gen_fill(buf, FRAME_GEN_SEED_1);
    FW_ASSERT_EQ_U32(0x899BU, (uint32_t)buf[0]);
    FW_ASSERT_EQ_U32(0xF913U, (uint32_t)buf[FRAME_GEN_WORDS - 1U]);

    fw_test_case_begin("verify accepts the pattern and rejects corruption");
    FW_ASSERT_TRUE(frame_gen_verify(buf, FRAME_GEN_SEED_1));
    buf[3] ^= 0x1U;
    FW_ASSERT_FALSE(frame_gen_verify(buf, FRAME_GEN_SEED_1));

    fw_test_case_begin("different seed differs, same seed matches");
    frame_gen_fill(buf, FRAME_GEN_SEED_1);
    frame_gen_fill(buf2, FRAME_GEN_SEED_2);
    FW_ASSERT_TRUE(frame_gen_verify(buf, FRAME_GEN_SEED_1));
    FW_ASSERT_TRUE(frame_gen_verify(buf2, FRAME_GEN_SEED_2));
    FW_ASSERT_FALSE(frame_gen_verify(buf2, FRAME_GEN_SEED_1));

    fw_test_case_begin("FNV-1a reference constant (no two truths)");
    frame_gen_fill(buf, FRAME_GEN_SEED_1);
    FW_ASSERT_EQ_U32(FRAME_GEN_REF_FNV1A, frame_gen_fnv1a(buf));

    fw_test_case_begin("NULL safety");
    frame_gen_fill(NULL, 1U);
    FW_ASSERT_FALSE(frame_gen_verify(NULL, 1U));
    FW_ASSERT_EQ_U32(0U, frame_gen_fnv1a(NULL));
}
