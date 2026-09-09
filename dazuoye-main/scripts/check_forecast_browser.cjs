const {chromium} = require(process.env.PLAYWRIGHT_MODULE || '../.runtime/ui-refactor/node_modules/playwright');
const assert = require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({headless:true,args:['--no-sandbox']});
 try {
 const page=await browser.newPage({viewport:{width:1440,height:1100},deviceScaleFactor:1});
 const errors=[]; page.on('pageerror',e=>errors.push(e.message));
 await page.goto('http://127.0.0.1:4173/ui/admin.html#forecast');
 await page.locator('.forecast-chart').waitFor();
 await page.evaluate(()=>document.fonts.ready);
 assert.equal(await page.locator('#forecast-scope option').count(),276);
 assert.ok((await page.locator('#forecast-content').innerText()).includes('2023-02-28'));
 await page.screenshot({path:'.runtime/forecast/admin-forecast.png',fullPage:true});
 const region=await page.locator('#forecast-scope option').nth(1).getAttribute('value');
 await page.selectOption('#forecast-scope',region);
 await page.locator('.forecast-kpis').getByText('区域 '+region,{exact:false}).waitFor();
 for(const width of [390,768,1920]) {
  await page.setViewportSize({width,height:1100});
  assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+2), 'overflow at '+width);
 }
 await page.locator('[data-page="dashboard"]').click();
 assert.ok(await page.locator('#page-dashboard').isVisible());
 await page.locator('[data-page="forecast"]').click();
 await page.locator('.forecast-chart').waitFor();
 assert.deepEqual(errors,[]);
 console.log('PASS: forecast deep link, 276 scopes, region change, historical date, responsive 390/768/1440/1920, navigation, no browser errors.');
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exit(1)});
