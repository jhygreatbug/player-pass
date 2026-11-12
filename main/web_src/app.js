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
  // The detail endpoint's `cover` is Steam's 460 x 215 header art; the device
  // card spans the full 216 px content width, so the stored cover matches it.
  cover: { width: 216, height: 101, crop: 'cover' },
};

const SLOT_FLAGS = {
  avatar: 'avatar',
};

const FIXED_IMAGE_SLOTS = Object.keys(SLOT_FLAGS);
// Platform and contact QR images were removed from the product; accept them in
// older backups but do not restore them.
const LEGACY_IMAGE_SLOTS = ['qr_steam', 'qr_switch', 'qr_psn', 'qr_wechat', 'qr_wechat_pay'];
const BACKUP_FORMAT = 'playerpass-backup';
const BACKUP_VERSION = 1;
const BACKUP_MAX_BYTES = 16 * 1024 * 1024;
const CONTACT_MAX = 8;

const PROFILE_LIMITS = {
  name: 24,
  tagline: 60,
  platform: 40,
  contactPlatform: 16,
  contactAccount: 40,
  contactLink: 480,
};

const state = {
  clientId: null,
  token: null,
  canEdit: false,
  occupied: false,
  loaded: { profile: false, games: false },
  claiming: false,
  saving: false,
  profile: null,
  games: [],
  status: null,
  imageCache: new Map(),
  pending: new Map(),
  pendingDeletes: new Set(),
  dirty: { profile: false, games: false },
  revision: { profile: 0, games: 0 },
  editor: { index: -1, isNew: false },
  gamePicker: {
    source: 'search',
    searchTimer: null,
    searchGeneration: 0,
    listGeneration: 0,
    details: new Map(),
  },
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
  if (dirty) state.revision[tab] += 1;
  state.dirty[tab] = dirty;
  const nodes = tab === 'profile'
    ? [$('#profile-dirty'), $('#contact-dirty')]
    : [$(`#${tab}-dirty`)];
  nodes.forEach(node => {
    if (node) node.textContent = dirty ? '有未保存的更改' : '';
  });
  updateSaveBar();
}

function anyDirty() {
  return state.dirty.profile || state.dirty.games;
}

function imagesDirty() {
  return state.pending.size > 0 || state.pendingDeletes.size > 0;
}

function hasUnsavedChanges() {
  return anyDirty() || imagesDirty();
}

function setPendingImage(slot, bytes) {
  state.pending.set(slot, bytes);
  state.pendingDeletes.delete(slot);
  updateSaveBar();
}

