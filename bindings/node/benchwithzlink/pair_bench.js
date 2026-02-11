'use strict';

const net = require('net');
const { spawnSync } = require('child_process');
const path = require('path');
const zlink = require('../src/index');

const ZLINK_PAIR = 0;

const CORE_BIN = {
  PUBSUB: 'comp_current_pubsub',
  DEALER_DEALER: 'comp_current_dealer_dealer',
  DEALER_ROUTER: 'comp_current_dealer_router',
  ROUTER_ROUTER: 'comp_current_router_router',
  ROUTER_ROUTER_POLL: 'comp_current_router_router_poll',
  STREAM: 'comp_current_stream',
  GATEWAY: 'comp_current_gateway',
  SPOT: 'comp_current_spot',
};

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

function runCore(pattern, transport, sizeArg, coreDirArg) {
  const binName = CORE_BIN[pattern];
  if (!binName) return 0;

  const coreDir = coreDirArg || process.env.ZLINK_CORE_BENCH_DIR || '';
  if (!coreDir) {
    console.error(`core bench dir is required for pattern ${pattern}`);
    return 2;
  }

  const bin = path.join(coreDir, binName);
  const r = spawnSync(bin, ['current', transport, sizeArg], {
    encoding: 'utf8',
    env: process.env,
  });
  if (r.stdout) process.stdout.write(r.stdout);
  if (r.stderr) process.stderr.write(r.stderr);
  if (typeof r.status === 'number') return r.status;
  return 2;
}

async function runPair(transport, size) {
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
    return 0;
  } catch (_) {
    return 2;
  } finally {
    try { a.close(); } catch (_) {}
    try { b.close(); } catch (_) {}
    try { ctx.close(); } catch (_) {}
  }
}

async function main() {
  if (process.argv.length < 5) process.exit(1);
  const pattern = process.argv[2].toUpperCase();
  const transport = process.argv[3];
  const sizeArg = process.argv[4];
  const coreDir = process.argv[5] || '';

  if (pattern === 'PAIR') {
    process.exit(await runPair(transport, parseInt(sizeArg, 10)));
  }
  process.exit(runCore(pattern, transport, sizeArg, coreDir));
}

main();
