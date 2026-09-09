const $ = (s, r = document) => r.querySelector(s),
  $$ = (s, r = document) => [...r.querySelectorAll(s)];
const SHENZHEN = [22.5431, 114.0579];
const fallbackStations = [
  {
    id: "fallback-1",
    name: "深圳湾充电站",
    address: "深圳市 · UrbanEV 预览站点",
    latitude: 22.519,
    longitude: 113.944,
    distance: 11.9,
    power: 120,
    time: 18,
    free: 12,
    total: 20,
    price: 1.28,
  },
  {
    id: "fallback-2",
    name: "福田中心充电站",
    address: "深圳市 · UrbanEV 预览站点",
    latitude: 22.535,
    longitude: 114.055,
    distance: 1.2,
    power: 120,
    time: 18,
    free: 8,
    total: 16,
    price: 1.36,
  },
  {
    id: "fallback-3",
    name: "罗湖口岸充电站",
    address: "深圳市 · UrbanEV 预览站点",
    latitude: 22.531,
    longitude: 114.116,
    distance: 6,
    power: 60,
    time: 28,
    free: 6,
    total: 12,
    price: 1.44,
  },
];
const demoOrders = [];
let stations = [],
  activeStation = 0,
  chargers = [],
  selectedCharger = null,
  activeOrder = null,
  chargeTimer,
  toastTimer,
  map,
  markers = [],
  onlyAvailable = false,
  lastPosition = null,
  mapFitted = false;
