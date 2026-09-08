const $ = (s, r = document) => r.querySelector(s),
  $$ = (s, r = document) => [...r.querySelectorAll(s)];
let stationRows = [],
  toastTimer;
const escapeHtml = (value) =>
  String(value ?? "").replace(
    /[&<>'\"]/g,
    (c) =>
      ({
        "&": "&amp;",
        "<": "&lt;",
        ">": "&gt;",
        "'": "&#39;",
        '\"': "&quot;",
      })[c],
  );
function toast(message) {
  const el = $("#toast");
  el.textContent = message;
  el.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.remove("show"), 2200);
}
function fmt(value) {
  return Number(value || 0).toLocaleString("zh-CN", {
    maximumFractionDigits: 2,
  });
}
function updateClock() {
  const now = new Date();
  $("#clock").textContent = now.toLocaleTimeString("zh-CN", { hour12: false });
}

function statusText(s) {
  return (
    {
      available: "运行正常",
      charging: "充电中",
      faulted: "存在故障",
      completed: "已完成",
      cancelled: "已取消",
      reserved: "已预约",
    }[s] || "运行正常"
  );
}
function renderStations(rows = stationRows) {
  stationRows = rows;
  const q = ($("#station-search")?.value || "").trim();
  const data = rows.filter(
    (x) => !q || x.name.includes(q) || x.address.includes(q),
  );
  $("#stations-table").innerHTML =
    `<div class="table-row header"><span>站点</span><span>地址</span><span>电桩</span><span>空闲</span><span>电价</span><span>来源</span></div>` +
    data
      .map(
        (x) =>
          `<button class="table-row" data-table-station="${escapeHtml(x.id)}"><b>${escapeHtml(x.name)}</b><span>${escapeHtml(x.address)}</span><span>${x.charger_count} 枪</span><span>${x.available_count ?? 0} 枪</span><span>¥${fmt(x.unit_price)}/kWh</span><em class="status-pill">${escapeHtml(x.source_dataset || "平台")}</em></button>`,
      )
      .join("");
  if (!data.length)
    $("#stations-table").innerHTML +=
      '<p class="empty-state">没有匹配的站点</p>';
  detailViews.tableUpdated(data);
}
function renderOrders(rows = []) {
  $("#orders-table").innerHTML =
    `<div class="table-row header"><span>订单</span><span>用户 / 站点</span><span>充电枪</span><span>金额</span><span>状态</span><span>更新时间</span></div>` +
    (rows.length
      ? rows
          .map(
            (x) =>
              `<div class="table-row"><b>${String(x.id).slice(0, 14)}</b><span>${escapeHtml(x.phone || "当前用户")}<small style="display:block;color:#656174;margin-top:3px">${escapeHtml(x.station_name)}</small></span><span>${escapeHtml(x.charger_code)}</span><span>¥${fmt(x.amount)}</span><em class="status-pill ${x.status === "charging" ? "busy" : x.status === "cancelled" ? "faulted" : ""}">${statusText(x.status)}</em><span>${x.ended_at || x.started_at || x.reserved_at ? new Date(x.ended_at || x.started_at || x.reserved_at).toLocaleString("zh-CN", { month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", hour12: false }) : "--"}</span></div>`,
          )
          .join("")
      : '<p class="empty-state">登录管理员后可查看订单明细</p>');
}
function renderBars(values) {
  const max = Math.max(...values, 1);
  $("#order-bars").innerHTML = values
    .map(
      (v, i) =>
        `<span style="--h:${Math.max(4, (Number(v) / max) * 96)}%" data-label="${String(i).padStart(2, "0")}:00" title="占用率 ${Number(v).toFixed(1)}%"></span>`,
    )
    .join("");
}
async function publicApi(path) {
  const response = await fetch(path);
  if (!response.ok) throw new Error("API error");
  return response.json();
}
async function adminApi(path) {
  const token = localStorage.getItem("charging_admin_token");
  if (!token) return [];
  const response = await fetch(path, {
    headers: { Authorization: `Bearer ${token}` },
  });
  if (response.status === 401 || response.status === 403) {
    localStorage.removeItem("charging_admin_token");
    return [];
  }
  if (!response.ok) throw new Error("API error");
  return response.json();
}
const colors = ["#10e9ed", "#247de7", "#8050ef", "#c235e7", "#5c4aa2"];
let allStations = [],
  hourly = [];
