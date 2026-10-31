/* motor-control-lab — 纯逻辑核（与 firmware 同算法，浏览器端复现）
 * 契约对齐 specs/0003-motor-pwm-skeleton（design.md §3）
 */
(function (global) {
  "use strict";

  // 四分之一波 sin 表：0..90°，0.1° 步，int16 千分比（与 svpwm_table.h 同构）
  function buildSinTable() {
    const t = new Int16Array(901);
    for (let i = 0; i <= 900; i++) {
      t[i] = Math.round(Math.sin((i * Math.PI) / 1800) * 1000);
    }
    return t;
  }
  const SIN = buildSinTable();

  // 全波 sin(θ)，θ 以 0.1° 为单位 (0..3599)，象限反射
  function sinX10(a) {
    a = ((a % 3600) + 3600) % 3600;
    if (a <= 900) return SIN[a];
    if (a <= 1800) return SIN[1800 - a];
    if (a <= 2700) return -SIN[a - 1800];
    return -SIN[3600 - a];
  }
  function cosX10(a) {
    return sinX10(a + 900);
  }

  // SVPWM：min-max 定心，千分比 0..1000；mag=0 → 零矢量 500/500/500
  // 过调制守卫：max−min > 1000 时整体等比缩放
  function svpwmModulate(angleX10, magPermille) {
    if (magPermille === 0) return { a: 500, b: 500, c: 500, guard: false, line: null };
    const sa = sinX10(angleX10);
    const ca = cosX10(angleX10);
    // α = mag·cos(θ)/1000 ; β = mag·sin(θ)/1000
    const alpha = (magPermille * ca) / 1000;
    const beta = (magPermille * sa) / 1000;
    let ta = alpha;
    let tb = -0.5 * alpha + 0.8660254 * beta;
    let tc = -0.5 * alpha - 0.8660254 * beta;
    const mn = Math.min(ta, tb, tc);
    const mx = Math.max(ta, tb, tc);
    let da = ta - mn, db = tb - mn, dc = tc - mn;
    const span = mx - mn;
    let guard = false;
    if (span > 1000) {
      guard = true;
      const k = 1000 / span;
      da = da * k; db = db * k; dc = dc * k;
    }
    return {
      a: Math.round(da), b: Math.round(db), c: Math.round(dc),
      guard,
      line: { ab: Math.round((ta - tb) * 100) / 100, bc: Math.round((tb - tc) * 100) / 100 }
    };
  }

  // 开环电角度积分器（0.1° 分辨率，rate 单位 0.01 Hz）
  function angleAdvance(state, tickUs) {
    if (!state || !tickUs) return state ? state.angleX10 : 0;
    // accum += rate_x100 * tick_us * 3600 ; steps = accum / 1e8 ; accum %= 1e8
    state.accum += BigInt(state.rateX100) * BigInt(tickUs) * 3600n;
    const steps = Number(state.accum / 100000000n);
    state.accum = state.accum % 100000000n;
    state.angleX10 = (state.angleX10 + steps) % 3600;
    return state.angleX10;
  }

  function makeAngleState(rateX100) {
    return { angleX10: 0, rateX100: rateX100, accum: 0n };
  }

  // ---- MCU 寄存器模型（字地址，对齐 ADR 0002 + hw_epwm.h）----
  // CMPA:CMPAHR@字 0x6A/0x6B；TBPRD:TBPRDHR@字 0x62/0x63
  const REG = {
    EPWM1_BASE: 0x4000, EPWM2_BASE: 0x4080, EPWM3_BASE: 0x4100,
    O_TBPRD: 0x62, O_TBPRD_H: 0x63,
    O_CMPA: 0x6A, O_CMPA_H: 0x6B,
    O_DBRED: 0x0C, O_DBFED: 0x0E, O_DBCTL: 0x0B,
    O_TZSEL: 0x80, O_TZFLG: 0x93,
    CMPSS1_BASE: 0x5C80,
    O_COMPSTS: 0x2, O_DACHVALS: 0x6,
    GPADAT_R_LO: 0x7F80, GPADAT_R_HI: 0x7F81,
    ADDR_ADC_RES0: 0x0B00
  };

  function EPWMRegs() {
    // 每片 ePWM 的字寄存器映像
    this.m = new Map();
  }
  EPWMRegs.prototype.write = function (base, off, val) {
    this.m.set(base + off, val & 0xFFFF);
    return this;
  };
  EPWMRegs.prototype.read = function (base, off) {
    return this.m.has(base + off) ? this.m.get(base + off) : 0;
  };

  global.MCLab = {
    sinX10, cosX10, svpwmModulate, angleAdvance, makeAngleState,
    REG, EPWMRegs, SIN
  };
})(window);