function updateSaveBar() {
  const button = $('#save-all');
  if (!button) return;
  const status = $('#save-status');
  const reload = $('#reload-device');
  const dirty = hasUnsavedChanges();
  const editable = state.canEdit && !document.body.classList.contains('readonly');
  const parts = [];
  if (state.dirty.profile) parts.push('资料');
  if (state.dirty.games) parts.push('游戏');
  if (imagesDirty()) parts.push('图片');
  button.disabled = !editable || state.saving || !dirty;
  button.textContent = state.saving ? '保存中…' : '保存更改';
  if (reload) {
    reload.disabled = state.saving;
    reload.textContent = dirty ? '放弃本地更改' : '重新加载';
  }
  if (status) {
    if (state.saving) status.textContent = '正在保存到设备…';
    else if (!editable) status.textContent = '当前为只读，取得编辑权限后可保存';
    else if (dirty) status.textContent = `未保存：${parts.join('、')}`;
    else status.textContent = '所有更改已保存';
  }
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
    setReadOnly(true);
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
  renderContacts();
  updatePickerTabs();
  updateSaveBar();
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
  if (state.claiming) return;
  if (state.canEdit && state.token) return;
  if (state.claimTimer) {
    clearTimeout(state.claimTimer);
    state.claimTimer = null;
  }
  state.claiming = true;
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
    setReadOnly(!state.loaded.profile);
    updateSessionBanner();
    startHeartbeat();
    // A reconnect must not discard local edits: only documents that never
    // loaded successfully are fetched again.
    await loadMissingDocuments();
    setReadOnly(!state.loaded.profile);
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
  } finally {
    state.claiming = false;
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
      // The device still considers this browser the editor; claim again to
      // recover the token after a 401 instead of touching local data.
      if (!state.canEdit) claim();
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

function encodeRgb565(imageData) {
  const { width, height, data } = imageData;
  const out = new Uint8Array(width * height * 2);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const i = (y * width + x) * 4;
      const delta = bayer4(x, y) * 10;
      const rr = clamp(data[i] + delta, 0, 255);
      const gg = clamp(data[i + 1] + delta, 0, 255);
      const bb = clamp(data[i + 2] + delta, 0, 255);
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
  const pixels = encodeRgb565(imageData);
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

async function processBitmap(bitmap, kind) {
  const spec = SPECS[kind];
  const canvas = document.createElement('canvas');
  canvas.width = spec.width;
  canvas.height = spec.height;
  const ctx = canvas.getContext('2d', { willReadFrequently: true });
  ctx.fillStyle = '#141e30';
  ctx.fillRect(0, 0, spec.width, spec.height);
  ctx.imageSmoothingEnabled = true;
  ctx.imageSmoothingQuality = 'high';

  const sw = bitmap.width;
  const sh = bitmap.height;
  if (spec.crop === 'square') {
    const side = Math.min(sw, sh);
    ctx.drawImage(bitmap, (sw - side) / 2, (sh - side) / 2, side, side, 0, 0, spec.width, spec.height);
  } else {
    const targetRatio = spec.width / spec.height;
    let cw = sw;
    let ch = sh;
    if (sw / sh > targetRatio) cw = sh * targetRatio;
    else ch = sw / targetRatio;
    ctx.drawImage(bitmap, (sw - cw) / 2, (sh - ch) / 2, cw, ch, 0, 0, spec.width, spec.height);
  }
  if (bitmap.close) bitmap.close();
  const imageData = ctx.getImageData(0, 0, spec.width, spec.height);
  return { bytes: encodePpim(spec, imageData), canvas, kind };
}

async function processImage(file, kind) {
  const bitmap = await fileToBitmap(file);
  return processBitmap(bitmap, kind);
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

function setImageCardStatus(slot, message, isError) {
  const card = $(`.image-card[data-slot="${slot}"]`);
  if (card) setImageStatus(card, message, isError);
}

function imageOnDevice(slot) {
  if (state.pendingDeletes.has(slot)) return false;
  if (state.imageCache.has(slot)) return true;
  const flag = state.profile && state.profile.images ? state.profile.images[SLOT_FLAGS[slot]] : false;
  return !!flag;
}

function setupImageCard(card) {
  const slot = card.dataset.slot;
  const kind = card.dataset.kind;
  const canvas = $('canvas', card);
  const preview = $('.preview', card);
  const input = $('.file-input', card);
  const remove = $('[data-action="remove"]', card);

  input.addEventListener('change', async () => {
    const file = input.files && input.files[0];
    if (!file) return;
    try {
      const processed = await processImage(file, kind);
      setPendingImage(slot, processed.bytes);
      drawCanvasInto(canvas, processed.canvas);
      markPreview(preview, true);
      setImageStatus(card, '已生成预览，点击“保存更改”写入设备', false);
    } catch (error) {
      input.value = '';
      setImageStatus(card, error.message, true);
      toast(error.message, 'error');
    }
  });

  remove.addEventListener('click', () => {
    if (!requireEdit()) return;
    const wasPending = state.pending.has(slot);
    const onDevice = imageOnDevice(slot);
    if (wasPending) {
      state.pending.delete(slot);
      input.value = '';
      if (onDevice) {
        loadSlotInto(slot, canvas, preview);
        setImageStatus(card, '已取消待上传的图片，仍使用设备上的图片', false);
      } else {
        clearPreview(canvas, preview);
        setImageStatus(card, '已取消待上传的图片', false);
      }
      updateSaveBar();
      return;
    }
    if (!onDevice) return;
    if (!confirm('移除这张图片？设备上的文件会在保存后删除。')) return;
    input.value = '';
    state.pendingDeletes.add(slot);
    state.imageCache.delete(slot);
    clearPreview(canvas, preview);
    if (state.profile && state.profile.images && SLOT_FLAGS[slot]) {
      state.profile.images[SLOT_FLAGS[slot]] = false;
    }
    setImageStatus(card, '保存后将移除设备上的图片', false);
    updateSaveBar();
  });
}

let imageLoadGeneration = 0;

async function loadInitialImages() {
  const generation = ++imageLoadGeneration;
  const cards = $$('#profile-images .image-card');
  for (const card of cards) {
    if (generation !== imageLoadGeneration) return;
    const slot = card.dataset.slot;
    if (state.pending.has(slot) || state.pendingDeletes.has(slot)) continue;
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
    version: 2,
    name: '',
    tagline: '',
    platforms: { steam: { id: '' }, switch: { id: '' }, playstation: { id: '' } },
    contacts: [],
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

function contactControlsEnabled() {
  return state.canEdit && !document.body.classList.contains('readonly');
}

function contactField(labelText, value, maxlength, placeholder, onInput) {
  const label = el('label', 'field', labelText);
  const input = document.createElement('input');
  input.type = 'text';
  input.maxLength = maxlength;
  input.value = value || '';
  input.placeholder = placeholder;
  input.disabled = !contactControlsEnabled();
  input.addEventListener('input', () => onInput(input.value));
  label.append(input);
  return { label, input };
}

function moveContact(index, delta) {
  if (!requireEdit()) return;
  const contacts = state.profile && state.profile.contacts;
  const target = index + delta;
  if (!contacts || target < 0 || target >= contacts.length) return;
  const [contact] = contacts.splice(index, 1);
  contacts.splice(target, 0, contact);
  setDirty('profile', true);
  renderContacts();
}

function removeContact(index) {
  if (!requireEdit()) return;
  const contacts = state.profile && state.profile.contacts;
  if (!contacts || !contacts[index]) return;
  contacts.splice(index, 1);
  setDirty('profile', true);
  renderContacts();
}

function addContact() {
  if (!requireEdit()) return;
  if (!state.profile) return;
  if (!Array.isArray(state.profile.contacts)) state.profile.contacts = [];
  if (state.profile.contacts.length >= CONTACT_MAX) {
    toast(`联系方式最多 ${CONTACT_MAX} 条`, 'warn');
    return;
  }
  state.profile.contacts.push({ platform: '', account: '', link: '' });
  setDirty('profile', true);
  renderContacts();
}

function renderContacts() {
  const list = $('#contacts-list');
  if (!list || !state.profile) return;
  const contacts = Array.isArray(state.profile.contacts) ? state.profile.contacts : [];
  list.replaceChildren();
  $('#contacts-empty').classList.toggle('hidden', contacts.length > 0);
  const add = $('#add-contact');
  if (add) add.disabled = !contactControlsEnabled() || contacts.length >= CONTACT_MAX;

  contacts.forEach((contact, index) => {
    const card = el('div', 'contact-card');
    const head = el('div', 'contact-card-head');
    const hint = el('span', 'muted small', contact.link ? '将生成二维码' : '未填写链接');
    const actions = el('div', 'contact-actions');
    const up = el('button', 'btn small ghost', '↑');
    const down = el('button', 'btn small ghost', '↓');
    const del = el('button', 'btn small danger', '删除');
    [up, down, del].forEach(button => {
      button.type = 'button';
      button.setAttribute('data-mutation', '');
    });
    const enabled = contactControlsEnabled();
    up.disabled = !enabled || index === 0;
    down.disabled = !enabled || index === contacts.length - 1;
    del.disabled = !enabled;
    up.addEventListener('click', () => moveContact(index, -1));
    down.addEventListener('click', () => moveContact(index, 1));
    del.addEventListener('click', () => removeContact(index));
    actions.append(up, down, del);
    head.append(el('span', 'contact-index', `#${index + 1}`), hint, actions);

    const grid = el('div', 'grid two');
    const platform = contactField(
      '平台', contact.platform, PROFILE_LIMITS.contactPlatform, '例如：微博',
      value => {
        contact.platform = value;
        setDirty('profile', true);
      });
    const account = contactField(
      '账号', contact.account, PROFILE_LIMITS.contactAccount, '该平台下的账号或昵称',
      value => {
        contact.account = value;
        setDirty('profile', true);
      });
    const link = contactField(
      '链接（可选，用于生成二维码）', contact.link, PROFILE_LIMITS.contactLink, 'https://…',
      value => {
        contact.link = value;
        hint.textContent = value.trim() ? '将生成二维码' : '未填写链接';
        setDirty('profile', true);
      });
    link.label.classList.add('span-two');
    grid.append(platform.label, account.label, link.label);
    card.append(head, grid);
    list.append(card);
  });
}

function trimmedContacts() {
  const contacts = state.profile && Array.isArray(state.profile.contacts)
    ? state.profile.contacts : [];
  return contacts.map((contact, index) => ({
    index,
    platform: (contact.platform || '').trim(),
    account: (contact.account || '').trim(),
    link: (contact.link || '').trim(),
  }));
}

function filledContacts() {
  return trimmedContacts().filter(contact => contact.platform || contact.account || contact.link);
}

function profilePayload() {
  return {
    version: 2,
    name: state.profile.name || '',
    tagline: state.profile.tagline || '',
    platforms: {
      steam: { id: state.profile.platforms.steam.id || '' },
      switch: { id: state.profile.platforms.switch.id || '' },
      playstation: { id: state.profile.platforms.playstation.id || '' },
    },
    contacts: filledContacts().map(contact => ({
      platform: contact.platform,
      account: contact.account,
      link: contact.link,
    })),
  };
}

function validateProfileClient(payload) {
  if (codePoints(payload.name) > PROFILE_LIMITS.name) return '姓名不能超过 24 个字';
  if (codePoints(payload.tagline) > PROFILE_LIMITS.tagline) return '一句话介绍不能超过 60 个字';
  const ids = [payload.platforms.steam.id, payload.platforms.switch.id, payload.platforms.playstation.id];
  if (ids.some(id => codePoints(id) > PROFILE_LIMITS.platform)) return '平台 ID 不能超过 40 个字';
  const contacts = filledContacts();
  if (contacts.length > CONTACT_MAX) return `联系方式最多 ${CONTACT_MAX} 条`;
  for (const contact of contacts) {
    const label = `第 ${contact.index + 1} 条联系方式`;
    if (!contact.platform) return `${label}需要填写平台`;
    if (codePoints(contact.platform) > PROFILE_LIMITS.contactPlatform) {
      return `${label}的平台不能超过 ${PROFILE_LIMITS.contactPlatform} 个字`;
    }
    if (codePoints(contact.account) > PROFILE_LIMITS.contactAccount) {
      return `${label}的账号不能超过 ${PROFILE_LIMITS.contactAccount} 个字`;
    }
    if (contact.link && !/^https?:\/\/\S+$/i.test(contact.link)) {
      return `${label}的链接必须以 http:// 或 https:// 开头`;
    }
    if (contact.link && new TextEncoder().encode(contact.link).length > PROFILE_LIMITS.contactLink) {
      return `${label}的链接不能超过 ${PROFILE_LIMITS.contactLink} 字节`;
    }
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

// Steam game data comes from the user's own Cloudflare Worker. The device AP
// has no internet path, so these calls only work when the phone itself can
// reach the internet; any failure must leave manual game entry fully usable.
const GAME_API_BASE = 'https://player-pass.jhygreatbug.workers.dev';
const GAME_API_PREFIX = '/api/v1';
const GAME_MAX_TITLE = 40;

const GAME_API_ERRORS = {
  INVALID_PARAMS: '请求参数有误',
  INVALID_STEAM_ID: 'Steam 好友码或 SteamID 格式不正确',
  GAME_NOT_FOUND: '游戏不存在',
  PLAYER_DATA_UNAVAILABLE: '玩家资料未公开，或暂无游玩记录',
  CHINESE_DATA_UNAVAILABLE: '该游戏暂无官方简体中文资料',
  UPSTREAM_ERROR: 'Steam 服务暂时不可用，请稍后再试',
  UPSTREAM_TIMEOUT: 'Steam 服务响应超时，请稍后再试',
  RATE_LIMITED: '请求过于频繁，请稍后再试',
  STEAM_API_KEY_MISSING: '游戏资料服务未配置 Steam API Key',
  INTERNAL_ERROR: '游戏资料服务内部错误',
};

function backendMessage(code, data) {
  if (GAME_API_ERRORS[code]) return GAME_API_ERRORS[code];
  return (data && data.error && data.error.message) || '游戏资料服务请求失败';
}

async function gameApi(path) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 15000);
  let response;
  try {
    response = await fetch(`${GAME_API_BASE}${GAME_API_PREFIX}${path}`, {
      headers: { Accept: 'application/json' },
      signal: controller.signal,
    });
  } catch (error) {
    const wrapped = new Error(
      controller.signal.aborted
        ? '游戏资料服务响应超时，请稍后再试'
        : '无法连接游戏资料服务，请确认手机可以访问互联网'
    );
    wrapped.code = controller.signal.aborted ? 'CLIENT_TIMEOUT' : 'CLIENT_NETWORK';
    throw wrapped;
  } finally {
    clearTimeout(timer);
  }
  let data = null;
  try {
    data = await response.json();
  } catch (parseError) {
    data = null;
  }
  if (!response.ok || !data || data.success === false) {
    const code = data && data.error && data.error.code ? data.error.code : `HTTP_${response.status}`;
    const wrapped = new Error(backendMessage(code, data));
    wrapped.code = code;
    wrapped.status = response.status;
    throw wrapped;
  }
  return data.data;
}

// SteamID64 or a numeric friend code; the worker converts friend codes itself.
function steamLookupId() {
  const value = state.profile && state.profile.platforms && state.profile.platforms.steam
    ? (state.profile.platforms.steam.id || '').trim()
    : '';
  if (/^[1-9]\d{0,9}$/.test(value)) return value;
  if (/^\d{17}$/.test(value)) return value;
  return '';
}

function formatPlaytime(minutes) {
  const value = Math.max(0, Math.round(Number(minutes) || 0));
  if (value < 60) return `${value} 分钟`;
  const hours = value / 60;
  return hours >= 100 ? `${Math.round(hours)} 小时` : `${hours.toFixed(1)} 小时`;
}

function formatPlayedAt(value) {
  if (!value) return '无记录';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return '无记录';
  const pad = number => String(number).padStart(2, '0');
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}`;
}

function clipCodePoints(text, max) {
  return Array.from(text || '').slice(0, max).join('');
}

async function fetchGameDetail(appId) {
  const key = String(appId);
  if (state.gamePicker.details.has(key)) return state.gamePicker.details.get(key);
  const detail = await gameApi(`/games/${key}`);
  state.gamePicker.details.set(key, detail);
  return detail;
}

async function fetchImageBitmap(url) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 20000);
  try {
    const response = await fetch(url, { mode: 'cors', signal: controller.signal });
    if (!response.ok) throw new Error(`封面下载失败（HTTP ${response.status}）`);
    const blob = await response.blob();
    try {
      return await createImageBitmap(blob);
    } catch (ignored) {
      // Fallback for browsers without createImageBitmap support.
      return await new Promise((resolve, reject) => {
        const objectUrl = URL.createObjectURL(blob);
        const image = new Image();
        image.onload = () => {
          URL.revokeObjectURL(objectUrl);
          resolve(image);
        };
        image.onerror = () => {
          URL.revokeObjectURL(objectUrl);
          reject(new Error('封面无法解码'));
        };
        image.src = objectUrl;
      });
    }
  } finally {
    clearTimeout(timer);
  }
}

/* --------------------------------- games --------------------------------- */

function newGameId() {
  const bytes = new Uint8Array(6);
  if (window.crypto && crypto.getRandomValues) crypto.getRandomValues(bytes);
  else for (let i = 0; i < bytes.length; i++) bytes[i] = Math.floor(Math.random() * 256);
  return `g-${Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('')}`;
}

function gameAppId(game) {
  if (!game) return null;
  if (game.__appId) return game.__appId;
  const match = /store\.steampowered\.com\/app\/(\d+)/i.exec(game.link || '');
  return match ? Number(match[1]) : null;
}

function findGameByAppId(appId) {
  return state.games.find(game => gameAppId(game) === appId);
}

async function loadGames() {
  try {
    const reply = await api('/api/games');
    state.games = reply.games || [];
    state.loaded.games = true;
    renderGames();
    updateSaveBar();
    return true;
  } catch (error) {
    showError(error.message);
    return false;
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
  const container = $('#games-list');
  if (!container) return;
  container.replaceChildren();
  const coverJobs = [];
  $('#games-empty').classList.toggle('hidden', state.games.length > 0);
  const tiles = el('div', 'game-tiles');
  state.games.forEach((game, index) => {
    const tile = document.createElement('button');
    tile.type = 'button';
    tile.className = 'game-tile';
    tile.setAttribute('data-mutation', '');
    if (game.__new) tile.classList.add('unsaved');
    if (!state.canEdit) tile.disabled = true;
    tile.addEventListener('click', () => openEditor(index));

    const cover = el('span', 'game-tile-cover');
    const canvas = document.createElement('canvas');
    canvas.width = SPECS.cover.width;
    canvas.height = SPECS.cover.height;
    cover.append(canvas, el('span', 'placeholder', '无封面'));
    tile.append(cover, el('span', 'game-tile-title', game.title || '未命名游戏'));
    tiles.append(tile);

    const pending = state.pending.get(`games/${game.id}`);
    if (pending) {
      try {
        drawImageDataInto(canvas, decodePpim(pending.buffer));
        markPreview(cover, true);
      } catch (ignored) {
        markPreview(cover, false);
      }
    } else if (game.cover) {
      coverJobs.push({ id: game.id, canvas, container: cover });
    } else {
      markPreview(cover, false);
    }
  });
  container.append(tiles);
  loadGameCoversSequentially(coverJobs);
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

function openEditor(index) {
  const game = state.games[index];
  if (!game) return;
  state.editor.index = index;
  state.editor.isNew = !!game.__new;
  $('#game-editor-title').textContent = state.editor.isNew ? '新增游戏' : '编辑游戏';
  $('#g-title').value = game.title || '';
  updateCounter($('#g-title'));

  const slot = `games/${game.id}`;
  const coverCanvas = $('#g-cover');
  const coverPreview = coverCanvas.closest('.preview');
  const pending = state.pending.get(slot);
  if (pending) {
    try {
      drawImageDataInto(coverCanvas, decodePpim(pending.buffer));
      markPreview(coverPreview, true);
    } catch (ignored) {
      clearPreview(coverCanvas, coverPreview);
    }
    $('#g-cover-status').textContent = '已生成预览，点击底部“保存更改”写入设备';
  } else if (state.pendingDeletes.has(slot)) {
    clearPreview(coverCanvas, coverPreview);
    $('#g-cover-status').textContent = '保存后将移除封面';
  } else if (game.cover) {
    $('#g-cover-status').textContent = '已有封面';
    loadSlotInto(slot, coverCanvas, coverPreview);
  } else {
    clearPreview(coverCanvas, coverPreview);
    $('#g-cover-status').textContent = '';
  }
  updateEditorMoveButtons();
  $('#game-editor').classList.remove('hidden');
  $('#game-editor').scrollIntoView({ behavior: 'smooth', block: 'nearest' });
}

function updateEditorMoveButtons() {
  const game = state.games[state.editor.index];
  const up = $('#game-editor-up');
  const down = $('#game-editor-down');
  const del = $('#game-editor-delete');
  if (!game) {
    [up, down, del].forEach(button => {
      if (button) button.disabled = true;
    });
    return;
  }
  const position = state.games.indexOf(game);
  if (up) up.disabled = !state.canEdit || position <= 0;
  if (down) down.disabled = !state.canEdit || position < 0 || position >= state.games.length - 1;
  if (del) del.disabled = !state.canEdit;
}

function closeEditor(removeNew) {
  const index = state.editor.index;
  if (removeNew && index >= 0 && state.games[index] && state.games[index].__new) {
    const id = state.games[index].id;
    state.games.splice(index, 1);
    state.pending.delete(`games/${id}`);
    state.pendingDeletes.delete(`games/${id}`);
    setDirty('games', true);
  } else if (index >= 0 && state.games[index]) {
    delete state.games[index].__new;
  }
  state.editor.index = -1;
  state.editor.isNew = false;
  $('#game-editor').classList.add('hidden');
  renderGames();
}

function editorValues() {
  return {
    title: $('#g-title').value.trim(),
  };
}

function validateEditorValues(values) {
  if (!values.title) return '游戏名称不能为空';
  if (codePoints(values.title) > 40) return '游戏名称不能超过 40 个字';
  return null;
}

// Applies the open editor to the list without touching the device.  Returns an
// error message, or null after the editor closes.
function commitEditorFields() {
  const index = state.editor.index;
  const game = state.games[index];
  if (!game) return null;
  const values = editorValues();
  const error = validateEditorValues(values);
  if (error) return error;
  game.title = values.title;
  delete game.__new;
  state.editor.index = -1;
  state.editor.isNew = false;
  $('#game-editor').classList.add('hidden');
  setDirty('games', true);
  renderGames();
  return null;
}

function saveEditorToList() {
  if (!requireEdit()) return;
  const error = commitEditorFields();
  if (error) {
    toast(error, 'error');
    return;
  }
  toast('已保存到列表，点击底部“保存更改”写入设备');
}

function validateGamesClient(games) {
  for (const game of games) {
    const title = (game.title || '').trim();
    if (!title) return '每款游戏都需要名称';
    if (codePoints(title) > 40) return `游戏名称「${title}」不能超过 40 个字`;
    if (codePoints(game.note || '') > 120) return `「${title}」的游戏介绍不能超过 120 个字`;
    if (game.link && !/^https?:\/\/\S+$/i.test(game.link)) {
      return `「${title}」的通用链接必须以 http:// 或 https:// 开头`;
    }
  }
  return null;
}

function moveEditorGame(delta) {
  if (!requireEdit()) return;
  const position = state.editor.index;
  const target = position + delta;
  if (position < 0 || target < 0 || target >= state.games.length) return;
  const game = state.games[position];
  const other = state.games[target];
  state.games[position] = other;
  state.games[target] = game;
  state.editor.index = target;
  setDirty('games', true);
  updateEditorMoveButtons();
  renderGames();
}

function deleteEditorGame() {
  if (!requireEdit()) return;
  const index = state.editor.index;
  const game = state.games[index];
  if (!game) return;
  if (!confirm(`删除「${game.title || '未命名游戏'}」？保存后设备上的封面也会被清理。`)) return;
  state.pending.delete(`games/${game.id}`);
  state.pendingDeletes.delete(`games/${game.id}`);
  state.games.splice(index, 1);
  setDirty('games', true);
  state.editor.index = -1;
  state.editor.isNew = false;
  $('#game-editor').classList.add('hidden');
  renderGames();
}

async function putImageBytes(slot, bytes) {
  let response;
  try {
    response = await fetch(`/api/images/${slot}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/octet-stream', 'X-PP-Token': state.token },
      body: bytes,
    });
  } catch (networkError) {
    const error = new Error('无法连接设备，请确认手机仍连接着 PlayerPass 热点');
    error.status = 0;
    throw error;
  }
  const text = await response.text();
  let data = null;
  try { data = text ? JSON.parse(text) : null; } catch (ignored) { data = null; }
  if (!response.ok) {
    const error = new Error((data && data.message) || `上传失败（HTTP ${response.status}）`);
    error.status = response.status;
    throw error;
  }
  return data;
}

function removeGameCover() {
  if (!requireEdit()) return;
  const game = state.games[state.editor.index];
  if (!game) return;
  const slot = `games/${game.id}`;
  const preview = $('#g-cover').closest('.preview');
  if (state.pending.has(slot)) {
    state.pending.delete(slot);
    if (game.cover) {
      loadSlotInto(slot, $('#g-cover'), preview);
      $('#g-cover-status').textContent = '已取消待上传的封面，仍使用设备上的封面';
    } else {
      clearPreview($('#g-cover'), preview);
      $('#g-cover-status').textContent = '已取消待上传的封面';
    }
    renderGames();
    updateSaveBar();
    return;
  }
  if (!game.cover) return;
  if (!confirm('移除这张游戏封面？设备上的文件会在保存后删除。')) return;
  game.cover = false;
  state.imageCache.delete(slot);
  state.pendingDeletes.add(slot);
  clearPreview($('#g-cover'), preview);
  $('#g-cover-status').textContent = '保存后将移除封面';
  renderGames();
  updateSaveBar();
}

/* ------------------------------ game picker ------------------------------ */

function openPicker() {
  if (!requireEdit()) return;
  $('#game-picker').classList.remove('hidden');
  updatePickerTabs();
  selectPickerSource(steamLookupId() ? 'recent' : 'search');
}

function closePicker() {
  $('#game-picker').classList.add('hidden');
}

function updatePickerTabs() {
  const id = steamLookupId();
  $$('.picker-tab').forEach(tab => {
    if (tab.dataset.source !== 'recent' && tab.dataset.source !== 'most-played') return;
    tab.disabled = !state.canEdit;
    tab.title = id ? '' : '请先在“个人资料”中填写 Steam 好友码';
  });
}

function selectPickerSource(source) {
  if (source === 'manual') {
    closePicker();
    startNewGame();
    return;
  }
  state.gamePicker.source = source;
  $$('.picker-tab').forEach(tab => {
    tab.classList.toggle('active', tab.dataset.source === source);
  });
  $$('.picker-panel').forEach(panel => panel.classList.add('hidden'));
  const panel = $(`#picker-${source}`);
  if (panel) panel.classList.remove('hidden');
  if (source === 'search') {
    const input = $('#steam-search');
    if (input) {
      input.focus();
      if (codePoints(input.value.trim()) >= 2) {
        scheduleSteamSearch();
      } else {
        renderSearchResults([]);
        setSearchStatus('输入游戏名开始搜索');
      }
    }
  } else {
    loadPlayerList(source);
  }
}

function startNewGame() {
  if (!requireEdit()) return;
  const game = { id: newGameId(), title: '', note: '', link: '', cover: false, __new: true };
  state.games.push(game);
  setDirty('games', true);
  renderGames();
  openEditor(state.games.length - 1);
  $('#g-title').focus();
}

function scheduleSteamSearch() {
  const input = $('#steam-search');
  if (!input) return;
  const query = input.value.trim();
  clearTimeout(state.gamePicker.searchTimer);
  const generation = ++state.gamePicker.searchGeneration;
  if (codePoints(query) < 2) {
    renderSearchResults([]);
    setSearchStatus(query ? '至少输入 2 个字符' : '输入游戏名开始搜索');
    return;
  }
  state.gamePicker.searchTimer = setTimeout(() => runSteamSearch(query, generation), 450);
}

async function runSteamSearch(query, generation) {
  setSearchStatus('搜索中…');
  try {
    const data = await gameApi(`/games/search?q=${encodeURIComponent(query)}&limit=10`);
    if (generation !== state.gamePicker.searchGeneration) return;
    const games = data.games || [];
    renderSearchResults(games);
    setSearchStatus(games.length ? '' : '没有找到匹配游戏');
  } catch (error) {
    if (generation !== state.gamePicker.searchGeneration) return;
    renderSearchResults([]);
    setSearchStatus(error.message, true);
  }
}

function setSearchStatus(text, isError) {
  const status = $('#search-status');
  if (!status) return;
  status.textContent = text || '';
  status.classList.toggle('error-text', !!isError);
}

function renderSearchResults(games) {
  const list = $('#search-results');
  if (!list) return;
  list.replaceChildren();
  games.forEach(game => {
    const existing = findGameByAppId(game.appId);
    const button = el('button', 'candidate');
    button.type = 'button';
    button.setAttribute('data-mutation', '');
    button.disabled = !state.canEdit;
    if (existing) button.classList.add('added');
    button.addEventListener('click', () => selectSteamCandidate({
      appId: game.appId,
      name: game.name,
      imageUrl: game.image,
      source: 'search',
    }));
    const thumb = el('span', 'candidate-thumb');
    if (game.image) {
      const image = document.createElement('img');
      image.src = game.image;
      image.alt = '';
      image.loading = 'lazy';
      image.referrerPolicy = 'no-referrer';
      thumb.append(image);
    } else {
      thumb.append(el('span', 'placeholder', '无图'));
    }
    const main = el('span', 'candidate-main');
    main.append(el('span', 'candidate-title', game.name || '未命名游戏'));
    main.append(el('span', 'candidate-meta muted small', existing ? '已在列表中' : 'Steam 搜索结果'));
    button.append(thumb, main, el('span', 'candidate-action', existing ? '已添加' : '添加'));
    list.append(button);
  });
}

function renderCandidates(kind, games) {
  const list = $(`#${kind}-results`);
  if (!list) return;
  list.replaceChildren();
  games.forEach(game => {
    const existing = findGameByAppId(game.appId);
    const meta = kind === 'recent'
      ? `最近两周 ${formatPlaytime(game.playtimeTwoWeeksMinutes)} · 总计 ${formatPlaytime(game.playtimeForeverMinutes)} · ${formatPlayedAt(game.lastPlayedAt)}`
      : `累计 ${formatPlaytime(game.playtimeForeverMinutes)} · 最近 ${formatPlayedAt(game.lastPlayedAt)}`;
    const button = el('button', 'candidate');
    button.type = 'button';
    button.setAttribute('data-mutation', '');
    button.disabled = !state.canEdit;
    if (existing) button.classList.add('added');
    button.addEventListener('click', () => selectSteamCandidate({
      appId: game.appId,
      name: game.name,
      iconUrl: game.icon,
      source: kind,
    }));
    // Recent and most-played lists only get Steam's square game icon, so their
    // thumbnail keeps a square frame instead of the capsule used by search.
    const thumb = el('span', 'candidate-thumb square');
    if (game.icon) {
      const image = document.createElement('img');
      image.src = game.icon;
      image.alt = '';
      image.loading = 'lazy';
      image.referrerPolicy = 'no-referrer';
      thumb.append(image);
    } else {
      thumb.append(el('span', 'placeholder', '无图'));
    }
    const main = el('span', 'candidate-main');
    main.append(el('span', 'candidate-title', game.name || '未命名游戏'));
    main.append(el('span', 'candidate-meta muted small', existing ? `${meta} · 已在列表` : meta));
    button.append(thumb, main, el('span', 'candidate-action', existing ? '已添加' : '添加'));
    list.append(button);
  });
}

async function loadPlayerList(kind) {
  const id = steamLookupId();
  const list = $(`#${kind}-results`);
  const status = $(`#${kind}-status`);
  if (!list || !status) return;
  if (!id) {
    list.replaceChildren();
    status.textContent = '请先在“个人资料”中填写 Steam 好友码；好友码可在 Steam 个人资料页复制。';
    status.classList.add('error-text');
    return;
  }
  const generation = ++state.gamePicker.listGeneration;
  list.replaceChildren();
  status.textContent = '正在读取 Steam 游玩记录…';
  status.classList.remove('error-text');
  try {
    const limit = kind === 'recent' ? 20 : 30;
    const data = await gameApi(`/players/${encodeURIComponent(id)}/${kind}?limit=${limit}`);
    if (generation !== state.gamePicker.listGeneration) return;
    const games = data.games || [];
    renderCandidates(kind, games);
    if (kind === 'most-played') {
      const summary = $('#most-played-summary');
      if (summary) {
        summary.textContent = `共 ${data.ownedGameCount || 0} 款游戏 · 累计 ${formatPlaytime(data.totalPlaytimeMinutes)}`;
      }
    }
    status.textContent = games.length ? '' : '暂无记录或玩家资料未公开';
    status.classList.remove('error-text');
  } catch (error) {
    if (generation !== state.gamePicker.listGeneration) return;
    status.textContent = error.message;
    status.classList.add('error-text');
  }
}

async function selectSteamCandidate(candidate) {
  if (!requireEdit()) return;
  const existing = findGameByAppId(candidate.appId);
  if (existing) {
    toast('这款游戏已经在列表里了');
    openEditor(state.games.indexOf(existing));
    closePicker();
    return;
  }
  const status = $(`#${state.gamePicker.source}-status`);
  if (status) {
    status.textContent = '正在读取游戏资料…';
    status.classList.remove('error-text');
  }
  let detail = null;
  let warning = '';
  try {
    detail = await fetchGameDetail(candidate.appId);
  } catch (error) {
    warning = error.code === 'CHINESE_DATA_UNAVAILABLE'
      ? '该游戏暂无官方简体中文资料，已使用原名'
      : '未能读取游戏详情，已使用候选名称';
  }
  let title = (detail && detail.name) || candidate.name || '未命名游戏';
  if (codePoints(title) > GAME_MAX_TITLE) {
    title = clipCodePoints(title, GAME_MAX_TITLE);
    warning = warning || '游戏名称超过 40 个字，已截断';
  }
  const link = (detail && detail.sourceUrl) || `https://store.steampowered.com/app/${candidate.appId}/`;
  // The detail cover is Steam's 460 x 215 header art. Search results fall back
  // to the capsule only when the detail request failed.
  const coverUrl = (detail && detail.cover) || candidate.imageUrl || '';
  const game = {
    id: newGameId(),
    title,
    note: '',
    link,
    cover: false,
    __new: true,
    __appId: candidate.appId,
  };
  state.games.push(game);
  setDirty('games', true);
  renderGames();
  closePicker();
  openEditor(state.games.length - 1);
  if (warning) toast(warning, 'warn');
  if (coverUrl) {
    $('#g-cover-status').textContent = '正在获取封面…';
    try {
      const bitmap = await fetchImageBitmap(coverUrl);
      const processed = await processBitmap(bitmap, 'cover');
      // The editor may have been cancelled while the cover was downloading.
      if (!state.games.includes(game)) return;
      setPendingImage(`games/${game.id}`, processed.bytes);
      drawCanvasInto($('#g-cover'), processed.canvas);
      markPreview($('#g-cover').closest('.preview'), true);
      $('#g-cover-status').textContent = '已生成预览，点击底部“保存更改”写入设备';
      renderGames();
    } catch (error) {
      $('#g-cover-status').textContent = '封面获取失败，可在编辑器中手动选择';
    }
  }
  toast('已加入列表，点击底部“保存更改”写入设备');
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
  if (hasUnsavedChanges() && !confirm('本地有未保存的更改，导出的是设备上已保存的数据。继续导出吗？')) return;
  const status = $('#export-status');
  status.textContent = '正在读取资料…';
  try {
    const profile = await api('/api/profile');
    const games = await api('/api/games');
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

function contactsFromBackup(value) {
  if (Array.isArray(value.contacts)) {
    return value.contacts
      .filter(contact => contact && typeof contact === 'object')
      .slice(0, CONTACT_MAX)
      .map(contact => ({
        platform: contact.platform || '',
        account: contact.account || '',
        link: contact.link || '',
      }));
  }
  // v1 backups stored fixed Weibo/WeChat fields; convert them to entries.
  const legacy = value.contact || {};
  const out = [];
  const weibo = legacy.weibo || {};
  if (weibo.handle || weibo.url) {
    out.push({ platform: '微博', account: weibo.handle || '', link: weibo.url || '' });
  }
  const wechat = legacy.wechat || {};
  if (wechat.name) out.push({ platform: '微信', account: wechat.name, link: '' });
  if (wechat.id) out.push({ platform: '微信号', account: wechat.id, link: '' });
  return out.slice(0, CONTACT_MAX);
}

function profileFromBackup(profile) {
  const value = profile || {};
  const platforms = value.platforms || {};
  return {
    version: 2,
    name: value.name || '',
    tagline: value.tagline || '',
    platforms: {
      steam: { id: (platforms.steam && platforms.steam.id) || '' },
      switch: { id: (platforms.switch && platforms.switch.id) || '' },
      playstation: { id: (platforms.playstation && platforms.playstation.id) || '' },
    },
    contacts: contactsFromBackup(value),
  };
}

function validateBackup(backup) {
  if (!backup || typeof backup !== 'object') throw new Error('备份文件不是 JSON 对象');
  if (backup.format !== BACKUP_FORMAT || backup.version !== BACKUP_VERSION) {
    throw new Error('不是受支持的 Player Pass 备份文件');
  }
  if (!backup.profile || !backup.games || !Array.isArray(backup.games.games)) {
    throw new Error('备份缺少个人资料或游戏数据');
  }
  if (!backup.images || typeof backup.images !== 'object') {
    throw new Error('备份缺少图片数据');
  }
  for (const [slot, data] of Object.entries(backup.images)) {
    const fixed = FIXED_IMAGE_SLOTS.includes(slot);
    const legacy = LEGACY_IMAGE_SLOTS.includes(slot);
    const game = /^games\/[a-z0-9-]{4,24}$/.test(slot);
    if (!fixed && !legacy && !game) throw new Error(`备份包含未知图片槽位：${slot}`);
    if (typeof data !== 'string') throw new Error(`图片槽位 ${slot} 的数据无效`);
  }
}

function ppimDimensions(bytes) {
  if (bytes.length < 20) return null;
  if (bytes[0] !== 0x50 || bytes[1] !== 0x50 || bytes[2] !== 0x49 || bytes[3] !== 0x4d) return null;
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return { width: view.getUint16(8, true), height: view.getUint16(10, true) };
}

function slotAcceptsBytes(slot, bytes) {
  const dims = ppimDimensions(bytes);
  if (!dims) return false;
  if (slot === 'avatar') {
    return dims.width === SPECS.avatar.width && dims.height === SPECS.avatar.height;
  }
  if (slot.startsWith('games/')) {
    return dims.width === SPECS.cover.width && dims.height === SPECS.cover.height;
  }
  return false;
}

async function uploadBackupImage(slot, base64) {
  const bytes = base64ToBytes(base64);
  // Older backups can carry game covers in the previous 72 x 108 format. The
  // device rejects those dimensions, so skip them instead of failing the
  // whole import; the rest of the backup still applies.
  if (!slotAcceptsBytes(slot, bytes)) return;
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
  if (!confirm('导入会覆盖设备上的个人资料、游戏与相关图片，本地未保存的更改也会被丢弃，且不提供自动回滚。建议先导出当前数据。继续吗？')) return;

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
    ];
    profile.contacts.forEach((contact, index) => {
      textEntries.push({ label: `联系方式${index + 1}平台`, text: contact.platform });
      textEntries.push({ label: `联系方式${index + 1}账号`, text: contact.account });
      textEntries.push({ label: `联系方式${index + 1}链接`, text: contact.link });
    });
    for (const game of backup.games.games) {
      textEntries.push({ label: `游戏名称「${game.title}」`, text: game.title });
      textEntries.push({ label: `游戏介绍「${game.title}」`, text: game.note || '' });
      textEntries.push({ label: `通用链接「${game.title}」`, text: game.link || '' });
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
          note: game.note || '',
          link: game.link || '',
        })),
      },
      token: state.token,
    });

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
    if (!Array.isArray(state.profile.contacts)) state.profile.contacts = [];
    fillProfileForm();
    renderContacts();
    loadInitialImages();
    state.loaded.profile = true;
    updateSaveBar();
    return true;
  } catch (error) {
    showError(error.message);
    return false;
  }
}

