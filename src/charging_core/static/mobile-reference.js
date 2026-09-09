/* Reference screens use public UrbanEV history; live account and order APIs stay separate. */
const referenceApp = (() => {
  let miniature,
    snapshot,
    tariffMode = "all",
    period = "month";
  const shades = ["#e589dc", "#edcd79", "#8850d8"];
  const fmt = (n) =>
    Number(n).toLocaleString("zh-CN", { maximumFractionDigits: 1 });
  function onScreen(name) {
    if (name === "home" && miniature)
      requestAnimationFrame(() => miniature.invalidateSize());
    if (name === "charge" && !activeOrder) {
      $("#charge-percent").innerHTML = "36<sup>%</sup>";
      $("#charge-minutes").textContent = "23";
      $("#charge-energy").textContent = "13.4";
      $("#charge-cost").textContent = "18.00";
      $("#charge-station").textContent = "Charging · 充电效果预览";
      $("#charge-code").textContent = "连接站点后，显示当前订单数据";
      $("#stop-button").innerHTML = "<span></span>查找充电站";
    }
    if (name === "stats" && !snapshot) loadAnalytics();
    if (name === "schedule" && !snapshot) loadAnalytics();
  }
  function onStations() {
    if (!miniature) {
      miniature = L.map("home-map", {
        zoomControl: false,
        dragging: false,
        scrollWheelZoom: false,
        doubleClickZoom: false,
        touchZoom: false,
        attributionControl: true,
      }).setView(SHENZHEN, 12);
      addRoadMap(miniature);
    }
    const nearby = stations.slice(0, 6);
    for (const [index, s] of nearby.entries()) {
      const marker = L.marker([s.latitude, s.longitude], {
        icon: markerIcon(s, index === 0),
      }).addTo(miniature);
      marker.on("click", () => {
        selectStation(index, true);
        showScreen("map");
      });
    }
    miniature.setView(SHENZHEN, 12);
    requestAnimationFrame(() => miniature.invalidateSize());
  }
  function onConnector() {
    $("#selected-connector-label").textContent = selectedCharger
      ? selectedCharger.code
      : "暂无空闲充电枪";
  }
  function estimate() {
    const s = stations[activeStation];
    if (!s) return;
    const energy = (Number($("#charge-limit").value) - 40) * 0.6;
    $("#estimated-energy").textContent = energy.toFixed(1) + " kWh";
    $("#estimated-rate").textContent = "¥" + s.price.toFixed(2) + " / kWh";
    $("#estimated-total").textContent = "¥" + (energy * s.price).toFixed(2);
  }
  function onStation(s) {
    estimate();
    $("#detail-page-title").textContent = s.name;
  }
  function statistics() {
    if (!snapshot) return;
    const days =
      period === "month" ? snapshot.daily.slice(-31) : snapshot.daily;
    const total = days.reduce((a, d) => a + d.energy, 0);
    $("#stats-energy").textContent = fmt(total / 1000);
    $("#stats-energy").nextElementSibling.textContent = "累计充电 · MWh";
    $("#stats-cost").textContent = fmt(total / days.length / 1000);
    $("#stats-second-label").textContent = "日均充电 · MWh";
    $("#stats-period-label").textContent =
      period === "month" ? "最近 31 天" : "全部历史 · 181 天";
    $("#stats-source").textContent =
      `UrbanEV · 深圳全域历史观测 ${days[0].date} — ${days.at(-1).date}`;
    const subset = days.filter((_, i) => i % Math.ceil(days.length / 7) === 0),
      max = Math.max(...subset.map((x) => x.high)) * 1.12;
    let body = "";
    for (let i = 0; i < 3; i++) {
      const y = 15 + i * 59;
      body += `<path d="M0 ${y}H273" stroke="#7d618d" opacity=".18"/><text x="277" y="${y + 3}" fill="#b8a9c7" font-size="8">${Math.round((max * (1 - i / 2)) / 1000)}</text>`;
    }
    subset.forEach((d, i) => {
      const x = 6 + i * 38;
      [d.open, d.high, d.close].forEach(
        (v, j) =>
          (body += `<rect x="${x + j * 6}" y="${133 - (v / max) * 118}" width="3" height="${(v / max) * 118}" rx="1" fill="${shades[j]}"/>`),
      );
      body += `<text x="${x + 7}" y="153" text-anchor="middle" fill="#c0a8d2" font-size="8">${d.date.slice(5)}</text>`;
    });
    $("#stats-chart").innerHTML =
      `<svg viewBox="0 0 303 165" preserveAspectRatio="none" aria-label="每日首小时、峰值和末小时充电量，MWh">${body}</svg>`;
    $("#stats-legend").innerHTML = ["首小时", "峰值", "末小时"]
      .map(
        (title, i) =>
          `<span><i style="--color:${shades[i]}">ϟ</i><b>${fmt(subset.reduce((a, d) => a + [d.open, d.high, d.close][i], 0) / subset.length / 1000)}</b><small>${title} · MWh</small></span>`,
      )
      .join("");
    const rank = snapshot.ranking.slice(0, 3),
      maxRank = rank[0].energy;
    $("#cost-breakdown").innerHTML = rank
      .map(
        (r, i) =>
          `<div class="cost-line"><div><span>区域 ${r.zone}</span><b>${fmt(r.energy)}</b></div><i><b style="width:${(r.energy / maxRank) * 100}%;--color:${shades[i]}"></b></i></div>`,
      )
      .join("");
  }
  function tariffs() {
    if (!snapshot) return;
    const hours = snapshot.hourly,
      prices = hours.map((h) => h.price),
      low = Math.min(...prices),
      high = Math.max(...prices);
    const tier = (p) =>
      p < low + (high - low) / 3
        ? "low"
        : p > low + (2 * (high - low)) / 3
          ? "high"
          : "mid";
    const color = (p) =>
      ({ low: "#62b57c", mid: "#e8c15e", high: "#ce635a" })[tier(p)];
    const rows = hours.filter(
      (h) => tariffMode === "all" || tier(h.price) === tariffMode,
    );
    $("#tariff-list").innerHTML = rows
      .map(
        (h) =>
          `<div class="tariff-row"><i style="--color:${color(h.price)}"></i><div><b>${String(h.hour).padStart(2, "0")}:00 — ${String(h.hour + 1).padStart(2, "0")}:00</b><small>${{ low: "低价时段", mid: "平价时段", high: "高价时段" }[tier(h.price)]} · 历史均值</small></div><em>¥${h.price.toFixed(2)}</em></div>`,
      )
      .join("");
    $("#time-rail").innerHTML = rows
      .map(
        (h) =>
          `<span style="--color:${color(h.price)}">${h.hour % 6 === 0 ? h.hour : "·"}</span>`,
      )
      .join("");
  }
  async function loadAnalytics() {
    try {
      const session = await fetch("/auth/demo", { method: "POST" });
      if (!session.ok) throw Error("演示数据未启用");
      const auth = await session.json(),
        response = await fetch("/demo/analytics", {
          headers: { Authorization: `Bearer ${auth.access_token}` },
        });
      if (!response.ok) throw Error("历史数据暂不可用");
      const data = await response.json();
      if (!data.ready) throw Error("历史数据尚未导入");
      snapshot = data;
      statistics();
      tariffs();
    } catch (e) {
      $("#stats-source").textContent = e.message;
      $("#tariff-list").innerHTML =
        '<p class="empty-state">暂时无法读取分时数据，请稍后重试</p>';
    }
  }
  $$(".stats-period button").forEach(
    (b) =>
      (b.onclick = () => {
        period = b.dataset.period;
        $$(".stats-period button").forEach((x) =>
          x.classList.toggle("active", x === b),
        );
        statistics();
      }),
  );
  $$(".schedule-period button").forEach(
    (b) =>
      (b.onclick = () => {
        tariffMode = b.dataset.tariff;
        $$(".schedule-period button").forEach((x) =>
          x.classList.toggle("active", x === b),
        );
        tariffs();
      }),
  );
  $$(".charge-mode button").forEach(
    (b) =>
      (b.onclick = () => {
        $$(".charge-mode button").forEach((x) =>
          x.classList.toggle("active", x === b),
        );
        const kind = b.dataset.mode === "fast" ? "fast" : "all";
        $$(".detail-screen [data-kind]").forEach((x) =>
          x.classList.toggle("active", x.dataset.kind === kind),
        );
        renderChargers(kind);
      }),
  );
  $("#notifications").onclick = () =>
    toast(activeOrder ? "当前充电订单正在进行" : "暂无新的充电通知");
  $("#charge-limit").onchange = () => estimate();
  const tick = () =>
    ($("#app-clock").textContent = new Date().toLocaleTimeString("en-GB", {
      hour: "2-digit",
      minute: "2-digit",
    }));
  tick();
  setInterval(tick, 60000);
  const initial = new URLSearchParams(location.search).get("screen");
  if (["stats", "schedule", "charge"].includes(initial))
    queueMicrotask(() => showScreen(initial));
  loadAnalytics();
  return { onScreen, onStations, onStation, onConnector };
})();

if (stations.length) {
  referenceApp.onStations();
  referenceApp.onStation(stations[activeStation]);
}
