/* Historical forecasts are loaded from the authenticated API, not synthesized here. */
let forecastRequest = 0;
async function renderForecast() {
  const request = ++forecastRequest;
  const container = document.querySelector("#forecast-content");
  const selector = document.querySelector("#forecast-scope");
  selector.onchange = renderForecast;
  container.innerHTML = '<p class="forecast-notice">正在读取预测结果…</p>';
  try {
    const d = await api(
      "/admin/console/forecast?mode=artifact&scope=" + encodeURIComponent(selector.value),
    );
    if (request !== forecastRequest) return;
    if (!d.ready) {
      container.textContent = d.message;
      return;
    }
    if (selector.options.length === 1) {
      selector.innerHTML = d.scopes
        .map((s) => `<option value="${esc(s.id)}">${esc(s.label)}</option>`)
        .join("");
      selector.value = d.scope;
    }
    const num = (n) =>
      Number(n).toLocaleString("zh-CN", { maximumFractionDigits: 1 });
    const selected = d.metrics.find((m) => m.selected);
    const peak = Math.max(...d.prediction),
      peakHour = d.prediction.indexOf(peak);
    const total = d.prediction.reduce((a, b) => a + b, 0);
    const max = Math.max(...d.history, ...d.upper, d.threshold) * 1.14;
    const x = (i) => 65 + (i / 71) * 960,
      y = (n) => 330 - (n / (max || 1)) * 285;
    const line = (values, start) =>
      values
        .map(
          (v, i) =>
            `${i ? "L" : "M"}${x(i + start).toFixed(2)},${y(v).toFixed(2)}`,
        )
        .join(" ");
    const upper = d.upper.map((v, i) => `${x(i + 48)},${y(v)}`);
    const lower = [...d.lower].reverse().map((v, i) => `${x(71 - i)},${y(v)}`);
    const grid = Array.from({ length: 5 }, (_, i) => {
      const v = (max * i) / 4;
      return `<line x1="65" y1="${y(v)}" x2="1025" y2="${y(v)}" stroke="#ffffff0d"/><text x="55" y="${y(v) + 4}" text-anchor="end">${num(v)}</text>`;
    }).join("");
    const labels = [0, 12, 24, 36, 48, 60, 71]
      .map((i) => {
        const date = i < 48 ? d.history_dates[i] : d.future_dates[i - 48];
        return `<text x="${x(i)}" y="355" text-anchor="middle">${date.slice(5, 10)} ${date.slice(11, 16)}</text>`;
      })
      .join("");
    container.innerHTML = `
      <div class="forecast-notice"><span>历史数据回放 · 非实时</span> 数据截止 ${esc(d.cutoff.replace("T", " "))} · 深圳本地时间 · 区域估算电量</div>
      <div class="forecast-kpis">
        <article class="card"><small>未来 24 小时预测电量</small><strong>${num(total)} <em>kWh</em></strong><p>${esc(d.future_dates[0].slice(0, 10))} · ${esc(d.label)}</p></article>
        <article class="card"><small>预测小时峰值</small><strong>${num(peak)} <em>kWh/小时</em></strong><p>${String(peakHour).padStart(2, "0")}:00 · ${d.high_hours} 小时超过历史高负荷阈值</p></article>
        <article class="card"><small>测试集加权绝对百分比误差</small><strong>${selected?.wape == null ? "—" : num(selected.wape)}<em>%</em></strong><p>按验证集选用 · ${esc(d.model)}</p></article>
        <article class="card"><small>经验范围 · 测试覆盖率</small><strong>${num(d.test_coverage)}<em>%</em></strong><p>验证集 90% 绝对误差分位数构建</p></article>
      </div>
      <div class="forecast-grid"><article class="card forecast-main"><div class="forecast-title"><div><small>24H / DEMAND OUTLOOK</small><h2>充电需求趋势</h2></div><span class="forecast-chip">${esc(d.model)}</span></div>
      <div class="forecast-legend"><span>● 最近 48 小时</span><span>● 未来 24 小时</span><span>▰ 经验误差范围</span><span>┄ 历史 95% 分位阈值</span></div>
      <svg class="forecast-chart" viewBox="0 0 1060 380" role="img" aria-label="最近48小时历史电量与未来24小时预测及经验误差范围"><defs><linearGradient id="forecast-fill" x1="0" x2="0" y1="0" y2="1"><stop stop-color="#df87fb" stop-opacity=".35"/><stop offset="1" stop-color="#bc78f5" stop-opacity=".02"/></linearGradient></defs>${grid}<rect x="${x(47.5)}" y="30" width="${1025 - x(47.5)}" height="300" fill="#ce82ff08"/><path d="${line(d.history, 0)} L${x(47)},330 L65,330 Z" fill="url(#forecast-fill)"/><polygon points="${upper.concat(lower).join(" ")}" fill="#c17bff33"/><line x1="65" x2="1025" y1="${y(d.threshold)}" y2="${y(d.threshold)}" stroke="#e6bc74" stroke-dasharray="5 6"/><path d="${line(d.history, 0)}" fill="none" stroke="#da97e3" stroke-width="2.5"/><path d="${line([d.history.at(-1), ...d.prediction], 47)}" fill="none" stroke="#8ff1d9" stroke-width="3"/><line x1="${x(47.5)}" x2="${x(47.5)}" y1="30" y2="330" stroke="#9885b0" stroke-dasharray="4 6"/><text x="${x(49)}" y="25">预测开始 →</text>${labels}</svg>
      <p class="forecast-footnote">纵轴：每小时估算充电电量（kWh/小时）。高负荷阈值来自本范围历史电量分布，不代表电网容量上限。</p></article>
      <article class="card forecast-side"><small>MODEL BENCHMARK</small><h2>模型效果对比</h2><p>统一的末尾 28 天测试集 · 误差越低越好</p>${d.metrics.map((m) => `<div class="forecast-model"><div><b>${esc(m.model)}</b>${m.selected ? "<span>已选用</span>" : ""}</div><strong>${m.wape === null ? "—" : num(m.wape)}<em>% WAPE</em></strong><div class="forecast-meter"><i style="width:${Math.min(100, Math.max(1, m.wape || 0))}%"></i></div><small>测试 MAE ${num(m.mae)} · RMSE ${num(m.rmse)} kWh<br>验证 MAE ${num(m.validation_mae)} kWh（选用依据）</small></div>`).join("")}</article></div>
      <div class="forecast-bottom"><article class="card"><h2>训练与评估时间线</h2><div class="forecast-timeline"><div><b>① 模型训练</b><p>${d.start.slice(0, 10)} — ${d.train_end.slice(0, 10)}</p></div><div><b>② 验证与选择</b><p>${d.validation_start.slice(0, 10)} — ${d.validation_end.slice(0, 10)}</p></div><div><b>③ 留出测试</b><p>${d.test_start.slice(0, 10)} — ${d.test_end.slice(0, 10)}</p></div></div><p>${esc(d.protocol)}</p></article><article class="card"><h2>数据与使用边界</h2><p>${num(d.rows)} 条记录 · ${d.regions} 个区域 · 数据库 UrbanEV</p><p>${esc(d.limitations)}</p></article></div>`;
  } catch (e) {
    if (request !== forecastRequest) return;
    container.innerHTML = `<p class="forecast-notice">预测加载失败：${esc(e.message)}</p><button class="outline" id="forecast-retry">重试</button>`;
    document.querySelector("#forecast-retry").onclick = renderForecast;
  }
}
