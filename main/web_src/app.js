'use strict';

/* Player Pass management console.
 *
 * Everything runs from the device's own flash; there is no build step and no
 * external dependency.  Text is inserted with textContent, images are decoded
 * from the PPIM RGB565 container in the browser, and every upload is produced
 * at the exact dimensions the firmware expects.
 */

const SPECS = {
  avatar: { width: 88, height: 88, crop: 'square' },
  cover: { width: 72, height: 96, crop: 'cover' },
  qr: { width: 180, height: 180, crop: 'contain', padding: 10 },
};

const SLOT_FLAGS = {
  avatar: 'avatar',
  qr_steam: 'steam_qr',
  qr_switch: 'switch_qr',
  qr_psn: 'psn_qr',
  qr_wechat: 'wechat_qr',
  qr_wechat_pay: 'wechat_pay_qr',
};

const FIXED_IMAGE_SLOTS = Object.keys(SLOT_FLAGS);
const BACKUP_FORMAT = 'playerpass-backup';
const BACKUP_VERSION = 1;
const BACKUP_MAX_BYTES = 16 * 1024 * 1024;

const PROFILE_LIMITS = {
  name: 24,
  tagline: 60,
  platform: 40,
  weiboHandle: 40,
  wechatName: 24,
  wechatId: 40,
};

const state = {
  clientId: null,
  token: null,
  canEdit: false,
  occupied: false,
  loaded: false,
  profile: null,
  games: [],
  nfc: null,
  status: null,
  imageCache: new Map(),
  pending: new Map(),
  dirty: { profile: false, games: false, nfc: false },
  editor: { index: -1, isNew: false },
  claimTimer: null,
  heartbeatTimer: null,
  statusTimer: null,
};

const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => Array.from(root.querySelectorAll(selector));

/* ------------------------------- utilities ------------------------------- */

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function uuid() {
  if (window.crypto && crypto.randomUUID) return crypto.randomUUID();
  const bytes = new Uint8Array(16);
  if (window.crypto && crypto.getRandomValues) crypto.getRandomValues(bytes);
  else for (let i = 0; i < bytes.length; i++) bytes[i] = Math.floor(Math.random() * 256);
  bytes[6] = (bytes[6] & 0x0f) | 0x40;
  bytes[8] = (bytes[8] & 0x3f) | 0x80;
  const hex = Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20)}`;
}

function codePoints(text) {
  return Array.from(text || '').length;
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function formatDuration(seconds) {
  const safe = Math.max(0, Math.floor(seconds || 0));
  const m = Math.floor(safe / 60);
  const s = safe % 60;
  return `${m}:${String(s).padStart(2, '0')}`;
}

function formatBytes(bytes) {
  if (!bytes) return '0 MB';
  const mb = bytes / (1024 * 1024);
  return `${mb.toFixed(mb >= 10 ? 0 : 1)} MB`;
}

let toastTimer = null;
function toast(message, type = 'ok') {
  const box = $('#toast');
  box.textContent = message;
  box.className = `toast ${type === 'error' ? 'error' : type === 'warn' ? 'warn' : ''}`;
  box.classList.remove('hidden');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => box.classList.add('hidden'), 3200);
}

let errorTimer = null;
function showError(message) {
  const banner = $('#error-banner');
  $('#error-banner-text').textContent = message;
  banner.classList.remove('hidden');
  clearTimeout(errorTimer);
  errorTimer = setTimeout(() => banner.classList.add('hidden'), 9000);
}

function clearError() {
  $('#error-banner').classList.add('hidden');
}

function setDirty(tab, dirty) {
  state.dirty[tab] = dirty;
  const nodes = tab === 'profile'
    ? [$('#profile-dirty'), $('#contact-dirty')]
    : [$(`#${tab}-dirty`)];
  nodes.forEach(node => {
    if (node) node.textContent = dirty ? '有未保存的更改' : '';
  });
}

function anyDirty() {
  return state.dirty.profile || state.dirty.games || state.dirty.nfc;
}

/* --------------------------------- API ----------------------------------- */

async function api(path, options = {}) {
  const headers = {};
  if (options.body !== undefined) headers['Content-Type'] = 'application/json';
  if (options.token) headers['X-PP-Token'] = options.token;
  let response;
  try {
    response = await fetch(path, {
      method: options.method || 'GET',
      headers,
      body: options.body !== undefined ? JSON.stringify(options.body) : undefined,
      cache: 'no-store',
    });
  } catch (networkError) {
    const error = new Error('无法连接设备，请确认手机仍连接着 PlayerPass 热点');
    error.status = 0;
    throw error;
  }
  const text = await response.text();
  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch (parseError) {
    data = null;
  }
  if (!response.ok) {
    const error = new Error((data && data.message) || `请求失败（HTTP ${response.status}）`);
    error.status = response.status;
    error.data = data;
    throw error;
  }
  return data;
}

function requireEdit() {
  if (state.canEdit && state.token) return true;
  toast('尚未取得编辑权限，请等待或点击“重试取得编辑权限”', 'warn');
  return false;
}

function handleAuthError(error) {
  if (error && error.status === 401) {
    state.canEdit = false;
    state.token = null;
    applyReadOnly(true);
    updateSessionBanner();
    scheduleClaimRetry(1500);
    return true;
  }
  return false;
}

/* ------------------------------- session --------------------------------- */

function clientName() {
  const ua = navigator.userAgent;
  if (/iPhone|iPad|iPod/.test(ua)) return 'iOS 浏览器';
  if (/Android/.test(ua)) return 'Android 浏览器';
  if (/Macintosh/.test(ua)) return 'Mac 浏览器';
  if (/Windows/.test(ua)) return 'Windows 浏览器';
  return '浏览器';
}

function setReadOnly(readonly) {
  document.body.classList.toggle('readonly', readonly);
  const selector = '.editable input, .editable textarea, .editable select, .editable button, .image-card input, .image-card button, .cover-editor input, .cover-editor button, [data-mutation]';
  $$(selector).forEach(node => {
    if (node.hasAttribute('data-readonly-ok')) return;
    node.disabled = readonly;
  });
  $('#retry-claim').disabled = false;
}

function updateChips() {
  const chip = $('#editor-chip');
  if (state.canEdit) {
    chip.textContent = '编辑中';
    chip.className = 'chip editor';
  } else if (state.occupied) {
    chip.textContent = '被占用';
    chip.className = 'chip occupied';
  } else {
    chip.textContent = '只读';
    chip.className = 'chip';
  }
}

