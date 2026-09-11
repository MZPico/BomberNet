// Two browser tabs play a BomberNet network match on the site (staging by default):
// tab A hosts, tab B joins by the code shown in the page's net panel, both ready up,
// then the panels are watched for "running" without DESYNC.
//   chromium --remote-debugging-port=9222 --autoplay-policy=no-user-gesture-required \
//            --ignore-gpu-blocklist --enable-unsafe-swiftshader about:blank &
//   node tools/browser_match.mjs [https://staging.mzpico.com/play/bombernet/] [seconds]
const [base = 'https://staging.mzpico.com/play/bombernet/', secs = '40'] = process.argv.slice(2);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const KEYS = {
  Up: { key: 'ArrowUp', code: 'ArrowUp', vk: 38 }, Down: { key: 'ArrowDown', code: 'ArrowDown', vk: 40 },
  Left: { key: 'ArrowLeft', code: 'ArrowLeft', vk: 37 }, Right: { key: 'ArrowRight', code: 'ArrowRight', vk: 39 },
  Space: { key: ' ', code: 'Space', vk: 32 },
};

async function tab(url) {
  const t = await (await fetch(`http://127.0.0.1:9222/json/new?${encodeURIComponent(url)}`, { method: 'PUT' })).json();
  const ws = new WebSocket(t.webSocketDebuggerUrl);
  await new Promise((r) => (ws.onopen = r));
  let id = 0; const pending = new Map(); const log = [];
  ws.onmessage = (e) => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
    if (m.method === 'Runtime.consoleAPICalled') log.push(m.params.args.map((a) => a.value ?? a.description).join(' ').slice(0, 120));
    if (m.method === 'Runtime.exceptionThrown') log.push('EXC ' + (m.params.exceptionDetails.exception?.description ?? m.params.exceptionDetails.text).slice(0, 160));
  };
  const send = (method, params = {}) => new Promise((res) => { const i = ++id; pending.set(i, res); ws.send(JSON.stringify({ id: i, method, params })); });
  const ev = async (x) => (await send('Runtime.evaluate', { expression: x, returnByValue: true, awaitPromise: true })).result?.result?.value;
  await send('Page.enable'); await send('Runtime.enable');
  // SDL (Emscripten) listens on the canvas; the page's touch controls inject
  // synthetic KeyboardEvents there, so do the same (CDP key events do not arrive)
  const key = async (name, hold = 160) => {
    const k = KEYS[name];
    const fire = (type) => ev(`document.getElementById('canvas').dispatchEvent(new KeyboardEvent('${type}', { code: '${k.code}', key: '${k.key}', keyCode: ${k.vk}, which: ${k.vk}, bubbles: true, cancelable: true })); 1`);
    await fire('keydown');
    await sleep(hold);
    await fire('keyup');
    await sleep(260);
  };
  const panel = () => ev("document.getElementById('netpanel')?.textContent ?? ''");
  const start = async () => {
    await sleep(2500);
    await ev("document.getElementById('play-start')?.click()");
    for (let i = 0; i < 60; i++) { if (await ev('!!window.__mzReady')) break; await sleep(1000); }
    await sleep(4000);                                   // title screen up, detection done
    await ev("document.getElementById('play-canvas')?.focus?.() ?? document.querySelector('canvas')?.focus?.()");
  };
  return { ws, ev, key, panel, start, log, ws_send: send };
}

const ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ';
for (const t of await (await fetch('http://127.0.0.1:9222/json/list')).json())   // close leftovers from earlier runs
  if (t.type === 'page') await fetch(`http://127.0.0.1:9222/json/close/${t.id}`).catch(() => {});
const a = await tab(base), b = await tab(base);
await a.start(); await b.start();
console.log('A panel at title:', await a.panel(), '| B:', await b.panel());
const shot = async (t, name) => { const r = await t.ws_send('Page.captureScreenshot', { format: 'png' }); (await import('node:fs')).writeFileSync(name, Buffer.from(r.result.data, 'base64')); };
await shot(a, 'build/browser_title.png');
// A: NETWORK row is the 4th (MODE, PLAYERS, JOYSTICK, NETWORK) -> HOST, then SPACE
for (let i = 0; i < 3; i++) await a.key('Down');
await a.key('Right');
await a.key('Space');
let code = '';
for (let i = 0; i < 30 && !code; i++) { await sleep(1000); code = (await a.panel()).match(/room ([A-Z]{4})/)?.[1] ?? ''; }
console.log('A hosts room', code || '(none)', '| panel:', await a.panel());
if (!code) { console.log('A log:', a.log.slice(-6).join(' | ')); process.exit(1); }
// B: NETWORK -> JOIN, SPACE, type the code (UP cycles the letter), SPACE joins
for (let i = 0; i < 3; i++) await b.key('Down');
await b.key('Right'); await b.key('Right');
await b.key('Space');
await sleep(1500);
for (let p = 0; p < 4; p++) {
  // the browser emulator can run below real time: slow, well-separated presses,
  // and the shorter way round the 24-letter ring
  const n = ALPHABET.indexOf(code[p]);
  const up = n <= 12;
  for (let i = 0; i < (up ? n : 24 - n); i++) { await b.key(up ? 'Up' : 'Down', 220); await sleep(200); }
  if (p < 3) await b.key('Right', 220);
}
await b.key('Space');
for (let i = 0; i < 20; i++) { await sleep(1000); if (/2 players/.test(await a.panel())) break; }
console.log('after join - A:', await a.panel(), '| B:', await b.panel());
await sleep(1500);
await a.key('Space'); await b.key('Space');           // ready
for (let i = 0; i < 20; i++) { await sleep(1000); if (/running/.test(await a.panel())) break; }
console.log('after ready - A:', await a.panel(), '| B:', await b.panel());
// play: A walks, B walks; then watch for a desync verdict
const t0 = Date.now(); let bad = false;
while (Date.now() - t0 < Number(secs) * 1000) {
  await a.key('Left', 400); await b.key('Right', 400);
  const pa = await a.panel(), pb = await b.panel();
  if (/DESYNC|left/.test(pa + pb)) { bad = true; console.log('problem:', pa, '|', pb); break; }
}
console.log('final - A:', await a.panel(), '| B:', await b.panel());
console.log('A log:', a.log.slice(-4).join(' | ')); console.log('B log:', b.log.slice(-4).join(' | '));
const ok = !bad && /running/.test(await a.panel()) && /running/.test(await b.panel());
console.log(ok ? 'RESULT: OK (both running, no desync)' : 'RESULT: FAIL');
a.ws.close(); b.ws.close(); process.exit(ok ? 0 : 1);
