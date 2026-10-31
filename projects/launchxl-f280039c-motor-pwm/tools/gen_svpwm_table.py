#!/usr/bin/env python3
# gen_svpwm_table.py - regenerate source/svpwm_table.h
#
# T[q] = round(1000 * sin(q * 0.1 deg)), q = 0..900 (quarter wave inclusive).
# The full-wave sine is derived in svpwm.c by quadrant reflection.
#
# Usage:  python tools/gen_svpwm_table.py > source/svpwm_table.h
# Contract: specs/0002-motor-pwm-skeleton/design.md (SVPWM section).

import math
import sys

QUARTER = 900          # 90.0 degrees in 0.1-degree steps
SCALE = 1000           # per-mille sine


def main() -> int:
    vals = []
    for q in range(QUARTER + 1):
        v = int(round(SCALE * math.sin(math.radians(q * 0.1))))
        vals.append(max(-SCALE, min(SCALE, v)))
    if vals[0] != 0 or vals[QUARTER] != SCALE:
        sys.stderr.write("table endpoints wrong: %d, %d\n" % (vals[0], vals[QUARTER]))
        return 1

    out = []
    out.append("//#############################################################################")
    out.append("//")
    out.append("// FILE:   svpwm_table.h")
    out.append("//")
    out.append("// TITLE:  Quarter-wave sine table for svpwm.c (GENERATED - do not hand-edit).")
    out.append("//")
    out.append("// DESCRIPTION:")
    out.append("//   T[q] = round(1000 * sin(q * 0.1 degrees)), q = 0..900 (quarter wave,")
    out.append("//   inclusive). 901 entries x 2 bytes = 1802 bytes of .const. Full-wave")
    out.append("//   sine is derived by quadrant reflection, so cos(x) = sin(x + 90 deg).")
    out.append("//   Regenerate with tools/gen_svpwm_table.py (spec 0002 T-003).")
    out.append("//")
    out.append("// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.")
    out.append("//")
    out.append("//#############################################################################")
    out.append("")
    out.append("#ifndef MOTORPWM_SVPWM_TABLE_H")
    out.append("#define MOTORPWM_SVPWM_TABLE_H")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("#define SVPWM_TABLE_QUARTER_STEPS   900U")
    out.append("#define SVPWM_TABLE_FULL_STEPS      3600U")
    out.append("")
    out.append("static const int16_t svpwm_sin_q[SVPWM_TABLE_QUARTER_STEPS + 1U] = {")
    for i in range(0, QUARTER + 1, 10):
        chunk = ", ".join("%d" % v for v in vals[i:i + 10])
        out.append("    " + chunk + ("," if i + 10 <= QUARTER else ""))
    out.append("};")
    out.append("")
    out.append("#endif // MOTORPWM_SVPWM_TABLE_H")
    sys.stdout.write("\n".join(out) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
