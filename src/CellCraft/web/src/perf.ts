// Lightweight per-frame phase timer. Accumulates ms per tag across frames,
// then report() divides by frames to get per-frame cost.
//
// Usage:
//   perf.mark('sim.inner'); tick(...); perf.end('sim.inner');
// or
//   const t = perf.start('x'); ...; perf.end('x', t);

interface Bucket {
	totalMs: number;
	calls: number;
	max: number;
}

const buckets = new Map<string, Bucket>();
let frameCount = 0;
let meshesThisFrame = 0;
let meshesTotal = 0;

export function start(tag: string): number {
	let b = buckets.get(tag);
	if (!b) {
		b = { totalMs: 0, calls: 0, max: 0 };
		buckets.set(tag, b);
	}
	return performance.now();
}

export function end(tag: string, startMs?: number): void {
	const b = buckets.get(tag);
	if (!b) return;
	const dt = performance.now() - (startMs ?? performance.now());
	b.totalMs += dt;
	b.calls++;
	if (dt > b.max) b.max = dt;
}

// Convenience: wrap a sync call.
export function time<T>(tag: string, fn: () => T): T {
	const t = start(tag);
	try {
		return fn();
	} finally {
		end(tag, t);
	}
}

export function countMesh(n = 1): void {
	meshesThisFrame += n;
}

export function endFrame(): void {
	frameCount++;
	meshesTotal += meshesThisFrame;
	meshesThisFrame = 0;
}

export function snapshotAndReset(): { line: string; frames: number } {
	if (frameCount === 0) return { line: '(no frames)', frames: 0 };
	const rows: Array<{ tag: string; per: number; max: number }> = [];
	for (const [tag, b] of buckets) {
		rows.push({ tag, per: b.totalMs / frameCount, max: b.max });
	}
	rows.sort((a, b) => b.per - a.per);
	const parts = rows.map(
		(r) => `${r.tag} ${r.per.toFixed(2)}ms(max${r.max.toFixed(1)})`
	);
	const meshesPerFrame = meshesTotal / frameCount;
	const line = `meshes/frame ${meshesPerFrame.toFixed(0)} | ` + parts.join(' | ');
	const frames = frameCount;
	for (const b of buckets.values()) { b.totalMs = 0; b.calls = 0; b.max = 0; }
	frameCount = 0;
	meshesTotal = 0;
	return { line, frames };
}
