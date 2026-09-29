// The BomberNet play page (mzpico.com) joins a room, for tools/netbench.py:
// a tab in Chromium over its debugging port (see tools/browser_match.mjs),
// keys as synthetic KeyboardEvents on the emulator's canvas.
//   node tools/web_join.mjs CODE LOBBY_SECONDS GAME_SECONDS [base URL]
// Prints JOINED, READY, RUNNING, DONE as it goes; ready after LOBBY_SECONDS
// in the lobby (the host readies after that), then stays GAME_SECONDS.
const [code, lobbyS = '12', gameS = '25', base = 'https://mzpico.com/play/bombernet/'] = process.argv.slice(2);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const KEYS = {
  Up: { key: 'ArrowUp', code: 'ArrowUp', vk: 38 }, Down: { key: 'ArrowDown', code: 'ArrowDown', vk: 40 },
  Left: { key: 'ArrowLeft', code: 'ArrowLeft', vk: 37 }, Right: { key: 'ArrowRight', code: 'ArrowRight', vk: 39 },
  Space: { key: ' ', code: 'Space', vk: 32 },
};
const ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ';
const say = (s) => console.log(`${s} ${(Date.now() / 1000).toFixed(3)}`);

const cdp = async (path, opts) => {                  // Chromium may still be starting
  for (let i = 0; ; i++) {
    try { return await fetch(`http://127.0.0.1:9222${path}`, opts); } catch (e) { if (i > 60) throw e; await sleep(500); }
  }
};
const t = await (await cdp(`/json/new?${encodeURIComponent(base)}`, { method: 'PUT' })).json();
for (const o of await (await cdp('/json/list')).json())   // leftovers from earlier runs; the new tab first:
  if (o.type === 'page' && o.id !== t.id) await cdp(`/json/close/${o.id}`).catch(() => {});   // Chromium quits with its last tab
const ws = new WebSocket(t.webSocketDebuggerUrl);
await new Promise((r) => (ws.onopen = r));
let id = 0; const pending = new Map();
ws.onmessage = (e) => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } };
const send = (method, params = {}) => new Promise((res) => { const i = ++id; pending.set(i, res); ws.send(JSON.stringify({ id: i, method, params })); });
const ev = async (x) => (await send('Runtime.evaluate', { expression: x, returnByValue: true, awaitPromise: true })).result?.result?.value;
const key = async (name, hold = 200) => {
  const k = KEYS[name];
  const fire = (type) => ev(`document.getElementById('canvas').dispatchEvent(new KeyboardEvent('${type}', { code: '${k.code}', key: '${k.key}', keyCode: ${k.vk}, which: ${k.vk}, bubbles: true, cancelable: true })); 1`);
  await fire('keydown'); await sleep(hold); await fire('keyup'); await sleep(300);
};
const panel = () => ev("document.getElementById('netpanel')?.textContent ?? ''");
const shot = async (name) => {                       // SHOT=<prefix>: screenshots for debugging
  if (!process.env.SHOT) return;
  const r = await send('Page.captureScreenshot', { format: 'png' });
  (await import('node:fs')).writeFileSync(`${process.env.SHOT}-${name}.png`, Buffer.from(r.result.data, 'base64'));
};

await send('Page.enable'); await send('Runtime.enable');
await sleep(2500);
await ev("document.getElementById('play-start')?.click()");
for (let i = 0; i < 60; i++) { if (await ev('!!window.__mzReady')) break; await sleep(1000); }
await sleep(4000);                                   // title up, network detected
await shot('title');
// title menu: MODE, NETWORK, ...: NETWORK -> JOIN, fire: the code screen
await key('Down'); await key('Right'); await key('Right'); await key('Space');
await sleep(1500);
for (let p = 0; p < 4; p++) {                        // UP/DOWN cycle a letter; slow presses, the shorter way round
  const n = ALPHABET.indexOf(code[p]), up = n <= 12;
  for (let i = 0; i < (up ? n : 24 - n); i++) { await key(up ? 'Up' : 'Down', 220); await sleep(200); }
  if (p < 3) await key('Right', 220);
}
await shot('code');
await key('Space');
for (let i = 0; i < 30; i++) { await sleep(500); if (/2 players|members 2|2\/2/.test(await panel())) break; }
await shot('joined');
say('JOINED'); console.log('panel:', await panel());
await sleep(Number(lobbyS) * 1000);
await key('Space'); say('READY');
for (let i = 0; i < 60; i++) { await sleep(500); if (/running/.test(await panel())) break; }
say('RUNNING'); console.log('panel:', await panel());
await sleep(Number(gameS) * 1000);
console.log('panel:', await panel()); say('DONE');
await cdp('/json/new?about:blank', { method: 'PUT' }).catch(() => {});   // keep Chromium for the next run
await cdp(`/json/close/${t.id}`).catch(() => {});
ws.close(); process.exit(0);
