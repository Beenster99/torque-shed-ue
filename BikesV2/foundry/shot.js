const { chromium } = require('playwright');
(async () => {
  const [,, url, out, w, h] = process.argv;
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: +w || 900, height: +h || 450 } });
  await p.goto(url); await p.screenshot({ path: out, fullPage: true }); await b.close();
})();