const percent = (n, total) => (total ? (Number(n) / total) * 100 : 0);
const compact = (n) =>
  Number(n || 0).toLocaleString("en-US", { maximumFractionDigits: 0 });
function renderSummary(summary) {
  $$("[data-kpi]").forEach(
    (el) => (el.textContent = fmt(summary[el.dataset.kpi])),
  );
  const available = percent(summary.available, summary.chargers),
    charging = percent(summary.charging, summary.chargers);
  $("#hero-utilization").textContent = charging.toFixed(1) + "%";
  $("#globe-available").textContent = compact(summary.available);
  $("#available-share").textContent = available.toFixed(1) + "%";
  $("#charging-share").textContent = charging.toFixed(1) + "%";
  $("#available-ring").style.background =
    `conic-gradient(#0fdaed 0 ${available}%,#26316b ${available}% 100%)`;
  $("#charging-ring").style.background =
    `conic-gradient(#cd35df 0 ${charging}%,#45267d ${charging}% 100%)`;
  renderCompact("#platform-bars", [
    ["站点", summary.stations],
    ["设备", summary.chargers],
    ["用户", summary.users],
  ]);
}
function renderCompact(target, rows) {
  const max = Math.max(...rows.map((x) => Number(x[1])), 1);
  $(target).innerHTML = rows
    .map(
      ([name, value]) =>
        `<div class="compact-bar"><span title="${escapeHtml(name)}">${escapeHtml(name)}</span><i><b style="--w:${(Number(value) / max) * 100}%"></b></i><strong>${compact(value)}</strong></div>`,
    )
    .join("");
}
function renderDeviceDistribution(stations) {
  const sorted = [...stations].sort(
    (a, b) => b.charger_count - a.charger_count,
  );
  const total = stations.reduce((sum, s) => sum + s.charger_count, 0);
  const groups = sorted.slice(0, 4).map((s) => [s.name, s.charger_count]);
  groups.push([
    "其他站点",
    sorted.slice(4).reduce((sum, s) => sum + s.charger_count, 0),
  ]);
  let offset = 0;
  $("#device-pie").style.background = `conic-gradient(${groups
    .map(([name, n], i) => {
      const end = offset + percent(n, total),
        piece = `${colors[i]} ${offset}% ${end}%`;
      offset = end;
      return piece;
    })
    .join(",")})`;
  $("#device-legend").innerHTML = groups
    .map(
      ([name, n], i) =>
        `<div class="legend-item" style="--color:${colors[i]}"><i></i><span>${escapeHtml(name)}</span><strong>${percent(n, total).toFixed(1)}%</strong></div>`,
    )
    .join("");
}
function positionBeacon() {
  const img = $(".globe-scene>img"),
    rect = img.getBoundingClientRect(),
    scale = Math.min(rect.width / 940, rect.height / 940);
  if (!rect.width || !rect.height) return;
  const pin = $(".globe-location");
  pin.style.left = (rect.width - 940 * scale) / 2 + 550.92 * scale + "px";
  pin.style.top = (rect.height - 940 * scale) / 2 + 575.94 * scale + "px";
}
$(".globe-scene>img").addEventListener("load", positionBeacon);
addEventListener("resize", positionBeacon);
requestAnimationFrame(positionBeacon);
function renderScale(stations) {
  const buckets = [
    ["≥ 20 枪", stations.filter((s) => s.charger_count >= 20).length],
    [
      "10–19 枪",
      stations.filter((s) => s.charger_count >= 10 && s.charger_count < 20)
        .length,
    ],
    [
      "5–9 枪",
      stations.filter((s) => s.charger_count >= 5 && s.charger_count < 10)
        .length,
    ],
    ["< 5 枪", stations.filter((s) => s.charger_count < 5).length],
  ];
  $("#scale-rings").innerHTML = buckets
    .map(([name, n], i) => {
      const r = 62 - i * 11,
        circ = 2 * Math.PI * r;
      return `<circle cx="75" cy="75" r="${r}" stroke="#3a2767" stroke-width="3"/><circle cx="75" cy="75" r="${r}" stroke="${colors[i]}" stroke-width="3" stroke-dasharray="${(circ * n) / Math.max(stations.length, 1)} ${circ}" transform="rotate(-90 75 75)"/>`;
    })
    .join("");
  $("#scale-legend").innerHTML = buckets
    .map(
      ([name, n], i) =>
        `<div class="legend-item" style="--color:${colors[i]}"><i></i><span>${name}</span><strong>${percent(n, stations.length).toFixed(1)}%</strong></div>`,
    )
    .join("");
}
function selectSpotlight(index) {
  const s = allStations[index];
  if (!s) return;
  $("#spotlight-rate").textContent =
    percent(s.available_count, s.charger_count).toFixed(1) + "%";
  $("#spotlight-name").textContent = s.name;
  $("#spotlight-detail").textContent =
    `${s.available_count} / ${s.charger_count} 把空闲 · ¥${fmt(s.unit_price)}/kWh`;
  $$("[data-rank-station]").forEach((el) =>
    el.classList.toggle("selected", Number(el.dataset.rankStation) === index),
  );
  $("#station-spotlight").onclick = () => {
    openView("stations");
    $("#station-search").value = s.name;
    renderStations();
  };
}
function renderRankings(stations, profile) {
  allStations = [...stations].sort((a, b) => b.charger_count - a.charger_count);
  const top = allStations.slice(0, 5),
    total = stations.reduce((sum, s) => sum + s.charger_count, 0);
  $("#capacity-ranking").innerHTML = top
    .map(
      (s, i) =>
        `<button class="line-rank ${i === 0 ? "selected" : ""}" data-rank-station="${i}"><span>· NO.${i + 1} ${escapeHtml(s.name)}</span><b>${percent(s.charger_count, total).toFixed(1)}%</b></button>`,
    )
    .join("");
  $$("[data-rank-station]").forEach(
    (el) =>
      (el.onclick = () => selectSpotlight(Number(el.dataset.rankStation))),
  );
  selectSpotlight(0);
  renderBottom(
    "#station-ranking",
    top.map((s) => [s.name, s.charger_count]),
  );
  renderBottom(
    "#hour-ranking",
    [...profile]
      .sort((a, b) => Number(b.energy_kwh) - Number(a.energy_kwh))
      .slice(0, 5)
      .map((h) => [
        String(h.hour).padStart(2, "0") + ":00",
        Number(h.energy_kwh),
      ]),
  );
  renderCompact(
    "#city-bars",
    top.map((s) => [s.name, s.charger_count]),
  );
}
function renderBottom(target, rows) {
  const max = Math.max(...rows.map((x) => x[1]), 1);
  $(target).innerHTML = rows
    .map(
      ([name, n], i) =>
        `<div class="bottom-rank" style="--w:${(n / max) * 94}%"><span>NO.${i + 1} ${escapeHtml(name)}</span><b>${compact(n)}</b><i></i></div>`,
    )
    .join("");
}
function renderLoad(profile) {
  const h = new Date().getHours(),
    current = profile.find((x) => Number(x.hour) === h) || profile[0];
  if (!current) return;
  const value = Number(current.occupancy_rate);
  $("#current-load").textContent = value.toFixed(1) + "%";
  $("#load-hour").textContent =
    String(current.hour).padStart(2, "0") + ":00 占用率";
  $("#load-ring").style.background =
    `conic-gradient(#15d8ed 0 ${value}%,#603ee5 ${value}% 85%,#b12ce3 85% 100%)`;
  const average =
    profile.reduce((sum, x) => sum + Number(x.occupancy_rate), 0) /
    profile.length;
  $("#load-labels").innerHTML =
    `<span><b>${Math.max(...profile.map((x) => Number(x.occupancy_rate))).toFixed(1)}%</b>峰值占用</span><span><b>${average.toFixed(1)}%</b>全天平均</span><span><b>${Math.min(...profile.map((x) => Number(x.occupancy_rate))).toFixed(1)}%</b>谷值占用</span>`;
  $("#hero-energy").textContent = compact(
    profile.reduce((sum, h) => sum + Number(h.energy_kwh), 0),
  );
}
function renderTrend(profile) {
  const svg = $("#revenue-chart"),
    w = 900,
    h = 190,
    left = 39,
    bottom = 164,
    top = 8,
    max = Math.max(...profile.map((p) => Number(p.energy_kwh)), 1) * 1.12;
  const pts = profile.map((p, i) => ({
    x: left + (i * (w - left - 8)) / Math.max(profile.length - 1, 1),
    y: bottom - (Number(p.energy_kwh) / max) * (bottom - top),
  }));
  let d = "";
  pts.forEach((p, i) => {
    if (!i) d = `M${p.x},${p.y}`;
    else {
      const q = pts[i - 1],
        mid = (q.x + p.x) / 2;
      d += ` C${mid},${q.y} ${mid},${p.y} ${p.x},${p.y}`;
    }
  });
  const grid = Array.from({ length: 6 }, (_, i) => {
    const y = top + (i * (bottom - top)) / 5;
    return `<line class="grid-line" x1="${left}" x2="${w}" y1="${y}" y2="${y}"/><text class="axis-text" x="0" y="${y + 3}">${compact(max * (1 - i / 5))}</text>`;
  }).join("");
  svg.innerHTML = `<defs><linearGradient id="energyFill" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#9385ff" stop-opacity=".85"/><stop offset=".6" stop-color="#5644c5" stop-opacity=".4"/><stop offset="1" stop-color="#272450" stop-opacity=".06"/></linearGradient></defs>${grid}${pts
    .filter((_, i) => i % 3 === 0)
    .map(
      (p, i) =>
        `<line class="grid-line" x1="${p.x}" x2="${p.x}" y1="${top}" y2="${bottom}"/><text class="axis-text" x="${p.x}" y="183" text-anchor="middle">${String(profile[i * 3].hour).padStart(2, "0")}:00</text>`,
    )
    .join(
      "",
    )}<path d="${d} L${pts.at(-1)?.x || w},${bottom} L${left},${bottom} Z" fill="url(#energyFill)"/><path d="${d}" stroke="#7581ef" stroke-width="1.5" fill="none"/><path d="M${left},${bottom}H${w}" stroke="#30cce4" stroke-width=".8"/>${pts.map((p, i) => `<rect data-trend="${i}" x="${p.x - 17}" y="0" width="34" height="${bottom}" fill="transparent"><title>${String(profile[i].hour).padStart(2, "0")}:00 · ${compact(profile[i].energy_kwh)} kWh</title></rect>`).join("")}`;
  svg.onpointermove = (e) => {
    const rect = svg.getBoundingClientRect(),
      i = Math.max(
        0,
        Math.min(
          profile.length - 1,
          Math.round(
            ((((e.clientX - rect.left) / rect.width) * w - left) /
              (w - left - 8)) *
              (profile.length - 1),
          ),
        ),
      );
    const p = profile[i];
    if (!p) return;
    const tip = $("#trend-tooltip");
    tip.textContent = `${String(p.hour).padStart(2, "0")}:00 · ${compact(p.energy_kwh)} kWh`;
    tip.style.display = "block";
    tip.style.left =
      Math.max(65, Math.min(rect.width - 65, e.clientX - rect.left)) + "px";
    tip.style.top = "40%";
  };
  svg.onpointerleave = () => ($("#trend-tooltip").style.display = "none");
}
async function refresh() {
  try {
    const [summary, stations, analysis, orders, revenue] = await Promise.all([
      publicApi("/public/stats/summary"),
      publicApi("/public/stations"),
      publicApi("/public/analytics/urbanev"),
      adminApi("/admin/orders?limit=20"),
      publicApi("/public/stats/revenue?days=7"),
    ]);
    hourly = analysis.hourly_profile || [];
    renderSummary(summary);
    renderStations(stations);
    renderOrders(orders);
    renderScale(stations);
    renderDeviceDistribution(stations);
    renderRankings(stations, hourly);
    renderLoad(hourly);
    renderTrend(hourly);
    renderBars(hourly.map((h) => Number(h.occupancy_rate)));
    detailViews.render({ summary, stations, revenue, orders });
    $("#mode-label").textContent = "实时同步";
    $("#update-time").textContent =
      "更新于 " + new Date().toLocaleTimeString("zh-CN", { hour12: false });
    if (typeof historicalScreen !== "undefined") historicalScreen.sync();
  } catch (e) {
    console.error("Dashboard refresh failed", e);
    $("#mode-label").textContent = "连接中断";
    toast("暂时无法同步运营数据，请点击刷新");
  }
}
function openView(view) {
  if (view === "overview") requestAnimationFrame(positionBeacon);
  $$(".screen-nav button").forEach((x) =>
    x.classList.toggle("active", x.dataset.view === view),
  );
  $$(".view-panel").forEach((x) =>
    x.classList.toggle("active", x.id === `view-${view}`),
  );
}
function particles() {
  const canvas = $("#particles"),
    ctx = canvas.getContext("2d");
  function draw() {
    const dpr = Math.min(devicePixelRatio, 2);
    canvas.width = innerWidth * dpr;
    canvas.height = innerHeight * dpr;
    ctx.scale(dpr, dpr);
    let seed = 341;
    const rnd = () => {
      seed = (seed * 16807) % 2147483647;
      return seed / 2147483647;
    };
    for (let i = 0; i < 210; i++) {
      const x = rnd() * innerWidth,
        y = rnd() * innerHeight,
        r = rnd();
      ctx.fillStyle = `rgba(180,152,252,${r * 0.65})`;
      ctx.beginPath();
      ctx.arc(x, y, r * 0.9, 0, Math.PI * 2);
      ctx.fill();
    }
  }
  addEventListener("resize", draw);
  draw();
}
$$(".screen-nav button").forEach(
  (x) => (x.onclick = () => openView(x.dataset.view)),
);
$("#globe-focus").onclick = () => openView("stations");
$("#station-search").oninput = () => renderStations();
$("#add-station").onclick = () => toast("新增站点请通过管理员接口提交");
$("#refresh").onclick = refresh;
$("#fullscreen").onclick = () =>
  document.fullscreenElement
    ? document.exitFullscreen()
    : document.documentElement.requestFullscreen();
$("#admin-connect").onclick = () => $("#admin-dialog").showModal();
$("[data-close]").onclick = () => $("#admin-dialog").close();
$("#admin-login").onclick = async () => {
  try {
    const response = await fetch("/auth/admin/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        username: $("#admin-name").value.trim(),
        password: $("#admin-password").value,
      }),
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data?.error?.message || "登录失败");
    localStorage.setItem("charging_admin_token", data.access_token);
    $("#admin-dialog").close();
    refresh();
  } catch (e) {
    toast(e.message);
  }
};
$("#admin-disconnect").onclick = () => {
  localStorage.removeItem("charging_admin_token");
  $("#admin-dialog").close();
  refresh();
};
$("#dashboard-date").textContent =
  new Date().toLocaleDateString("zh-CN").replaceAll("/", "-") + " · SHENZHEN";
detailViews.bind();
setInterval(updateClock, 1000);
updateClock();
particles();
refresh();
setInterval(refresh, 30000);
const initialView = new URLSearchParams(location.search).get("view");
if (["stations", "orders"].includes(initialView)) openView(initialView);