function updateSessionBanner() {
  const banner = $('#session-banner');
  if (state.canEdit) {
    banner.classList.add('hidden');
    updateChips();
    return;
  }
  const session = state.status && state.status.session ? state.status.session : null;
  let message = '尚未取得编辑权限。';
  if (session && session.occupied) {
    const who = session.client_name ? `「${session.client_name}」` : '另一个浏览器';
    if (session.grace_remaining_s > 0) {
      message = `${who}已断开，编辑权限为其保留 ${formatDuration(session.grace_remaining_s)}，到期后可接管。`;
    } else {
      message = `设备正被${who}编辑。只有取得编辑权限的浏览器可以修改资料。`;
    }
  }
  $('#session-banner-text').textContent = message;
  banner.classList.remove('hidden');
  updateChips();
}

async function claim() {
  if (state.canEdit && state.token) return;
  if (state.claimTimer) {
    clearTimeout(state.claimTimer);
    state.claimTimer = null;
  }
  try {
    const reply = await api('/api/session/claim', {
      method: 'POST',
      body: { client_id: state.clientId, client_name: clientName() },
    });
    state.token = reply.token;
    state.canEdit = true;
    state.occupied = false;
    if (state.claimTimer) {
      clearTimeout(state.claimTimer);
      state.claimTimer = null;
    }
    clearError();
    // Keep the form disabled until the first data load completes so a fast
    // typist cannot fill fields that are about to be replaced.
    setReadOnly(!state.loaded);
    updateSessionBanner();
    startHeartbeat();
    if (state.loaded) {
      await reloadAll();
    }
  } catch (error) {
    if (error.status === 409) {
      state.occupied = true;
      state.canEdit = false;
      setReadOnly(true);
      if (error.data) {
        state.status = state.status || {};
        state.status.session = {
          occupied: true,
          editor: false,
          client_name: error.data.client_name || '',
          grace_remaining_s: error.data.grace_remaining_s || 0,
          reserved_for_me: !!error.data.reserved_for_me,
        };
      }
      updateSessionBanner();
      scheduleClaimRetry(error.data && error.data.reserved_for_me ? 2500 : 5000);
    } else {
      showError(error.message);
      scheduleClaimRetry(5000);
    }
  }
}

function scheduleClaimRetry(delay) {
  if (state.claimTimer || state.canEdit) return;
  state.claimTimer = setTimeout(() => {
    state.claimTimer = null;
    claim();
  }, delay);
}

function startHeartbeat() {
  if (state.heartbeatTimer) return;
  state.heartbeatTimer = setInterval(async () => {
    if (!state.token) return;
    try {
      await api('/api/session/heartbeat', { method: 'POST', token: state.token });
    } catch (error) {
      if (handleAuthError(error)) return;
      showError(error.message);
    }
  }, 10000);
}

function releaseSession() {
  if (!state.token) return;
  try {
    fetch('/api/session/release', {
      method: 'POST',
      headers: { 'X-PP-Token': state.token },
      keepalive: true,
    });
  } catch (ignored) { /* best effort */ }
}

/* -------------------------------- status --------------------------------- */

async function pollStatus() {
  try {
    const query = `?client=${encodeURIComponent(state.clientId)}`;
    const status = await api(`/api/status${query}`, { token: state.token || undefined });
    state.status = status;
    renderStatus(status);
    const session = status.session || {};
    if (session.editor) {
      if (!state.canEdit) {
        state.canEdit = true;
        setReadOnly(false);
      }
    } else if (state.canEdit && state.token) {
      state.canEdit = false;
      state.token = null;
      setReadOnly(true);
      scheduleClaimRetry(1500);
    }
    state.occupied = !!session.occupied;
    updateSessionBanner();
    $('#conn-dot').className = 'dot on';
    $('#conn-text').textContent = `${status.wifi.ssid} · ${status.wifi.ip}`;
  } catch (error) {
    $('#conn-dot').className = 'dot error';
    $('#conn-text').textContent = '设备未连接';
    if (error.status !== 0) showError(error.message);
  }
}

function renderStatus(status) {
  const wifi = status.wifi || {};
  const storage = status.storage || {};
  const session = status.session || {};
  $('#ov-ssid').textContent = wifi.ssid || '—';
  $('#ov-ip').textContent = wifi.ip || '—';
  $('#ov-url').textContent = `http://${wifi.ip || '192.168.4.1'}`;
  $('#ov-clients').textContent = `${wifi.clients || 0} 台`;
  $('#ov-firmware').textContent = status.firmware || '—';
  $('#ov-uptime').textContent = formatDuration((status.uptime_ms || 0) / 1000);
  $('#ov-session').textContent = session.editor
    ? '本浏览器编辑中'
    : session.occupied
      ? `被${session.client_name ? `「${session.client_name}」` : '其他浏览器'}占用`
      : '等待取得编辑权限';
  $('#ov-lease').textContent = '不自动退出';

  const percent = clamp(storage.percent || 0, 0, 100);
  const bar = $('#ov-storage-bar');
  bar.style.width = `${percent}%`;
  bar.style.background = storage.level >= 2 ? 'var(--error)' : storage.level === 1 ? 'var(--warn)' : 'var(--mint)';
  $('#ov-storage-percent').textContent = `${percent}%`;
  $('#ov-storage-percent').style.color = storage.level >= 2 ? 'var(--error)' : storage.level === 1 ? 'var(--warn)' : 'var(--mint)';
  $('#ov-storage-text').textContent = `${formatBytes(storage.used)} / ${formatBytes(storage.total)}`;
  const warning = $('#ov-storage-warning');
  if (storage.level >= 2) {
    warning.textContent = '存储已超过 90%，请立即删除不用的图片或游戏封面。';
    warning.classList.remove('hidden');
  } else if (storage.level === 1) {
    warning.textContent = '存储已超过 80%，建议清理不用的图片。';
    warning.classList.remove('hidden');
  } else {
    warning.classList.add('hidden');
  }
}

/* ------------------------------ image codec ------------------------------ */

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
    table[n] = c >>> 0;
  }
  return table;
})();

function crc32(bytes) {
  let crc = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) crc = CRC_TABLE[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

function loadImageElement(file) {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(file);
    const image = new Image();
    image.onload = () => {
      URL.revokeObjectURL(url);
      resolve(image);
    };
    image.onerror = () => {
      URL.revokeObjectURL(url);
      reject(new Error('图片无法解码，请换一张 JPG 或 PNG'));
    };
    image.src = url;
  });
}

async function fileToBitmap(file) {
  const typeOk = /^image\/(jpeg|png)$/i.test(file.type || '');
  const nameOk = /\.(jpe?g|png)$/i.test(file.name || '');
  if (!typeOk && !nameOk) throw new Error('仅支持 JPG/PNG，不支持 WebP/GIF');
  if (file.size > 20 * 1024 * 1024) throw new Error('图片过大（超过 20 MB）');
  if (file.type === 'image/gif' || file.type === 'image/webp') {
    throw new Error('不支持 WebP / GIF，请先转换为 JPG 或 PNG');
  }
  try {
    return await createImageBitmap(file, { imageOrientation: 'from-image' });
  } catch (error) {
    return await loadImageElement(file);
  }
}