async function loadMissingDocuments() {
  const jobs = [];
  if (!state.loaded.profile) jobs.push(loadProfile());
  if (!state.loaded.games) jobs.push(loadGames());
  if (jobs.length) await Promise.all(jobs);
}

async function reloadAll() {
  state.imageCache.clear();
  state.pending.clear();
  state.pendingDeletes.clear();
  state.editor.index = -1;
  state.editor.isNew = false;
  $('#game-editor').classList.add('hidden');
  const [profileOk, gamesOk] = await Promise.all([loadProfile(), loadGames()]);
  if (profileOk) setDirty('profile', false);
  if (gamesOk) setDirty('games', false);
  updateSaveBar();
  return profileOk && gamesOk;
}

async function reloadDevice() {
  if (state.saving) return;
  const message = hasUnsavedChanges()
    ? '将丢弃本地未保存的更改，并从设备重新读取数据。继续吗？'
    : '从设备重新读取数据？';
  if (!confirm(message)) return;
  const ok = await reloadAll();
  toast(ok ? '已从设备重新加载' : '重新加载失败，请检查设备连接', ok ? 'ok' : 'error');
}

/* ------------------------------- whole save ------------------------------ */

function bytesToBuffer(bytes) {
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
}

// Saves profile, games, and every pending image in one user action.  Each
// part reports its own outcome: a failed part keeps its dirty flag and pending
// image, so a later save only retries what is still missing.
async function saveAll() {
  if (state.saving) return;
  if (!requireEdit()) return;
  const editorError = commitEditorFields();
  if (editorError) {
    toast(editorError, 'error');
    return;
  }
  if (!hasUnsavedChanges()) return;

  // Only sections with local changes are validated and written, so an
  // untouched (possibly empty) section can never block the save.
  const plan = {
    profile: state.dirty.profile,
    games: state.dirty.games,
  };
  if (plan.profile && !state.profile) {
    toast('个人资料尚未加载，请刷新页面', 'error');
    return;
  }

  let profileBody = null;
  if (plan.profile) {
    profileBody = profilePayload();
    const profileError = validateProfileClient(profileBody);
    if (profileError) {
      toast(profileError, 'error');
      return;
    }
  }

  let gamesBody = null;
  if (plan.games) {
    gamesBody = {
      version: 1,
      games: state.games.map(game => ({
        id: game.id,
        title: game.title,
        note: game.note || '',
        link: game.link || '',
      })),
    };
    const gamesError = validateGamesClient(gamesBody.games);
    if (gamesError) {
      toast(gamesError, 'error');
      return;
    }
  }

  const entries = [];
  if (profileBody) {
    entries.push(
      { label: '姓名', text: profileBody.name },
      { label: '个人介绍', text: profileBody.tagline },
      { label: 'Steam ID', text: profileBody.platforms.steam.id },
      { label: 'Switch ID', text: profileBody.platforms.switch.id },
      { label: 'PlayStation ID', text: profileBody.platforms.playstation.id }
    );
    filledContacts().forEach(contact => {
      const number = contact.index + 1;
      entries.push({ label: `联系方式${number}平台`, text: contact.platform });
      entries.push({ label: `联系方式${number}账号`, text: contact.account });
      entries.push({ label: `联系方式${number}链接`, text: contact.link });
    });
  }
  if (gamesBody) {
    gamesBody.games.forEach(game => {
      entries.push({ label: `游戏名称「${game.title}」`, text: game.title });
      entries.push({ label: `游戏介绍「${game.title}」`, text: game.note });
      entries.push({ label: `通用链接「${game.title}」`, text: game.link });
    });
  }

  if (entries.length) {
    try {
      const bad = await validateTexts(entries);
      if (bad.length) {
        toast(bad[0], 'error');
        return;
      }
    } catch (error) {
      if (!handleAuthError(error)) toast(`保存前校验失败：${error.message}`, 'error');
      return;
    }
  }

  const revisions = { ...state.revision };
  const appIds = new Map(
    state.games.filter(game => game.__appId).map(game => [game.id, game.__appId])
  );
  // Covers may only be written for games that exist on the device after this
  // save; a game added while saving stays pending for the next save.
  const deviceGameIds = new Set(
    plan.games ? gamesBody.games.map(game => game.id) : state.games.map(game => game.id)
  );
  const failures = [];
  let savedSomething = false;
  let gamesSaved = !plan.games;
  let aborted = false;

  const fail = (label, error) => {
    failures.push(`${label}：${error.message}`);
    if (handleAuthError(error) || error.status === 0) aborted = true;
  };

  state.saving = true;
  updateSaveBar();

  try {
    if (!aborted && plan.profile) {
      try {
        const reply = await api('/api/profile', { method: 'PUT', body: profileBody, token: state.token });
        savedSomething = true;
        if (state.revision.profile === revisions.profile) {
          const images = state.profile.images || {};
          state.profile = reply || profileBody;
          if (!state.profile.images) state.profile.images = images;
          if (!Array.isArray(state.profile.contacts)) state.profile.contacts = [];
          fillProfileForm();
          renderContacts();
          updatePickerTabs();
          setDirty('profile', false);
        }
      } catch (error) {
        fail('个人资料', error);
      }
    }

    if (!aborted && plan.games) {
      try {
        const reply = await api('/api/games', { method: 'PUT', body: gamesBody, token: state.token });
        savedSomething = true;
        gamesSaved = true;
        if (state.revision.games === revisions.games) {
          state.games = (reply.games || []).map(game => ({ ...game, __appId: appIds.get(game.id) }));
          setDirty('games', false);
          renderGames();
        }
      } catch (error) {
        fail('游戏列表', error);
      }
    }

    if (!aborted) {
      const avatarBytes = state.pending.get('avatar');
      if (avatarBytes) {
        try {
          await putImageBytes('avatar', avatarBytes);
          if (state.pending.get('avatar') === avatarBytes) state.pending.delete('avatar');
          state.imageCache.set('avatar', bytesToBuffer(avatarBytes));
          if (state.profile && state.profile.images) state.profile.images.avatar = true;
          setImageCardStatus('avatar', '已保存到设备', false);
          savedSomething = true;
        } catch (error) {
          fail('头像', error);
          setImageCardStatus('avatar', '保存失败，可重试', true);
        }
      }
    }

    if (!aborted && state.pendingDeletes.has('avatar')) {
      try {
        await api('/api/images/avatar', { method: 'DELETE', token: state.token });
        state.pendingDeletes.delete('avatar');
        state.imageCache.delete('avatar');
        if (state.profile && state.profile.images) state.profile.images.avatar = false;
        setImageCardStatus('avatar', '已从设备移除', false);
        savedSomething = true;
      } catch (error) {
        if (error.status === 404) {
          state.pendingDeletes.delete('avatar');
          state.imageCache.delete('avatar');
          if (state.profile && state.profile.images) state.profile.images.avatar = false;
          setImageCardStatus('avatar', '已从设备移除', false);
        } else {
          fail('头像', error);
          setImageCardStatus('avatar', '保存失败，可重试', true);
        }
      }
    }

    if (!aborted && gamesSaved) {
      for (const [slot, bytes] of Array.from(state.pending.entries())) {
        if (aborted || !slot.startsWith('games/')) continue;
        const id = slot.slice('games/'.length);
        if (!deviceGameIds.has(id) || state.pending.get(slot) !== bytes) continue;
        const game = state.games.find(item => item.id === id);
        if (!game) {
          state.pending.delete(slot);
          continue;
        }
        try {
          await putImageBytes(slot, bytes);
          if (state.pending.get(slot) === bytes) state.pending.delete(slot);
          state.imageCache.set(slot, bytesToBuffer(bytes));
          game.cover = true;
          savedSomething = true;
        } catch (error) {
          fail(`「${game.title}」封面`, error);
        }
      }
      for (const slot of Array.from(state.pendingDeletes)) {
        if (aborted || !slot.startsWith('games/')) continue;
        const id = slot.slice('games/'.length);
        if (!deviceGameIds.has(id)) {
          state.pendingDeletes.delete(slot);
          continue;
        }
        try {
          await api(`/api/images/${slot}`, { method: 'DELETE', token: state.token });
          state.pendingDeletes.delete(slot);
          state.imageCache.delete(slot);
          savedSomething = true;
        } catch (error) {
          if (error.status === 404) {
            state.pendingDeletes.delete(slot);
            state.imageCache.delete(slot);
          } else {
            fail('游戏封面', error);
          }
        }
      }
    }
  } finally {
    state.saving = false;
    renderGames();
    updateSaveBar();
    pollStatus();
  }

  if (failures.length) {
    const detail = failures.length > 1 ? `${failures[0]}（共 ${failures.length} 项失败）` : failures[0];
    toast(`部分保存失败：${detail}`, 'error');
  } else if (savedSomething) {
    toast('更改已保存到设备');
  }
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
    saveAll();
  });
  $('#add-contact').addEventListener('click', addContact);

  $('#add-game').addEventListener('click', openPicker);
  $('#picker-close').addEventListener('click', closePicker);
  $$('.picker-tab').forEach(tab => {
    tab.addEventListener('click', () => selectPickerSource(tab.dataset.source));
  });
  $('#steam-search').addEventListener('input', scheduleSteamSearch);
  $('#p-steam').addEventListener('input', updatePickerTabs);
  $('#steam-search').addEventListener('keydown', event => {
    if (event.key !== 'Enter') return;
    event.preventDefault();
    const query = $('#steam-search').value.trim();
    if (codePoints(query) < 2) return;
    clearTimeout(state.gamePicker.searchTimer);
    const generation = ++state.gamePicker.searchGeneration;
    runSteamSearch(query, generation);
  });
  $('#game-editor-save').addEventListener('click', saveEditorToList);
  $('#game-editor-up').addEventListener('click', () => moveEditorGame(-1));
  $('#game-editor-down').addEventListener('click', () => moveEditorGame(1));
  $('#game-editor-delete').addEventListener('click', deleteEditorGame);
  $('#game-editor-close').addEventListener('click', () => closeEditor(true));
  $('#game-editor-cancel').addEventListener('click', () => closeEditor(true));
  $('#g-title').addEventListener('input', () => updateCounter($('#g-title')));

  const coverInput = $('#g-cover-file');
  coverInput.addEventListener('change', async () => {
    const game = state.games[state.editor.index];
    if (!game) return;
    const file = coverInput.files && coverInput.files[0];
    if (!file) return;
    try {
      const processed = await processImage(file, 'cover');
      setPendingImage(`games/${game.id}`, processed.bytes);
      drawCanvasInto($('#g-cover'), processed.canvas);
      markPreview($('#g-cover').closest('.preview'), true);
      $('#g-cover-status').textContent = '已生成预览，点击底部“保存更改”写入设备';
    } catch (error) {
      coverInput.value = '';
      $('#g-cover-status').textContent = error.message;
      toast(error.message, 'error');
    }
  });
  $('#g-cover-remove').addEventListener('click', removeGameCover);

  $('#save-all').addEventListener('click', saveAll);
  $('#reload-device').addEventListener('click', reloadDevice);

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
    if (!hasUnsavedChanges()) return;
    event.preventDefault();
    event.returnValue = '';
  });
  window.addEventListener('pagehide', releaseSession);

  $$('#profile-images .image-card').forEach(setupImageCard);
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
  const loads = [pollStatus()];
  if (!state.loaded.profile) loads.push(loadProfile());
  if (!state.loaded.games) loads.push(loadGames());
  await Promise.all(loads);
  setReadOnly(!state.canEdit);
  updateSaveBar();
  if (state.token) startHeartbeat();
  state.statusTimer = setInterval(pollStatus, 5000);
}

init();
