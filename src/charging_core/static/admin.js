"use strict";
const $ = (s, r = document) => r.querySelector(s),
  $$ = (s, r = document) => [...r.querySelectorAll(s)];
const esc = (v) =>
  String(v ?? "").replace(
    /[&<>"']/g,
    (c) =>
      ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[
        c
      ],
  );
// 仅翻译展示值；数据字段、接口参数和状态类名保持原值。
const adminFieldLabels = {
  id: "记录编号",
  user_id: "用户编号",
  charger_id: "电桩编号",
  station_id: "站点编号",
  admin_id: "管理员编号",
  phone: "手机号",
  nickname: "昵称",
  avatar_path: "头像地址",
  balance: "账户余额",
  balance_after: "变动后余额",
  status: "状态",
  created_at: "创建时间",
  updated_at: "更新时间",
  reserved_at: "预约时间",
  reserved_until: "预约截止",
  started_at: "开始时间",
  ended_at: "结束时间",
  energy_kwh: "电量（kWh）",
  amount: "金额（元）",
  unit_price: "单价（元/kWh）",
  power_kw: "功率（kW）",
  time_scale: "计时倍率",
  stop_reason: "结束原因",
  station_name: "站点名称",
  charger_code: "电桩编号",
  code: "设备编号",
  name: "名称",
  address: "地址",
  longitude: "经度",
  latitude: "纬度",
  kind: "充电类型",
  total_sessions: "累计订单",
  charger_count: "电桩数量",
  online_count: "在线数量",
  entry_type: "账目类型",
  idempotency_key: "幂等编号",
  order_id: "订单编号",
  request_key: "请求编号",
  action: "操作名称",
  target_type: "对象类型",
  target_id: "对象编号",
  detail: "详细信息",
  reason: "操作原因",
  from: "原状态",
  to: "新状态",
  entry_id: "账目编号",
  vehicle: "车型",
  charge_limit: "充电上限",
  daily_goal: "每日目标",
  weekly_goal: "每周目标",
  source_dataset: "数据来源",
  source_station_id: "来源站点编号",
  zone_id: "区域编号",
  role: "角色",
  active: "启用状态",
};
const adminValueLabels = {
  available: "空闲",
  reserved: "已预约",
  charging: "充电中",
  completed: "已完成",
  cancelled: "已取消",
  faulted: "故障",
  active: "正常",
  frozen: "已冻结",
  fast: "快充",
  slow: "慢充",
  recharge: "充值",
  charge: "充电扣款",
  adjustment: "余额调整",
  refund: "退款",
  user: "用户",
  admin: "管理员",
  demo_admin: "只读演示管理员",
  station: "站点",
  charger: "电桩",
  order: "订单",
  wallet: "钱包",
  settings: "运营设置",
  console: "管理后台",
  user_stopped: "用户结束",
  balance_exhausted: "余额耗尽",
  reservation_expired: "预约过期",
  user_cancelled: "用户取消",
};
const adminVehicleLabels = {
  "Tesla M-3": "特斯拉 Model 3",
  "MG-4 Luxury": "名爵 MG4 豪华版",
  "BYD Seal": "比亚迪海豹",
};
function zhField(key) {
  return adminFieldLabels[key] || key;
}
function zhValue(value, key = "") {
  if (value === null || value === undefined) return "—";
  if (key === "detail" && typeof value === "string") {
    try {
      return zhValue(JSON.parse(value));
    } catch {
      return value;
    }
  }
  if (typeof value === "object")
    return JSON.stringify(
      Object.fromEntries(
        Object.entries(value).map(([k, v]) => [zhField(k), zhValue(v, k)]),
      ),
    );
  if (typeof value === "boolean") return value ? "是" : "否";
  if (key === "action") return activityHeading(String(value));
  if (key === "vehicle") return adminVehicleLabels[value] || value;
  if (
    [
      "status",
      "entry_type",
      "target_type",
      "role",
      "kind",
      "stop_reason",
      "from",
      "to",
    ].includes(key)
  )
    return adminValueLabels[value] || value;
  return value;
}
const icons = {
  grid: "M3 3h6v6H3z M15 3h6v6h-6z M3 15h6v6H3z M15 15h6v6h-6z",
  car: "M4 10l2-6h12l2 6 M3 10h18v8H3z M5 18v3 M19 18v3 M6 13h2 M16 13h2",
  plug: "M4 3h11v18H4z M7 6h5v5H7z M16 7l3 3v7a2 2 0 004 0v-5l-3-3 M9 13l-2 3h4l-2 3",
  history:
    "M7 3h11a3 3 0 013 3v12a3 3 0 01-3 3H7a3 3 0 01-3-3V6a3 3 0 013-3 M8 7h9 M8 11h5 M12 15v3h3",
  search: "M10 3a7 7 0 100 14 7 7 0 000-14 M15 15l6 6",
  bell: "M6 9a6 6 0 0112 0v6l2 3H4l2-3z M10 21h4",
  settings:
    "M12 8a4 4 0 100 8 4 4 0 000-8 M9 2h6l1 3 3 1 3 3v6l-3 1-1 3-3 3H9l-1-3-3-1-3-3V9l3-1 1-3z",
  bolt: "M12 2L4 13h6l-1 9 11-13h-7l2-7z",
  target: "M18 5a9 9 0 10 3 7 M15 8a5 5 0 10 2 4 M12 12l8-8 M17 3h4v4",
  sliders: "M4 6h16 M4 12h16 M4 18h16 M8 3v6 M16 9v6 M10 15v6",
  chart: "M3 3v18h18 M6 15l5-5 4 3 6-8 M6 15v3 M11 10v8 M16 13v5",
  users:
    "M9 3a4 4 0 100 8 4 4 0 000-8 M2 21v-3a7 7 0 0114 0v3 M17 3a4 4 0 010 8 M20 21v-3a7 7 0 00-3-6",
};
function paintIcons(root = document) {
  $$("[data-icon]", root).forEach(
    (el) =>
      (el.innerHTML = `<svg class="icon" viewBox="0 0 24 24" aria-hidden="true"><path d="${icons[el.dataset.icon] || icons.grid}"/></svg>`),
  );
}
const state = {
  token: null,
  stations: [],
  chargers: [],
  users: [],
  orders: [],
  logs: [],
  summary: {},
  preferences: {
    daily_goal: 72,
    weekly_goal: 18,
    charge_limit: 70,
    vehicle: "Tesla M-3",
  },
  analytics: null,
  page: "dashboard",
  drawer: null,
  selectedStation: null,
  zoom: 1,
};
let toastTimer;
function toast(message) {
  $("#toast").textContent = message;
  $("#toast").classList.add("visible");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => $("#toast").classList.remove("visible"), 4000);
}
async function api(path, method = "GET", body) {
  const res = await fetch(path, {
    method,
    headers: {
      "Content-Type": "application/json",
      ...(state.token ? { Authorization: "Bearer " + state.token } : {}),
    },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }),
  });
  if (res.status === 204) return null;
  const data = await res.json();
  if (!res.ok)
    throw Error(
      data.error?.message ||
        (Array.isArray(data.detail)
          ? data.detail.map((x) => x.msg).join("；")
          : data.detail) ||
        "请求失败",
    );
  return data;
}
async function refresh() {
  const [stations, chargers, users, orders, logs, summary, preferences] =
    await Promise.all(
      [
        "/admin/stations",
        "/admin/chargers",
        "/admin/users?limit=200",
        "/admin/orders?limit=200",
        "/admin/ops-logs?limit=200",
        "/admin/stats/summary",
        "/admin/console/settings",
      ].map((p) => api(p)),
    );
  Object.assign(state, {
    stations,
    chargers,
    users,
    orders,
    logs,
    summary,
    preferences,
  });
  if (
    !state.selectedStation ||
    !stations.some((x) => x.id === state.selectedStation)
  )
    state.selectedStation = stations[0]?.id;
  render();
  if (state.drawer) renderManager(state.drawer);
}
function showPage(page) {
  state.page = page;
  $$(".page").forEach((el) =>
    el.classList.toggle("active", el.id === "page-" + page),
  );
  $$("[data-page]").forEach((el) =>
    el.classList.toggle("active", el.dataset.page === page),
  );
  history.replaceState(null, "", "#" + page);
  if (page === "station") renderStations();
  if (page === "forecast") renderForecast();
}
function goalChart() {
  const n = state.preferences.daily_goal,
    w = state.preferences.weekly_goal,
    selected = $("#goal-period").value === "week" ? w : n;
  const tick = Array.from({ length: 51 }, (_, i) => {
    const a = Math.PI + (i * Math.PI) / 50;
    const x = 180 + 151 * Math.cos(a),
      y = 181 + 151 * Math.sin(a);
    const x2 = 180 + (i % 5 === 0 ? 146 : 148) * Math.cos(a),
      y2 = 181 + (i % 5 === 0 ? 146 : 148) * Math.sin(a);
    return `<path d="M${x} ${y}L${x2} ${y2}"/>`;
  }).join("");
  $("#goal-chart").innerHTML =
    `<svg viewBox="0 0 360 215"><defs><linearGradient id="goal-gradient"><stop stop-color="#9150f0"/><stop offset="1" stop-color="#fca5da"/></linearGradient></defs><g stroke="#aa94bc" stroke-width="1.4">${tick}</g><path d="M50 181A130 130 0 0 1 310 181" fill="none" stroke="#251735" stroke-width="25" stroke-linecap="round"/><path d="M50 181A130 130 0 0 1 310 181" pathLength="100" fill="none" stroke="url(#goal-gradient)" stroke-width="25" stroke-linecap="round" stroke-dasharray="${selected} 100"/><path d="M87 181A93 93 0 0 1 273 181" fill="none" stroke="#241831" stroke-width="19" stroke-linecap="round"/>${Array.from(
      { length: Math.max(1, Math.round(w / 4)) },
      (_, i) => {
        let a = Math.PI + i * 0.24;
        return `<circle cx="${180 + 93 * Math.cos(a)}" cy="${181 + 93 * Math.sin(a)}" r="9" fill="url(#goal-gradient)"/>`;
      },
    ).join(
      "",
    )}<g fill="#b8a4c9" font-size="14" font-family="Arial"><text x="18" y="187" transform="rotate(-90 18 187)">0</text><text x="48" y="76" transform="rotate(-50 48 76)">25</text><text x="171" y="18">50</text><text x="299" y="74" transform="rotate(50 299 74)">75</text><text x="339" y="164" transform="rotate(90 339 164)">100</text></g></svg>`;
  $("#daily-value").innerHTML = n + "<small>%</small>";
  $("#weekly-value").innerHTML = w + "<small>%</small>";
}
function flowChart() {
  let totals = [79, 43, 86],
    labels = ["10月", "11月", "12月"];
  const historical = $("#stat-period").value === "historical";
  if (historical && state.analytics?.daily) {
    const groups = {};
    state.analytics.daily.forEach(
      (d) =>
        (groups[d.date.slice(0, 7)] =
          (groups[d.date.slice(0, 7)] || 0) + d.energy),
    );
    labels = Object.keys(groups).slice(-3);
    const max = Math.max(...Object.values(groups));
    totals = labels.map((k) => Math.round((groups[k] / max) * 100));
  }
  const colors = ["#d155c7", "#e9a83d", "#65409e", "#32224f"];
  const gradient = ["pinkbar", "goldbar"];
  let shape = "";
  const xs = [2, 230, 458],
    width = 92,
    base = 202;
  const values = totals.map((t) =>
    [t * 0.27, t * 0.31, t * 0.27, t * 0.15].map((v) => v * 1.8),
  );
  for (let j = 0; j < 3; j++) {
    let y = base;
    for (let k = 0; k < 4; k++) {
      const h = values[j][k];
      y -= h;
      shape += `<rect x="${xs[j]}" y="${y}" width="${width}" height="${Math.max(4, h - 4)}" rx="6" fill="${k < 2 ? "url(#" + gradient[k] + ")" : colors[k]}"/>`;
      if (j < 2) {
        const lower = base - values[j].slice(0, k).reduce((a, b) => a + b, 0);
        const nextlower =
            base - values[j + 1].slice(0, k).reduce((a, b) => a + b, 0),
          nextupper = nextlower - values[j + 1][k];
        shape += `<path d="M${xs[j] + width + 3} ${y}C${xs[j] + 145} ${y} ${xs[j + 1] - 50} ${nextupper} ${xs[j + 1] - 4} ${nextupper}L${xs[j + 1] - 4} ${nextlower - 5}C${xs[j + 1] - 45} ${nextlower - 5} ${xs[j] + 145} ${lower - 5} ${xs[j] + width + 3} ${lower - 5}Z" fill="${colors[k]}" opacity=".24"/>`;
      }
    }
    shape += `<text x="${xs[j] + width / 2}" y="${y - 12}" fill="#c9b6d7" text-anchor="middle" font-size="13">合计： ${totals[j]}%</text><text x="${xs[j] + width / 2}" y="227" fill="#c9b6d7" text-anchor="middle" font-size="13">${labels[j]}</text>`;
  }
  $("#flow-chart").innerHTML =
    `<svg viewBox="0 0 555 238" role="img" aria-label="${historical ? "UrbanEV 月能量相对峰值" : "演示分组统计"}"><defs><linearGradient id="pinkbar" x2="0" y2="1"><stop stop-color="#f3a1e1"/><stop offset="1" stop-color="#c445bc"/></linearGradient><linearGradient id="goldbar" x2="0" y2="1"><stop stop-color="#ffdc90"/><stop offset="1" stop-color="#d79428"/></linearGradient></defs>${shape}</svg>`;
}
function drawMaps() {
  const roads = [
    "M-50 75L650 445",
    "M-30 460L430-40",
    "M-50 340L660 85",
    "M60-20L580 550",
    "M-20 160L560-20",
    "M100 550L355-20",
    "M430 550L540 0",
    "M220 0L40 550",
    "M15 0L140 100 40 220 155 490",
    "M230 550L180 360 330 160 270 0",
    "M600 400L370 290 440 170 610 210",
    "M-20 240L160 220 210 290 340 240 405 325 660 390",
    "M190 15L260 60 310 20",
    "M90 80L165 65 130 150 70 175Z",
    "M315 320L370 380 300 450 215 395Z",
    "M470 65L420 130 455 190 580 155",
    "M70 490L100 400 25 355 0 420",
    "M355 10L385 90 490 100",
    "M470 465L545 420 630 490",
    "M165 120L235 185 180 220",
    "M285 460L340 505 390 465",
    "M510 210L460 300 560 350",
    "M240 70L190 120 220 155",
  ];
  const selected = Math.max(
    0,
    state.stations.findIndex((s) => s.id === state.selectedStation),
  );
  const endX = 475 + (selected % 3) * 12,
    endY = 285 + (selected % 4) * 12;
  const route = `M180 125L65 175Q57 180 68 187L300 365Q308 370 312 360L377 245Q383 238 390 246L${endX} ${endY}`;
  $$(".map-layer").forEach(
    (el) =>
      (el.innerHTML = `<svg viewBox="0 0 600 500" preserveAspectRatio="xMidYMid slice" role="img" aria-label="模拟路线示意图"><rect width="600" height="500" fill="#251636"/><g fill="none" stroke="#6a4a76" opacity=".4">${roads.map((r, i) => `<path d="${r}" stroke-width="${i < 8 ? 9 : 4}"/>`).join("")}</g><path d="${route}" stroke="#e175d9" stroke-width="9" fill="none" stroke-linejoin="round"/><path d="${route}" stroke="#743c87" stroke-width="2" stroke-dasharray="5 4" fill="none"/><circle cx="180" cy="125" r="32" fill="#8524e5" opacity=".18"/><circle cx="180" cy="125" r="18" fill="#a254e4"/><path d="M168 132l15-20 9 24z" fill="#fff"/><path d="M${endX} ${endY + 29}c-35-34-15-54 0-54s35 20 0 54" fill="#e2a4df"/><circle cx="${endX}" cy="${endY - 6}" r="12" fill="#392147"/><text x="${endX}" y="${endY - 1}" text-anchor="middle" fill="white" font-size="14">ϟ</text></svg>`),
  );
  const s = state.stations[selected];
  $("#turn-address").textContent = s?.address || "暂无站点";
  $("#turn-distance").textContent = 650 + selected * 130 + "米";
}
function stationRows(rows, navigation = false) {
  return (
    rows
      .map((s) => {
        const available = state.chargers.filter(
          (c) => c.station_id === s.id && c.status === "available",
        ).length;
        return `<div class="station-row"><div class="station-identity"><span class="station-thumb"></span><div><strong title="${esc(s.name)}">${esc(s.name)}</strong><small title="${esc(s.address)}">${esc(s.address)}</small></div></div><div><p><b class="port-count">${available}</b> 空闲接口</p><p>◎ ¥${Number(s.unit_price).toFixed(2)}/kWh</p></div><div class="station-time"><p>ϟ 全天营业</p><p>▣ ${s.charger_count} 充电接口</p></div><button class="outline" data-${navigation ? "direction" : "book-station"}="${esc(s.id)}">${navigation ? "查看路线" : "立即预约"}</button></div>`;
      })
      .join("") || '<p class="empty">没有匹配的站点</p>'
  );
}
function renderStations() {
  const only = $("#station-filter").value === "available";
  const rows = state.stations.filter(
    (s) =>
      !only ||
      state.chargers.some(
        (c) => c.station_id === s.id && c.status === "available",
      ),
  );
  $("#home-stations").innerHTML = stationRows(rows.slice(0, 4));
  const query = $("#nearby-search").value.toLowerCase();
  $("#navigation-stations").innerHTML = stationRows(
    state.stations
      .filter((s) => (s.name + s.address).toLowerCase().includes(query))
      .slice(0, 3),
    true,
  );
  drawMaps();
}
const statusCell = (v) =>
  `<span class="status ${esc(v)}">${esc(zhValue(v, "status"))}</span>`;
