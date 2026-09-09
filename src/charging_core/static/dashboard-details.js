/* Additional screens consume the same snapshot as the overview. */
const detailViews = (() => {
  let rows = [],
    selectedId = null;
  const esc = (value) => escapeHtml(value);
  const money = (value) =>
    Number(value || 0).toLocaleString("zh-CN", {
      minimumFractionDigits: 2,
      maximumFractionDigits: 2,
    });
  function bind() {
    document.querySelector("#orders-connect").onclick = () =>
      document.querySelector("#admin-dialog").showModal();
  }
  function select(id) {
    const station = rows.find((s) => s.id === id);
    if (!station) return;
    selectedId = id;
    const free = Number(station.available_count),
      total = Number(station.charger_count),
      share = total ? (free / total) * 100 : 0;
    $("#region-station-name").textContent = station.name;
    $("#region-station-address").textContent = station.address;
    $("#region-free").textContent = free;
    $("#region-total").textContent = total;
    $("#station-free-share").textContent = share.toFixed(1) + "%";
    $("#station-free-ring").style.background =
      `conic-gradient(#16d8e9 0 ${share}%,#6b39cb ${share}% 100%)`;
    $("#station-gauge-legend").innerHTML =
      `<span>空闲设备<b>${free} 把</b></span><span>非空闲设备<b>${total - free} 把</b></span>`;
    $("#station-price").textContent = money(station.unit_price);
    $("#station-max-power").textContent = Number(station.max_power_kw) + " kW";
    $("#station-source").textContent = station.source_dataset || "平台";
    $("#station-resource-summary").innerHTML =
      `<div><small>站点设备</small><strong>${total}</strong></div><div><small>可用充电枪</small><strong>${free}</strong></div><div><small>非空闲设备</small><strong>${total - free}</strong></div><div><small>空闲占比</small><strong>${share.toFixed(1)}%</strong></div>`;
    $$(
      "[data-region-station],[data-resource-station],[data-table-station]",
    ).forEach((el) => {
      const sid =
        el.dataset.regionStation ||
        el.dataset.resourceStation ||
        el.dataset.tableStation;
      el.classList.toggle(
        el.classList.contains("region-pin") ? "active" : "selected",
        sid === id,
      );
    });
    // Out-of-region platform stations still have details, but are not plotted in Shenzhen.
    $(".region-coordinate").textContent =
      station.source_dataset === "UrbanEV"
        ? "SHENZHEN / 22°32′ N · 114°03′ E"
        : "该站点位于深圳区域图之外";
  }
  function tableUpdated(filtered) {
    $("#station-results").textContent = `${filtered.length} 座站点`;
    $$("[data-table-station]").forEach((el) => {
      el.onclick = () => select(el.dataset.tableStation);
      el.classList.toggle("selected", el.dataset.tableStation === selectedId);
    });
    if (filtered.length === 1) select(filtered[0].id);
  }
  function renderMap(stations) {
    const local = stations.filter((s) => s.source_dataset === "UrbanEV");
    $("#regional-nodes").innerHTML = local
      .map((s, i) => {
        const x =
            ((80 + ((Number(s.longitude) - 113.75) / 0.9) * 740) / 900) * 100,
          y = ((560 - ((Number(s.latitude) - 22.4) / 0.48) * 400) / 700) * 100;
        return `<button class="region-pin" data-region-station="${esc(s.id)}" style="left:${x}%;top:${y}%" aria-label="${esc(s.name)}，空闲 ${s.available_count} 把"><i></i><span>${String(i + 1).padStart(2, "0")}</span></button>`;
      })
      .join("");
    $$("[data-region-station]").forEach(
      (el) => (el.onclick = () => select(el.dataset.regionStation)),
    );
    const ranked = [...stations].sort(
        (a, b) => b.charger_count - a.charger_count,
      ),
      max = Math.max(...ranked.map((s) => s.charger_count), 1);
    $("#station-resource-bars").innerHTML = ranked
      .slice(0, 8)
      .map(
        (s) =>
          `<button class="resource-column" data-resource-station="${esc(s.id)}" style="--h:${(s.charger_count / max) * 82}%" aria-label="${esc(s.name)}，${s.charger_count} 把枪"><strong>${s.charger_count}</strong><i></i><span>${esc(s.name)}</span></button>`,
      )
      .join("");
    $$("[data-resource-station]").forEach(
      (el) => (el.onclick = () => select(el.dataset.resourceStation)),
    );
    renderBottom(
      "#network-rank",
      ranked.slice(0, 5).map((s) => [s.name, s.charger_count]),
    );
    select(
      stations.some((s) => s.id === selectedId)
        ? selectedId
        : (local[0] || stations[0])?.id,
    );
  }
  function renderRevenue(revenue) {
    const total = revenue.reduce((sum, r) => sum + Number(r.revenue), 0),
      count = revenue.reduce((sum, r) => sum + Number(r.orders), 0);
    $("#week-revenue").textContent = money(total);
    $("#week-orders").textContent = count;
    $("#week-average").textContent = count ? money(total / count) : "--";
    const max = Math.max(...revenue.map((r) => Number(r.revenue)), 1) * 1.1,
      left = 50,
      bottom = 170,
      w = 900;
    const pts = revenue.map((r, i) => ({
      x: left + (i * (w - left - 15)) / Math.max(revenue.length - 1, 1),
      y: bottom - (Number(r.revenue) / max) * 150,
    }));
    const d = pts.map((p, i) => (i ? "L" : "M") + p.x + "," + p.y).join(" ");
    $("#finance-chart").innerHTML =
      `<defs><linearGradient id="revenueShade" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#967deb" stop-opacity=".72"/><stop offset="1" stop-color="#553692" stop-opacity=".03"/></linearGradient></defs>${Array.from(
        { length: 5 },
        (_, i) => {
          const y = 20 + (i * 150) / 4;
          return `<path class="finance-grid" d="M${left} ${y}H${w}"/><text x="0" y="${y + 4}">${(max * (1 - i / 4)).toFixed(1)}</text>`;
        },
      ).join(
        "",
      )}<path d="${d}L${pts.at(-1)?.x || w},${bottom}H${left}Z" fill="url(#revenueShade)"/><path d="${d}" stroke="#ab8bfa" stroke-width="2" fill="none"/>${pts.map((p, i) => `<circle cx="${p.x}" cy="${p.y}" r="3" fill="#6aeaf4"><title>${esc(revenue[i].date)}：¥${money(revenue[i].revenue)}，${revenue[i].orders} 笔</title></circle><text x="${p.x}" y="194" text-anchor="middle">${esc(revenue[i].date.slice(5))}</text>`).join("")}`;
  }
  function renderOrderState(orders) {
    const connected = Boolean(localStorage.getItem("charging_admin_token"));
    $("#orders-connect").textContent = connected
      ? "切换管理员账户 ↗"
      : "连接管理员账户 ↗";
    $("#order-results").textContent = connected
      ? `最近 ${orders.length} 笔订单`
      : "登录后查看真实交易明细";
    if (!connected) {
      $("#order-state-chart").innerHTML =
        '<div class="empty-state"><span>订单状态待同步</span><small>连接管理员账户后显示</small><button data-admin-open>连接账户 ↗</button></div>';
    } else if (!orders.length) {
      $("#order-state-chart").innerHTML =
        '<div class="empty-state">暂无订单数据</div>';
    } else {
      const names = [
          ["completed", "已完成"],
          ["charging", "充电中"],
          ["reserved", "已预约"],
          ["cancelled", "已取消"],
        ],
        counts = names.map(([key, name]) => [
          name,
          orders.filter((o) => o.status === key).length,
        ]);
      let offset = 0;
      const gradient = counts
        .map(([name, n], i) => {
          const end = offset + (n / orders.length) * 100,
            piece = `${colors[i]} ${offset}% ${end}%`;
          offset = end;
          return piece;
        })
        .join(",");
      $("#order-state-chart").innerHTML =
        `<div class="detail-gauge"><div class="ring-chart" style="background:conic-gradient(${gradient})"><span><strong>${orders.length}</strong><small>最近订单</small></span></div><div class="mini-legend">${counts.map(([name, n], i) => `<div class="legend-item" style="--color:${colors[i]}"><i></i><span>${name}</span><strong>${n}</strong></div>`).join("")}</div></div>`;
    }
    $$("[data-admin-open]").forEach(
      (el) => (el.onclick = () => $("#admin-dialog").showModal()),
    );
  }
  function render({ summary, stations, revenue, orders }) {
    rows = stations;
    $("#network-devices").textContent = summary.chargers;
    $("#network-available").textContent = summary.available;
    $("#network-share").textContent =
      percent(summary.available, summary.chargers).toFixed(1) + "%";
    $("#orders-today-revenue").textContent = money(summary.today_revenue);
    $("#orders-today-count").textContent = summary.today_orders;
    $("#orders-completed").textContent = summary.completed_orders;
    renderMap(stations);
    renderRevenue(revenue);
    renderOrderState(orders);
    if (!orders.length) {
      $("#orders-table").innerHTML = localStorage.getItem(
        "charging_admin_token",
      )
        ? '<div class="empty-state">暂无充电订单</div>'
        : '<div class="empty-state"><span>连接运营账户，查看真实充电交易</span><small>订单金额、用户和充电枪信息仅对管理员可见</small><button data-admin-open>连接管理员账户 ↗</button></div>';
      $$("[data-admin-open]").forEach(
        (el) => (el.onclick = () => $("#admin-dialog").showModal()),
      );
    }
  }
  return { bind, render, select, tableUpdated };
})();