function bayer4(x, y) {
  const matrix = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];
  return matrix[(y & 3) * 4 + (x & 3)] / 16 - 0.5;
}

function encodeRgb565(imageData, dither, qrMode) {
  const { width, height, data } = imageData;
  const out = new Uint8Array(width * height * 2);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const i = (y * width + x) * 4;
      const r = data[i];
      const g = data[i + 1];
      const b = data[i + 2];
      if (qrMode) {
        const luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        const value = luminance < 145 ? 0x0000 : 0xffff;
        out[(y * width + x) * 2] = value & 0xff;
        out[(y * width + x) * 2 + 1] = value >> 8;
        continue;
      }
      let rr = r, gg = g, bb = b;
      if (dither) {
        const delta = bayer4(x, y) * 10;
        rr = clamp(r + delta, 0, 255);
        gg = clamp(g + delta, 0, 255);
        bb = clamp(b + delta, 0, 255);
      }
      const r5 = Math.round((rr * 31) / 255);
      const g6 = Math.round((gg * 63) / 255);
      const b5 = Math.round((bb * 31) / 255);
      const value = (r5 << 11) | (g6 << 5) | b5;
      out[(y * width + x) * 2] = value & 0xff;
      out[(y * width + x) * 2 + 1] = value >> 8;
    }
  }
  return out;
}

function encodePpim(spec, imageData) {
  const pixels = encodeRgb565(imageData, spec.crop !== 'contain', spec.crop === 'contain');
  const file = new Uint8Array(20 + pixels.length);
  file.set([0x50, 0x50, 0x49, 0x4d], 0);
  file[4] = 1;
  file[5] = 1;
  file[6] = 0;
  file[7] = 0;
  const view = new DataView(file.buffer);
  view.setUint16(8, spec.width, true);
  view.setUint16(10, spec.height, true);
  view.setUint32(12, pixels.length, true);
  view.setUint32(16, crc32(pixels), true);
  file.set(pixels, 20);
  return file;
}

function decodePpim(buffer) {
  const bytes = new Uint8Array(buffer);
  if (bytes.length < 20) throw new Error('图片文件过短');
  if (bytes[0] !== 0x50 || bytes[1] !== 0x50 || bytes[2] !== 0x49 || bytes[3] !== 0x4d) {
    throw new Error('图片格式无效');
  }
  if (bytes[4] !== 1 || bytes[5] !== 1) throw new Error('图片版本不支持');
  const view = new DataView(buffer);
  const width = view.getUint16(8, true);
  const height = view.getUint16(10, true);
  const dataLength = view.getUint32(12, true);
  const expected = view.getUint32(16, true);
  if (dataLength !== width * height * 2 || bytes.length !== 20 + dataLength) {
    throw new Error('图片长度不一致');
  }
  const pixels = bytes.slice(20);
  if (crc32(pixels) !== expected) throw new Error('图片校验失败');
  const imageData = new ImageData(width, height);
  for (let i = 0; i < width * height; i++) {
    const value = pixels[i * 2] | (pixels[i * 2 + 1] << 8);
    const r = ((value >> 11) & 0x1f) * 255 / 31;
    const g = ((value >> 5) & 0x3f) * 255 / 63;
    const b = (value & 0x1f) * 255 / 31;
    imageData.data[i * 4] = r;
    imageData.data[i * 4 + 1] = g;
    imageData.data[i * 4 + 2] = b;
    imageData.data[i * 4 + 3] = 255;
  }
  return { width, height, imageData };
}

async function processImage(file, kind) {
  const spec = SPECS[kind];
  const bitmap = await fileToBitmap(file);
  const canvas = document.createElement('canvas');
  canvas.width = spec.width;
  canvas.height = spec.height;
  const ctx = canvas.getContext('2d', { willReadFrequently: true });
  if (spec.crop === 'contain') {
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, spec.width, spec.height);
  } else {
    ctx.fillStyle = '#141e30';
    ctx.fillRect(0, 0, spec.width, spec.height);
  }
  ctx.imageSmoothingEnabled = true;
  ctx.imageSmoothingQuality = 'high';

  const sw = bitmap.width;
  const sh = bitmap.height;
  if (spec.crop === 'square') {
    const side = Math.min(sw, sh);
    ctx.drawImage(bitmap, (sw - side) / 2, (sh - side) / 2, side, side, 0, 0, spec.width, spec.height);
  } else if (spec.crop === 'cover') {
    const targetRatio = spec.width / spec.height;
    let cw = sw;
    let ch = sh;
    if (sw / sh > targetRatio) cw = sh * targetRatio;
    else ch = sw / targetRatio;
    ctx.drawImage(bitmap, (sw - cw) / 2, (sh - ch) / 2, cw, ch, 0, 0, spec.width, spec.height);
  } else {
    const pad = spec.padding || 0;
    const scale = Math.min((spec.width - pad * 2) / sw, (spec.height - pad * 2) / sh);
    const dw = Math.max(1, Math.round(sw * scale));
    const dh = Math.max(1, Math.round(sh * scale));
    const dx = Math.round((spec.width - dw) / 2);
    const dy = Math.round((spec.height - dh) / 2);
    ctx.drawImage(bitmap, dx, dy, dw, dh);
  }
  if (bitmap.close) bitmap.close();
  const imageData = ctx.getImageData(0, 0, spec.width, spec.height);
  return { bytes: encodePpim(spec, imageData), canvas, kind };
}

function drawCanvasInto(target, source) {
  if (!target) return;
  const ctx = target.getContext('2d');
  ctx.imageSmoothingEnabled = false;
  ctx.clearRect(0, 0, target.width, target.height);
  ctx.drawImage(source, 0, 0, target.width, target.height);
}

function drawImageDataInto(target, decoded) {
  const offscreen = document.createElement('canvas');
  offscreen.width = decoded.width;
  offscreen.height = decoded.height;
  offscreen.getContext('2d').putImageData(decoded.imageData, 0, 0);
  drawCanvasInto(target, offscreen);
}

function markPreview(container, hasImage) {
  if (!container) return;
  container.classList.toggle('has-image', hasImage);
}

async function fetchSlot(slot) {
  if (state.imageCache.has(slot)) return state.imageCache.get(slot);
  const response = await fetch(`/api/images/${slot}`, { cache: 'no-store' });
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  const buffer = await response.arrayBuffer();
  state.imageCache.set(slot, buffer);
  return buffer;
}

async function loadSlotInto(slot, canvas, previewContainer) {
  try {
    const buffer = await fetchSlot(slot);
    const decoded = decodePpim(buffer);
    drawImageDataInto(canvas, decoded);
    markPreview(previewContainer, true);
    return true;
  } catch (error) {
    markPreview(previewContainer, false);
    return false;
  }
}