const date = (v) => {
  if (!v) return "—";
  const d = new Date(v);
  if (Number.isNaN(d.getTime())) return String(v);
  const pad = (part) => String(part).padStart(2, "0");
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
};
function table(headers, rows) {
  return `<div class="table-scroll"><table class="data-table"><thead><tr>${headers.map((h) => `<th>${h}</th>`).join("")}</tr></thead><tbody>${rows.map((r) => "<tr>" + r.map((c) => "<td>" + c + "</td>").join("") + "</tr>").join("") || `<tr><td colspan="${headers.length}" class="empty">暂无记录</td></tr>`}</tbody></table></div>`;
}
function actionButton(action, id, label) {
  return `<button data-command="${action}" data-id="${esc(id)}">${label}</button>`;
}
function orderTable(rows) {
  return table(
    ["订单 / 站点", "用户 / 电桩", "状态", "电量 / 金额", "操作"],
    rows.map((o) => [
      `${esc(o.id.slice(0, 8))}<br>${esc(o.station_name)}`,
      `${esc(o.phone)}<br>${esc(o.charger_code)}`,
      statusCell(o.status),
      `${Number(o.energy_kwh).toFixed(2)} kWh<br>¥${Number(o.amount).toFixed(2)}`,
      `<div class="table-actions">${actionButton("order-detail", o.id, "详情")}${o.status === "reserved" ? actionButton("start", o.id, "启动") + actionButton("cancel", o.id, "取消") : o.status === "charging" ? actionButton("stop", o.id, "结束") : o.status === "completed" ? actionButton("refund", o.id, "退款") : ""}</div>`,
    ]),
  );
}
function logsTable(rows) {
  return table(
    ["时间", "操作", "对象", "详情"],
    rows.map((l) => [
      esc(date(l.created_at)),
      esc(activityHeading(l.action)),
      `${esc(zhValue(l.target_type, "target_type"))}<br>${esc(String(l.target_id || "").slice(0, 8))}`,
      esc(zhValue(l.detail, "detail")),
    ]),
  );
}
function render() {
  goalChart();
  flowChart();
  renderStations();
  if (
    ![...$("#vehicle-select").options].some(
      (o) => o.value === state.preferences.vehicle,
    )
  ) {
    $("#vehicle-select").add(
      new Option(state.preferences.vehicle, state.preferences.vehicle),
    );
  }
  $("#vehicle-select").value = state.preferences.vehicle;
  $(".vehicle-name").textContent = zhValue(
    state.preferences.vehicle,
    "vehicle",
  );
  if (
    ![...$("#charge-limit").options].some(
      (o) => o.value === String(state.preferences.charge_limit),
    )
  ) {
    $("#charge-limit").add(
      new Option(
        state.preferences.charge_limit + "%",
        String(state.preferences.charge_limit),
      ),
    );
  }
  $("#charge-limit").value = String(state.preferences.charge_limit);
  renderActivity();
}
function openDrawer(title, content) {
  $("#drawer-title").textContent = title;
  $("#drawer-content").innerHTML = content;
  if (!$("#drawer").open) $("#drawer").showModal();
  paintIcons($("#drawer"));
}
function renderManager(kind, search = "") {
  state.drawer = kind;
  const titles = {
    users: "用户管理",
    stations: "站点管理",
    chargers: "电桩管理",
    orders: "订单管理",
    logs: "操作日志",
  };
  const rows = state[kind].filter((r) =>
    Object.values(r).join(" ").toLowerCase().includes(search.toLowerCase()),
  );
  let content = "";
  if (kind === "users")
    content = table(
      ["用户", "手机号", "余额", "状态", "操作"],
      rows.map((u) => [
        esc(u.nickname),
        esc(u.phone),
        "¥" + Number(u.balance).toFixed(2),
        statusCell(u.status),
        `<div class="table-actions">${actionButton("edit-user", u.id, "编辑")}${actionButton("wallet", u.id, "账本")}${actionButton("money", u.id, "调账")}${actionButton("toggle-user", u.id, u.status === "active" ? "冻结" : "解冻")}${actionButton("delete-user", u.id, "删除")}</div>`,
      ]),
    );
  if (kind === "stations")
    content = table(
      ["站点", "地址", "单价", "电桩", "操作"],
      rows.map((s) => [
        esc(s.name),
        esc(s.address),
        "¥" + Number(s.unit_price).toFixed(2),
        s.charger_count,
        `<div class="table-actions">${actionButton("edit-station", s.id, "编辑")}${actionButton("station-chargers", s.id, "电桩")}${actionButton("delete-station", s.id, "删除")}</div>`,
      ]),
    );
  if (kind === "chargers")
    content = table(
      ["编号", "站点", "规格", "状态", "操作"],
      rows.map((c) => [
        esc(c.code),
        esc(c.station_name),
        `${esc(zhValue(c.kind, "kind"))} / ${c.power_kw} kW`,
        statusCell(c.status),
        `<div class="table-actions">${actionButton("edit-charger", c.id, "编辑")}${actionButton("fault", c.id, c.status === "faulted" ? "恢复" : "故障")}${c.status === "faulted" ? actionButton("restart", c.id, "重启") : ""}${actionButton("delete-charger", c.id, "删除")}</div>`,
      ]),
    );
  if (kind === "orders") content = orderTable(rows);
  if (kind === "logs") content = logsTable(rows);
  openDrawer(
    titles[kind],
    `<div class="drawer-toolbar"><input id="manager-search" placeholder="搜索管理记录…" value="${esc(search)}" aria-label="筛选管理记录">${kind !== "logs" ? `<button class="primary" data-action="new-${kind}">＋ 新增记录</button>` : ""}<button class="outline" data-action="export-current">导出 CSV</button><button class="outline" data-action="refresh">↻</button></div>${content}<p class="source-note">${["users", "orders", "logs"].includes(kind) ? "显示最近 200 条记录；接口支持 limit / offset 分页。" : "完整站点与电桩列表。"} 所有修改写入 PostgreSQL 并记录审计日志。</p>`,
  );
  $("#manager-search").oninput = (e) => {
    const pos = e.target.selectionStart;
    renderManager(kind, e.target.value);
    $("#manager-search").focus();
    $("#manager-search").setSelectionRange(pos, pos);
  };
}
function field(name, label, value = "", type = "text", options) {
  return `<label>${esc(label)}${options ? `<select name="${name}" required>${options.map((o) => `<option value="${esc(o.value ?? o)}" ${String(o.value ?? o) === String(value) ? "selected" : ""}>${esc(o.label ?? o)}</option>`).join("")}</select>` : `<input name="${name}" type="${type}" value="${esc(value)}" required ${type === "number" ? 'step="any"' : ""}>`}</label>`;
}
function edit(title, fields, submit, label = "保存修改") {
  $("#editor-title").textContent = title;
  $("#form-fields").innerHTML = fields;
  $("#form-error").textContent = "";
  $("#submit-edit").textContent = label;
  $("#edit-form").onsubmit = async (e) => {
    e.preventDefault();
    const button = $("#submit-edit");
    button.disabled = true;
    $("#form-error").textContent = "";
    try {
      await submit(Object.fromEntries(new FormData(e.target)));
      $("#editor").close();
      toast("操作已保存");
      await refresh();
    } catch (error) {
      $("#form-error").textContent = error.message;
    } finally {
      button.disabled = false;
    }
  };
  if (!$("#editor").open) $("#editor").showModal();
}
function userEdit(id) {
  const u = state.users.find((x) => x.id === id) || {};
  edit(
    id ? "编辑用户" : "创建用户",
    field("nickname", "昵称", u.nickname) + field("phone", "手机号", u.phone),
    (data) =>
      api("/admin/users" + (id ? "/" + id : ""), id ? "PATCH" : "POST", data),
  );
}
function stationEdit(id) {
  const s = state.stations.find((x) => x.id === id) || {
    longitude: 114.05,
    latitude: 22.54,
    unit_price: 1.2,
  };
  edit(
    id ? "编辑站点" : "新增站点",
    field("name", "站点名称", s.name) +
      field("address", "地址", s.address) +
      field("longitude", "经度", s.longitude, "number") +
      field("latitude", "纬度", s.latitude, "number") +
      field("unit_price", "单价 / 元每 kWh", s.unit_price, "number"),
    (data) =>
      api(
        "/admin/stations" + (id ? "/" + id : ""),
        id ? "PATCH" : "POST",
        data,
      ),
  );
}
function chargerEdit(id) {
  const c = state.chargers.find((x) => x.id === id) || {
    kind: "fast",
    power_kw: 120,
  };
  edit(
    id ? "编辑电桩" : "新增电桩",
    (!id
      ? field(
          "station_id",
          "所属站点",
          state.selectedStation,
          "text",
          state.stations.map((s) => ({ value: s.id, label: s.name })),
        )
      : "") +
      field("code", "设备编号", c.code) +
      field("kind", "电桩类型", c.kind, "text", [
        { value: "fast", label: "快充" },
        { value: "slow", label: "慢充" },
      ]) +
      field("power_kw", "额定功率 kW", c.power_kw, "number"),
    (data) =>
      api(
        "/admin/chargers" + (id ? "/" + id : ""),
        id ? "PATCH" : "POST",
        data,
      ),
  );
}
function book(stationId) {
  const chargers = state.chargers.filter(
    (c) =>
      c.status === "available" && (!stationId || c.station_id === stationId),
  );
  const users = state.users.filter((u) => u.status === "active");
  if (!users.length) {
    toast("请先创建用户并调整余额");
    manage("users");
    return;
  }
  if (!chargers.length) {
    toast("没有可预约的空闲电桩");
    return;
  }
  const key = crypto.randomUUID();
  edit(
    "管理员代预约",
    field(
      "user_id",
      "用户（余额至少 ¥5）",
      "",
      "text",
      users.map((u) => ({
        value: u.id,
        label: `${u.nickname} · ${u.phone} · ¥${u.balance}`,
      })),
    ) +
      field(
        "charger_id",
        "空闲充电枪",
        "",
        "text",
        chargers.map((c) => ({
          value: c.id,
          label: `${c.station_name} · ${c.code} · ${c.power_kw}kW`,
        })),
      ),
    async (data) => {
      await api("/admin/orders", "POST", { ...data, idempotency_key: key });
      showPage("trips");
    },
    "确认预约",
  );
}
function settingsForm() {
  const p = state.preferences;
  edit(
    "演示运营目标设置",
    field("vehicle", "车型名称", p.vehicle) +
      field("daily_goal", "每日目标进度 %", p.daily_goal, "number") +
      field("weekly_goal", "每周目标进度 %", p.weekly_goal, "number") +
      field("charge_limit", "充电上限 %（展示）", p.charge_limit, "number"),
    (data) => api("/admin/console/settings", "PUT", data),
  );
}
async function manage(kind) {
  try {
    if (!state.token) throw Error("管理员尚未连接");
    renderManager(kind);
  } catch (e) {
    toast(e.message);
  }
}
function exportRows(rows, name) {
  if (!rows.length) {
    toast("没有可导出的记录");
    return;
  }
  const cell = (v) =>
    '"' +
    String(typeof v === "object" ? JSON.stringify(v) : (v ?? ""))
      .replace(/^[=+@\-\t\r]/, "'$&")
      .replace(/"/g, '""') +
    '"';
  const headers = Object.keys(rows[0]);
  const csv =
    "\uFEFF" +
    [
      headers.map((k) => cell(zhField(k))).join(","),
      ...rows.map((r) => headers.map((k) => cell(zhValue(r[k], k))).join(",")),
    ].join("\r\n");
  const url = URL.createObjectURL(
    new Blob([csv], { type: "text/csv;charset=utf-8" }),
  );
  const a = document.createElement("a");
  a.href = url;
  a.download = "electra-" + name + ".csv";
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
async function command(action, id) {
  const user = state.users.find((x) => x.id === id);
  const charger = state.chargers.find((x) => x.id === id);
  if (action === "edit-user") return userEdit(id);
  if (action === "edit-station") return stationEdit(id);
  if (action === "edit-charger") return chargerEdit(id);
  if (action === "station-chargers") {
    manage("chargers");
    $("#manager-search").value = state.stations.find((s) => s.id === id).name;
    $("#manager-search").dispatchEvent(new Event("input"));
    return;
  }
  if (action === "money" || action === "refund") {
    const key = crypto.randomUUID();
    edit(
      action === "money" ? "余额调整" : "订单退款",
      field("amount", "金额（调账允许负数；退款必须正数）", "", "number") +
        field("reason", "操作原因", ""),
      (data) =>
        api(
          action === "money"
            ? `/admin/users/${id}/wallet-adjustments`
            : `/admin/orders/${id}/refund`,
          "POST",
          { ...data, idempotency_key: key },
        ),
    );
    return;
  }
  if (action === "wallet") {
    const rows = await api(`/admin/users/${id}/wallet`);
    state.drawer = null;
    openDrawer(
      "钱包 · " + user.nickname,
      table(
        ["时间", "类型", "金额", "结余"],
        rows.map((r) => [
          esc(date(r.created_at)),
          esc(zhValue(r.entry_type, "entry_type")),
          esc(r.amount),
          esc(r.balance_after),
        ]),
      ),
    );
    return;
  }
  if (action === "order-detail") {
    const o = await api("/admin/orders/" + id);
    state.drawer = null;
    openDrawer(
      "订单详情",
      `<div class="detail-grid">${Object.entries(o)
        .map(
          ([k, v]) =>
            `<div><small>${esc(zhField(k))}</small><strong>${esc(zhValue(v, k))}</strong></div>`,
        )
        .join("")}</div>`,
    );
    return;
  }
  let url,
    method = "POST",
    body;
  if (action === "toggle-user") {
    url = `/admin/users/${id}/status`;
    method = "PATCH";
    body = { status: user.status === "active" ? "frozen" : "active" };
  } else if (action === "fault") {
    url = `/admin/chargers/${id}/status`;
    method = "PATCH";
    body = { status: charger.status === "faulted" ? "available" : "faulted" };
  } else if (action === "restart") url = `/admin/chargers/${id}/restart`;
  else if (["start", "stop", "cancel"].includes(action))
    url = `/admin/orders/${id}/${action}`;
  else if (action.startsWith("delete-")) {
    const type = action.slice(7);
    url = `/admin/${type === "user" ? "users" : type === "station" ? "stations" : "chargers"}/${id}`;
    method = "DELETE";
    edit(
      "删除确认",
      `<p class="source-note">将删除选中记录。存在历史订单或资金记录时，服务器会拒绝删除。</p>`,
      () => api(url, method),
      "确认删除",
    );
    return;
  } else return;
  await api(url, method, body);
  toast("操作成功");
  await refresh();
}
async function action(name) {
  if (["users", "chargers", "stations", "orders", "logs"].includes(name))
    return manage(name);
  if (name === "new-users") return userEdit();
  if (name === "new-stations") return stationEdit();
  if (name === "new-chargers") return chargerEdit();
  if (["book", "new-orders"].includes(name)) return book();
  if (name === "settings") return settingsForm();
  if (name === "notifications") return manage("logs");
  if (name === "search") {
    manage("stations");
    $("#manager-search").focus();
    return;
  }
  if (name === "planner") {
    edit(
      "路线规划示意",
      field(
        "station_id",
        "目的站点",
        state.selectedStation,
        "text",
        state.stations.map((s) => ({ value: s.id, label: s.name })),
      ),
      async (data) => {
        state.selectedStation = data.station_id;
        showPage("station");
        drawMaps();
      },
      "查看路线",
    );
    return;
  }
  if (name === "account") {
    state.drawer = null;
    openDrawer(
      "管理员",
      `<div class="detail-grid"><div><small>角色</small><strong>全部管理权限</strong></div><div><small>工作空间</small><strong>充电平台模拟环境</strong></div></div><p class="source-note">当前为已授权的全权限模拟后台。用户、站点、电桩、订单与资金变更会写入数据库。车型、电量、目标、分组流图和路线为视觉演示，地图不提供真实道路导航。统计切换 UrbanEV 可查看历史能量相对峰值。</p><div class="drawer-toolbar"><button class="primary" data-action="users">用户管理</button><button class="outline" data-action="stations">站点管理</button><button class="outline" data-action="chargers">设备管理</button><button class="outline" data-action="orders">订单管理</button></div>`,
    );
    return;
  }
  if (name === "refresh") {
    await refresh();
    toast("数据已刷新");
    return;
  }
  if (name === "export-logs") return exportRows(state.logs, "logs");
  if (name === "export-current") {
    const query = $("#manager-search")?.value || "";
    exportRows(
      state[state.drawer].filter((r) =>
        Object.values(r).join(" ").toLowerCase().includes(query.toLowerCase()),
      ),
      state.drawer,
    );
  }
}
document.addEventListener("click", async (event) => {
  const target = event.target.closest("button");
  if (!target) return;
  try {
    if (target.dataset.page) showPage(target.dataset.page);
    if (target.dataset.close) {
      $("#" + target.dataset.close).close();
      if (target.dataset.close === "drawer") state.drawer = null;
    }
    if (target.dataset.action) await action(target.dataset.action);
    if (target.dataset.command)
      await command(target.dataset.command, target.dataset.id);
    if (target.dataset.bookStation) book(target.dataset.bookStation);
    if (target.dataset.direction) {
      state.selectedStation = target.dataset.direction;
      drawMaps();
      toast("已选择站点 · 模拟路线预览");
    }
    if (target.dataset.map) {
      state.zoom =
        target.dataset.map === "center"
          ? 1
          : Math.max(
              1,
              Math.min(
                2.3,
                state.zoom + (target.dataset.map === "in" ? 0.2 : -0.2),
              ),
            );
      target.closest(".route-map").querySelector(".map-layer").style.transform =
        `scale(${state.zoom})`;
    }
  } catch (error) {
    toast(error.message);
  }
});
$("#drawer").addEventListener("close", () => (state.drawer = null));
$("#goal-period").onchange = goalChart;
$("#stat-period").onchange = async () => {
  if ($("#stat-period").value === "historical" && !state.analytics) {
    try {
      state.analytics = await api("/admin/console/analytics");
    } catch (e) {
      toast(e.message);
      $("#stat-period").value = "reference";
    }
  }
  flowChart();
};
$("#station-filter").onchange = renderStations;
$("#nearby-search").oninput = renderStations;
for (const selector of ["#vehicle-select", "#charge-limit"])
  $(selector).onchange = async () => {
    try {
      await api("/admin/console/settings", "PUT", {
        ...state.preferences,
        vehicle: $("#vehicle-select").value,
        charge_limit: Number($("#charge-limit").value),
      });
      await refresh();
      toast("展示设置已保存");
    } catch (e) {
      toast(e.message);
    }
  };
async function init() {
  paintIcons();
  goalChart();
  flowChart();
  drawMaps();
  try {
    const login = await api("/auth/console", "POST");
    state.token = login.access_token;
    await refresh();
    $("#connection-dot").classList.add("online");
    $("#connection-label").textContent = "管理员 · 已连接";
    showPage(
      ["dashboard", "station", "trips", "history", "forecast"].includes(
        location.hash.slice(1),
      )
        ? location.hash.slice(1)
        : "dashboard",
    );
  } catch (e) {
    $("#connection-label").textContent = "连接不可用";
    $("#home-stations").innerHTML =
      `<p class="login-error">${esc(e.message)}</p><button class="outline" id="manual-login">管理员登录</button>`;
    $("#manual-login").onclick = () =>
      edit(
        "管理员登录",
        field("username", "用户名", "admin") +
          field("password", "密码", "", "password"),
        async (data) => {
          state.token = (
            await api("/auth/admin/login", "POST", data)
          ).access_token;
          await refresh();
          $("#connection-dot").classList.add("online");
          $("#connection-label").textContent = "管理员 · 已连接";
        },
        "登录",
      );
  }
}
init();
