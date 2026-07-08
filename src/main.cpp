#include <Arduino.h>
#include <DNSServer.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#include <WebServer.h>
#include <WiFi.h>

namespace {
constexpr char kApSsid[] = "TDongle-KVM";
constexpr char kApPassword[] = "tdongle123";
constexpr byte kDnsPort = 53;
constexpr uint32_t kMaxScreenshotBytes = 180UL * 1024UL;
constexpr char kScreenshotMagic[] = "TDKVIMG1";
constexpr uint8_t kScreenshotMagicLen = 8;

IPAddress apIp(192, 168, 4, 1);
IPAddress netmask(255, 255, 255, 0);

DNSServer dnsServer;
WebServer server(80);
USBHIDKeyboard keyboard;
USBHIDMouse mouse;

uint8_t *screenshotBuffer = nullptr;
uint32_t screenshotLength = 0;
uint32_t screenshotSequence = 0;
uint32_t lastScreenshotMillis = 0;

enum class ScreenshotRxState : uint8_t {
  Magic,
  Length,
  Payload,
};

ScreenshotRxState screenshotRxState = ScreenshotRxState::Magic;
uint8_t screenshotMagicIndex = 0;
uint8_t screenshotLengthIndex = 0;
uint32_t screenshotExpectedLength = 0;
uint32_t screenshotReceivedLength = 0;

const char kIndexHtml[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>T-Dongle Remote HID</title>
<style>
:root{font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;color:#101820;background:#f4f7fb}
body{margin:0;min-height:100vh;display:grid;grid-template-rows:auto 1fr;background:linear-gradient(180deg,#eef4fb,#f8fafc)}
header{padding:14px 16px;background:#0f172a;color:#fff;box-shadow:0 2px 12px #0002}
h1{font-size:18px;margin:0 0 4px}
.status{font-size:13px;color:#cbd5e1}
main{display:grid;gap:14px;padding:14px;max-width:980px;width:100%;box-sizing:border-box;margin:0 auto}
.panel{background:#fff;border:1px solid #d9e2ec;border-radius:8px;padding:14px;box-shadow:0 1px 5px #0f172a12}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
button,input,textarea{font:inherit}
button{border:1px solid #b7c4d6;background:#f8fafc;border-radius:7px;padding:10px 12px;cursor:pointer}
button:active{transform:translateY(1px);background:#e2e8f0}
.danger{background:#fff1f2;border-color:#fda4af}
.primary{background:#dbeafe;border-color:#93c5fd}
#capture{min-height:132px;border:2px dashed #94a3b8;border-radius:8px;display:grid;place-items:center;text-align:center;padding:18px;outline:none;background:#f8fafc;touch-action:none}
#capture:focus{border-color:#2563eb;background:#eff6ff}
#pad{height:320px;border:1px solid #94a3b8;border-radius:8px;background:#111827;color:#e5e7eb;display:grid;place-items:center;text-align:center;touch-action:none;user-select:none}
#screen{width:100%;min-height:180px;background:#0f172a;border:1px solid #334155;border-radius:8px;display:grid;place-items:center;overflow:hidden}
#screen img{width:100%;height:auto;display:none}
#screen .empty{color:#cbd5e1;text-align:center;padding:24px;font-size:14px}
textarea{width:100%;min-height:80px;box-sizing:border-box;border:1px solid #cbd5e1;border-radius:7px;padding:10px;resize:vertical}
.grid{display:grid;grid-template-columns:1fr;gap:14px}
@media (min-width:760px){.grid{grid-template-columns:1fr 1fr}}
</style>
</head>
<body>
<header>
  <h1>T-Dongle Remote HID</h1>
  <div class="status" id="status">Ready. Click keyboard capture before typing.</div>
</header>
<main>
  <section class="panel">
    <div id="capture" tabindex="0">
      <div><strong>Keyboard capture</strong><br>Click here, then type. Use the release button if a key gets stuck.</div>
    </div>
    <p class="row">
      <button class="danger" onclick="releaseAll()">Release all keys</button>
      <button onclick="combo('ctrl_alt_del')">Ctrl+Alt+Del</button>
      <button onclick="combo('win_l')">Win+L</button>
      <button onclick="combo('alt_tab')">Alt+Tab</button>
      <button onclick="combo('ctrl_shift_esc')">Ctrl+Shift+Esc</button>
    </p>
  </section>
  <section class="panel">
    <h2 style="font-size:16px;margin:0 0 10px">Screen Preview</h2>
    <div id="screen">
      <img id="screenImg" alt="Latest screenshot from host">
      <div class="empty" id="screenEmpty">No screenshot received. Run the host screenshot sender on the controlled computer.</div>
    </div>
    <p class="status" id="screenStatus">Waiting for frames over USB serial.</p>
  </section>
  <section class="grid">
    <div class="panel">
      <h2 style="font-size:16px;margin:0 0 10px">Mouse</h2>
      <div id="pad">Drag here to move mouse<br>Tap/click for left click<br>Two-finger scroll may work on touchpads</div>
      <p class="row">
        <button onclick="mouseButton('left','click')">Left Click</button>
        <button onclick="mouseButton('right','click')">Right Click</button>
        <button onclick="mouseMove(0,0,-4)">Wheel Up</button>
        <button onclick="mouseMove(0,0,4)">Wheel Down</button>
      </p>
    </div>
    <div class="panel">
      <h2 style="font-size:16px;margin:0 0 10px">Send Text</h2>
      <textarea id="text" placeholder="Text to type on host"></textarea>
      <p class="row">
        <button class="primary" onclick="sendText()">Type Text</button>
        <button onclick="document.getElementById('text').value=''">Clear</button>
      </p>
    </div>
  </section>
</main>
<script>
const statusEl = document.getElementById('status');
const capture = document.getElementById('capture');
const pad = document.getElementById('pad');
const screenImg = document.getElementById('screenImg');
const screenEmpty = document.getElementById('screenEmpty');
const screenStatus = document.getElementById('screenStatus');
const pressed = new Set();
let pointerId = null;
let lastX = 0;
let lastY = 0;
let startX = 0;
let startY = 0;
let pendingDx = 0;
let pendingDy = 0;
let mouseTimer = 0;
let lastScreenSeq = 0;

function setStatus(text){ statusEl.textContent = text; }

async function post(path, data = {}) {
  const body = new URLSearchParams(data);
  try {
    const res = await fetch(path, {method:'POST', body});
    if (!res.ok) throw new Error(res.status);
  } catch (err) {
    setStatus('Request failed: ' + err.message);
  }
}

function keyPayload(ev, action) {
  return {action, code: ev.code || '', key: ev.key || ''};
}

capture.addEventListener('keydown', ev => {
  if (ev.repeat || pressed.has(ev.code)) {
    ev.preventDefault();
    return;
  }
  pressed.add(ev.code);
  post('/api/key', keyPayload(ev, 'down'));
  setStatus('Key down: ' + (ev.key || ev.code));
  ev.preventDefault();
});

capture.addEventListener('keyup', ev => {
  pressed.delete(ev.code);
  post('/api/key', keyPayload(ev, 'up'));
  setStatus('Key up: ' + (ev.key || ev.code));
  ev.preventDefault();
});

window.addEventListener('blur', releaseAll);

function releaseAll() {
  pressed.clear();
  post('/api/release');
  setStatus('Released all keys and mouse buttons');
}

function combo(name) {
  post('/api/combo', {name});
  setStatus('Sent ' + name.replaceAll('_', '+'));
}

function sendText() {
  const text = document.getElementById('text').value;
  post('/api/text', {text});
  setStatus('Sent text');
}

function flushMouse() {
  mouseTimer = 0;
  const dx = Math.max(-127, Math.min(127, Math.round(pendingDx)));
  const dy = Math.max(-127, Math.min(127, Math.round(pendingDy)));
  pendingDx -= dx;
  pendingDy -= dy;
  if (dx || dy) post('/api/mouse', {dx, dy, wheel:0});
  if (Math.abs(pendingDx) >= 1 || Math.abs(pendingDy) >= 1) scheduleMouse();
}

function scheduleMouse() {
  if (!mouseTimer) mouseTimer = setTimeout(flushMouse, 18);
}

function mouseMove(dx, dy, wheel = 0) {
  post('/api/mouse', {dx, dy, wheel});
}

function mouseButton(button, action) {
  post('/api/mouseButton', {button, action});
}

pad.addEventListener('pointerdown', ev => {
  pointerId = ev.pointerId;
  lastX = ev.clientX;
  lastY = ev.clientY;
  startX = ev.clientX;
  startY = ev.clientY;
  pad.setPointerCapture(pointerId);
  ev.preventDefault();
});

pad.addEventListener('pointermove', ev => {
  if (ev.pointerId !== pointerId) return;
  pendingDx += (ev.clientX - lastX) * 1.3;
  pendingDy += (ev.clientY - lastY) * 1.3;
  lastX = ev.clientX;
  lastY = ev.clientY;
  scheduleMouse();
  ev.preventDefault();
});

pad.addEventListener('pointerup', ev => {
  if (ev.pointerId !== pointerId) return;
  pad.releasePointerCapture(pointerId);
  pointerId = null;
  if (Math.abs(ev.clientX - startX) < 6 && Math.abs(ev.clientY - startY) < 6) mouseButton('left', 'click');
  ev.preventDefault();
});

pad.addEventListener('wheel', ev => {
  mouseMove(0, 0, ev.deltaY > 0 ? 3 : -3);
  ev.preventDefault();
}, {passive:false});

async function refreshScreen() {
  try {
    const res = await fetch('/api/screenshotStatus', {cache:'no-store'});
    if (!res.ok) throw new Error(res.status);
    const data = await res.json();
    if (!data.available) {
      screenStatus.textContent = 'Waiting for frames over USB serial.';
      return;
    }
    screenStatus.textContent = 'Latest frame: ' + data.bytes + ' bytes, ' + data.age_ms + ' ms old';
    if (data.seq !== lastScreenSeq) {
      lastScreenSeq = data.seq;
      screenImg.onload = () => {
        screenImg.style.display = 'block';
        screenEmpty.style.display = 'none';
      };
      screenImg.src = '/screenshot.jpg?seq=' + data.seq;
    }
  } catch (err) {
    screenStatus.textContent = 'Screen status failed: ' + err.message;
  }
}

setInterval(refreshScreen, 1000);
refreshScreen();
</script>
</body>
</html>
)rawliteral";

void resetScreenshotRx() {
  screenshotRxState = ScreenshotRxState::Magic;
  screenshotMagicIndex = 0;
  screenshotLengthIndex = 0;
  screenshotExpectedLength = 0;
  screenshotReceivedLength = 0;
}

void readScreenshotSerial() {
  if (!screenshotBuffer) {
    return;
  }

  uint16_t processed = 0;
  while (Serial.available() && processed < 4096) {
    const uint8_t b = static_cast<uint8_t>(Serial.read());
    ++processed;

    switch (screenshotRxState) {
      case ScreenshotRxState::Magic:
        if (b == static_cast<uint8_t>(kScreenshotMagic[screenshotMagicIndex])) {
          ++screenshotMagicIndex;
          if (screenshotMagicIndex == kScreenshotMagicLen) {
            screenshotRxState = ScreenshotRxState::Length;
            screenshotLengthIndex = 0;
            screenshotExpectedLength = 0;
          }
        } else {
          screenshotMagicIndex = (b == static_cast<uint8_t>(kScreenshotMagic[0])) ? 1 : 0;
        }
        break;

      case ScreenshotRxState::Length:
        screenshotExpectedLength |= static_cast<uint32_t>(b) << (8 * screenshotLengthIndex);
        ++screenshotLengthIndex;
        if (screenshotLengthIndex == 4) {
          if (screenshotExpectedLength == 0 || screenshotExpectedLength > kMaxScreenshotBytes) {
            resetScreenshotRx();
          } else {
            screenshotLength = 0;
            screenshotReceivedLength = 0;
            screenshotRxState = ScreenshotRxState::Payload;
          }
        }
        break;

      case ScreenshotRxState::Payload:
        screenshotBuffer[screenshotReceivedLength++] = b;
        if (screenshotReceivedLength == screenshotExpectedLength) {
          screenshotLength = screenshotExpectedLength;
          ++screenshotSequence;
          lastScreenshotMillis = millis();
          resetScreenshotRx();
        }
        break;
    }
  }
}

uint8_t mouseButtonMask(const String &button) {
  if (button == "right") {
    return MOUSE_RIGHT;
  }
  if (button == "middle") {
    return MOUSE_MIDDLE;
  }
  return MOUSE_LEFT;
}

int clampHidDelta(const String &value) {
  return constrain(value.toInt(), -127, 127);
}

uint8_t mapKeyboardCode(const String &code, const String &key) {
  if (code == "ControlLeft") return KEY_LEFT_CTRL;
  if (code == "ControlRight") return KEY_RIGHT_CTRL;
  if (code == "ShiftLeft") return KEY_LEFT_SHIFT;
  if (code == "ShiftRight") return KEY_RIGHT_SHIFT;
  if (code == "AltLeft") return KEY_LEFT_ALT;
  if (code == "AltRight") return KEY_RIGHT_ALT;
  if (code == "MetaLeft") return KEY_LEFT_GUI;
  if (code == "MetaRight") return KEY_RIGHT_GUI;
  if (code == "ArrowUp") return KEY_UP_ARROW;
  if (code == "ArrowDown") return KEY_DOWN_ARROW;
  if (code == "ArrowLeft") return KEY_LEFT_ARROW;
  if (code == "ArrowRight") return KEY_RIGHT_ARROW;
  if (code == "Backspace") return KEY_BACKSPACE;
  if (code == "Tab") return KEY_TAB;
  if (code == "Enter" || code == "NumpadEnter") return KEY_RETURN;
  if (code == "Escape") return KEY_ESC;
  if (code == "Insert") return KEY_INSERT;
  if (code == "Delete") return KEY_DELETE;
  if (code == "Home") return KEY_HOME;
  if (code == "End") return KEY_END;
  if (code == "PageUp") return KEY_PAGE_UP;
  if (code == "PageDown") return KEY_PAGE_DOWN;
  if (code == "CapsLock") return KEY_CAPS_LOCK;
  if (code == "F1") return KEY_F1;
  if (code == "F2") return KEY_F2;
  if (code == "F3") return KEY_F3;
  if (code == "F4") return KEY_F4;
  if (code == "F5") return KEY_F5;
  if (code == "F6") return KEY_F6;
  if (code == "F7") return KEY_F7;
  if (code == "F8") return KEY_F8;
  if (code == "F9") return KEY_F9;
  if (code == "F10") return KEY_F10;
  if (code == "F11") return KEY_F11;
  if (code == "F12") return KEY_F12;

  if (key.length() == 1) {
    return static_cast<uint8_t>(key[0]);
  }
  return 0;
}

void sendNoCache() {
  server.sendHeader("Cache-Control", "no-store");
}

void sendOk(const char *message = "ok") {
  server.send(200, "text/plain", message);
}

void handleRoot() {
  sendNoCache();
  server.send_P(200, "text/html", kIndexHtml);
}

void handleScreenshot() {
  sendNoCache();
  if (!screenshotBuffer || screenshotLength == 0) {
    server.send(404, "text/plain", "no screenshot");
    return;
  }

  server.setContentLength(screenshotLength);
  server.send(200, "image/jpeg", "");
  server.client().write(screenshotBuffer, screenshotLength);
}

void handleScreenshotStatus() {
  sendNoCache();
  const bool available = screenshotBuffer && screenshotLength > 0;
  const uint32_t ageMs = available ? millis() - lastScreenshotMillis : 0;
  String json = "{";
  json += "\"available\":";
  json += available ? "true" : "false";
  json += ",\"seq\":";
  json += screenshotSequence;
  json += ",\"bytes\":";
  json += screenshotLength;
  json += ",\"age_ms\":";
  json += ageMs;
  json += "}";
  server.send(200, "application/json", json);
}

void handleNotFound() {
  sendNoCache();
  server.sendHeader("Location", String("http://") + apIp.toString() + "/", true);
  server.send(302, "text/plain", "");
}

void handleKey() {
  const String action = server.arg("action");
  const String code = server.arg("code");
  const String key = server.arg("key");
  const uint8_t mapped = mapKeyboardCode(code, key);
  if (!mapped) {
    server.send(400, "text/plain", "unsupported key");
    return;
  }

  if (action == "down") {
    keyboard.press(mapped);
  } else if (action == "up") {
    keyboard.release(mapped);
  } else {
    server.send(400, "text/plain", "bad action");
    return;
  }
  sendOk();
}

void handleText() {
  keyboard.print(server.arg("text"));
  sendOk();
}

void handleMouse() {
  const int dx = clampHidDelta(server.arg("dx"));
  const int dy = clampHidDelta(server.arg("dy"));
  const int wheel = clampHidDelta(server.arg("wheel"));
  mouse.move(dx, dy, wheel);
  sendOk();
}

void handleMouseButton() {
  const uint8_t button = mouseButtonMask(server.arg("button"));
  const String action = server.arg("action");
  if (action == "down") {
    mouse.press(button);
  } else if (action == "up") {
    mouse.release(button);
  } else if (action == "click") {
    mouse.click(button);
  } else {
    server.send(400, "text/plain", "bad action");
    return;
  }
  sendOk();
}

void tapKey(uint8_t key, uint16_t holdMs = 35) {
  keyboard.press(key);
  delay(holdMs);
  keyboard.release(key);
}

void handleCombo() {
  const String name = server.arg("name");
  if (name == "ctrl_alt_del") {
    keyboard.press(KEY_LEFT_CTRL);
    keyboard.press(KEY_LEFT_ALT);
    tapKey(KEY_DELETE);
    keyboard.release(KEY_LEFT_ALT);
    keyboard.release(KEY_LEFT_CTRL);
  } else if (name == "win_l") {
    keyboard.press(KEY_LEFT_GUI);
    tapKey('l');
    keyboard.release(KEY_LEFT_GUI);
  } else if (name == "alt_tab") {
    keyboard.press(KEY_LEFT_ALT);
    tapKey(KEY_TAB, 80);
    keyboard.release(KEY_LEFT_ALT);
  } else if (name == "ctrl_shift_esc") {
    keyboard.press(KEY_LEFT_CTRL);
    keyboard.press(KEY_LEFT_SHIFT);
    tapKey(KEY_ESC);
    keyboard.release(KEY_LEFT_SHIFT);
    keyboard.release(KEY_LEFT_CTRL);
  } else {
    server.send(400, "text/plain", "bad combo");
    return;
  }
  sendOk();
}

void handleRelease() {
  keyboard.releaseAll();
  mouse.release(MOUSE_LEFT);
  mouse.release(MOUSE_RIGHT);
  mouse.release(MOUSE_MIDDLE);
  sendOk();
}

void setupServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/generate_204", HTTP_GET, handleRoot);
  server.on("/gen_204", HTTP_GET, handleRoot);
  server.on("/hotspot-detect.html", HTTP_GET, handleRoot);
  server.on("/ncsi.txt", HTTP_GET, handleRoot);
  server.on("/connecttest.txt", HTTP_GET, handleRoot);
  server.on("/screenshot.jpg", HTTP_GET, handleScreenshot);
  server.on("/api/screenshotStatus", HTTP_GET, handleScreenshotStatus);
  server.on("/api/key", HTTP_POST, handleKey);
  server.on("/api/text", HTTP_POST, handleText);
  server.on("/api/mouse", HTTP_POST, handleMouse);
  server.on("/api/mouseButton", HTTP_POST, handleMouseButton);
  server.on("/api/combo", HTTP_POST, handleCombo);
  server.on("/api/release", HTTP_POST, handleRelease);
  server.onNotFound(handleNotFound);
  server.begin();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  screenshotBuffer = static_cast<uint8_t *>(malloc(kMaxScreenshotBytes));

  keyboard.begin();
  mouse.begin();
  USB.begin();

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIp, apIp, netmask);
  WiFi.softAP(kApSsid, kApPassword);

  dnsServer.start(kDnsPort, "*", apIp);
  setupServer();

  Serial.println();
  Serial.println("T-Dongle Remote HID ready");
  Serial.print("SSID: ");
  Serial.println(kApSsid);
  Serial.print("URL: http://");
  Serial.println(apIp);
  Serial.print("Screenshot buffer: ");
  Serial.println(screenshotBuffer ? "ready" : "allocation failed");
}

void loop() {
  readScreenshotSerial();
  dnsServer.processNextRequest();
  server.handleClient();
}