function clearPreview(canvas, previewContainer) {
  if (canvas) canvas.getContext('2d').clearRect(0, 0, canvas.width, canvas.height);
  markPreview(previewContainer, false);
}

function setImageStatus(card, message, isError) {
  const node = $('.image-status', card);
  if (!node) return;
  node.textContent = message;
  node.classList.toggle('error-text', !!isError);
}

function setupImageCard(card) {
  const slot = card.dataset.slot;
  const kind = card.dataset.kind;
  const canvas = $('canvas', card);
  const preview = $('.preview', card);
  const input = $('.file-input', card);
  const upload = $('[data-action="upload"]', card);
  const remove = $('[data-action="remove"]', card);

  input.addEventListener('change', async () => {
    const file = input.files && input.files[0];
    if (!file) return;
    try {
      const processed = await processImage(file, kind);
      state.pending.set(slot, processed.bytes);
      drawCanvasInto(canvas, processed.canvas);
      markPreview(preview, true);
      setImageStatus(card, '已生成预览，点击“上传”保存到设备', false);
    } catch (error) {
      input.value = '';
      setImageStatus(card, error.message, true);
      toast(error.message, 'error');
    }
  });

  upload.addEventListener('click', async () => {
    if (!requireEdit()) return;
    const bytes = state.pending.get(slot);
    if (!bytes) {
      setImageStatus(card, '请先选择一张图片', true);
      return;
    }
    setImageStatus(card, '上传中…', false);
    upload.disabled = true;
    try {
      const response = await fetch(`/api/images/${slot}`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/octet-stream', 'X-PP-Token': state.token },
        body: bytes,
      });
      const text = await response.text();
      let data = null;
      try { data = text ? JSON.parse(text) : null; } catch (ignored) { data = null; }
      if (!response.ok) {
        const error = new Error((data && data.message) || `上传失败（HTTP ${response.status}）`);
        error.status = response.status;
        throw error;
      }
      state.pending.delete(slot);
      state.imageCache.set(slot, bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength));
      setImageStatus(card, '上传成功', false);
      if (state.profile && state.profile.images && SLOT_FLAGS[slot]) {
        state.profile.images[SLOT_FLAGS[slot]] = true;
      }
      toast('图片已保存到设备');
      pollStatus();
    } catch (error) {
      if (handleAuthError(error)) setImageStatus(card, '编辑权限已失效，请重试', true);
      else setImageStatus(card, error.message, true);
      toast(error.message, 'error');
    } finally {
      upload.disabled = !state.canEdit;
    }
  });

  remove.addEventListener('click', async () => {
    if (!requireEdit()) return;
    if (!confirm('移除这张图片？设备上的文件会被删除。')) return;
    try {
      await api(`/api/images/${slot}`, { method: 'DELETE', token: state.token });
      state.pending.delete(slot);
      state.imageCache.delete(slot);
      clearPreview(canvas, preview);
      input.value = '';
      if (state.profile && state.profile.images && SLOT_FLAGS[slot]) {
        state.profile.images[SLOT_FLAGS[slot]] = false;
      }
      setImageStatus(card, '已移除', false);
      toast('图片已移除');
      pollStatus();
    } catch (error) {
      if (!handleAuthError(error)) {
        setImageStatus(card, error.message, true);
        toast(error.message, 'error');
      }
    }
  });
}

let imageLoadGeneration = 0;

async function loadInitialImages() {
  const generation = ++imageLoadGeneration;
  const cards = $$('#profile-images .image-card, #contact-images .image-card');
  for (const card of cards) {
    if (generation !== imageLoadGeneration) return;
    const slot = card.dataset.slot;
    if (state.pending.has(slot)) continue;
    const canvas = $('canvas', card);
    const preview = $('.preview', card);
    const flag = state.profile && state.profile.images ? state.profile.images[SLOT_FLAGS[slot]] : false;
    if (flag) await loadSlotInto(slot, canvas, preview);
    else clearPreview(canvas, preview);
  }
}

/* -------------------------------- profile -------------------------------- */

function defaultProfile() {
  return {
    version: 1,
    name: '',
    tagline: '',
    platforms: { steam: { id: '' }, switch: { id: '' }, playstation: { id: '' } },
    contact: { weibo: { handle: '', url: '' }, wechat: { name: '', id: '' } },
    images: {},
  };
}

function getPath(object, path) {
  return path.split('.').reduce((node, key) => (node && node[key] !== undefined ? node[key] : ''), object);
}

function setPath(object, path, value) {
  const parts = path.split('.');
  let node = object;
  for (let i = 0; i < parts.length - 1; i++) {
    if (!node[parts[i]] || typeof node[parts[i]] !== 'object') node[parts[i]] = {};
    node = node[parts[i]];
  }
  node[parts[parts.length - 1]] = value;
}

function fillProfileForm() {
  $$('[data-path]').forEach(input => {
    const value = getPath(state.profile, input.dataset.path);
    input.value = value || '';
    updateCounter(input);
  });
}

function updateCounter(input) {
  const counter = $(`[data-counter-for="${input.id}"]`);
  if (!counter) return;
  const max = Number(input.getAttribute('maxlength')) || 0;
  const count = codePoints(input.value);
  counter.textContent = max ? `${count}/${max}` : `${count}`;
  counter.classList.toggle('over', max > 0 && count > max);
}

function profilePayload() {
  return {
    version: 1,
    name: state.profile.name || '',
    tagline: state.profile.tagline || '',
    platforms: {
      steam: { id: state.profile.platforms.steam.id || '' },
      switch: { id: state.profile.platforms.switch.id || '' },
      playstation: { id: state.profile.platforms.playstation.id || '' },
    },
    contact: {
      weibo: {
        handle: state.profile.contact.weibo.handle || '',
        url: state.profile.contact.weibo.url || '',
      },
      wechat: {
        name: state.profile.contact.wechat.name || '',
        id: state.profile.contact.wechat.id || '',
      },
    },
  };
}

function validateProfileClient(payload) {
  if (codePoints(payload.name) > PROFILE_LIMITS.name) return '姓名不能超过 24 个字';
  if (codePoints(payload.tagline) > PROFILE_LIMITS.tagline) return '一句话介绍不能超过 60 个字';
  const ids = [payload.platforms.steam.id, payload.platforms.switch.id, payload.platforms.playstation.id];
  if (ids.some(id => codePoints(id) > PROFILE_LIMITS.platform)) return '平台 ID 不能超过 40 个字';
  if (codePoints(payload.contact.weibo.handle) > PROFILE_LIMITS.weiboHandle) return '微博账号不能超过 40 个字';
  if (codePoints(payload.contact.wechat.name) > PROFILE_LIMITS.wechatName) return '微信昵称不能超过 24 个字';
  if (codePoints(payload.contact.wechat.id) > PROFILE_LIMITS.wechatId) return '微信号不能超过 40 个字';
  if (payload.contact.weibo.url && !/^https?:\/\/\S+$/i.test(payload.contact.weibo.url)) {
    return '微博链接必须以 http:// 或 https:// 开头';
  }
  return null;
}

