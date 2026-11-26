// Usage: (cd tools/page_test && npm install) && node tools/page_test/run.js data/index.html <screenshot-dir>
// Fake hub: serves data/index.html and a WebSocket that sends canned states.
// Drives the page with Playwright at iPhone size and screenshots each screen.
const http = require('http'), fs = require('fs'), path = require('path');
const { WebSocketServer } = require('ws');
const { chromium, devices } = require('playwright');
const page_html = fs.readFileSync(process.argv[2]);
const out = process.argv[3];
const received = [];

const base = {
  t: 'state', phase: 'idle', countdown: 3, drill: 0, seconds: 30, secondsOptions: [20, 30, 45, 60],
  drills: [
    { name: 'Reaction', desc: 'Hit the pod that lights. Random gaps, never the same pod twice.' },
    { name: 'Sprint', desc: 'As many hits as you can. Next pod lights straight away.' },
    { name: 'Sequence', desc: 'Pods light in order 1, 2, 3... for circuits.' }],
  player: 0, players: ['Sam', 'Dad', 'Player 3', 'Player 4'],
  pods: [['#ff3b30', 'connected'], ['#34c759', 'connected'], ['#0a84ff', 'searching'], ['#ffb400', 'connected']]
    .map(([c, st], i) => ({ n: i + 1, c, st })),
  demo: false,
};
let state = { ...base };
const srv = http.createServer((req, res) => {
  if (req.url === '/api/status') {
    res.setHeader('content-type', 'application/json');
    return res.end(JSON.stringify({ up: 754000, heap: 98304, minHeap: 81920, pods: [{ n: 1, st: 'connected', rssi: -61, drops: 0 }] }));
  }
  res.setHeader('content-type', 'text/html'); res.end(page_html);
});
const wss = new WebSocketServer({ server: srv, path: '/ws' });
const socks = new Set();
wss.on('connection', (s) => { socks.add(s); s.send(JSON.stringify(state)); s.on('message', (m) => received.push(JSON.parse(m))); });
const push = (st) => { state = st; for (const s of socks) s.send(JSON.stringify(st)); };

srv.listen(0, async () => {
  const url = `http://127.0.0.1:${srv.address().port}/`;
  const browser = await chromium.launch();
  for (const scheme of ['light', 'dark']) {
    const ctx = await browser.newContext({ ...devices['iPhone 13'], colorScheme: scheme });
    const p = await ctx.newPage();
    const errors = [];
    p.on('pageerror', (e) => errors.push(e.message));
    p.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
    push({ ...base });
    await p.goto(url); await p.waitForTimeout(400);
    await p.screenshot({ path: `${out}/1-home-${scheme}.png`, fullPage: true });
    if (scheme === 'light') {
      await p.click('#startBtn'); await p.click('[data-drill="1"]'); await p.click('[data-sec="45"]'); await p.click('[data-pod="0"]');
    }
    push({ ...base, phase: 'countdown', countdown: 2 }); await p.waitForTimeout(200);
    await p.screenshot({ path: `${out}/2-countdown-${scheme}.png` });
    push({ ...base, phase: 'running', run: { hits: 14, misses: 2, lastMs: 412, avgMs: 488, remainingMs: 12000, durationMs: 30000, lit: 1 } });
    await p.waitForTimeout(600);
    await p.screenshot({ path: `${out}/3-running-${scheme}.png` });
    if (scheme === 'light') await p.click('#stopBtn');
    push({ ...base, phase: 'finished', result: { hits: 23, misses: 2, wrong: 1, avgMs: 451, bestMs: 298, worstMs: 802, aborted: false, pb: true,
      pods: [{ hits: 7, misses: 0, avgMs: 430, bestMs: 298 }, { hits: 6, misses: 1, avgMs: 470, bestMs: 350 }, { hits: 0, misses: 0, avgMs: 0, bestMs: 0 }, { hits: 10, misses: 1, avgMs: 455, bestMs: 320 }] } });
    await p.waitForTimeout(200);
    await p.screenshot({ path: `${out}/4-results-${scheme}.png`, fullPage: true });
    if (scheme === 'light') { await p.click('#repeatBtn'); await p.click('#doneBtn'); }
    push({ ...base }); await p.waitForTimeout(200);
    await p.click('#playerBtn'); await p.waitForTimeout(200);
    if (scheme === 'light') { await p.click('[data-player="1"]'); await p.fill('#renameInput', 'Pete'); await p.click('#saveName'); }
    await p.screenshot({ path: `${out}/5-players-${scheme}.png` });
    await p.click('#closePlayers');
    await p.click('#details summary'); await p.waitForTimeout(300);
    await p.screenshot({ path: `${out}/6-details-${scheme}.png`, fullPage: true });
    // Hub goes away -> offline banner
    for (const s of socks) s.terminate();
    await p.waitForTimeout(300);
    await p.screenshot({ path: `${out}/7-offline-${scheme}.png` });
    // horizontal overflow check
    const overflow = await p.evaluate(() => document.documentElement.scrollWidth > window.innerWidth);
    console.log(scheme, 'errors:', JSON.stringify(errors), 'h-overflow:', overflow);
    await ctx.close();
  }
  await browser.close();
  console.log('commands received:', JSON.stringify(received.map((r) => r.cmd === 'hello' ? { cmd: 'hello', time: r.time.length } : r)));
  srv.close(); process.exit(0);
});
