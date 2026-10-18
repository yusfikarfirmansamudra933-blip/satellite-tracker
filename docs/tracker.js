// Pelacak satelit online: hitung posisi dan lintasan satelit di browser (satellite.js, SGP4/SDP4).
(() => {
  const S = window.satellite;
  const $ = (id) => document.getElementById(id);
  const DEG = 180 / Math.PI;
  const TLE_CACHE_MS = 6 * 3600 * 1000;  // jangan membebani CelesTrak: unduh ulang paling cepat tiap 6 jam

  // ---------- Penyimpanan lokal (aman jika diblokir) ----------
  const store = {
    get(k) { try { return JSON.parse(localStorage.getItem(k)); } catch { return null; } },
    set(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch { /* abaikan */ } },
  };

  // ---------- Status ----------
  let obs = store.get("loc") || { lat: -6.2, lon: 106.8166, src: "default (Jakarta)" };
  let sat = null;      // { norad, name, l1, l2, satrec, geo, fetched }
  let passes = [];
  let path = [];       // jalur lintasan berikutnya: [{az, el}]

  const observerGd = () => ({ latitude: obs.lat / DEG, longitude: obs.lon / DEG, height: 0.05 });

  // ---------- Matahari (presisi rendah, cukup untuk visibilitas) ----------
  function sunEci(date) {
    const n = date / 86400000 + 2440587.5 - 2451545.0;
    const g = (357.528 + 0.9856003 * n) / DEG;
    const lambda = (280.46 + 0.9856474 * n + 1.915 * Math.sin(g) + 0.02 * Math.sin(2 * g)) / DEG;
    const eps = (23.439 - 0.0000004 * n) / DEG;
    const r = (1.00014 - 0.01671 * Math.cos(g) - 0.00014 * Math.cos(2 * g)) * 149597870.7;
    return { x: r * Math.cos(lambda), y: r * Math.cos(eps) * Math.sin(lambda), z: r * Math.sin(eps) * Math.sin(lambda) };
  }

  function lookAngles(eci, gmst) {
    const la = S.ecfToLookAngles(observerGd(), S.eciToEcf(eci, gmst));
    return { az: ((la.azimuth * DEG) % 360 + 360) % 360, el: la.elevation * DEG, range: la.rangeSat };
  }

  // Satelit terkena sinar matahari? (model bayangan silinder bumi)
  function sunlit(p, sun) {
    const len = Math.hypot(sun.x, sun.y, sun.z);
    const s = { x: sun.x / len, y: sun.y / len, z: sun.z / len };
    const d = p.x * s.x + p.y * s.y + p.z * s.z;
    if (d > 0) return true;
    const perp = Math.hypot(p.x - d * s.x, p.y - d * s.y, p.z - d * s.z);
    return perp > 6378;
  }

  function lookAt(t) {
    const date = new Date(t);
    const pv = S.propagate(sat.satrec, date);
    if (!pv || !pv.position) return null;
    const gmst = S.gstime(date);
    const la = lookAngles(pv.position, gmst);
    const gd = S.eciToGeodetic(pv.position, gmst);
    const v = pv.velocity;
    return {
      ...la,
      lat: S.degreesLat(gd.latitude),
      lon: S.degreesLong(gd.longitude),
      alt: gd.height,
      speed: Math.hypot(v.x, v.y, v.z),
      eci: pv.position,
      gmst,
    };
  }

  const elAt = (t) => { const p = lookAt(t); return p ? p.el : -90; };

  // ---------- Prediksi lintasan ----------
  // Cari titik naik/turun di antara a (di bawah) dan b (di atas) dengan bisection
  function edge(a, b, rising) {
    for (let i = 0; i < 14; i++) {
      const m = (a + b) / 2;
      if ((elAt(m) > 0) === rising) b = m; else a = m;
    }
    return (a + b) / 2;
  }

  function buildPass(rise, set) {
    const n = 60;
    let maxEl = -90, peak = rise, visible = false;
    const pts = [];
    for (let i = 0; i <= n; i++) {
      const t = rise + ((set - rise) * i) / n;
      const p = lookAt(t);
      if (!p) continue;
      pts.push({ az: p.az, el: Math.max(0, p.el) });
      if (p.el > maxEl) { maxEl = p.el; peak = t; }
      if (!visible && p.el > 5) {
        const sun = sunEci(t);
        const sunEl = lookAngles(sun, p.gmst).el;
        if (sunEl < -6 && sunlit(p.eci, sun)) visible = true;
      }
    }
    return { rise, set, peak, maxEl, azRise: pts[0].az, azSet: pts[pts.length - 1].az, visible, pts };
  }

  function predict(now) {
    passes = [];
    path = [];
    if (!sat || sat.geo) return;
    const step = 30000;
    let t = now - 20 * 60000;
    const end = now + 3 * 86400000;
    let up = elAt(t) > 0;
    let rise = null;
    if (up) {  // lintasan sudah berjalan: cari titik naiknya ke belakang
      let b = t;
      while (elAt(b) > 0 && t - b < 3600000) b -= step;
      rise = edge(b, b + step, true);
    }
    while (t < end && passes.length < 6) {
      const t2 = t + step;
      const up2 = elAt(t2) > 0;
      if (!up && up2) rise = edge(t, t2, true);
      if (up && !up2 && rise !== null) {
        const set = edge(t, t2, false);
        if (set > now) passes.push(buildPass(rise, set));
        rise = null;
      }
      up = up2;
      t = t2;
    }
    if (passes.length) path = passes[0].pts;
  }

  // ---------- Data orbit ----------
  async function loadTle(norad) {
    const key = "tle" + norad;
    const cached = store.get(key);
    if (cached && Date.now() - cached.fetched < TLE_CACHE_MS) return cached;
    try {
      const res = await fetch(`https://celestrak.org/NORAD/elements/gp.php?CATNR=${norad}&FORMAT=tle`);
      if (!res.ok) throw new Error("HTTP " + res.status);
      const lines = (await res.text()).split(/\r?\n/).map((l) => l.trim()).filter(Boolean);
      if (lines.length < 3 || !lines[1].startsWith("1 ") || !lines[2].startsWith("2 ")) {
        throw new Error("Satelit dengan NORAD ID " + norad + " tidak ditemukan");
      }
      const tle = { norad, name: lines[0], l1: lines[1], l2: lines[2], fetched: Date.now() };
      store.set(key, tle);
      return tle;
    } catch (e) {
      if (cached) { setStatus("Gagal memperbarui data orbit, memakai data tersimpan."); return cached; }
      throw e;
    }
  }

  async function selectSat(norad) {
    norad = parseInt(norad, 10);
    if (!(norad > 0)) return;
    setStatus("Mengunduh data orbit…");
    try {
      const tle = await loadTle(norad);
      const satrec = S.twoline2satrec(tle.l1, tle.l2);
      if (satrec.error) throw new Error("Data orbit tidak bisa dihitung");
      const revPerDay = (satrec.no * 1440) / (2 * Math.PI);
      sat = { ...tle, satrec, geo: revPerDay < 1.5 };
      store.set("norad", norad);
      $("preset").value = [...$("preset").options].some((o) => o.value == norad) ? String(norad) : "";
      predict(Date.now());
      setStatus("");
      render();
    } catch (e) {
      setStatus("⚠️ " + e.message);
    }
  }

  // ---------- Tampilan ----------
  function setStatus(msg) { $("status").textContent = msg; }

  const DIRS = ["Utara", "Timur Laut", "Timur", "Tenggara", "Selatan", "Barat Daya", "Barat", "Barat Laut"];
  const SHORT = ["U", "TL", "T", "TG", "S", "BD", "B", "BL"];
  const dirName = (az) => DIRS[Math.round(az / 45) % 8];
  const dirShort = (az) => SHORT[Math.round(az / 45) % 8];

  function duration(ms) {
    let s = Math.max(0, Math.round(ms / 1000));
    const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60);
    s %= 60;
    if (h) return `${h} j ${m} m`;
    if (m) return `${m} m ${s} d`;
    return `${s} d`;
  }

  const timeFmt = new Intl.DateTimeFormat("id-ID", { weekday: "short", day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" });

  function renderPasses() {
    const body = $("passes");
    if (!sat) return;
    if (sat.geo) {
      body.innerHTML = '<tr><td colspan="5" class="muted">Satelit geostasioner: posisinya tetap di langit, tidak ada lintasan.</td></tr>';
      return;
    }
    if (!passes.length) {
      body.innerHTML = '<tr><td colspan="5" class="muted">Tidak ada lintasan di atas lokasimu dalam 3 hari ke depan.</td></tr>';
      return;
    }
    body.innerHTML = passes.map((p) => `
      <tr class="${p.visible ? "vis" : ""}">
        <td>${timeFmt.format(new Date(p.rise))}</td>
        <td>${duration(p.set - p.rise)}</td>
        <td>${p.maxEl.toFixed(0)}°</td>
        <td>${dirShort(p.azRise)} → ${dirShort(p.azSet)}</td>
        <td>${p.visible ? "👁" : ""}</td>
      </tr>`).join("");
  }

  // Peta langit: tengah = zenith, lingkaran luar = horizon, utara di atas
  const cv = $("sky");
  const ctx = cv.getContext("2d");
  function setupCanvas() {
    const dpr = window.devicePixelRatio || 1;
    const size = cv.clientWidth || 380;
    cv.width = size * dpr;
    cv.height = size * dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  }

  function drawSky(cur, sunPos) {
    const size = cv.clientWidth || 380, C = size / 2, R = C - 22;
    const xy = (az, el) => {
      const r = (R * (90 - Math.max(el, 0))) / 90, a = az / DEG;
      return [C + r * Math.sin(a), C - r * Math.cos(a)];
    };
    ctx.clearRect(0, 0, size, size);
    ctx.fillStyle = "#05080f";
    ctx.beginPath(); ctx.arc(C, C, R, 0, 2 * Math.PI); ctx.fill();
    ctx.strokeStyle = "#1f2c44"; ctx.lineWidth = 1;
    for (const e of [0, 30, 60]) { ctx.beginPath(); ctx.arc(C, C, (R * (90 - e)) / 90, 0, 2 * Math.PI); ctx.stroke(); }
    ctx.beginPath(); ctx.moveTo(C, C - R); ctx.lineTo(C, C + R); ctx.moveTo(C - R, C); ctx.lineTo(C + R, C); ctx.stroke();
    ctx.fillStyle = "#93a3bb"; ctx.font = "13px system-ui"; ctx.textAlign = "center"; ctx.textBaseline = "middle";
    [["U", 0], ["T", 90], ["S", 180], ["B", 270]].forEach(([t, a]) => {
      const r = R + 12, ang = a / DEG;
      ctx.fillText(t, C + r * Math.sin(ang), C - r * Math.cos(ang));
    });
    ctx.fillStyle = "#4b5d78"; ctx.font = "11px system-ui";
    ctx.fillText("30°", C + 4, C - (R * 60) / 90 + 9);
    ctx.fillText("60°", C + 4, C - (R * 30) / 90 + 9);

    if (sunPos && sunPos.el > 0) {
      const [x, y] = xy(sunPos.az, sunPos.el);
      ctx.fillStyle = "#ff9e40"; ctx.beginPath(); ctx.arc(x, y, 7, 0, 2 * Math.PI); ctx.fill();
    }
    if (path.length > 1) {
      ctx.strokeStyle = "#4da3ff"; ctx.lineWidth = 2; ctx.beginPath();
      path.forEach((p, i) => { const [x, y] = xy(p.az, p.el); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); });
      ctx.stroke();
      const [x, y] = xy(path[0].az, path[0].el);
      ctx.fillStyle = "#4da3ff"; ctx.beginPath(); ctx.arc(x, y, 4, 0, 2 * Math.PI); ctx.fill();
    }
    if (cur && cur.el > 0) {
      const [x, y] = xy(cur.az, cur.el);
      ctx.fillStyle = "rgba(255,213,79,0.25)"; ctx.beginPath(); ctx.arc(x, y, 14, 0, 2 * Math.PI); ctx.fill();
      ctx.fillStyle = "#ffd54f"; ctx.beginPath(); ctx.arc(x, y, 6, 0, 2 * Math.PI); ctx.fill();
    }
  }

  function render() {
    if (!sat) return;
    const now = Date.now();
    if (!sat.geo && passes.length && now > passes[0].set) predict(now);

    const cur = lookAt(now);
    const sun = sunEci(now);
    const sunPos = cur ? lookAngles(sun, cur.gmst) : null;
    $("satName").textContent = sat.name;

    if (!cur) {
      $("headline").textContent = "Data orbit kedaluwarsa atau satelit sudah jatuh.";
    } else {
      $("az").textContent = `${cur.az.toFixed(1)}° ${dirShort(cur.az)}`;
      $("el").textContent = `${cur.el.toFixed(1)}°`;
      $("range").textContent = `${Math.round(cur.range).toLocaleString("id-ID")} km`;
      $("alt").textContent = `${Math.round(cur.alt).toLocaleString("id-ID")} km`;
      $("speed").textContent = `${cur.speed.toFixed(2)} km/s`;
      $("sub").textContent = `${cur.lat.toFixed(1)}°, ${cur.lon.toFixed(1)}°`;

      let h;
      if (cur.el > 0) {
        const dark = sunPos.el < -6, lit = sunlit(cur.eci, sun);
        h = `Di atas horizon: lihat ke ${dirName(cur.az)}, ${cur.el.toFixed(0)}° dari horizon.`;
        if (!sat.geo && passes.length) h += ` Hilang dalam ${duration(passes[0].set - now)}.`;
        if (!sat.geo && dark && lit) h += " 👁 Bisa dilihat mata!";
      } else if (sat.geo) {
        h = "Satelit geostasioner ini berada di bawah horizon lokasimu.";
      } else if (passes.length) {
        h = `Di bawah horizon. Muncul dalam ${duration(passes[0].rise - now)} dari arah ${dirName(passes[0].azRise)}.`;
      } else {
        h = "Di bawah horizon.";
      }
      $("headline").textContent = h;
    }
    drawSky(cur, sunPos);
    renderPasses();

    const ageDays = (now - (S.twoline2satrec(sat.l1, sat.l2).jdsatepoch - 2440587.5) * 86400000) / 86400000;
    $("tleInfo").textContent = `NORAD ${sat.norad} · umur data orbit ${ageDays.toFixed(1)} hari · diunduh ${new Date(sat.fetched).toLocaleString("id-ID")}`;
  }

  function showLoc() {
    $("lat").value = obs.lat.toFixed(4);
    $("lon").value = obs.lon.toFixed(4);
    $("locInfo").textContent = `Dipakai: ${obs.lat.toFixed(4)}, ${obs.lon.toFixed(4)} (${obs.src})`;
  }

  function setLoc(lat, lon, src) {
    if (!isFinite(lat) || !isFinite(lon) || Math.abs(lat) > 90 || Math.abs(lon) > 180) {
      setStatus("⚠️ Koordinat tidak valid");
      return;
    }
    obs = { lat, lon, src };
    store.set("loc", obs);
    showLoc();
    predict(Date.now());
    render();
  }

  // ---------- Event ----------
  $("preset").addEventListener("change", (e) => selectSat(e.target.value));
  $("useNorad").addEventListener("click", () => selectSat($("norad").value));
  $("norad").addEventListener("keydown", (e) => { if (e.key === "Enter") selectSat($("norad").value); });
  $("saveLoc").addEventListener("click", () => setLoc(parseFloat($("lat").value), parseFloat($("lon").value), "manual"));
  $("gps").addEventListener("click", () => {
    if (!navigator.geolocation) { setStatus("⚠️ Browser tidak mendukung lokasi"); return; }
    setStatus("Mengambil lokasi HP…");
    navigator.geolocation.getCurrentPosition(
      (p) => { setStatus(""); setLoc(p.coords.latitude, p.coords.longitude, "lokasi HP"); },
      () => setStatus("⚠️ Izin lokasi ditolak. Isi lintang/bujur secara manual."),
      { enableHighAccuracy: false, timeout: 15000 }
    );
  });
  window.addEventListener("resize", () => { setupCanvas(); render(); });

  // ---------- Mulai ----------
  if (!S) { setStatus("⚠️ Library satellite.js gagal dimuat. Periksa koneksi internet."); return; }
  setupCanvas();
  showLoc();
  selectSat(new URLSearchParams(location.search).get("norad") || store.get("norad") || 25544);
  setInterval(render, 500);
})();
