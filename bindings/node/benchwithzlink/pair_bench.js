'use strict';

const net = require('net');
const zlink = require('../src/index');

const ZLINK_PAIR = 0;

function getPort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.listen(0, '127.0.0.1', () => {
      const { port } = server.address();
      server.close(() => resolve(port));
    });
    server.on('error', reject);
  });
}

async function endpointFor(transport) {
  if (transport === 'inproc') {
    return `inproc://bench-pair-${Date.now()}`;
  }
  const port = await getPort();
  return `${transport}://127.0.0.1:${port}`;
}

function resolveMsgCount(size) {
  const env = process.env.BENCH_MSG_COUNT;
  if (env && /^\d+$/.test(env) && parseInt(env, 10) > 0) {
    return parseInt(env, 10);
  }
  return size <= 1024 ? 200000 : 20000;
}

async function main() {
  if (process.argv.length < 5) process.exit(1);
  const pattern = process.argv[2].toUpperCase();
  const transport = process.argv[3];
  const size = parseInt(process.argv[4], 10);
  if (pattern !== 'PAIR') process.exit(0);

  const warmup = parseInt(process.env.BENCH_WARMUP_COUNT || '1000', 10);
  const latCount = parseInt(process.env.BENCH_LAT_COUNT || '500', 10);
  const msgCount = resolveMsgCount(size);

  const ctx = new zlink.Context();
  const a = new zlink.Socket(ctx, ZLINK_PAIR);
  const b = new zlink.Socket(ctx, ZLINK_PAIR);

  try {
    const ep = await endpointFor(transport);
    a.bind(ep);
    b.connect(ep);
    await new Promise((r) => setTimeout(r, 50));

    const buf = Buffer.alloc(size, 'a');

    for (let i = 0; i < warmup; i++) {
      b.send(buf, 0);
      a.recv(size, 0);
    }

    let t0 = process.hrtime.bigint();
    for (let i = 0; i < latCount; i++) {
      b.send(buf, 0);
      const x = a.recv(size, 0);
      a.send(x, 0);
      b.recv(size, 0);
    }
    const latUs = (Number(process.hrtime.bigint() - t0) / 1000.0) / (latCount * 2);

    t0 = process.hrtime.bigint();
    for (let i = 0; i < msgCount; i++) b.send(buf, 0);
    for (let i = 0; i < msgCount; i++) a.recv(size, 0);
    const elapsedSec = Number(process.hrtime.bigint() - t0) / 1e9;
    const thr = msgCount / elapsedSec;

    console.log(`RESULT,current,PAIR,${transport},${size},throughput,${thr}`);
    console.log(`RESULT,current,PAIR,${transport},${size},latency,${latUs}`);
    process.exit(0);
  } catch (_) {
    process.exit(2);
  } finally {
    try { a.close(); } catch (_) {}
    try { b.close(); } catch (_) {}
    try { ctx.close(); } catch (_) {}
  }
}

main();