async function validateTexts(entries) {
  const bad = [];
  for (let start = 0; start < entries.length; start += 8) {
    const chunk = entries.slice(start, start + 8);
    const reply = await api('/api/validate-text', {
      method: 'POST',
      body: { texts: chunk.map(entry => entry.text || '') },
    });
    for (const item of reply.unsupported || []) {
      const entry = chunk[item.index];
      if (entry) bad.push(`${entry.label} 含字体不支持的字符 ${item.codepoint}`);
    }
  }
  return bad;
}

async function saveProfile() {
  if (!requireEdit()) return;
  if (!state.profile) {
    toast('个人资料尚未加载，请刷新页面', 'error');
    return;
  }
  const payload = profilePayload();
  const clientError = validateProfileClient(payload);
  if (clientError) {
    toast(clientError, 'error');
    return;
  }
  const entries = [
    { label: '姓名', text: payload.name },
    { label: '个人介绍', text: payload.tagline },
    { label: 'Steam ID', text: payload.platforms.steam.id },
    { label: 'Switch ID', text: payload.platforms.switch.id },
    { label: 'PlayStation ID', text: payload.platforms.playstation.id },
    { label: '微博账号', text: payload.contact.weibo.handle },
    { label: '微博链接', text: payload.contact.weibo.url },
    { label: '微信昵称', text: payload.contact.wechat.name },
    { label: '微信号', text: payload.contact.wechat.id },
  ];
  try {
    const bad = await validateTexts(entries);
    if (bad.length) {
      toast(bad[0], 'error');
      return;
    }
    const reply = await api('/api/profile', { method: 'PUT', body: payload, token: state.token });
    state.profile = reply;
    fillProfileForm();
    setDirty('profile', false);
    loadInitialImages();
    toast('资料已保存到设备');
    pollStatus();
  } catch (error) {
    if (!handleAuthError(error)) toast(error.message, 'error');
  }
}

/* --------------------------------- games --------------------------------- */

function newGameId() {
  const bytes = new Uint8Array(6);
  if (window.crypto && crypto.getRandomValues) crypto.getRandomValues(bytes);
  else for (let i = 0; i < bytes.length; i++) bytes[i] = Math.floor(Math.random() * 256);
  return `g-${Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('')}`;
}

function groupLabel(group) {
  return group === 'playing' ? '正在玩' : '喜欢的游戏';
}

async function loadGames() {
  try {
    const reply = await api('/api/games');
    state.games = reply.games || [];
    renderGames();
  } catch (error) {
    showError(error.message);
  }
}

let coverLoadGeneration = 0;

async function loadGameCoversSequentially(jobs) {
  const generation = ++coverLoadGeneration;
  for (const job of jobs) {
    if (generation !== coverLoadGeneration) return;
    await loadGameCover(job.id, job.canvas, job.container);
  }
}

function renderGames() {
  const list = $('#games-list');
  list.replaceChildren();
  const coverJobs = [];
  $('#games-empty').classList.toggle('hidden', state.games.length > 0);
  state.games.forEach((game, index) => {
    const card = el('div', 'game-card');
    const cover = el('div', 'game-cover');
    const canvas = document.createElement('canvas');
    canvas.width = 54;
    canvas.height = 72;
    cover.append(canvas, el('span', 'placeholder', '无封面'));
    const main = el('div', 'game-main');
    main.append(
      el('div', 'game-title', game.title || '未命名游戏'),
      el('div', 'game-note', game.note || '—'),
    );
    const meta = el('div', 'game-meta');
    const badge = el('span', `group-badge ${game.group === 'playing' ? 'playing' : ''}`, groupLabel(game.group));
    meta.append(badge, el('span', 'muted small', game.link ? '已设置小黑盒链接' : '未设置链接'));
    main.append(meta);
    const actions = el('div', 'game-actions');
    const up = el('button', 'btn small ghost', '↑');
    const down = el('button', 'btn small ghost', '↓');
    const edit = el('button', 'btn small', '编辑');
    const del = el('button', 'btn small danger', '删除');
    up.disabled = index === 0;
    down.disabled = index === state.games.length - 1;
    [up, down, edit, del].forEach(button => button.setAttribute('data-mutation', ''));
    up.addEventListener('click', () => moveGame(index, -1));
    down.addEventListener('click', () => moveGame(index, 1));
    edit.addEventListener('click', () => openEditor(index));
    del.addEventListener('click', () => deleteGame(index));
    actions.append(up, down, edit, del);
    card.append(cover, main, actions);
    list.append(card);
    if (game.cover) coverJobs.push({ id: game.id, canvas, container: cover });
    else markPreview(cover, false);
  });
  loadGameCoversSequentially(coverJobs);
  $$('#games-list button').forEach(button => {
    if (button.hasAttribute('data-mutation') && !state.canEdit) button.disabled = true;
  });
}

async function loadGameCover(id, canvas, container) {
  try {
    const buffer = await fetchSlot(`games/${id}`);
    const decoded = decodePpim(buffer);
    drawImageDataInto(canvas, decoded);
    markPreview(container, true);
  } catch (error) {
    markPreview(container, false);
  }
}

function moveGame(index, delta) {
  if (!requireEdit()) return;
  if (state.editor.index >= 0) closeEditor(false);
  const target = index + delta;
  if (target < 0 || target >= state.games.length) return;
  const [game] = state.games.splice(index, 1);
  state.games.splice(target, 0, game);
  setDirty('games', true);
  renderGames();
}

function deleteGame(index) {
  if (!requireEdit()) return;
  if (state.editor.index >= 0) closeEditor(false);
  const game = state.games[index];
  if (!confirm(`删除「${game.title || '未命名游戏'}」？保存列表后设备上的封面也会被清理。`)) return;
  state.games.splice(index, 1);
  setDirty('games', true);
  if (state.editor.index === index) closeEditor(true);
  renderGames();
}

function openEditor(index) {
  const game = state.games[index];
  if (!game) return;
  state.editor.index = index;
  state.editor.isNew = !!game.__new;
  $('#game-editor-title').textContent = state.editor.isNew ? '新增游戏' : '编辑游戏';
  $('#g-title').value = game.title || '';
  $('#g-group').value = game.group || 'favorite';
  $('#g-note').value = game.note || '';
  $('#g-link').value = game.link || '';
  updateCounter($('#g-title'));
  updateCounter($('#g-note'));
  $('#g-cover-status').textContent = game.cover ? '已有封面' : '';
  const coverCanvas = $('#g-cover');
  const coverPreview = coverCanvas.closest('.preview');
  state.pending.delete(`games/${game.id}`);
  if (game.cover) loadSlotInto(`games/${game.id}`, coverCanvas, coverPreview);
  else clearPreview(coverCanvas, coverPreview);
  $('#game-editor').classList.remove('hidden');
  $('#game-editor').scrollIntoView({ behavior: 'smooth', block: 'nearest' });
}