const token = () => localStorage.getItem("charging_user_token");
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
function toast(text) {
  const el = $("#toast");
  el.textContent = text;
  el.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.remove("show"), 2200);
}
function uuid() {
  return crypto.randomUUID
    ? crypto.randomUUID()
    : "00000000-0000-4000-8000-" +
        Date.now().toString().padStart(12, "0").slice(-12);
}
async function api(path, options = {}) {
  const headers = {
    "Content-Type": "application/json",
    ...(options.headers || {}),
  };
  if (token()) headers.Authorization = `Bearer ${token()}`;
  const response = await fetch(path, { ...options, headers });
  const data = response.status === 204 ? null : await response.json();
  if (!response.ok) throw new Error(data?.error?.message || "请求失败");
  return data;
}
function showScreen(name) {
  $$(".mobile-screen").forEach((x) =>
    x.classList.toggle("active", x.id === `screen-${name}`),
  );
  $$(".tab-bar button").forEach((x) =>
    x.classList.toggle("active", x.dataset.screen === name),
  );
  const immersive = ["detail", "charge", "map", "schedule"].includes(name);
  $(".phone-app").classList.toggle("immersive", immersive);
  $(".tab-bar").style.display = immersive ? "none" : "grid";
  if (name === "map") requestAnimationFrame(fitMap);
  if (name === "orders") loadOrders();
  if (name === "profile") loadProfile();
  if (name === "charge" && !activeOrder)
    $("#charge-state").textContent = "等待连接";
  if (typeof referenceApp !== "undefined") referenceApp.onScreen(name);
}
function normalizeStation(row) {
  const power = Number(row.max_power_kw || 120),
    distance = Number(row.distance_km ?? 0);
  return {
    ...row,
    latitude: Number(row.latitude),
    longitude: Number(row.longitude),
    distance,
    power,
    time: power >= 120 ? 18 : power >= 60 ? 28 : 50,
    free: Number(row.available_count ?? 0),
    total: Number(row.charger_count ?? 0),
    price: Number(row.unit_price),
  };
}
function markerIcon(station, selected = false) {
  return L.divIcon({
    className: "ev-marker-wrap",
    html: `<button class="ev-marker ${selected ? "selected" : ""}" aria-label="${escapeHtml(station.name)}"><svg viewBox="0 0 24 24"><path d="M6 3h9v17H6zM8 6h5v5H8zM15 8h2l2 3v6h-2v-5M4 20h13"/></svg><span>${station.free}</span></button>`,
    iconSize: [46, 54],
    iconAnchor: [23, 48],
  });
}
function renderMarkers() {
  markers.forEach((x) => x?.remove());
  markers = [];
  stations.forEach((s, i) => {
    if (onlyAvailable && !s.free) return;
    const marker = L.marker([s.latitude, s.longitude], {
      icon: markerIcon(s, i === activeStation),
    }).addTo(map);
    marker.on("click", () => selectStation(i, true));
    marker.bindTooltip(`${escapeHtml(s.name)} · ${s.free}/${s.total} 空闲`, {
      direction: "top",
      offset: [0, -42],
    });
    markers[i] = marker;
  });
}
function fitMap() {
  if (!map || !$("#screen-map").classList.contains("active")) return;
  map.invalidateSize();
  if (stations.length && !mapFitted) {
    const selected = stations[activeStation];
    map.setView([selected.latitude, selected.longitude], 13);
    map.panBy([0, 110], { animate: false });
    mapFitted = true;
  }
}
let roadDataPromise;
function addRoadMap(target) {
  roadDataPromise ||= fetch("assets/shenzhen-roads.geojson").then(
    (response) => {
      if (!response.ok) throw new Error("Local road map unavailable");
      return response.json();
    },
  );
  target.attributionControl.addAttribution(
    '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>',
  );
  roadDataPromise
    .then((data) => {
      L.geoJSON(data, {
        interactive: false,
        renderer: L.canvas({ padding: 0.3 }),
        style: (feature) => ({
          color: {
            motorway: "#604560",
            trunk: "#584156",
            primary: "#493447",
            secondary: "#392b3b",
            tertiary: "#29222e",
          }[feature.properties.highway],
          weight: {
            motorway: 4,
            trunk: 3.5,
            primary: 3,
            secondary: 2,
            tertiary: 1.2,
          }[feature.properties.highway],
          opacity: 0.85,
        }),
      }).addTo(target);
    })
    .catch(() => {
      L.tileLayer("https://tile.openstreetmap.org/{z}/{x}/{y}.png", {
        maxZoom: 19,
      }).addTo(target);
    });
}
function initMap() {
  map = L.map("leaflet-map", {
    zoomControl: false,
    attributionControl: true,
  }).setView(SHENZHEN, 11);
  addRoadMap(map);
  map.on("locationfound", (event) => {
    lastPosition = event.latlng;
    L.circleMarker(event.latlng, {
      radius: 7,
      color: "#fff",
      weight: 2,
      fillColor: "#a43cff",
      fillOpacity: 1,
    })
      .addTo(map)
      .bindTooltip("我的位置")
      .openTooltip();
    map.flyTo(event.latlng, 14);
    toast("已定位到当前位置");
  });
  map.on("locationerror", () => toast("无法获取定位，请检查浏览器权限"));
}
async function loadStations() {
  try {
    const rows = await api(
      `/public/stations?latitude=${SHENZHEN[0]}&longitude=${SHENZHEN[1]}`,
    );
    stations = rows
      .filter((x) => x.source_dataset === "UrbanEV")
      .map(normalizeStation);
    if (!stations.length) stations = rows.map(normalizeStation);
    if (!stations.length) throw new Error("暂无站点");
    $("#location-name").firstChild.textContent = "深圳市 ";
    renderMarkers();
    selectStation(0);
    renderHomeStations();
    if (typeof referenceApp !== "undefined") referenceApp.onStations();
    fitMap();
  } catch {
    stations = fallbackStations;
    renderMarkers();
    selectStation(0);
    renderHomeStations();
    if (typeof referenceApp !== "undefined") referenceApp.onStations();
    fitMap();
    toast("API 暂不可用，显示离线预览站点");
  }
}
function selectStation(index, pan = false) {
  if (!stations.length) return;
  activeStation = +index;
  const s = stations[activeStation];
  renderMarkers();
  if (pan) map.flyTo([s.latitude, s.longitude], Math.max(map.getZoom(), 13));
  $("#station-name").textContent = s.name;
  $("#station-address").textContent =
    `${s.address} · ${s.distance.toFixed(1)} km`;
  $("#station-power").textContent = `${s.power} kW`;
  $("#station-time").textContent = `${s.time} 分钟`;
  $("#station-free").textContent = `${s.free} / ${s.total}`;
  $("#detail-station-name").textContent = s.name;
  $("#detail-station-address").textContent = `${s.address} · 营业中`;
  $("#detail-free").textContent = s.free;
  $("#unit-price").textContent = s.price.toFixed(2);
  if (typeof referenceApp !== "undefined") referenceApp.onStation(s);
}
function renderChargers(kind = "all") {
  const list = chargers.filter((c) => kind === "all" || c.kind === kind);
  if (typeof referenceApp !== "undefined") referenceApp.onConnector();
  $("#connector-list").innerHTML = list.length
    ? list
        .map(
          (c) =>
            `<button class="connector ${c.status !== "available" ? "busy" : ""} ${selectedCharger?.id === c.id ? "selected" : ""}" data-id="${c.id}" ${c.status !== "available" ? "disabled" : ""}><span><svg viewBox="0 0 24 24"><path d="M7 3h8v10a4 4 0 01-8 0zM10 7h2M15 6h2a2 2 0 012 2v9a2 2 0 002 2"/></svg></span><div><h3>${escapeHtml(c.code)} · ${c.kind === "fast" ? "直流快充" : "交流慢充"}</h3><p>${Number(c.power_kw)} kW · ${c.kind === "fast" ? "预计 18 分钟充满" : "适合长时停放"}</p></div><em>${c.status === "available" ? "空闲" : "使用中"}</em></button>`,
        )
        .join("")
    : '<p class="empty-state">当前分类暂无充电枪</p>';
  $$(".connector").forEach(
    (x) =>
      (x.onclick = () => {
        selectedCharger = chargers.find((c) => c.id === x.dataset.id);
        renderChargers(kind);
      }),
  );
}
async function openDetail() {
  if (!stations.length) return;
  showScreen("detail");
  const s = stations[activeStation];
  try {
    if (s.id.startsWith("fallback")) throw new Error("离线站点");
    chargers = await api(`/public/stations/${s.id}/chargers`);
    selectedCharger = chargers.find((x) => x.status === "available") || null;
    renderChargers();
  } catch {
    chargers = [];
    selectedCharger = null;
    renderChargers();
    toast("该站点当前无法读取充电枪");
  }
}
async function reserve() {
  if (!token()) {
    $("#login-dialog").showModal();
    return toast("请先登录并充值后预约");
  }
  if (!selectedCharger || selectedCharger.status !== "available")
    return toast("请选择空闲充电枪");
  const s = stations[activeStation];
  try {
    activeOrder = await api("/orders", {
      method: "POST",
      body: JSON.stringify({
        charger_id: selectedCharger.id,
        idempotency_key: uuid(),
      }),
    });
    activeOrder = await api(`/orders/${activeOrder.id}/start`, {
      method: "POST",
    });
    toast("预约成功，已开始充电");
  } catch (e) {
    return toast(e.message);
  }
  $("#charge-station").textContent = s.name;
  $("#charge-power").textContent = `${selectedCharger.power_kw} kW`;
  $("#charge-code").textContent =
    `${selectedCharger.code} · ${selectedCharger.power_kw} kW ${selectedCharger.kind === "fast" ? "快充" : "慢充"}`;
  showScreen("charge");
  startCharge();
}
function startCharge() {
  clearInterval(chargeTimer);
  let percent = 36,
    polling = false;
  $("#charge-state").textContent = "正在充电";
  $("#stop-button").innerHTML = "<span></span>结束充电";
  $("#home-charge-state").textContent = "正在充电";
  $("#home-charge-hint").textContent = "点击查看当前充电进度";
  $("#stop-button").disabled = false;
  function paint() {
    $("#charge-percent").innerHTML = `${Math.floor(percent)}<sup>%</sup>`;
    $("#charge-energy").textContent = Number(
      activeOrder?.energy_kwh ?? 0,
    ).toFixed(1);
    $("#charge-cost").textContent = Number(activeOrder?.amount ?? 0).toFixed(2);
    $("#charge-minutes").textContent = Math.max(
      1,
      Math.ceil((100 - percent) * 0.36),
    );
    $("#charge-bar").style.width = `${percent}%`;
  }
  paint();
  chargeTimer = setInterval(async () => {
    if (polling || !activeOrder) return;
    polling = true;
    try {
      activeOrder = await api(`/orders/${activeOrder.id}`);
      percent = Math.min(99, percent + 0.2);
      paint();
      $("#charge-state").textContent =
        activeOrder.status === "completed" ? "充电已完成" : activeOrder.status === "pending_payment" ? "账单待支付" : "正在充电";
      if(activeOrder.status === "pending_payment")$("#stop-button").textContent="确认支付";
      if (activeOrder.status === "completed") {
        clearInterval(chargeTimer);
        $("#stop-button").disabled = true;
      }
    } catch {
      $("#charge-state").textContent = "正在重新连接";
    } finally {
      polling = false;
    }
  }, 2000);
}
async function stopCharge() {
  if (!activeOrder) return showScreen("map");
  try {
    const action=activeOrder.status==="pending_payment"?"pay":"stop";
    if(action==="pay"&&!confirm(`确认支付 ¥${Number(activeOrder.amount).toFixed(2)}？`))return;
    activeOrder = await api(`/orders/${activeOrder.id}/${action}`, {
      method: "POST",
    });
    clearInterval(chargeTimer);
    if(activeOrder.status==="pending_payment"){
      $("#charge-state").textContent="充电已停止，账单待支付";$("#stop-button").textContent="确认支付";$("#stop-button").disabled=false;
      toast("费用已固定，请确认付款");return;
    }
    $("#charge-state").textContent = "充电已完成";
    $("#home-charge-state").textContent = "充电已完成";
    $("#home-charge-hint").textContent = "在充电记录中查看结算详情";
    $("#stop-button").disabled = true;
    toast(`充电结束，结算 ¥${Number(activeOrder.amount).toFixed(2)}`);
    setTimeout(() => showScreen("orders"), 900);
  } catch (e) {
    toast(e.message);
  }
}
$("#mobile-orders").addEventListener("click",async event=>{
 const id=event.target.closest("[data-pay-order]")?.dataset.payOrder;if(!id)return;
 try{activeOrder=await api("/orders/"+id);await stopCharge();await loadOrders();}catch(e){toast(e.message);}
});
function renderOrders(rows) {
  const unresolved=rows.find(x=>["reserved","charging","pending_payment"].includes(x.status));
  if(unresolved){activeOrder=unresolved;$("#home-charge-state").textContent=unresolved.status==="pending_payment"?"账单待支付":"有未完成订单";}
  $("#history-energy").textContent = rows
    .reduce((sum, x) => sum + Number(x.energy_kwh || 0), 0)
    .toFixed(1);
  $("#history-cost").textContent = rows
    .reduce((sum, x) => sum + Number(x.amount || 0), 0)
    .toFixed(2);
  $("#mobile-orders").innerHTML = rows.length
    ? rows
        .map(
          (x) =>
            `<article class="order-item">${x.status==="pending_payment"?`<button data-pay-order="${escapeHtml(x.id)}">支付账单</button>`:""}<span><svg viewBox="0 0 24 24"><path d="m13 2-7 12h6l-1 8 7-12h-6z"/></svg></span><div><h3>${escapeHtml(x.station_name)}</h3><p>${escapeHtml(x.charger_code)} · ${new Date(x.ended_at || x.created_at || Date.now()).toLocaleDateString("zh-CN")}</p></div><em>¥${Number(x.amount || 0).toFixed(2)}<small>${Number(x.energy_kwh || 0).toFixed(1)} kWh</small></em></article>`,
        )
        .join("")
    : '<p class="empty-state">暂无充电记录</p>';
}
async function loadOrders() {
  if (!token()) return renderOrders([]);
  try {
    renderOrders(await api("/me/orders?limit=20"));
  } catch {
    renderOrders([]);
  }
}
async function loadProfile() {
  if (!token()) return;
  try {
    const me = await api("/me");
    $("#profile-name").textContent = me.nickname;
    $("#profile-phone").textContent = me.phone.replace(
      /(\d{3})\d{4}(\d{4})/,
      "$1 **** $2",
    );
    $("#balance").textContent = Number(me.balance).toFixed(2);
    $("#login-button").textContent = "已连接";
  } catch {
    localStorage.removeItem("charging_user_token");
  }
}
async function sendCode() {
  const phone = $("#phone-input").value.trim();
  try {
    const data = await api("/auth/otp/request", {
      method: "POST",
      body: JSON.stringify({ phone }),
    });
    if (data.development_code) $("#code-input").value = data.development_code;
    toast("验证码已发送");
  } catch (e) {
    toast(e.message);
  }
}
async function verifyCode() {
  try {
    const data = await api("/auth/otp/verify", {
      method: "POST",
      body: JSON.stringify({
        phone: $("#phone-input").value.trim(),
        code: $("#code-input").value.trim(),
      }),
    });
    localStorage.setItem("charging_user_token", data.access_token);
    $("#login-dialog").close();
    await loadProfile();
    toast("账户连接成功，请先充值");
  } catch (e) {
    toast(e.message);
  }
}
async function recharge() {
  if (!token()) {
    $("#recharge-dialog").close();
    $("#login-dialog").showModal();
    return toast("请先登录账户");
  }
  const amount = +$(".amount-grid button.active").textContent;
  try {
    const data = await api("/wallet/recharges", {
      method: "POST",
      body: JSON.stringify({ amount, idempotency_key: uuid() }),
    });
    $("#balance").textContent = Number(data.balance_after).toFixed(2);
    $("#recharge-dialog").close();
    toast(`充值 ¥${amount} 成功`);
  } catch (e) {
    toast(e.message);
  }
}
function route() {
  const s = stations[activeStation];
  if (!s) return;
  const center = map.getCenter(),
    from = lastPosition
      ? `${lastPosition.lat},${lastPosition.lng}`
      : `${center.lat},${center.lng}`;
  window.open(
    `https://www.openstreetmap.org/directions?engine=fossgis_osrm_car&route=${encodeURIComponent(from)}%3B${s.latitude}%2C${s.longitude}`,
    "_blank",
    "noopener",
  );
}
function renderHomeStations() {
  $("#home-stations").innerHTML = stations
    .slice(0, 2)
    .map(
      (station, index) =>
        `<button class="nearby-station" data-nearby="${index}"><span>ϟ</span><span><b>${escapeHtml(station.name)}</b><small>${station.distance.toFixed(1)} km · ${station.power} kW 快充${station.id.startsWith("fallback") ? " · 离线预览" : ""}</small></span><em>¥${station.price.toFixed(2)}<small>${station.free} 个空闲 / ${station.total}</small></em></button>`,
    )
    .join("");
  $$("[data-nearby]").forEach(
    (button) =>
      (button.onclick = () => {
        selectStation(Number(button.dataset.nearby));
        openDetail();
      }),
  );
}
function orb() {
  if (matchMedia("(prefers-reduced-motion: reduce)").matches) return;
  const c = $("#orb-canvas"),
    ctx = c.getContext("2d"),
    points = 180;
  function frame(t) {
    if (!$("#screen-charge").classList.contains("active")) {
      requestAnimationFrame(frame);
      return;
    }
    const size = c.clientWidth * devicePixelRatio;
    if (c.width !== size) c.width = c.height = size;
    ctx.clearRect(0, 0, size, size);
    ctx.save();
    ctx.translate(size / 2, size / 2);
    for (let layer = 0; layer < 38; layer++) {
      ctx.beginPath();
      for (let i = 0; i <= points; i++) {
        const a = (i / points) * Math.PI * 2,
          r =
            size * (0.3 + layer * 0.0013) +
            Math.sin(a * 7 + t / 850 + layer * 0.24) * size * 0.022 +
            Math.cos(a * 3 - t / 1100 + layer * 0.13) * size * 0.016;
        i
          ? ctx.lineTo(Math.cos(a) * r, Math.sin(a) * r)
          : ctx.moveTo(Math.cos(a) * r, Math.sin(a) * r);
      }
      ctx.closePath();
      ctx.strokeStyle = `hsla(${265 + layer * 1.7},100%,${60 + layer * 0.8}%,${0.55 - layer * 0.011})`;
      ctx.lineWidth = (0.9 - layer * 0.013) * devicePixelRatio;
      ctx.shadowBlur = 13 * devicePixelRatio;
      ctx.shadowColor = layer % 2 ? "#f25dff" : "#7135ff";
      ctx.stroke();
    }
    ctx.restore();
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
}
$$("[data-screen]").forEach(
  (x) => (x.onclick = () => showScreen(x.dataset.screen)),
);
$$("[data-back]").forEach(
  (x) => (x.onclick = () => showScreen(x.dataset.back)),
);
$("#detail-button").onclick = openDetail;
$("#reserve-button").onclick = reserve;
$("#stop-button").onclick = stopCharge;
$("#route-button").onclick = route;
$("#locate").onclick = () => map.locate({ enableHighAccuracy: true });
$$("[data-zoom]").forEach(
  (x) =>
    (x.onclick = () =>
      x.dataset.zoom === "in" ? map.zoomIn() : map.zoomOut()),
);
$(".sheet-handle").onclick = () =>
  $("#station-sheet").classList.toggle("expanded");
$(".favorite").onclick = (e) => e.currentTarget.classList.toggle("on");
$("#map-search").oninput = (e) => {
  const q = e.target.value.trim();
  const i = stations.findIndex(
    (s) => s.name.includes(q) || s.address.includes(q),
  );
  if (i >= 0) selectStation(i, true);
};
$$(".segment [data-kind]").forEach(
  (x) =>
    (x.onclick = () => {
      $$(".segment [data-kind]").forEach((b) =>
        b.classList.toggle("active", b === x),
      );
      renderChargers(x.dataset.kind);
    }),
);
$("#login-button").onclick = () => $("#login-dialog").showModal();
$("#recharge-button").onclick = () =>
  token() ? $("#recharge-dialog").showModal() : $("#login-dialog").showModal();
$$("[data-close]").forEach(
  (x) => (x.onclick = () => x.closest("dialog").close()),
);
$("#send-code").onclick = sendCode;
$("#verify-code").onclick = verifyCode;
$$(".amount-grid button").forEach(
  (x) =>
    (x.onclick = () => {
      $$(".amount-grid button").forEach((b) =>
        b.classList.toggle("active", b === x),
      );
      $("#confirm-recharge").textContent = `确认充值 ¥${x.textContent}`;
    }),
);
$("#confirm-recharge").onclick = recharge;
$("#refresh-orders").onclick = () => {
  loadOrders();
  toast("记录已刷新");
};
$("#filter-button").onclick = () => {
  onlyAvailable = !onlyAvailable;
  $("#filter-button").classList.toggle("active", onlyAvailable);
  renderMarkers();
  toast(onlyAvailable ? "仅显示有空闲枪的站点" : "显示全部站点");
};
$("#settings-button").onclick = () => toast("设置功能将在后续版本开放");
initMap();
loadStations();
renderOrders(demoOrders);
orb();
loadProfile();
const initialScreen = new URLSearchParams(location.search).get("screen");
if (["home", "map", "charge", "orders", "profile"].includes(initialScreen))
  showScreen(initialScreen);
