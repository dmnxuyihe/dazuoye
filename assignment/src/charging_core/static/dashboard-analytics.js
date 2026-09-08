/* Historical UrbanEV display; no operational order or wallet mutations. */
const historicalScreen = (() => {
  const palette = [
    "#17edf3",
    "#9064fb",
    "#ea58df",
    "#4796ff",
    "#b3a1fa",
    "#41cbb8",
  ];
  let dataset,
    range = 90,
    plotId = 0;
  const number = (v) =>
    Number(v).toLocaleString("zh-CN", { maximumFractionDigits: 0 });
  const svg = (body, box = "0 0 300 120") =>
    `<svg viewBox="${box}" preserveAspectRatio="none">${body}</svg>`;
  function line(values, color = palette[0], fill = true) {
    const gradient = `history-fill-${++plotId}`;
    const max = Math.max(...values, 1),
      min = Math.min(...values, 0);
    const points = values
      .map(
        (v, i) =>
          `${(i * 296) / Math.max(values.length - 1, 1) + 2},${108 - ((v - min) / (max - min)) * 94}`,
      )
      .join(" ");
    return svg(
      `<defs><linearGradient id="${gradient}" x2="0" y2="1"><stop stop-color="${color}" stop-opacity=".5"/><stop offset="1" stop-color="${color}" stop-opacity="0"/></linearGradient></defs>${[20, 50, 80, 110].map((y) => `<path d="M0 ${y}H300" stroke="#a795ff" opacity=".12"/>`).join("")}${fill ? `<polygon points="2,115 ${points} 298,115" fill="url(#${gradient})"/>` : ""}<polyline points="${points}" fill="none" stroke="${color}" stroke-width="1.7"/>`,
    );
  }
  function bars(values, labels) {
    const max = Math.max(...values, 1);
    return `<div class="micro-rank">${values
      .slice(0, 6)
      .map(
        (v, i) =>
          `<div><span>${escapeHtml(labels[i])}</span><i style="--w:${(v / max) * 100}%;--c:${palette[i % 6]}"></i><b>${number(v)}</b></div>`,
      )
      .join("")}</div>`;
  }
  function columns(values) {
    const max = Math.max(...values, 1),
      w = 298 / values.length;
    return svg(
      values
        .map(
          (v, i) =>
            `<rect x="${i * w}" y="${112 - (v / max) * 100}" width="${Math.max(w - 2, 1)}" height="${(v / max) * 100}" fill="${palette[i % 3]}" opacity="${0.45 + (0.55 * v) / max}"/>`,
        )
        .join(""),
    );
  }
  function ring(values, labels) {
    const total = values.reduce((a, b) => a + b, 0) || 1;
    let start = 0;
    const slices = values.map((v, i) => {
      const old = start;
      start += (v / total) * 100;
      return `${palette[i % 6]} ${old}% ${start}%`;
    });
    return `<div class="micro-donut"><div style="background:conic-gradient(${slices.join(",")})"><b>${((values[0] / total) * 100).toFixed(1)}<small>%</small></b></div><ul>${values.map((v, i) => `<li><i style="background:${palette[i % 6]}"></i>${escapeHtml(labels[i])}<b>${((v / total) * 100).toFixed(1)}%</b></li>`).join("")}</ul></div>`;
  }
  function heatmap(values) {
    const max = Math.max(...values, 1);
    return `<div class="micro-heat">${values.map((v) => `<i title="${number(v)} kWh" style="background:rgba(${v / max > 0.6 ? "180,91,249" : "26,225,242"},${0.12 + (0.88 * v) / max})"></i>`).join("")}</div>`;
  }
  function radar(values) {
    const max = Math.max(...values, 1),
      points = (scale) =>
        values
          .map((v, i) => {
            const a = (i / values.length) * Math.PI * 2 - Math.PI / 2,
              r = (scale === null ? v / max : scale) * 49;
            return `${150 + Math.cos(a) * r},${59 + Math.sin(a) * r}`;
          })
          .join(" ");
    return svg(
      [0.3, 0.6, 1]
        .map(
          (s) =>
            `<polygon points="${points(s)}" fill="none" stroke="#7662b0" stroke-width=".6"/>`,
        )
        .join("") +
        `<polygon points="${points(null)}" fill="#13e9ed33" stroke="#36e7ef" stroke-width="1.4"/>`,
    );
  }
  function cards(view) {
    const d = dataset,
      h = d.hourly,
      day = d.daily,
      rank = d.ranking,
      offset = view === "stations" ? 6 : view === "orders" ? 12 : 0;
    const ranked = rank.slice(offset, offset + 6),
      poi = Object.entries(d.poi)
        .sort((a, b) => b[1] - a[1])
        .slice(0, 5);
    const month = Object.values(
      day.reduce((a, x) => {
        const m = x.date.slice(0, 7);
        a[m] = (a[m] || 0) + x.energy;
        return a;
      }, {}),
    );
    return [
      [
        "区域充电量构成",
        ring(
          ranked.slice(0, 4).map((x) => x.energy),
          ranked.slice(0, 4).map((x) => "区域 " + x.zone),
        ),
        "累计电量 · kWh",
      ],
      [
        "24H 负荷波形",
        line(h.map((x) => x.energy)),
        "00:00　06:00　12:00　18:00",
      ],
      [
        "区域容量排行",
        bars(
          ranked.map((x) => x.capacity),
          ranked.map((x) => "TAZ " + x.zone),
        ),
        "区域充电桩 · 把",
      ],
      [
        "每日充电量分布",
        columns(day.slice(-42).map((x) => x.energy)),
        "最近 42 天 · kWh",
      ],
      [
        "区域能量排行",
        bars(
          ranked.map((x) => x.energy),
          ranked.map((x) => "TAZ " + x.zone),
        ),
        "历史累计 · kWh",
      ],
      [
        "占用率时钟",
        ring([h[12].occupancy, 100 - h[12].occupancy], ["12时占用", "未占用"]),
        "历史同小时均值",
      ],
      [
        "每日负荷热力",
        heatmap(day.slice(-168).map((x) => x.energy)),
        "24 × 7 格 · 每格一天",
      ],
      [
        "综合电价曲线",
        line(
          h.map((x) => x.price),
          palette[2],
        ),
        "电费 + 服务费 · 元/kWh",
      ],
      ["月度充电能量", columns(month), "09月　10月　11月　12月　01月　02月"],
      [
        "分时负荷雷达",
        radar(h.filter((_, i) => i % 4 === 0).map((x) => x.energy)),
        "00　04　08　12　16　20 时",
      ],
      [
        "城市 POI 类型",
        ring(
          poi.map((x) => x[1]),
          poi.map(
            (x) =>
              ({
                "lifestyle services": "生活服务",
                "business services": "商业服务",
                transportation: "交通设施",
                "public services": "公共服务",
                "business and residential": "商住设施",
                "food and beverage services": "餐饮服务",
              })[x[0]] || x[0],
          ),
        ),
        "原始 POI 分类 · 前五类",
      ],
      [
        "气温变化监测",
        line(
          d.weather.map((x) => x.temperature),
          palette[4],
        ),
        "中心气象站 · 最近 168 个观测",
      ],
      [
        "日内占用率",
        line(
          h.map((x) => x.occupancy),
          palette[1],
        ),
        "历史均值 · %",
      ],
      [
        "数据资产档案",
        `<div class="data-facts"><span>区域时序<b>${number(d.observations)}</b></span><span>观测区域<b>${d.zones}</b></span><span>充电站点<b>${number(d.stations)}</b></span><span>充电桩容量<b>${number(d.chargers)}</b></span><span>源文件<b>${d.files} / 121 MB</b></span></div>`,
        "UrbanEV · 完整 GitHub 数据",
      ],
      [
        "区域负荷对比",
        columns(rank.slice(offset, offset + 32).map((x) => x.energy)),
        "32 个区域 · 累计充电量",
      ],
    ];
  }
  function renderMatrices() {
    for (const view of ["overview", "stations", "orders"]) {
      const old = $(
        `#view-${view} > .right-matrix, #view-${view} > .detail-right`,
      );
      old.classList.add("legacy-matrix");
      let matrix = $(`#view-${view} .dense-matrix`);
      if (!matrix) {
        matrix = document.createElement("div");
        matrix.className = "dense-matrix";
        old.after(matrix);
      }
      matrix.innerHTML = cards(view)
        .map(
          ([title, body, caption], i) =>
            `<article class="micro-panel"><header><h2>${title}</h2><span>${String(i + 1).padStart(2, "0")}</span></header><div class="micro-body">${body}</div><footer>${caption}</footer></article>`,
        )
        .join("");
    }
  }
  function chart() {
    const days = dataset.daily.slice(-range),
      max = Math.max(...days.map((x) => x.high)) * 1.08,
      min = Math.min(...days.map((x) => x.low)) * 0.85;
    const x = (i) => 58 + (i * 860) / days.length,
      y = (v) => 345 - ((v - min) / (max - min)) * 285,
      w = 860 / days.length;
    const ma = (n) =>
      days
        .map(
          (d, i) =>
            `${x(i) + w / 2},${y(days.slice(Math.max(0, i - n + 1), i + 1).reduce((a, b) => a + b.close, 0) / Math.min(i + 1, n))}`,
        )
        .join(" ");
    const energyMax = Math.max(...days.map((x) => x.energy));
    let body = `<defs><linearGradient id="market-fill" x2="0" y2="1"><stop stop-color="#866aff" stop-opacity=".24"/><stop offset="1" stop-color="#866aff" stop-opacity="0"/></linearGradient></defs>`;
    for (let i = 0; i < 7; i++) {
      const yy = 60 + (i * 285) / 6;
      body += `<path d="M52 ${yy}H930" stroke="#8b75c5" opacity=".18"/><text x="45" y="${yy + 4}" text-anchor="end" fill="#9286b8" font-size="10">${number(max - ((max - min) * i) / 6)}</text>`;
    }
    body += days
      .map((d, i) => {
        const c = d.close >= d.open ? "#25e9d4" : "#de6aeb";
        return `<path d="M${x(i) + w / 2} ${y(d.high)}V${y(d.low)}" stroke="${c}"/><rect x="${x(i) + w * 0.2}" y="${Math.min(y(d.open), y(d.close))}" width="${w * 0.6}" height="${Math.max(1, Math.abs(y(d.open) - y(d.close)))}" fill="${c}"/><rect x="${x(i) + w * 0.2}" y="${455 - (d.energy / energyMax) * 76}" width="${w * 0.6}" height="${(d.energy / energyMax) * 76}" fill="${c}" opacity=".55"/>${i % Math.ceil(days.length / 6) === 0 ? `<text x="${x(i)}" y="477" fill="#9286b8" font-size="11">${d.date.slice(5)}</text>` : ""}`;
      })
      .join("");
    body += `<polyline points="${ma(5)}" fill="none" stroke="#e9c477" stroke-width="1.6"/><polyline points="${ma(20)}" fill="none" stroke="#a389ff" stroke-width="1.6"/><text x="58" y="373" fill="#8b85b4" font-size="11">VOL · 日累计充电量 kWh</text><line id="market-crosshair" x1="58" x2="58" y1="50" y2="456" stroke="#c4b4ff" stroke-dasharray="4 5" opacity="0"/>`;
    $("#market-canvas").innerHTML = body;
    $("#market-canvas").onpointermove = (e) => {
      const bounds = e.currentTarget.getBoundingClientRect(),
        i = Math.max(
          0,
          Math.min(
            days.length - 1,
            Math.floor(
              ((((e.clientX - bounds.left) / bounds.width) * 960 - 58) / 860) *
                days.length,
            ),
          ),
        ),
        d = days[i];
      $("#market-readout").textContent =
        `${d.date}　开 ${number(d.open)}　高 ${number(d.high)}　低 ${number(d.low)}　收 ${number(d.close)} kWh`;
      const cross = $("#market-crosshair");
      cross.setAttribute("x1", x(i) + w / 2);
      cross.setAttribute("x2", x(i) + w / 2);
      cross.setAttribute("opacity", 0.8);
    };
  }
  function details() {
    for (const view of ["stations", "orders"]) {
      if ($(`#${view}-live-dialog`)) continue;
      const panel = $(`#view-${view}`),
        ledger = $(`.${view === "stations" ? "station" : "order"}-ledger`);
      const dialog = document.createElement("dialog");
      dialog.id = `${view}-live-dialog`;
      dialog.className = "history-dialog";
      dialog.innerHTML =
        '<button class="history-close" aria-label="关闭明细">×</button>';
      dialog.append(ledger);
      panel.append(dialog);
      $(".history-close", dialog).onclick = () => dialog.close();
      const button = document.createElement("button");
      button.className = "live-details";
      button.textContent = "平台实时明细 ↗";
      button.onclick = () => dialog.showModal();
      $(`.${view === "stations" ? "station" : "order"}-left`).append(button);
    }
  }
  function scene() {
    const station = $(".station-left");
    if (!$(".focus-world", station)) {
      station.insertAdjacentHTML(
        "afterbegin",
        '<div class="focus-world" aria-hidden="true"><img src="assets/energy-globe.svg" alt=""/><div class="focus-sweep"></div></div><button class="focus-back" title="切换聚焦尺度">↗</button><span class="focus-caption">CHINA / SHENZHEN · 区域聚焦</span>',
      );
      $(".focus-back").onclick = () => station.classList.toggle("wide-focus");
    }
    let market = $(".market-scene");
    if (!market) {
      market = document.createElement("div");
      market.className = "market-scene";
      market.innerHTML = `<div class="market-heading"><div><small>URBANEV / ENERGY MARKET</small><h2>充电负荷行情 <em>日 K</em></h2></div><div class="market-ranges">${[30, 90, 181].map((n) => `<button data-days="${n}" class="${n === range ? "active" : ""}">${n === 181 ? "全部" : n + "日"}</button>`).join("")}</div></div><div class="market-tickers">${dataset.ranking
        .slice(0, 5)
        .map(
          (x) =>
            `<span>TAZ ${x.zone}<b>${number(x.energy / 1000)} <small>MWh</small></b><i>容量 ${x.capacity}</i></span>`,
        )
        .join(
          "",
        )}</div><p id="market-readout">${dataset.start} — ${dataset.end} · 每日首 / 末小时充电量为开 / 收</p><div class="market-legend"><span>● 负荷上升</span><span>● 负荷下降</span><span>― MA5</span><span>― MA20</span></div><svg id="market-canvas" viewBox="0 0 960 490" preserveAspectRatio="none" aria-label="历史充电负荷日K线及日累计充电量"></svg><div class="market-bottom"><div><span>24H 平均负荷</span>${line(dataset.hourly.map((x) => x.energy))}</div><div><span>日平均占用数量</span>${columns(dataset.daily.slice(-60).map((x) => x.occupied))}</div></div><footer>UrbanEV 历史充电数据 · kWh · 非实时结算订单</footer>`;
      $(".order-left").append(market);
      $$(".market-ranges button").forEach(
        (b) =>
          (b.onclick = () => {
            range = Number(b.dataset.days);
            $$(".market-ranges button").forEach((x) =>
              x.classList.toggle("active", x === b),
            );
            chart();
          }),
      );
    }
    chart();
  }
  function sync() {
    if (!dataset) return;
    const d = dataset,
      h = d.hourly,
      average = h.reduce((a, b) => a + b.occupancy, 0) / 24;
    $$(
      '#view-overview [data-kpi="stations"], #view-stations [data-kpi="stations"]',
    ).forEach((e) => (e.textContent = number(d.stations)));
    $('#view-overview [data-kpi="chargers"]').textContent = number(d.chargers);
    $("#hero-energy").textContent = number(
      d.daily.reduce((a, b) => a + b.energy, 0),
    );
    $(".global-kpis>p").textContent = "历史累计充电量";
    $("#hero-utilization").textContent = average.toFixed(1) + "%";
    $("#hero-utilization").nextElementSibling.textContent = "历史平均占用率";
    $("#globe-available").textContent = number(d.chargers);
    $("#globe-available").nextElementSibling.textContent = "把充电桩";
    renderCompact("#platform-bars", [
      ["站点", d.stations],
      ["设备", d.chargers],
      ["区域", d.zones],
    ]);
    renderCompact(
      "#city-bars",
      d.ranking.slice(0, 5).map((x) => ["TAZ " + x.zone, x.capacity]),
    );
    $(".left-breakdowns .viz-panel:nth-child(2) h2").textContent =
      "区域容量分布";
    renderTrend(
      h.map((x) => ({
        hour: x.hour,
        energy_kwh: x.energy,
        occupancy_rate: x.occupancy,
      })),
    );
    $("#network-devices").textContent = number(d.chargers);
    $("#network-available").textContent = number(d.zones);
    $("#network-available").previousElementSibling.textContent = "观测区域";
    $("#network-available").nextElementSibling.textContent = "个区域";
    $("#network-share").textContent = average.toFixed(1) + "%";
    $("#network-share").previousElementSibling.textContent = "历史平均占用率";
    $(".station-left .telemetry-hero>small").textContent =
      "STATION NETWORK / 城市充电站态势";
    $(".region-selected>small").textContent = "平台演示站点 · 实时状态";
    $("#mode-label").textContent = "演示管理员 · 已登录";
    $(".screen-footer").children[1].textContent =
      "UrbanEV 历史研究数据 · 平台演示站点独立展示 · 管理员演示会话";
  }
  async function refresh() {
    try {
      let token = sessionStorage.getItem("charging_demo_token");
      if (!token) {
        const response = await fetch("/auth/demo", { method: "POST" });
        if (!response.ok) return;
        const session = await response.json();
        token = session.access_token;
        sessionStorage.setItem("charging_demo_token", token);
      }
      let response = await fetch("/demo/analytics", {
        headers: { Authorization: `Bearer ${token}` },
      });
      if (response.status === 401) {
        sessionStorage.removeItem("charging_demo_token");
        return refresh();
      }
      if (!response.ok) throw Error("analytics unavailable");
      dataset = await response.json();
      if (!dataset.ready) return;
      document.body.classList.add("history-ready");
      $("#mode-label").textContent = "演示管理员 · 已登录";
      $("#dashboard-date").textContent =
        `${dataset.start} — ${dataset.end} · URBANEV 历史观测`;
      renderMatrices();
      scene();
      details();
      sync();
    } catch (e) {
      console.error("UrbanEV dashboard:", e);
      toast("历史数据暂时无法加载，请刷新重试");
    }
  }
  return { refresh, sync };
})();
historicalScreen.refresh();
