#!/usr/bin/env node
// Drives a page in headless Chrome over the DevTools protocol (no window, no dependencies).
// node tools/cdp.js <url> <steps.json> [outdir]
// Steps: {"wait": ms} | {"until": "js expr"} | {"click": "css"} | {"eval": "js"} | {"type": ["css", "text"]}
//        | {"shot": "name.png", "full": true} | {"log": "js expr"}
var fs = require('fs');
var os = require('os');
var path = require('path');
var spawn = require('child_process').spawn;

var CHROME = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
var url = process.argv[2], steps = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
var outdir = process.argv[4] || '.';
var WIDTH = +process.env.PLANNER_WIDTH || 412;  // CSS pixels, a phone's width

function sleep(ms) { return new Promise(function (r) { setTimeout(r, ms); }); }

async function main() {
  var profile = fs.mkdtempSync(path.join(os.tmpdir(), 'coach-cdp-'));
  var chrome = spawn(CHROME, ['--headless=new', '--remote-debugging-port=0', '--user-data-dir=' + profile, '--no-first-run',
    '--no-default-browser-check', '--hide-scrollbars', '--window-size=' + WIDTH + ',915', 'about:blank'], { stdio: 'ignore' });
  try {
    var portFile = path.join(profile, 'DevToolsActivePort'), port;
    for (var i = 0; i < 100 && !port; i++) {
      await sleep(100);
      if (fs.existsSync(portFile)) port = fs.readFileSync(portFile, 'utf8').split('\n')[0];
    }
    if (!port) throw new Error('Chrome did not start');
    var targets = await (await fetch('http://127.0.0.1:' + port + '/json/list')).json();
    var page = targets.filter(function (t) { return t.type === 'page'; })[0];
    var ws = new WebSocket(page.webSocketDebuggerUrl);
    await new Promise(function (r, j) { ws.onopen = r; ws.onerror = j; });
    var seq = 0, pending = {};
    ws.onmessage = function (m) {
      var msg = JSON.parse(m.data);
      if (msg.id && pending[msg.id]) { pending[msg.id](msg); delete pending[msg.id]; }
    };
    function call(method, params) {
      return new Promise(function (resolve, reject) {
        var id = ++seq;
        pending[id] = function (msg) { msg.error ? reject(new Error(method + ': ' + msg.error.message)) : resolve(msg.result); };
        ws.send(JSON.stringify({ id: id, method: method, params: params || {} }));
      });
    }
    async function evaluate(expr) {
      var r = await call('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true });
      if (r.exceptionDetails) throw new Error('eval failed: ' + (r.exceptionDetails.exception && r.exceptionDetails.exception.description));
      return r.result.value;
    }
    await call('Emulation.setDeviceMetricsOverride', { width: WIDTH, height: 915, deviceScaleFactor: 2, mobile: true });
    await call('Page.enable');
    await call('Page.navigate', { url: url });
    await sleep(800);
    for (var s = 0; s < steps.length; s++) {
      var st = steps[s];
      if (st.wait) await sleep(st.wait);
      if (st.until) {
        var ok = false;
        for (var t = 0; t < 100 && !ok; t++) { try { ok = await evaluate(st.until); } catch (e) { ok = false; } if (!ok) await sleep(100); }
        if (!ok) throw new Error('timed out waiting for ' + st.until);
      }
      if (st.click) {
        var found = await evaluate('(function(){var e=document.querySelector(' + JSON.stringify(st.click) + ');if(!e)return false;e.click();return true})()');
        if (!found) throw new Error('no element ' + st.click);
        await sleep(120);
      }
      if (st.type) {
        await evaluate('(function(){var e=document.querySelector(' + JSON.stringify(st.type[0]) + ');e.focus();e.value=' + JSON.stringify(st.type[1]) +
          ';e.dispatchEvent(new Event("input",{bubbles:true}));e.dispatchEvent(new Event("change",{bubbles:true}))})()');
        await sleep(120);
      }
      if (st.eval) await evaluate(st.eval);
      if (st.log) console.log(JSON.stringify(await evaluate(st.log)));
      if (st.shot) {
        var params = { format: 'png' };
        if (st.full) {
          var h = await evaluate('Math.max(document.documentElement.scrollHeight, document.body.scrollHeight)');
          params.captureBeyondViewport = true;
          params.clip = { x: 0, y: 0, width: WIDTH, height: h, scale: 1 };
        }
        var shot = await call('Page.captureScreenshot', params);
        fs.writeFileSync(path.join(outdir, st.shot), Buffer.from(shot.data, 'base64'));
      }
    }
    ws.close();
  } finally {
    chrome.kill();
    await sleep(300);
    fs.rmSync(profile, { recursive: true, force: true });
  }
}

main().catch(function (e) { console.error('cdp: ' + e.message); process.exit(1); });