function closeEditor(removeNew) {
  const index = state.editor.index;
  if (removeNew && index >= 0 && state.games[index] && state.games[index].__new) {
    const id = state.games[index].id;
    state.games.splice(index, 1);
    state.pending.delete(`games/${id}`);
    setDirty('games', true);
  } else if (index >= 0 && state.games[index]) {
    delete state.games[index].__new;
  }
  state.editor.index = -1;
  state.editor.isNew = false;
  $('#game-editor').classList.add('hidden');
  renderGames();
}

function saveEditorToList() {
  if (!requireEdit()) return;
  const index = state.editor.index;
  const game = state.games[index];
  if (!game) return;
  const title = $('#g-title').value.trim();
  const note = $('#g-note').value.trim();
  const link = $('#g-link').value.trim();
  if (!title) {
    toast('游戏名称不能为空', 'error');
    return;
  }
  if (codePoints(title) > 40) {
    toast('游戏名称不能超过 40 个字', 'error');
    return;
  }
  if (codePoints(note) > 120) {
    toast('游戏介绍不能超过 120 个字', 'error');
    return;
  }
  if (link && !/^https?:\/\/\S+$/i.test(link)) {
    toast('小黑盒链接必须以 http:// 或 https:// 开头', 'error');
    return;
  }
  game.title = title;
  game.group = $('#g-group').value;
  game.note = note;
  game.link = link;
  delete game.__new;
  state.editor.isNew = false;
  setDirty('games', true);
  closeEditor(false);
  toast('已保存到列表，记得点击“保存游戏列表”写入设备');
}

async function saveGames() {
  if (!requireEdit()) return;
  if (state.editor.index >= 0) {
    toast('请先点击“保存到列表”，再保存整份游戏列表', 'warn');
    return;
  }
  if (state.games.some(game => !game.title.trim())) {
    toast('每款游戏都需要名称', 'error');
    return;
  }
  const entries = [];
  state.games.forEach(game => {
    entries.push({ label: `游戏名称「${game.title}」`, text: game.title });
    entries.push({ label: `游戏介绍「${game.title}」`, text: game.note });
    entries.push({ label: `小黑盒链接「${game.title}」`, text: game.link });
  });
  try {
    const bad = await validateTexts(entries);
    if (bad.length) {
      toast(bad[0], 'error');
      return;
    }
    const payload = {
      version: 1,
      games: state.games.map(game => ({
        id: game.id,
        title: game.title,
        group: game.group,
        note: game.note || '',
        link: game.link || '',
      })),
    };
    const reply = await api('/api/games', { method: 'PUT', body: payload, token: state.token });
    state.games = reply.games || [];
    setDirty('games', false);
    renderGames();
    toast('游戏列表已保存');
    pollStatus();
  } catch (error) {
    if (!handleAuthError(error)) toast(error.message, 'error');
  }
}

