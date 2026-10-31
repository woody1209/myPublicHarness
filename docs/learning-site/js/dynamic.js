/* dynamic.js — 动态控制原理演示（浏览器端） */
(function () {
  "use strict";
  const Lab = window.MCLab;

  // ======== 公共工具 ========
  function $(id) { return document.getElementById(id); }
  function fmt(n, d) { return Number(n).toFixed(d === undefined ? 0 : d); }
  function clamp(v, a, b) { return Math.max(a, Math.min(b, v)); }

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

  // ======== 1. SVPWM ========
  const svpwmCv = $("svpwmCanvas");
  let svpwmPlaying = false, svpwmPhase = 0;

  function drawSvpwm() {
    const { ctx, w, h } = setupCanvas(svpwmCv);
    ctx.clearRect(0, 0, w, h);
    const ang = parseInt($("angSlider").value, 10);
    const mag = parseInt($("magSlider").value, 10);
    $("angVal").textContent = (ang / 10).toFixed(1) + "°";
    $("magVal").textContent = mag + "‰";

    const r = Lab.svpwmModulate(ang, mag);
    $("mAlpha").textContent = fmt(Lab.cosX10(ang) * mag / 1000, 1);
    $("mBeta").textContent = fmt(Lab.sinX10(ang) * mag / 1000, 1);
    $("mA").textContent = r.a;
    $("mB").textContent = r.b;
    $("mC").textContent = r.c;
    $("mAB").textContent = r.line ? r.line.ab.toFixed(1) : "0.0";
    $("mGuard").textContent = r.guard ? "触发" : "否";
    $("mGuard").style.color = r.guard ? "var(--red)" : "var(--green)";

    // 三块布局：矢量图 | 占空比柱 | 线电压
    const pad = 16;
    const blockW = (w - pad * 4) / 3;
    const blockH = h - 60;

    // --- 块1：矢量空间 ---
    const cx = pad + blockW / 2, cy = 30 + blockH / 2, R = Math.min(blockW, blockH) / 2 - 18;
    ctx.strokeStyle = "#888780"; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.arc(cx, cy, R, 0, Math.PI * 2); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(cx - R, cy); ctx.lineTo(cx + R, cy); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(cx, cy - R); ctx.lineTo(cx, cy + R); ctx.stroke();
    ctx.fillStyle = "#5F5E5A"; ctx.font = "11px sans-serif";
    ctx.textAlign = "center"; ctx.fillText("α", cx + R + 10, cy + 4);
    ctx.fillText("β", cx, cy - R - 8);

    const alpha = Lab.cosX10(ang) * mag / 1000;
    const beta = Lab.sinX10(ang) * mag / 1000;
    const vx = cx + (alpha / 1000) * R, vy = cy - (beta / 1000) * R;
    ctx.strokeStyle = "#378ADD"; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(vx, vy); ctx.stroke();
    ctx.fillStyle = "#378ADD";
    ctx.beginPath(); ctx.arc(vx, vy, 5, 0, Math.PI * 2); ctx.fill();
    ctx.fillStyle = "#185FA5"; ctx.font = "11px sans-serif";
    ctx.fillText("mag=" + mag + "‰", cx, cy + R + 18);

    // --- 块2：三相占空比柱 ---
    const barX = pad * 2 + blockW, barW = blockW;
    const barTop = 30, barH = blockH;
    ctx.fillStyle = "#5F5E5A"; ctx.font = "11px sans-serif"; ctx.textAlign = "left";
    ctx.fillText("三相占空比 (‰)", barX, barTop - 6);
    const labels = ["a", "b", "c"], vals = [r.a, r.b, r.c];
    const colors = ["#0F6E56", "#1D9E75", "#5DCAA5"];
    for (let i = 0; i < 3; i++) {
      const bx = barX + 10 + i * (barW - 20) / 3;
      const bw = (barW - 20) / 3 - 10;
      const bh = (vals[i] / 1000) * (barH - 30);
      ctx.fillStyle = "#F1EFE8"; ctx.fillRect(bx, barTop, bw, barH - 30);
      ctx.fillStyle = colors[i]; ctx.fillRect(bx, barTop + (barH - 30) - bh, bw, bh);
      ctx.fillStyle = "#2C2C2A"; ctx.font = "11px sans-serif"; ctx.textAlign = "center";
      ctx.fillText(labels[i], bx + bw / 2, barTop + barH + 2);
      ctx.fillText(vals[i], bx + bw / 2, barTop + (barH - 30) - bh - 4);
    }

    // --- 块3：线电压 ---
    const lvX = pad * 3 + blockW * 2, lvW = blockW;
    ctx.fillStyle = "#5F5E5A"; ctx.font = "11px sans-serif"; ctx.textAlign = "left";
    ctx.fillText("线电压 (‰)", lvX, barTop - 6);
    const lvMid = barTop + barH / 2, lvR = Math.min(lvW, barH) / 2 - 20;
    ctx.strokeStyle = "#D3D1C7"; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(lvX + 20, lvMid); ctx.lineTo(lvX + lvW - 20, lvMid); ctx.stroke();
    const lvs = r.line ? [r.line.ab, r.line.bc] : [0, 0];
    const lvColors = ["#BA7517", "#EF9F27"];
    for (let i = 0; i < 2; i++) {
      const bx = lvX + 30 + i * (lvW - 60) / 2, bw = (lvW - 60) / 2 - 12;
      const scale = (lvs[i] / 1000) * lvR;
      ctx.fillStyle = lvColors[i];
      if (scale >= 0) ctx.fillRect(bx, lvMid - scale, bw, scale);
      else ctx.fillRect(bx, lvMid, bw, -scale);
      ctx.fillStyle = "#2C2C2A"; ctx.font = "11px sans-serif"; ctx.textAlign = "center";
      ctx.fillText(i === 0 ? "a−b" : "b−c", bx + bw / 2, barTop + barH + 2);
      ctx.fillText(lvs[i].toFixed(0), bx + bw / 2, lvMid + (scale >= 0 ? -scale - 4 : -scale + 12));
    }
    ctx.fillStyle = "#5F5E5A"; ctx.font = "10px sans-serif"; ctx.textAlign = "center";
    ctx.fillText("定心只改共模，不改差模", lvX + lvW / 2, barTop + barH + 22);
  }

  $("angSlider").addEventListener("input", drawSvpwm);
  $("magSlider").addEventListener("input", drawSvpwm);
  $("svpwmPlay").addEventListener("click", function () {
    svpwmPlaying = !svpwmPlaying;
    this.textContent = svpwmPlaying ? "⏸ 暂停" : "▶ 自动旋转";
    if (svpwmPlaying) requestAnimationFrame(stepSvpwm);
  });
  $("svpwmZero").addEventListener("click", function () {
    $("magSlider").value = 0; drawSvpwm();
  });
  function stepSvpwm() {
    if (!svpwmPlaying) return;
    svpwmPhase = (svpwmPhase + 4) % 3600;
    $("angSlider").value = svpwmPhase;
    drawSvpwm();
    requestAnimationFrame(stepSvpwm);
  }

  // ======== 2. 载波 + PWM ========
  const carrierCv = $("carrierCanvas");
  function drawCarrier() {
    const { ctx, w, h } = setupCanvas(carrierCv);
    ctx.clearRect(0, 0, w, h);
    const da = parseInt($("pwma").value, 10);
    const db = parseInt($("pwmb").value, 10);
    const dc = parseInt($("pwmc").value, 10);
    const dbCycles = parseInt($("dbSlider").value, 10);
    $("pwmaVal").textContent = da + "%";
    $("pwmbVal").textContent = db + "%";
    $("pwmcVal").textContent = dc + "%";
    $("dbVal").textContent = dbCycles + " (" + (dbCycles / 120).toFixed(2) + "µs)";
    $("mDB").textContent = (dbCycles / 120).toFixed(2) + " µs";
    const shootRisk = dbCycles === 0 ? "是" : "否";
    $("mShoot").textContent = shootRisk;
    $("mShoot").style.color = dbCycles === 0 ? "var(--red)" : "var(--green)";

    const padL = 50, padR = 16, padT = 18, padB = 24;
    const plotW = w - padL - padR;
    const rows = 4;
    const rowH = (h - padT - padB) / rows;

    // 载波（第 0 行）
    const cy0 = padT, ch = rowH - 10;
    ctx.strokeStyle = "#D3D1C7"; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(padL, cy0 + ch); ctx.lineTo(padL + plotW, cy0 + ch); ctx.stroke();
    ctx.fillStyle = "#185FA5"; ctx.font = "11px sans-serif"; ctx.textAlign = "right";
    ctx.fillText("TBCTR", padL - 6, cy0 + ch / 2 + 4);
    // 三角载波
    ctx.strokeStyle = "#378ADD"; ctx.lineWidth = 2; ctx.beginPath();
    for (let x = 0; x <= plotW; x++) {
      const phase = (x / plotW) * 2; // 0..2 = 一个完整 up-down 周期
      const tri = phase < 1 ? phase : 2 - phase;
      const y = cy0 + ch - tri * ch;
      if (x === 0) ctx.moveTo(padL + x, y); else ctx.lineTo(padL + x, y);
    }
    ctx.stroke();
    ctx.fillStyle = "#5F5E5A"; ctx.font = "10px sans-serif"; ctx.textAlign = "left";
    ctx.fillText("10 kHz up-down (PRD=6000)", padL, cy0 + 10);

    // 三相 PWM 行
    const phases = [
      { d: da, name: "A / A̅", hi: "#0F6E56" },
      { d: db, name: "B / B̅", hi: "#BA7517" },
      { d: dc, name: "C / C̅", hi: "#D85A30" }
    ];
    for (let p = 0; p < 3; p++) {
      const y0 = padT + rowH * (p + 1), hh = rowH - 12;
      ctx.strokeStyle = "#D3D1C7"; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(padL, y0 + hh); ctx.lineTo(padL + plotW, y0 + hh); ctx.stroke();
      ctx.fillStyle = phases[p].hi; ctx.font = "11px sans-serif"; ctx.textAlign = "right";
      ctx.fillText(phases[p].name, padL - 6, y0 + hh / 2 + 4);

      const duty = phases[p].d / 100;
      const cmpX = padL + plotW * (1 - duty); // up-down: CMPA 对应位置
      const dbx = (dbCycles / 6000) * plotW; // 死区像素宽度

      // 上桥（A）：在 up 段 [0, cmpX] 为高（SET at ZRO, CLEAR at UP_CMPA）
      ctx.strokeStyle = phases[p].hi; ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(padL, y0 + hh); ctx.lineTo(padL, y0 + hh * 0.25);
      ctx.lineTo(cmpX - dbx, y0 + hh * 0.25);
      ctx.lineTo(cmpX - dbx, y0 + hh);
      ctx.stroke();
      // 下桥（A̅）互补 + 死区
      ctx.strokeStyle = "#AFA9EC"; ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.moveTo(padL, y0 + hh * 0.85); ctx.lineTo(cmpX + dbx, y0 + hh * 0.85);
      ctx.lineTo(cmpX + dbx, y0 + hh * 0.45); ctx.lineTo(padL + plotW, y0 + hh * 0.45);
      ctx.stroke();
      // 死区窗口标注
      if (dbx > 1) {
        ctx.fillStyle = "rgba(180,83,126,0.25)";
        ctx.fillRect(cmpX - dbx, y0, dbx * 2, hh);
      }
      ctx.fillStyle = "#5F5E5A"; ctx.font = "10px sans-serif"; ctx.textAlign = "left";
      ctx.fillText("duty=" + phases[p].d + "%", padL + 4, y0 + 12);
    }
  }

  ["pwma", "pwmb", "pwmc", "dbSlider"].forEach(function (id) {
    $(id).addEventListener("input", drawCarrier);
  });
  $("deadlockBtn").addEventListener("click", function () {
    $("pwma").value = 100; $("pwmb").value = 100; $("pwmc").value = 100;
    $("dbSlider").value = 0;
    drawCarrier();
  });

  // ======== 3. 电角度发生器 ========
  const angleCv = $("angleCanvas");
  let angState = Lab.makeAngleState(500);
  let angTimeUs = 0, angPlaying = false;

  function drawAngle() {
    const { ctx, w, h } = setupCanvas(angleCv);
    ctx.clearRect(0, 0, w, h);
    const rate = parseInt($("rateSlider").value, 10);
    const tickUs = parseInt($("tickSlider").value, 10);
    $("rateVal").textContent = rate + " (" + (rate / 100).toFixed(2) + " Hz)";
    $("tickVal").textContent = tickUs;

    $("mAng").textContent = angState.angleX10;
    $("mAngDeg").textContent = (angState.angleX10 / 10).toFixed(1);
    $("mAccum").textContent = angState.accum.toString();
    $("mTime").textContent = angTimeUs.toLocaleString();
    const revMs = 100000 / (rate / 100) / 1000;
    $("mRevTime").textContent = revMs.toFixed(1) + " ms";

    // 角度扇形 + 旋转箭头
    const pad = 20;
    const cx = pad + 90, cy = h / 2, R = Math.min(80, h / 2 - 15);
    ctx.strokeStyle = "#D3D1C7"; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.arc(cx, cy, R, 0, Math.PI * 2); ctx.stroke();
    const angRad = (angState.angleX10 / 10) * Math.PI / 180;
    ctx.strokeStyle = "#534AB7"; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.moveTo(cx, cy);
    ctx.lineTo(cx + R * Math.cos(angRad), cy - R * Math.sin(angRad)); ctx.stroke();
    ctx.fillStyle = "#534AB7";
    ctx.beginPath(); ctx.arc(cx + R * Math.cos(angRad), cy - R * Math.sin(angRad), 5, 0, Math.PI * 2); ctx.fill();
    ctx.fillStyle = "#5F5E5A"; ctx.font = "11px sans-serif"; ctx.textAlign = "center";
    ctx.fillText("θ = " + (angState.angleX10 / 10).toFixed(1) + "°", cx, cy + R + 18);

    // 累加器条
    const barX = cx + R + 40, barW = w - barX - pad;
    ctx.fillStyle = "#5F5E5A"; ctx.font = "11px sans-serif"; ctx.textAlign = "left";
    ctx.fillText("64-bit 累加器进度 (accum / 1e8)", barX, 24);
    const frac = Number(angState.accum) / 100000000;
    ctx.fillStyle = "#F1EFE8"; ctx.fillRect(barX, 34, barW, 18);
    ctx.fillStyle = "#7F77DD"; ctx.fillRect(barX, 34, barW * frac, 18);
    ctx.fillStyle = "#2C2C2A"; ctx.font = "11px sans-serif";
    ctx.fillText((frac * 100).toFixed(2) + " %", barX + barW / 2 - 16, 47);

    // tick 脉冲示意
    ctx.fillStyle = "#5F5E5A"; ctx.fillText("每次 tick_us 推进 → 累加 → 取整除 → 模 3600", barX, 80);
    const steps = Math.floor(frac * 8);
    for (let i = 0; i < 8; i++) {
      const bx = barX + i * (barW / 8);
      ctx.fillStyle = i < steps ? "#0F6E56" : "#E1F5EE";
      ctx.fillRect(bx, 92, barW / 8 - 6, 12);
    }
    ctx.fillStyle = "#5F5E5A"; ctx.font = "10px sans-serif";
    ctx.fillText("steps 分片", barX, 118);

    // 结合律提示
    ctx.fillStyle = "#0F6E56"; ctx.font = "11px sans-serif";
    ctx.fillText("✓ 结合律：1×100000 / 100×1000 / 1000×100 → 同 angle_x10", barX, 150);
    ctx.fillStyle = "#5F5E5A";
    ctx.fillText("rate 上限 50000 (500 Hz)；tick_us=0 或空指针原样返回", barX, 172);
  }

  $("rateSlider").addEventListener("input", function () {
    angState.rateX100 = parseInt(this.value, 10); drawAngle();
  });
  $("tickSlider").addEventListener("input", drawAngle);
  $("angPlay").addEventListener("click", function () {
    angPlaying = !angPlaying;
    this.textContent = angPlaying ? "⏸ 暂停" : "▶ 播放";
    if (angPlaying) stepAngle();
  });
  $("angReset").addEventListener("click", function () {
    angState = Lab.makeAngleState(parseInt($("rateSlider").value, 10));
    angTimeUs = 0; drawAngle();
  });
  function stepAngle() {
    if (!angPlaying) return;
    const tickUs = parseInt($("tickSlider").value, 10);
    // 每帧推进 20 步，模拟真实 tick
    for (let i = 0; i < 20; i++) {
      Lab.angleAdvance(angState, tickUs);
      angTimeUs += tickUs;
    }
    drawAngle();
    requestAnimationFrame(stepAngle);
  }

  // ======== 4. 保护状态机 ========
  const protectCv = $("protectCanvas");
  let fsmState = "RUN";
  let dwellMs = 0, dwellTimer = null;

  function drawProtect() {
    const { ctx, w, h } = setupCanvas(protectCv);
    ctx.clearRect(0, 0, w, h);
    const dac = parseInt($("dacSlider").value, 10);
    const a2 = parseInt($("a2Slider").value, 10);
    $("dacVal").textContent = dac;
    $("a2Val").textContent = a2;

    // COMPSTS: a2 > dac → trip
    const comp = a2 > dac ? 1 : 0;
    $("mComp").textContent = comp;
    $("mComp").style.color = comp ? "var(--red)" : "var(--green)";

    const tzFlg = fsmState === "TRIP" || fsmState === "DWELL" ? 1 : 0;
    $("mTzFlg").textContent = tzFlg;
    $("mTzFlg").style.color = tzFlg ? "var(--red)" : "var(--green)";
    $("mPwmState").textContent = tzFlg ? "全关 (LOW)" : "运行";
    $("mPwmState").style.color = tzFlg ? "var(--red)" : "var(--green)";
    $("mDwell").textContent = dwellMs + " ms";
    $("mFsm").textContent = fsmState;

    // 状态机节点
    const states = [
      { id: "RUN", x: 60, color: "#0F6E56", label: "RUN" },
      { id: "TRIP", x: 200, color: "#D85A30", label: "TRIP" },
      { id: "DWELL", x: 340, color: "#BA7517", label: "DWELL 5s" },
      { id: "RECOVER", x: 500, color: "#378ADD", label: "RECOVER" }
    ];
    const cy = h / 2, boxW = 110, boxH = 46;
    states.forEach(function (s, i) {
      const active = fsmState === s.id;
      ctx.fillStyle = active ? s.color : "#F1EFE8";
      ctx.strokeStyle = s.color; ctx.lineWidth = active ? 2 : 1;
      ctx.fillRect(s.x, cy - boxH / 2, boxW, boxH);
      ctx.strokeRect(s.x, cy - boxH / 2, boxW, boxH);
      ctx.fillStyle = active ? "#fff" : s.color;
      ctx.font = "13px sans-serif"; ctx.textAlign = "center";
      ctx.fillText(s.label, s.x + boxW / 2, cy + 5);
      if (i < states.length - 1) {
        ctx.strokeStyle = "#888780"; ctx.lineWidth = 1;
        ctx.beginPath(); ctx.moveTo(s.x + boxW, cy); ctx.lineTo(states[i + 1].x, cy); ctx.stroke();
        ctx.beginPath();
        ctx.moveTo(states[i + 1].x - 8, cy - 4); ctx.lineTo(states[i + 1].x, cy);
        ctx.lineTo(states[i + 1].x - 8, cy + 4); ctx.stroke();
      }
    });
    ctx.fillStyle = "#5F5E5A"; ctx.font = "11px sans-serif"; ctx.textAlign = "left";
    ctx.fillText("硬件 trip（零软件介入）", 60, cy - 50);
    ctx.fillText("软件 dwell + 清 TZ", 340, cy + 55);
  }

  $("dacSlider").addEventListener("input", drawProtect);
  $("a2Slider").addEventListener("input", drawProtect);
  $("injectBtn").addEventListener("click", function () {
    $("dacSlider").value = 0;
    fsmState = "TRIP"; dwellMs = 0;
    if (dwellTimer) clearInterval(dwellTimer);
    drawProtect();
  });
  $("recoverBtn").addEventListener("click", function () {
    $("dacSlider").value = 4095;
    $("a2Slider").value = 38;
    fsmState = "RUN"; dwellMs = 0;
    if (dwellTimer) clearInterval(dwellTimer);
    drawProtect();
  });

  // ======== 初始化 ========
  drawSvpwm();
  drawCarrier();
  drawAngle();
  drawProtect();
  window.addEventListener("resize", function () {
    drawSvpwm(); drawCarrier(); drawAngle(); drawProtect();
  });
})();
