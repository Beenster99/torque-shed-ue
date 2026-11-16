// Turns the TFT screens' pictures (SimTest/tft_test <dir> writes them as SVG) into one PNG sheet:
//     node Tools/tft_sheets.js <svg_dir> <out.png> <columns> <name> [<name> ...]
// Needs Playwright with Chromium (as the cloud containers have).
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

(async () => {
  const [dir, out, cols, ...names] = process.argv.slice(2);
  const cells = names.map((n) => {
    const svg = fs.readFileSync(path.join(dir, n + '.svg'), 'utf8');
    const round = n.startsWith('round') ? ' r' : '';
    return `<div class="c"><div class="s${round}">${svg}</div><div class="n">${n.replace(/_/g, ' ')}</div></div>`;
  }).join('');
  const html = `<html><body style="margin:0;background:#1b1d21;font-family:Arial">
  <div id="g" style="display:grid;grid-template-columns:repeat(${cols},max-content);gap:18px;padding:18px;align-items:start">${cells}</div>
  <style>.c{width:max-content}.s{background:#000;border-radius:12px;overflow:hidden;line-height:0;border:7px solid #2a2c31}.s.r{border-radius:50%}
  .n{color:#aab;font-size:15px;padding:6px 2px}</style></body></html>`;
  const browser = await chromium.launch();
  const page = await browser.newPage({ deviceScaleFactor: 1, viewport: { width: 3000, height: 2200 } });
  await page.setContent(html);
  await (await page.$('#g')).screenshot({ path: out });
  await browser.close();
})();