async function uploadGameCover() {
  if (!requireEdit()) return;
  const game = state.games[state.editor.index];
  if (!game) return;
  const slot = `games/${game.id}`;
  const bytes = state.pending.get(slot);
  if (!bytes) {
    $('#g-cover-status').textContent = '请先选择封面图片';
    return;
  }
  $('#g-cover-status').textContent = '上传中…';
  try {
    const response = await fetch(`/api/images/${slot}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/octet-stream', 'X-PP-Token': state.token },
      body: bytes,
    });
    const text = await response.text();
    let data = null;
    try { data = text ? JSON.parse(text) : null; } catch (ignored) { data = null; }
    if (!response.ok) {
      const error = new Error((data && data.message) || `上传失败（HTTP ${response.status}）`);
      error.status = response.status;
      throw error;
    }
    state.pending.delete(slot);
    state.imageCache.set(slot, bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength));
    game.cover = true;
    $('#g-cover-status').textContent = '封面已上传';
    renderGames();
    toast('封面已保存到设备');
  } catch (error) {
    if (handleAuthError(error)) $('#g-cover-status').textContent = '编辑权限已失效';
    else $('#g-cover-status').textContent = error.message;
    toast(error.message, 'error');
  }
}

async function removeGameCover() {
  if (!requireEdit()) return;
  const game = state.games[state.editor.index];
  if (!game) return;
  if (!confirm('移除这张游戏封面？')) return;
  try {
    await api(`/api/images/games/${game.id}`, { method: 'DELETE', token: state.token });
    game.cover = false;
    state.pending.delete(`games/${game.id}`);
    state.imageCache.delete(`games/${game.id}`);
    clearPreview($('#g-cover'), $('#g-cover').closest('.preview'));
    $('#g-cover-status').textContent = '封面已移除';
    renderGames();
    toast('封面已移除');
  } catch (error) {
    if (!handleAuthError(error)) toast(error.message, 'error');
  }
}

/* --------------------------------- NFC ----------------------------------- */

function fillNfcForm() {
  $('#n-enabled').checked = !!state.nfc.enabled;
  $('#n-label').value = state.nfc.label || '';
  $('#n-type').value = state.nfc.type || 'url';
  $('#n-content').value = state.nfc.content || '';
  ['#n-label', '#n-content'].forEach(selector => updateCounter($(selector)));
}

async function loadNfc() {
  try {
    state.nfc = await api('/api/nfc');
    fillNfcForm();
  } catch (error) {
    showError(error.message);
  }
}

async function saveNfc() {
  if (!requireEdit()) return;
  const enabled = $('#n-enabled').checked;
  const label = $('#n-label').value.trim();
  const type = $('#n-type').value;
  const content = $('#n-content').value.trim();
  if (!label) {
    toast('入口名称不能为空', 'error');
    return;
  }
  if (codePoints(label) > 16) {
    toast('入口名称不能超过 16 个字', 'error');
    return;
  }
  if (codePoints(content) > 160) {
    toast('NFC 内容不能超过 160 个字', 'error');
    return;
  }
  if (enabled && !content) {
    toast('启用 NFC 时需要填写内容', 'error');
    return;
  }
  if (enabled && type === 'url' && !/^https?:\/\/\S+$/i.test(content)) {
    toast('网址类型必须以 http:// 或 https:// 开头', 'error');
    return;
  }
  try {
    const bad = await validateTexts([
      { label: 'NFC 名称', text: label },
      { label: 'NFC 内容', text: content },
    ]);
    if (bad.length) {
      toast(bad[0], 'error');
      return;
    }
    const payload = { version: 1, enabled, label, type, content };
    state.nfc = await api('/api/nfc', { method: 'PUT', body: payload, token: state.token });
    fillNfcForm();
    setDirty('nfc', false);
    toast('NFC 配置已保存');
  } catch (error) {
    if (!handleAuthError(error)) toast(error.message, 'error');
  }
}


/* -------------------------------- backup --------------------------------- */

function bytesToBase64(bytes) {
  let binary = '';
  const chunk = 0x8000;
  for (let offset = 0; offset < bytes.length; offset += chunk) {
    binary += String.fromCharCode.apply(null, bytes.subarray(offset, offset + chunk));
  }
  return btoa(binary);
}

function base64ToBytes(text) {
  if (typeof text !== 'string') throw new Error('图片数据不是 Base64 字符串');
  const binary = atob(text);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
  return bytes;
}

function backupTimestamp() {
  const now = new Date();
  const pad = value => String(value).padStart(2, '0');
  return `${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}-${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}`;
}

function downloadBackup(backup) {
  const blob = new Blob([JSON.stringify(backup, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = `player-pass-backup-${backupTimestamp()}.json`;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  setTimeout(() => URL.revokeObjectURL(url), 2000);
}

async function fetchFreshImage(slot) {
  const response = await fetch(`/api/images/${slot}`, { cache: 'no-store' });
  if (!response.ok) throw new Error(`读取图片失败：${slot}（HTTP ${response.status}）`);
  return new Uint8Array(await response.arrayBuffer());
}

async function exportBackup() {
  const status = $('#export-status');
  status.textContent = '正在读取资料…';
  try {
    const profile = await api('/api/profile');
    const games = await api('/api/games');
    const nfc = await api('/api/nfc');
    const slots = [];
    for (const slot of FIXED_IMAGE_SLOTS) {
      if (profile.images && profile.images[SLOT_FLAGS[slot]]) slots.push(slot);
    }
    for (const game of games.games || []) {
      if (game.cover) slots.push(`games/${game.id}`);
    }
    const images = {};
    for (let i = 0; i < slots.length; i++) {
      status.textContent = `正在读取图片 ${i + 1}/${slots.length}…`;
      images[slots[i]] = bytesToBase64(await fetchFreshImage(slots[i]));
    }
    const backup = {
      format: BACKUP_FORMAT,
      version: BACKUP_VERSION,
      exported_at: new Date().toISOString(),
      profile,
      games,
      nfc,
      images,
    };
    downloadBackup(backup);
    status.textContent = `导出完成：${slots.length} 张图片`;
    toast('备份文件已生成');
  } catch (error) {
    status.textContent = error.message;
    toast(error.message, 'error');
  }
}

function profileFromBackup(profile) {
  const value = profile || {};
  const platforms = value.platforms || {};
  const contact = value.contact || {};
  const weibo = contact.weibo || {};
  const wechat = contact.wechat || {};
  return {
    version: 1,
    name: value.name || '',
    tagline: value.tagline || '',
    platforms: {
      steam: { id: (platforms.steam && platforms.steam.id) || '' },
      switch: { id: (platforms.switch && platforms.switch.id) || '' },
      playstation: { id: (platforms.playstation && platforms.playstation.id) || '' },
    },
    contact: {
      weibo: { handle: weibo.handle || '', url: weibo.url || '' },
      wechat: { name: wechat.name || '', id: wechat.id || '' },
    },
  };
}

function validateBackup(backup) {
  if (!backup || typeof backup !== 'object') throw new Error('备份文件不是 JSON 对象');
  if (backup.format !== BACKUP_FORMAT || backup.version !== BACKUP_VERSION) {
    throw new Error('不是受支持的 Player Pass 备份文件');
  }
  if (!backup.profile || !backup.games || !Array.isArray(backup.games.games) || !backup.nfc) {
    throw new Error('备份缺少个人资料、游戏或 NFC 数据');
  }
  if (!backup.images || typeof backup.images !== 'object') {
    throw new Error('备份缺少图片数据');
  }
  for (const [slot, data] of Object.entries(backup.images)) {
    const fixed = FIXED_IMAGE_SLOTS.includes(slot);
    const game = /^games\/[a-z0-9-]{4,24}$/.test(slot);
    if (!fixed && !game) throw new Error(`备份包含未知图片槽位：${slot}`);
    if (typeof data !== 'string') throw new Error(`图片槽位 ${slot} 的数据无效`);
  }
}

async function uploadBackupImage(slot, base64) {
  const bytes = base64ToBytes(base64);
  const response = await fetch(`/api/images/${slot}`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/octet-stream', 'X-PP-Token': state.token },
    body: bytes,
  });
  const text = await response.text();
  let data = null;
  try { data = text ? JSON.parse(text) : null; } catch (ignored) { data = null; }
  if (!response.ok) {
    const error = new Error((data && data.message) || `图片写入失败：${slot}（HTTP ${response.status}）`);
    error.status = response.status;
    throw error;
  }
}

async function deleteBackupImage(slot) {
  try {
    await api(`/api/images/${slot}`, { method: 'DELETE', token: state.token });
  } catch (error) {
    if (error.status !== 404) throw error;
  }
}

async function importBackup() {
  if (!requireEdit()) return;
  const status = $('#import-status');
  const input = $('#import-file');
  const file = input.files && input.files[0];
  if (!file) {
    toast('请先选择备份 JSON 文件', 'error');
    return;
  }
  if (file.size > BACKUP_MAX_BYTES) {
    toast('备份文件超过 16 MB', 'error');
    return;
  }
  if (!confirm('导入会覆盖当前个人资料、游戏、NFC 与相关图片，且不提供自动回滚。建议先导出当前数据。继续吗？')) return;

  try {
    status.textContent = '正在读取备份…';
    const backup = JSON.parse(await file.text());
    validateBackup(backup);

    const profile = profileFromBackup(backup.profile);
    const textEntries = [
      { label: '姓名', text: profile.name },
      { label: '个人介绍', text: profile.tagline },
      { label: 'Steam ID', text: profile.platforms.steam.id },
      { label: 'Switch ID', text: profile.platforms.switch.id },
      { label: 'PlayStation ID', text: profile.platforms.playstation.id },
      { label: '微博账号', text: profile.contact.weibo.handle },
      { label: '微博链接', text: profile.contact.weibo.url },
      { label: '微信昵称', text: profile.contact.wechat.name },
      { label: '微信号', text: profile.contact.wechat.id },
      { label: 'NFC 名称', text: backup.nfc.label },
      { label: 'NFC 内容', text: backup.nfc.content },
    ];
    for (const game of backup.games.games) {
      textEntries.push({ label: `游戏名称「${game.title}」`, text: game.title });
      textEntries.push({ label: `游戏介绍「${game.title}」`, text: game.note || '' });
      textEntries.push({ label: `小黑盒链接「${game.title}」`, text: game.link || '' });
    }
    status.textContent = '正在校验文字与字体…';
    const badText = await validateTexts(textEntries);
    if (badText.length) throw new Error(badText[0]);

    status.textContent = '正在写入个人资料…';
    await api('/api/profile', { method: 'PUT', body: profile, token: state.token });
    status.textContent = '正在写入游戏列表…';
    await api('/api/games', {
      method: 'PUT',
      body: {
        version: 1,
        games: backup.games.games.map(game => ({
          id: game.id,
          title: game.title,
          group: game.group,
          note: game.note || '',
          link: game.link || '',
        })),
      },
      token: state.token,
    });
    status.textContent = '正在写入 NFC 配置…';
    await api('/api/nfc', { method: 'PUT', body: backup.nfc, token: state.token });

    let done = 0;
    const images = backup.images;
    for (const slot of FIXED_IMAGE_SLOTS) {
      status.textContent = `正在恢复图片 ${++done}…`;
      if (images[slot]) await uploadBackupImage(slot, images[slot]);
      else await deleteBackupImage(slot);
    }
    for (const game of backup.games.games) {
      const slot = `games/${game.id}`;
      status.textContent = `正在恢复图片 ${++done}…`;
      if (images[slot]) await uploadBackupImage(slot, images[slot]);
      else await deleteBackupImage(slot);
    }

    await reloadAll();
    status.textContent = '导入完成';
    toast('备份已导入，资料和图片已刷新');
    pollStatus();
  } catch (error) {
    status.textContent = error.message;
    if (!handleAuthError(error)) toast(error.message, 'error');
    // Reflect anything that was written before the failure.
    try { await reloadAll(); } catch (ignored) { /* keep the error visible */ }
  }
}

/* ------------------------------ data loading ----------------------------- */

async function loadProfile() {
  try {
    state.profile = await api('/api/profile');
    if (!state.profile.images) state.profile.images = {};
    fillProfileForm();
    loadInitialImages();
    state.loaded = true;
  } catch (error) {
    showError(error.message);
  }
}

async function reloadAll() {
  state.imageCache.clear();
  await Promise.all([loadProfile(), loadGames(), loadNfc()]);
  setDirty('profile', false);
  setDirty('games', false);
  setDirty('nfc', false);
}

/* -------------------------------- events --------------------------------- */

function setupTabs() {
  $$('.tab').forEach(button => {
    button.addEventListener('click', () => {
      $$('.tab').forEach(item => item.classList.toggle('active', item === button));
      $$('.tab-panel').forEach(panel => panel.classList.toggle('active', panel.id === `tab-${button.dataset.tab}`));
    });
  });
}

function setupForms() {
  $$('[data-path]').forEach(input => {
    input.addEventListener('input', () => {
      if (!state.profile) return;
      setPath(state.profile, input.dataset.path, input.value);
      updateCounter(input);
      setDirty('profile', true);
    });
  });

  $('#profile-form').addEventListener('submit', event => {
    event.preventDefault();
    saveProfile();
  });
  $('#contact-form').addEventListener('submit', event => {
    event.preventDefault();
    saveProfile();
  });
  $('#nfc-form').addEventListener('submit', event => {
    event.preventDefault();
    saveNfc();
  });
  ['#n-enabled', '#n-label', '#n-type', '#n-content'].forEach(selector => {
    $(selector).addEventListener('input', () => {
      updateCounter($('#n-label'));
      updateCounter($('#n-content'));
      setDirty('nfc', true);
    });
    $(selector).addEventListener('change', () => {
      updateCounter($('#n-label'));
      updateCounter($('#n-content'));
      setDirty('nfc', true);
    });
  });

  $('#add-game').addEventListener('click', () => {
    if (!requireEdit()) return;
    const game = { id: newGameId(), title: '', group: 'favorite', note: '', link: '', cover: false, __new: true };
    state.games.push(game);
    setDirty('games', true);
    renderGames();
    openEditor(state.games.length - 1);
    $('#g-title').focus();
  });
  $('#save-games').addEventListener('click', saveGames);
  $('#game-editor-save').addEventListener('click', saveEditorToList);
  $('#game-editor-close').addEventListener('click', () => closeEditor(true));
  $('#game-editor-cancel').addEventListener('click', () => closeEditor(true));
  $('#g-title').addEventListener('input', () => updateCounter($('#g-title')));
  $('#g-note').addEventListener('input', () => updateCounter($('#g-note')));

  const coverInput = $('#g-cover-file');
  coverInput.addEventListener('change', async () => {
    const game = state.games[state.editor.index];
    if (!game) return;
    const file = coverInput.files && coverInput.files[0];
    if (!file) return;
    try {
      const processed = await processImage(file, 'cover');
      state.pending.set(`games/${game.id}`, processed.bytes);
      drawCanvasInto($('#g-cover'), processed.canvas);
      markPreview($('#g-cover').closest('.preview'), true);
      $('#g-cover-status').textContent = '已生成预览，点击“上传封面”保存';
    } catch (error) {
      coverInput.value = '';
      $('#g-cover-status').textContent = error.message;
      toast(error.message, 'error');
    }
  });
  $('#g-cover-upload').addEventListener('click', uploadGameCover);
  $('#g-cover-remove').addEventListener('click', removeGameCover);

  $('#export-backup').addEventListener('click', exportBackup);
  $('#import-backup').addEventListener('click', importBackup);

  $('#retry-claim').addEventListener('click', () => {
    if (state.claimTimer) {
      clearTimeout(state.claimTimer);
      state.claimTimer = null;
    }
    claim();
  });

  window.addEventListener('beforeunload', event => {
    if (!anyDirty()) return;
    event.preventDefault();
    event.returnValue = '';
  });
  window.addEventListener('pagehide', releaseSession);

  $$('#profile-images .image-card, #contact-images .image-card').forEach(setupImageCard);
  $$('[data-counter-for]').forEach(counter => {
    const input = $(`#${counter.dataset.counterFor}`);
    if (input) updateCounter(input);
  });
}

async function init() {
  // One editing identity per tab.  sessionStorage survives a reload (so the
  // same tab can reclaim its lease) but a second tab is a different client and
  // cannot steal the token back and forth.
  try {
    state.clientId = sessionStorage.getItem('pp_client_id');
    if (!state.clientId) {
      state.clientId = uuid();
      sessionStorage.setItem('pp_client_id', state.clientId);
    }
  } catch (storageError) {
    // Private browsing modes can disable storage; an in-memory identity still
    // works until the page is reloaded.
    state.clientId = state.clientId || uuid();
  }
  setupTabs();
  setupForms();
  setReadOnly(true);
  await claim();
  await Promise.all([loadProfile(), loadGames(), loadNfc(), pollStatus()]);
  setReadOnly(!state.canEdit);
  if (state.token) startHeartbeat();
  state.statusTimer = setInterval(pollStatus, 5000);
}

init();
