// Pictures of where the bike dirt lies, in the cloud (headless Chromium, three.js), with the game's own
// dirt sums (dirt.glsl from make_glsl.py). Setup once here: npm i three@0.160.0
// node shoot.js '[{"q":"bike=hartwell_naked&view=ex,ey,ez,tx,ty,tz&fov=32[&mode=old][&grime=0.8]","out":"a.png"}]'
// (view in the glb's metres: x forward, y up, z across; mode=old draws it as before the wheel spray.)
const {chromium} = require('playwright');
(async () => {
  const b = await chromium.launch({args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--allow-file-access-from-files']});
  const p = await b.newPage({viewport: {width: 1400, height: 900}});
  p.on('console', m => console.log('console:', m.text().slice(0, 400)));
  p.on('pageerror', e => console.log('err:', e.message.slice(0, 2000)));
  const shots = JSON.parse(process.argv[2]);
  const http = require('http'), fs = require('fs'), path = require('path');
  const srv = http.createServer((q, r) => { const name = decodeURIComponent(q.url.split('?')[0]);
    let f = path.join(__dirname, name);
    if (!fs.existsSync(f)) f = path.join(__dirname, '..', '..', 'Data', 'BikesV2', name);   // the bikes' glb and json
    fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, {'Content-Type': f.endsWith('.js') ? 'text/javascript' : f.endsWith('.html') ? 'text/html' : 'application/octet-stream'}); r.end(d); }); }).listen(8765);
  for (const s of shots) {
    await p.goto('http://localhost:8765/page.html?' + s.q);
    await p.waitForFunction('window.DONE === true', null, {timeout: 120000}).catch(e => console.log('timeout', s.out));
    await p.screenshot({path: s.out});
  }
  await b.close(); srv.close();
})();
