//#############################################################################
//
// FILE:   math_ref.h
//
// TITLE:  Pure-C math references (host-testable mirrors of TMU workloads).
//
// DESCRIPTION:
//   The C28x TMU (spec 0002 REQ-005) executes sin/atan2 as single hardware
//   instructions that only exist on the target. To prove the TMU results are
//   RIGHT, the CPU compares them against these pure-C reference functions,
//   which compile and run on BOTH the host (unit tests vs libm) and the
//   target. Accuracy: relative error < 1e-3 over the documented domain -
//   deliberately not libm-grade, but far tighter than the 1e-4 relative band
//   the TMU judgement needs (TMU atan2/sin are ~1e-2 max abs in ulp terms,
//   the band absorbs both).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_MATH_REF_H
#define FULLPLATFORM_MATH_REF_H

#include <stdint.h>

//! Polynomial reference for sin(x), x in radians, any finite value
//! (range-reduced internally). |error| < 2e-4 vs libm.
float math_ref_sin_f32(float x);

//! Polynomial reference for atan2(y, x); both zero yields 0.0f.
//! Result in [-pi, pi]. |error| < 2e-4 rad vs libm.
float math_ref_atan2_f32(float y, float x);

//! Reference value for a fixed-point sin check (Q15 sine of
//! (index/256)*2pi, matching what the CLA/TMU demo tasks consume).
//! index wraps modulo 256.
int16_t math_ref_sin_q15(uint16_t index);

#endif // FULLPLATFORM_MATH_REF_H
