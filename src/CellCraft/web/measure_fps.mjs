import puppeteer from 'puppeteer-core';

const url = process.argv[2] || 'http://localhost:4173/';
const durationMs = parseInt(process.argv[3] || '20000', 10);

const browser = await puppeteer.launch({
	executablePath: '/usr/bin/google-chrome',
	headless: 'new',
	args: [
		'--no-sandbox',
		'--disable-dev-shm-usage',
		'--window-size=1280,800',
		'--enable-unsafe-webgl',
		'--enable-webgl',
		'--use-gl=angle',
		'--use-angle=swiftshader',
		'--enable-features=Vulkan',
		'--ignore-gpu-blocklist',
		'--enable-gpu-rasterization',
	],
	defaultViewport: { width: 1280, height: 800 },
});

const page = await browser.newPage();
const fpsLines = [];
page.on('console', (msg) => {
	const text = msg.text();
	if (text.startsWith('[fps]') || text.startsWith('[perf]')) {
		fpsLines.push(text);
		console.log(text);
	}
});
page.on('pageerror', (e) => console.error('PAGEERROR:', e.message));

await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 30000 });
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
await sleep(2500);
await page.bringToFront();
await page.click('#canvas').catch(() => {});
await sleep(400);
await page.keyboard.press('Enter');      // PLAY
await sleep(1500);
await page.keyboard.press('Enter');      // pick first starter
await sleep(2500);                        // let match warm up
console.log('--- MATCH BEGIN ---');
await sleep(durationMs);

await browser.close();

// Print summary
const parse = (line) => {
	const m = line.match(/fps ([\d.]+)\s+1%low ([\d.]+)\s+worst ([\d.]+)ms/);
	return m ? { fps: +m[1], low: +m[2], worst: +m[3] } : null;
};
const parsed = fpsLines.map(parse).filter(Boolean);
if (parsed.length === 0) {
	console.log('\nNo fps samples captured.');
	process.exit(1);
}
// Skip first 2 samples (warmup)
const warm = parsed.slice(2);
const avg = warm.reduce((s, x) => s + x.fps, 0) / warm.length;
const minFps = Math.min(...warm.map((x) => x.fps));
const worstFrame = Math.max(...warm.map((x) => x.worst));
const avgLow = warm.reduce((s, x) => s + x.low, 0) / warm.length;
console.log('\n==== SUMMARY (' + warm.length + 's after 2s warmup) ====');
console.log('avg fps:        ' + avg.toFixed(1));
console.log('min 1s-avg fps: ' + minFps.toFixed(1));
console.log('avg 1% low fps: ' + avgLow.toFixed(1));
console.log('worst frame:    ' + worstFrame.toFixed(1) + ' ms');
