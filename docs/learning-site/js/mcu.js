/* mcu.js — MCU 资源控制时序演示 */
(function () {
  "use strict";
  const Lab = window.MCLab;

  function $(id) { return document.getElementById(id); }
  function fmt(n, d) { return Number(n).toFixed(d === undefined ? 0 : d); }

  function setupCanvas(cv) {
    const dpr = window.devicePixelRatio || 1;
    const rect = cv.getBoundingClientRect();
    const w = rect.width, h = parseInt(cv.getAttribute("height"), 10) || 300;
    cv.width = w * dpr; cv.height = h * dpr;
    cv.style.height = h + "px";
    const ctx = cv.getContext("2d");
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    return { ctx, w, h };
  }

  // ======== 1. 启动顺序 ========
  const bootCv = $("bootCanvas");
  let bootStep = 0, bootPlaying = false;

  function drawBoot() {
    const { ctx, w, h } = setupCanvas(bootCv);
    ctx.clearRect(0, 0, w, h);
    const steps = [
      "ramfunc", "看门狗", "PIE", "时钟", "外设", "自检"
    ];
    const colors = ["#378ADD", "#D85A30", "#BA7517", "#7F77DD", "#0F6E56", "#0F6E56"];
    const padL = 30, padR = 30, padT = 30, padB = 30;
    const boxW = (w - padL - padR) / steps.length - 12;
    const boxH = 44;
    const y = padT + 10;

    ctx.fillStyle = "#5F5E5A"; ctx.font = "11px sans-serif"; ctx.textAlign = "left";
    ctx.fillText("启动序列（硬顺序，不可调换）", padL, 18);

    steps.forEach(function (s, i) {
      const x = padL + i * ((w - padL - padR) / steps.length);
      const active = i === bootStep;
      const done = i < bootStep;
      ctx.fillStyle = active ? colors[i] : done ? "#E1F5EE" : "#F1EFE8";
      ctx.strokeStyle = colors[i]; ctx.lineWidth = active ? 2 : 1;
      ctx.fillRect(x, y, boxW, boxH); ctx.strokeRect(x, y, boxW, boxH);
      ctx.fillStyle = active ? "#fff" : done ? colors[i] : "#5F5E5A";
      ctx.font = "12px sans-serif"; ctx.textAlign = "center";
      ctx.fillText((i + 1) + ". " + s, x + boxW / 2, y + boxH / 2 + 4);

      if (i < steps.length - 1) {
        ctx.strokeStyle = "#888780"; ctx.lineWidth = 1;
        ctx.beginPath(); ctx.moveTo(x + boxW, y + boxH / 2);
        ctx.lineTo(x + boxW + 12, y + boxH / 2); ctx.stroke();
        ctx.beginPath();
        ctx.moveTo(x + boxW + 6, y + boxH / 2 - 4); ctx.lineTo(x + boxW + 12, y + boxH / 2);
        ctx.lineTo(x + boxW + 6, y + boxH / 2 + 4); ctx.stroke();
      }
    });

    // 详细条
    const detailY = y + boxH + 24;
    const details = [
      "memcpy(ramfunc) → RAMLS0 0x8000，SysCtl_delay 编入此段",
      "WDCR.WDDIS=1，防 ITRAP 后看门狗复位",
      "PIECTRL.ENPIE=1，向量表→PIE RAM，IER=0",
      "SysCtl_setClock → 120 MHz SYSCLK（必须在 ramfunc 之后）",
      "PCLKCRn 全开：ePWM1/2/3 + ADCA + SCIA + CMPSS1",
      "pwm 回读 → CMPSS 双向 → 武装 TZ → superloop"
    ];
    ctx.fillStyle = "#2C2C2A"; ctx.font = "11px sans-serif"; ctx.textAlign = "left";
    if (bootStep < details.length) {
      ctx.fillStyle = "#5F5E5A";
      ctx.fillText("当前步：" + details[bootStep], padL, detailY);
    } else {
      ctx.fillStyle = "#0F6E56";
      ctx.fillText("启动完成，进入 superloop", padL, detailY);
    }
  }

  function updateBootSteps() {
    const cards = document.querySelectorAll("#bootSteps .card-soft");
    cards.forEach(function (c, i) {
      const step = parseInt(c.getAttribute("data-step"), 10);
      if (step < bootStep) {
        c.style.background = "var(--green-soft)";
        c.style.borderColor = "var(--green)";
      } else if (step === bootStep) {
        c.style.background = "var(--blue-soft)";
        c.style.borderColor = "var(--blue)";
      } else {
        c.style.background = "var(--bg)";
        c.style.borderColor = "var(--line)";
      }
    });
    $("bootStatus").textContent = bootStep >= 6 ? "✓ 启动完成" : "第 " + (bootStep + 1) + " 步 / 6";
  }

  $("bootPlay").addEventListener("click", function () {
    if (bootPlaying) return;
    bootPlaying = true; bootStep = 0;
    this.disabled = true;
    const btn = this;
    const timer = setInterval(function () {
      bootStep++;
      drawBoot(); updateBootSteps();
      if (bootStep >= 6) {
        clearInterval(timer);
        bootPlaying = false;
        btn.disabled = false;
      }
    }, 700);
    drawBoot(); updateBootSteps();
  });
  $("bootReset").addEventListener("click", function () {
    bootStep = 0; drawBoot(); updateBootSteps();
    $("bootPlay").disabled = false; bootPlaying = false;
  });

  // ======== 3. 寄存器映像模拟器 ========
  const REG = Lab.REG;
  const regs = new Lab.EPWMRegs();
  // 预置：TBPRD=6000, DBRED=120, DBFED=120, DBCTL=0x000B, TZSEL=0x8000
  regs.write(REG.EPWM1_BASE, REG.O_TBPRD_H, 6000);
  regs.write(REG.EPWM1_BASE, REG.O_TBPRD, 0);
  regs.write(REG.EPWM1_BASE, REG.O_DBRED, 120);
  regs.write(REG.EPWM1_BASE, REG.O_DBFED, 120);
  regs.write(REG.EPWM1_BASE, REG.O_DBCTL, 0x000B);
  regs.write(REG.EPWM1_BASE, REG.O_TZSEL, 0x8000);
  regs.write(REG.EPWM1_BASE, REG.O_CMPA_H, 3000);
  regs.write(REG.EPWM1_BASE, REG.O_CMPA, 0);

  function renderRegGrid() {
    const items = [
      [REG.O_TBPRD_H, "TBPRD_H (0x63)", "有效周期"],
      [REG.O_TBPRD, "TBPRD (0x62)", "TBPRDHR"],
      [REG.O_CMPA_H, "CMPA_H (0x6B)", "有效占空比"],
      [REG.O_CMPA, "CMPA (0x6A)", "CMPAHR"],
      [REG.O_DBRED, "DBRED (0x0C)", "死区上升沿"],
      [REG.O_DBFED, "DBFED (0x0E)", "死区下降沿"],
      [REG.O_DBCTL, "DBCTL (0x0B)", "极性/模式"],
      [REG.O_TZSEL, "TZSEL (0x80)", "Trip 选择"]
    ];
    let html = "";
    items.forEach(function (it) {
      const val = regs.read(REG.EPWM1_BASE, it[0]);
      const isHigh = it[0] === REG.O_CMPA_H || it[0] === REG.O_TBPRD_H;
      html += '<div class="reg-cell" style="' + (isHigh ? "border-color:#B4537E;" : "") + '">' +
        '<div class="addr">0x' + (REG.EPWM1_BASE + it[0]).toString(16).toUpperCase() + '</div>' +
        '<div class="name">' + it[1] + ' · ' + it[2] + '</div>' +
        '<div class="val" style="color:' + (isHigh ? "var(--purple)" : "var(--ink)") + '">0x' +
        val.toString(16).toUpperCase().padStart(4, "0") + ' (' + val + ')</div></div>';
    });
    $("regGrid").innerHTML = html;
  }

  function drawRegCanvas() {
    const cv = $("regCanvas");
    const { ctx, w, h } = setupCanvas(cv);
    ctx.clearRect(0, 0, w, h);
    const padL = 10, padR = 10, padT = 24, padB = 24;
    const wordW = (w - padL - padR) / 2 - 10;
    const wordH = 80;

    const words = [
      { addr: "0x6A", name: "CMPA (低字) — CMPAHR", val: regs.read(REG.EPWM1_BASE, REG.O_CMPA), x: padL },
      { addr: "0x6B", name: "CMPA (高字) — 有效值", val: regs.read(REG.EPWM1_BASE, REG.O_CMPA_H), x: padL + wordW + 20 }
    ];

    words.forEach(function (word) {
      const x = word.x, y = padT;
      ctx.fillStyle = "#5F5E5A"; ctx.font = "11px sans-serif"; ctx.textAlign = "left";
      ctx.fillText(word.addr + "  " + word.name, x, y - 6);
      // 16 个位
      for (let b = 0; b < 16; b++) {
        const bw = wordW / 16;
        const bit = (word.val >> (15 - b)) & 1;
        ctx.fillStyle = bit ? "#0F6E56" : "#F1EFE8";
        ctx.fillRect(x + b * bw, y, bw - 1, wordH);
        ctx.strokeStyle = "#D3D1C7"; ctx.lineWidth = 0.5;
        ctx.strokeRect(x + b * bw, y, bw - 1, wordH);
        if (b % 4 === 0) {
          ctx.fillStyle = "#5F5E5A"; ctx.font = "9px sans-serif"; ctx.textAlign = "center";
          ctx.fillText((15 - b).toString(), x + b * bw + bw / 2, y + wordH + 12);
        }
      }
    });

    // 高字高亮
    const hx = padL + wordW + 20;
    ctx.strokeStyle = "#B4537E"; ctx.lineWidth = 2;
    ctx.strokeRect(hx - 3, padT - 3, wordW + 6, wordH + 6);

    ctx.fillStyle = "#8B2E2E"; ctx.font = "11px sans-serif"; ctx.textAlign = "center";
    ctx.fillText("读 0x6A 得 0x0000（CMPAHR 恒 0）→ 假 FAIL", w / 2, padT + wordH + 32);
    ctx.fillStyle = "#0F6E56";
    ctx.fillText("读 0x6B 得有效占空比（driverlib 写此字）", w / 2, padT + wordH + 50);
  }

  $("regDuty").addEventListener("input", function () {
    $("regDutyVal").textContent = this.value;
    const permille = parseInt(this.value, 10);
    const cmp = Math.round(permille * 6000 / 1000);
    regs.write(REG.EPWM1_BASE, REG.O_CMPA_H, cmp);
    regs.write(REG.EPWM1_BASE, REG.O_CMPA, 0);
    renderRegGrid(); drawRegCanvas();
    $("regLogText").textContent = "占空比 " + permille + "‰ → CMPA=" + cmp + " (写 0x6B)";
    $("regLogText").style.color = "var(--green)";
  });

  $("regWrite").addEventListener("click", function () {
    const permille = parseInt($("regDuty").value, 10);
    const cmp = Math.round(permille * 6000 / 1000);
    // driverlib: EPWM_setCounterCompareValue writes HWREGH(base + O_CMPA + 1) = 0x6B
    regs.write(REG.EPWM1_BASE, REG.O_CMPA_H, cmp);
    renderRegGrid(); drawRegCanvas();
    $("regLogText").innerHTML = "driverlib EPWM_setCounterCompareValue(" + cmp + ") → 写 <b>0x6B</b> = 0x" +
      cmp.toString(16).toUpperCase().padStart(4, "0") + "（高字）";
    $("regLogText").style.color = "var(--green)";
  });

  $("regRead").addEventListener("click", function () {
    const val = regs.read(REG.EPWM1_BASE, REG.O_CMPA);
    $("regLogText").innerHTML = "读 <b>0x6A</b> (低字) = 0x" + val.toString(16).toUpperCase().padStart(4, "0") +
      " = " + val + "  → <b style='color:var(--red)'>CMPAHR 恒 0，假 FAIL 根因</b>";
    $("regLogText").style.color = "var(--red)";
  });

  $("regReadHi").addEventListener("click", function () {
    const val = regs.read(REG.EPWM1_BASE, REG.O_CMPA_H);
    $("regLogText").innerHTML = "读 <b>0x6B</b> (高字) = 0x" + val.toString(16).toUpperCase().padStart(4, "0") +
      " = " + val + "  → <b style='color:var(--green)'>有效占空比</b>";
    $("regLogText").style.color = "var(--green)";
  });

  // ======== 4. 总线时序 ========
  const busCv = $("busCanvas");
  function drawBus() {
    const { ctx, w, h } = setupCanvas(busCv);
    ctx.clearRect(0, 0, w, h);
    const padL = 60, padR = 20, padT = 24, padB = 24;
    const plotW = w - padL - padR;
    const rows = 5;
    const rowH = (h - padT - padB) / rows;

    const signals = [
      { name: "TBCTR", color: "#378ADD", y: 0 },
      { name: "SOCA", color: "#0F6E56", y: 1 },
      { name: "ADC SOC0", color: "#BA7517", y: 2 },
      { name: "CMPSS", color: "#D85A30", y: 3 },
      { name: "TZ trip", color: "#8B2E2E", y: 4 }
    ];

    signals.forEach(function (sig) {
      const y0 = padT + rowH * sig.y, hh = rowH - 12;
      ctx.strokeStyle = "#D3D1C7"; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(padL, y0 + hh); ctx.lineTo(padL + plotW, y0 + hh); ctx.stroke();
      ctx.fillStyle = sig.color; ctx.font = "11px sans-serif"; ctx.textAlign = "right";
      ctx.fillText(sig.name, padL - 6, y0 + hh / 2 + 4);
    });

    // TBCTR 三角波
    const tbctrY = padT + rowH * 0, tbctrH = rowH - 12;
    ctx.strokeStyle = "#378ADD"; ctx.lineWidth = 2; ctx.beginPath();
    for (let x = 0; x <= plotW; x++) {
      const phase = (x / plotW) * 2;
      const tri = phase < 1 ? phase : 2 - phase;
      const y = tbctrY + tbctrH - tri * tbctrH;
      if (x === 0) ctx.moveTo(padL + x, y); else ctx.lineTo(padL + x, y);
    }
    ctx.stroke();

    // SOCA 脉冲（谷底 = x=plotW 处 + 起点）
    const socaY = padT + rowH * 1, socaH = rowH - 12;
    ctx.strokeStyle = "#0F6E56"; ctx.lineWidth = 2;
    [0, plotW].forEach(function (px) {
      ctx.beginPath();
      ctx.moveTo(padL + px, socaY + socaH); ctx.lineTo(padL + px, socaY + socaH * 0.2);
      ctx.stroke();
    });

    // ADC 转换（SOCA 后一段）
    const adcY = padT + rowH * 2, adcH = rowH - 12;
    ctx.fillStyle = "#BA7517"; ctx.globalAlpha = 0.4;
    ctx.fillRect(padL, adcY + adcH * 0.3, plotW * 0.15, adcH * 0.5);
    ctx.globalAlpha = 1;
    ctx.fillStyle = "#BA7517"; ctx.font = "10px sans-serif"; ctx.textAlign = "left";
    ctx.fillText("转换窗", padL + 4, adcY + 10);

    // CMPSS 异步（横线）
    const cmpssY = padT + rowH * 3, cmpssH = rowH - 12;
    ctx.strokeStyle = "#D85A30"; ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(padL, cmpssY + cmpssH * 0.4); ctx.lineTo(padL + plotW, cmpssY + cmpssH * 0.4);
    ctx.stroke();
    ctx.fillStyle = "#D85A30"; ctx.font = "10px sans-serif";
    ctx.fillText("异步连续比较", padL + 4, cmpssY + 10);

    // TZ trip
    const tzY = padT + rowH * 4, tzH = rowH - 12;
    ctx.strokeStyle = "#8B2E2E"; ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(padL, tzY + tzH); ctx.lineTo(padL + plotW * 0.65, tzY + tzH);
    ctx.lineTo(padL + plotW * 0.65, tzY + tzH * 0.15); ctx.lineTo(padL + plotW, tzY + tzH * 0.15);
    ctx.stroke();
    ctx.fillStyle = "#8B2E2E"; ctx.font = "10px sans-serif";
    ctx.fillText("trip 触发点（一次性锁存）", padL + plotW * 0.65 + 6, tzY + 10);
    ctx.fillText("→ 六路 PWM 全关", padL + plotW * 0.65 + 6, tzY + 26);

    // 时间标注
    ctx.fillStyle = "#5F5E5A"; ctx.font = "11px sans-serif"; ctx.textAlign = "center";
    ctx.fillText("10 kHz 载波周期 = 100 µs", padL + plotW / 2, h - 6);
  }

  // ======== 初始化 ========
  drawBoot(); updateBootSteps();
  renderRegGrid(); drawRegCanvas();
  drawBus();
  window.addEventListener("resize", function () {
    drawBoot(); renderRegGrid(); drawRegCanvas(); drawBus();
  });
})();
