/* 订单与审计视图，指标均取自已加载的服务端记录。 */
const activityView = { sessionFilter: "all", selectedEvent: null };
const activityColors = ["#aa6af0", "#ed79d0", "#f2bf60", "#65cad0"];
function activitySum(rows, key) {
  return rows.reduce((sum, row) => sum + (Number(row[key]) || 0), 0);
}
function activityHeading(action) {
  const labels = {
    "order.reserve": "订单已预约",
    "order.reserved": "订单已预约",
    "order.start": "已开始充电",
    "order.stop": "订单已结算",
    "order.cancel": "预约已取消",
    "wallet.adjustment": "余额已调整",
    "wallet.refund": "退款已处理",
    "station.created": "站点已新增",
    "station.updated": "站点资料已更新",
    "station.deleted": "站点已删除",
    "charger.created": "电桩已接入",
    "charger.updated": "电桩资料已更新",
    "charger.status_changed": "电桩状态已变更",
    "charger.restarted": "电桩已重启",
    "charger.deleted": "电桩已删除",
    "user.created": "用户已创建",
    "user.updated": "用户资料已更新",
    "user.frozen": "用户已冻结",
    "user.active": "用户已解冻",
    "user.deleted": "用户已删除",
    "console.settings": "运营设置已保存",
  };
  return labels[action] || action.replaceAll(".", " · ");
}
function activityGroup(action) {
  return action.split(".")[0];
}
function activityDetail(log) {
  try {
    return typeof log.detail === "string"
      ? JSON.parse(log.detail)
      : log.detail || {};
  } catch {
    return { detail: log.detail };
  }
}
function activityDonut(items, center, caption, id) {
  const total = items.reduce((s, i) => s + i.value, 0);
  let offset = 0;
  return `<div class="insight-donut"><svg viewBox="0 0 200 200" role="img" aria-label="${esc(caption)}"><defs><linearGradient id="${id}"><stop stop-color="#854de0"/><stop offset="1" stop-color="#d18cf5"/></linearGradient></defs><circle cx="100" cy="100" r="76" fill="none" stroke="#312044" stroke-width="25"/>${items
    .map((item, i) => {
      const percent = total ? (item.value / total) * 100 : 0;
      const shape = `<circle cx="100" cy="100" r="76" pathLength="100" fill="none" stroke="${i === 0 ? "url(#" + id + ")" : activityColors[i % 4]}" stroke-width="25" stroke-dasharray="${Math.max(0, percent - 0.8)} ${100 - Math.max(0, percent - 0.8)}" stroke-dashoffset="${-offset}" transform="rotate(-90 100 100)"><title>${esc(item.label)}: ${item.value}</title></circle>`;
      offset += percent;
      return shape;
    })
    .join(
      "",
    )}<text x="100" y="101" text-anchor="middle" fill="#f8edff" font-size="30">${esc(center)}</text><text x="100" y="123" text-anchor="middle" fill="#b09ac4" font-size="10">${esc(caption)}</text></svg><div class="donut-key">${items.map((item, i) => `<div><i style="background:${activityColors[i % 4]}"></i><span>${esc(item.label)}<small>${item.value} 条记录</small></span><b>${total ? Math.round((item.value / total) * 100) : 0}%</b></div>`).join("")}</div></div>`;
}
function activityDays(rows, dateKey, count) {
  const stamps = rows
    .map((r) => new Date(r[dateKey]).getTime())
    .filter(Number.isFinite);
  const end = stamps.length ? new Date(Math.max(...stamps)) : new Date();
  end.setUTCHours(0, 0, 0, 0);
  return Array.from({ length: count }, (_, i) => {
    const d = new Date(end);
    d.setUTCDate(end.getUTCDate() - count + 1 + i);
    return {
      day: d.toISOString().slice(0, 10),
      rows: rows.filter(
        (r) =>
          String(r[dateKey] || "").slice(0, 10) ===
          d.toISOString().slice(0, 10),
      ),
    };
  });
}
function activityArea(values, id) {
  const max = Math.max(1, ...values);
  const points = values.map((v, i) => [
    25 + (i * 350) / Math.max(1, values.length - 1),
    145 - (v / max) * 115,
  ]);
  const line = points.map((p, i) => (i ? "L" : "M") + p.join(" ")).join(" ");
  return `<svg viewBox="0 0 405 180" role="img" aria-label="每日实际电量"><defs><linearGradient id="${id}" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#ed84dc" stop-opacity=".8"/><stop offset="1" stop-color="#9944d8" stop-opacity=".05"/></linearGradient></defs>${[30, 68, 106, 145].map((y, i) => `<path d="M25 ${y}H380" stroke="#6a457840" stroke-dasharray="3 4"/><text x="2" y="${y + 4}" fill="#9b83b1" font-size="8">${((max * (3 - i)) / 3).toFixed(1)}</text>`).join("")}<path d="${line}L375 145H25Z" fill="url(#${id})"/><path d="${line}" fill="none" stroke="#e486da" stroke-width="2.2"/>${points.map((p, i) => `<circle cx="${p[0]}" cy="${p[1]}" r="3" fill="#f6b5ea"><title>${values[i].toFixed(3)} kWh</title></circle>`).join("")}<text x="26" y="171" fill="#b299c8" font-size="9">开始</text><text x="345" y="171" fill="#b299c8" font-size="9">最近</text></svg>`;
}
function activityJourney() {
  const order =
    state.orders.find((o) => ["charging", "reserved"].includes(o.status)) ||
    state.orders[0];
  const name = order?.station_name || state.stations[0]?.name || "暂无站点";
  const roads = [
    "M0 50L550 230",
    "M0 190L470 0",
    "M80 0L210 250",
    "M300 0L410 250",
    "M0 110L550 50",
    "M30 250L250 0",
    "M230 250L500 0",
    "M420 40L490 130 400 180",
    "M20 20L100 60 80 140 130 210",
    "M190 10L260 60 210 120 340 190",
    "M90 90L150 130 100 170",
    "M300 200L360 130 320 70",
  ];
  $("#session-journey").innerHTML =
    `<div class="journey-map"><svg viewBox="0 0 560 240" preserveAspectRatio="xMidYMid slice"><g fill="none" stroke="#654474" opacity=".35">${roads.map((r, i) => `<path d="${r}" stroke-width="${i < 7 ? 5 : 2}"/>`).join("")}</g><path d="M135 62L264 109L335 177L482 143" fill="none" stroke="#e37bd7" stroke-width="5"/><path d="M135 62L264 109L335 177L482 143" fill="none" stroke="#7e438a" stroke-dasharray="4 4" stroke-width="1.5"/>${[
      [135, 62],
      [482, 143],
    ]
      .map(
        ([x, y]) =>
          `<circle cx="${x}" cy="${y}" r="19" fill="#b456da33"/><circle cx="${x}" cy="${y}" r="10" fill="#a465d5"/><text x="${x}" y="${y + 4}" text-anchor="middle" font-size="13" fill="#fff">ϟ</text>`,
      )
      .join(
        "",
      )}</svg><img src="assets/admin-xray-car.png" alt="充电行程车辆示意"><div class="journey-origin"><strong>订单</strong><small>${esc(order?.phone || "准备充电")}</small></div><div class="journey-destination"><strong>${esc(name)}</strong><small>${esc(order?.charger_code || "请选择充电接口")}</small></div></div><div class="journey-values"><div><small>电量</small><strong>${Number(order?.energy_kwh || 0).toFixed(2)} <em>kWh</em></strong></div><div><small>订单状态</small><strong>${esc(zhValue(order?.status || "待充电", "status"))}</strong></div><div><small>结算金额</small><strong>¥${Number(order?.amount || 0).toFixed(2)}</strong></div><div><small>订单编号</small><strong>${esc(order?.id.slice(0, 8) || "—")}</strong></div></div>`;
}
function activitySessionRows() {
  const query = $("#session-search").value.toLowerCase();
  const rows = state.orders.filter(
    (o) =>
      (activityView.sessionFilter === "all" ||
        o.status === activityView.sessionFilter) &&
      [o.station_name, o.phone, o.charger_code, o.id]
        .join(" ")
        .toLowerCase()
        .includes(query),
  );
  $("#trips-table").innerHTML = table(
    ["充电订单", "行程与接口", "电量", "费用", "状态", "操作"],
    rows.map((o) => [
      `<div class="session-identity"><span class="station-thumb"></span><span><strong>${esc(o.station_name)}</strong><small>${esc(date(o.reserved_at))}</small><small>${esc(o.phone)}</small></span></div>`,
      `<div class="mini-journey"><span>◎</span><i></i><b>ϟ</b></div><small class="connector-caption">${esc(o.charger_code)} · ${esc(o.id.slice(0, 8))}</small>`,
      `<strong>${Number(o.energy_kwh).toFixed(2)}</strong><small class="unit-caption">kWh</small>`,
      `<strong>¥${Number(o.amount).toFixed(2)}</strong>`,
      statusCell(o.status),
      `<div class="table-actions">${actionButton("order-detail", o.id, "详情")}${o.status === "reserved" ? actionButton("start", o.id, "启动") + actionButton("cancel", o.id, "取消") : o.status === "charging" ? actionButton("stop", o.id, "结束") : o.status === "completed" ? actionButton("refund", o.id, "退款") : ""}</div>`,
    ]),
  );
}
function activitySelectedEvent() {
  const log = state.logs.find(
    (l) => String(l.id) === String(activityView.selectedEvent),
  );
  if (!log) {
    $("#selected-event").innerHTML = '<p class="empty">尚未选择事件</p>';
    return;
  }
  activityView.selectedEvent = log.id;
  const d = activityDetail(log);
  $("#selected-event").innerHTML =
    `<div class="event-detail-title"><span class="event-node">ϟ</span><div><strong>${esc(activityHeading(log.action))}</strong><small>${esc(activityHeading(log.action))}</small></div></div><dl><div><dt>记录时间</dt><dd>${esc(date(log.created_at))}</dd></div><div><dt>操作对象</dt><dd>${esc(zhValue(log.target_type, "target_type"))}</dd></div><div><dt>记录编号</dt><dd>${esc(log.target_id || "—")}</dd></div>${Object.entries(
      d,
    )
      .map(
        ([k, v]) =>
          `<div><dt>${esc(zhField(k))}</dt><dd>${esc(zhValue(v, k))}</dd></div>`,
      )
      .join(
        "",
      )}</dl><button class="outline" data-audit-full="${esc(log.id)}">查看完整记录 ↗</button>`;
}
function activityTimeline() {
  const type = $("#event-filter").value,
    query = $("#event-search").value.toLowerCase();
  const rows = state.logs.filter(
    (l) =>
      (type === "all" || activityGroup(l.action) === type) &&
      [
        l.action,
        activityHeading(l.action),
        l.target_id,
        JSON.stringify(l.detail),
      ]
        .join(" ")
        .toLowerCase()
        .includes(query),
  );
  if (!rows.some((l) => String(l.id) === String(activityView.selectedEvent))) {
    activityView.selectedEvent = rows[0]?.id ?? null;
  }
  activitySelectedEvent();
  let day = "";
  $("#history-table").innerHTML =
    rows
      .map((log) => {
        const d = String(log.created_at).slice(0, 10),
          group = activityGroup(log.action),
          detail = activityDetail(log);
        let heading = "";
        if (d !== day) {
          day = d;
          heading = `<div class="timeline-date">${esc(day)}<span></span></div>`;
        }
        return (
          heading +
          `<button class="timeline-event ${String(log.id) === String(activityView.selectedEvent) ? "selected" : ""}" data-event="${esc(log.id)}"><time>${esc(new Date(log.created_at).toLocaleTimeString("zh-CN", { hour: "2-digit", minute: "2-digit", timeZone: "UTC" }))}</time><span class="event-node ${esc(group)}">${{ wallet: "¥", charger: "ϟ", station: "⌂", user: "◎", order: "↗", console: "⚙" }[group] || "•"}</span><span class="event-copy"><strong>${esc(activityHeading(log.action))}</strong><small>${esc(activityHeading(log.action))} · ${esc(String(log.target_id || "全局").slice(0, 8))}</small></span><span class="event-meta">${esc(detail.amount !== undefined ? "¥" + detail.amount : detail.reason || zhValue(log.target_type, "target_type"))}<small>${esc(detail.reason && detail.amount !== undefined ? detail.reason : "管理员")}</small></span><span class="event-arrow">⌄</span></button>`
        );
      })
      .join("") || '<p class="empty">没有匹配的操作记录</p>';
}
function activityHistoryCharts() {
  const groups = ["order", "wallet", "charger", "station"];
  const items = groups.map((g, i) => ({
    label: ["充电订单", "钱包", "电桩", "其他"][i],
    value: state.logs.filter((l) =>
      i === 3
        ? !groups.slice(0, 3).includes(activityGroup(l.action))
        : activityGroup(l.action) === g,
    ).length,
  }));
  $("#operations-mix").innerHTML = activityDonut(
    items,
    state.logs.length,
    "事件总数",
    "ops-gradient",
  );
  const days = activityDays(state.logs, "created_at", 14);
  const max = Math.max(1, ...days.map((d) => d.rows.length));
  $("#activity-bars").innerHTML =
    `<svg viewBox="0 0 310 180" role="img" aria-label="最近14天各类操作次数">${[30, 70, 110, 150].map((y) => `<path d="M12 ${y}H300" stroke="#64477640" stroke-dasharray="3 4"/>`).join("")}${days
      .map((d, i) => {
        let y = 150;
        return items
          .map((_, j) => {
            const count = d.rows.filter((l) =>
              j === 3
                ? !groups.slice(0, 3).includes(activityGroup(l.action))
                : activityGroup(l.action) === groups[j],
            ).length;
            const h = (count / max) * 120;
            y -= h;
            return `<rect x="${16 + i * 20}" y="${y}" width="12" height="${h}" rx="2" fill="${activityColors[j]}"><title>${d.day}: ${count} ${items[j].label}</title></rect>`;
          })
          .join("");
      })
      .join(
        "",
      )}<text x="12" y="174" fill="#ad92c2" font-size="9">${days[0].day.slice(5)}</text><text x="260" y="174" fill="#ad92c2" font-size="9">${days.at(-1).day.slice(5)}</text></svg><div class="chart-caption">14 天，截至 ${days.at(-1).day}</div>`;
  const buckets = Array.from({ length: 7 }, () => Array(12).fill(0));
  state.logs.forEach((log) => {
    const d = new Date(log.created_at);
    buckets[d.getUTCDay()][Math.floor(d.getUTCHours() / 2)]++;
  });
  const highest = Math.max(1, ...buckets.flat());
  $("#activity-heatmap").innerHTML =
    `<div class="heatmap-grid">${buckets.map((row, i) => `<span>${["周日", "周一", "周二", "周三", "周四", "周五", "周六"][i]}</span>${row.map((v, j) => `<b style="background:${v ? "rgba(190,107,239," + (0.25 + (0.75 * v) / highest) + ")" : "#2b193d"}" title="${["周日", "周一", "周二", "周三", "周四", "周五", "周六"][i]} ${j * 2}:00 UTC · ${v} 次操作"></b>`).join("")}`).join("")}</div><div class="heatmap-hours"><span>00</span><span>06</span><span>12</span><span>18</span><span>22</span></div><div class="heatmap-legend">低 <i></i> 高 <span>UTC · 当前事件</span></div>`;
  const cards = [
    ["审计事件", state.logs.length, "已记录操作"],
    [
      "钱包操作",
      state.logs.filter((l) => l.action.startsWith("wallet.")).length,
      "调账与退款",
    ],
    ["管理设备", state.chargers.length, "充电接口总数"],
  ];
  $("#history-metrics").innerHTML = cards
    .map(([label, value, caption], i) => {
      const series =
        i === 0
          ? days.map((d) => d.rows.length)
          : i === 1
            ? days.map(
                (d) =>
                  d.rows.filter((l) => l.action.startsWith("wallet.")).length,
              )
            : ["available", "charging", "reserved", "faulted"].map(
                (status) =>
                  state.chargers.filter((c) => c.status === status).length,
              );
      const peak = Math.max(1, ...series),
        step = 140 / series.length;
      return `<article class="card history-metric"><div><small>${label}</small><strong>${value}</strong><span>${caption}</span></div><svg viewBox="0 0 140 48" role="img" aria-label="${i === 2 ? "当前设备状态分布" : "最近14天操作次数"}">${series.map((v, j) => `<rect x="${j * step}" y="${45 - (v / peak) * 40}" width="${Math.max(4, step - 5)}" height="${Math.max(1, (v / peak) * 40)}" rx="2" fill="${activityColors[i]}" opacity="${0.4 + (j / series.length) * 0.5}"><title>${v}</title></rect>`).join("")}</svg></article>`;
    })
    .join("");
}
function renderActivity() {
  activityJourney();
  $("#session-mix").innerHTML = activityDonut(
    ["completed", "charging", "reserved", "cancelled"].map((status, i) => ({
      label: ["已完成", "充电中", "已预约", "已取消"][i],
      value: state.orders.filter((o) => o.status === status).length,
    })),
    state.orders.length,
    "订单总数",
    "session-gradient",
  );
  const days = activityDays(
      state.orders,
      "reserved_at",
      Number($("#pulse-days").value),
    ),
    energy = days.map((d) => activitySum(d.rows, "energy_kwh"));
  $("#energy-pulse").innerHTML =
    activityArea(energy, "energy-area") +
    `<div class="pulse-values"><div><small>累计电量</small><strong>${energy.reduce((a, b) => a + b, 0).toFixed(2)} <em>kWh</em></strong></div><div><small>已结算费用</small><strong>¥${days.reduce((s, d) => s + activitySum(d.rows, "amount"), 0).toFixed(2)}</strong></div><div><small>充电订单</small><strong>${days.reduce((s, d) => s + d.rows.length, 0)}</strong></div></div><div class="chart-caption">${days[0].day} — ${days.at(-1).day} · 按预约日期统计</div>`;
  activitySessionRows();
  const summary = [
    ["订单总数", state.orders.length],
    ["累计电量", activitySum(state.orders, "energy_kwh").toFixed(2) + " kWh"],
    ["已结算费用", "¥" + activitySum(state.orders, "amount").toFixed(2)],
    [
      "进行中订单",
      state.orders.filter((o) => ["reserved", "charging"].includes(o.status))
        .length,
    ],
    [
      "每单平均电量",
      (state.orders.length
        ? activitySum(state.orders, "energy_kwh") / state.orders.length
        : 0
      ).toFixed(2) + " kWh",
    ],
  ];
  $("#order-metrics").innerHTML = summary
    .map(([k, v]) => `<div><span>${k}</span><strong>${v}</strong></div>`)
    .join("");
  const count = state.chargers.length,
    available = state.chargers.filter((c) => c.status === "available").length,
    faulted = state.chargers.filter((c) => c.status === "faulted").length;
  $("#charger-overview").innerHTML =
    `<div class="device-indicator"><span>ϟ</span><strong>${faulted ? "设备需要关注" : "设备运行正常"}</strong></div><div class="device-capacity"><strong>${available}<small> / ${count}</small></strong><span>空闲充电接口</span></div><div class="capacity-track"><i style="width:${count ? (available / count) * 100 : 0}%"></i></div><div class="device-breakdown"><span>使用中 <b>${count - available - faulted}</b></span><span>故障 <b>${faulted}</b></span></div>`;
  activityHistoryCharts();
  activitySelectedEvent();
  activityTimeline();
  paintIcons($("#page-trips"));
  paintIcons($("#page-history"));
}
document.addEventListener("click", (event) => {
  const button = event.target.closest("button");
  if (!button) return;
  if (button.dataset.sessionFilter) {
    activityView.sessionFilter = button.dataset.sessionFilter;
    document
      .querySelectorAll("[data-session-filter]")
      .forEach((b) => b.classList.toggle("active", b === button));
    activitySessionRows();
  }
  if (button.dataset.event) {
    activityView.selectedEvent = button.dataset.event;
    activitySelectedEvent();
    activityTimeline();
  }
  if (button.dataset.auditFull) {
    const log = state.logs.find(
      (l) => String(l.id) === button.dataset.auditFull,
    );
    state.drawer = null;
    openDrawer(
      "审计详情",
      `<div class="detail-grid">${Object.entries(log)
        .map(
          ([k, v]) =>
            `<div><small>${esc(zhField(k))}</small><strong>${esc(zhValue(v, k))}</strong></div>`,
        )
        .join("")}</div>`,
    );
  }
});
document
  .querySelector("#session-search")
  .addEventListener("input", activitySessionRows);
document
  .querySelector("#event-search")
  .addEventListener("input", activityTimeline);
document
  .querySelector("#event-filter")
  .addEventListener("change", activityTimeline);
document
  .querySelector("#pulse-days")
  .addEventListener("change", renderActivity);
