'use strict';

const net = require('net');
const zlink = require('../src/index');

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

async function endpointFor(transport, name) {
  if (transport === 'inproc') {
    return `inproc://bench-${name}-${Date.now()}`;
  }
  const port = await getPort();
  return `${transport}://127.0.0.1:${port}`;
}

function parseEnv(name, def) {
  const v = process.env[name];
  if (!v) return def;
  const p = parseInt(v, 10);
  return Number.isFinite(p) && p > 0 ? p : def;
}

function resolveMsgCount(size) {
  const env = process.env.BENCH_MSG_COUNT;
  if (env && /^\d+$/.test(env) && parseInt(env, 10) > 0) {
    return parseInt(env, 10);
  }
  return size <= 1024 ? 200000 : 20000;
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

async function settle() {
  await sleep(300);
}

function intSockOpt(value) {
  const buf = Buffer.alloc(4);
  buf.writeInt32LE(value, 0);
  return buf;
}

function printResult(pattern, transport, size, thr, latUs) {
  console.log(`RESULT,current,${pattern},${transport},${size},throughput,${thr}`);
  console.log(`RESULT,current,${pattern},${transport},${size},latency,${latUs}`);
}

function waitForInput(socket, timeoutMs) {
  const poller = new zlink.Poller();
  poller.addSocket(socket, zlink.PollEvent.POLLIN);
  const evs = poller.poll(timeoutMs);
  return evs.length > 0;
}

function streamExpectConnectEvent(socket) {
  for (let i = 0; i < 64; i++) {
    const rid = socket.recv(256, zlink.ReceiveFlag.NONE);
    const payload = socket.recv(16, zlink.ReceiveFlag.NONE);
    if (payload.length === 1 && payload[0] === 0x01) {
      return rid;
    }
  }
  throw new Error('invalid STREAM connect event');
}

function streamSend(socket, rid, payload) {
  socket.send(rid, zlink.SendFlag.SNDMORE);
  socket.send(payload, zlink.SendFlag.NONE);
}

function streamRecv(socket, cap) {
  const rid = socket.recv(256, zlink.ReceiveFlag.NONE);
  const data = socket.recv(cap, zlink.ReceiveFlag.NONE);
  return { rid, data };
}

async function runPairLike(pattern, typeA, typeB, transport, size) {
  const warmup = parseEnv('BENCH_WARMUP_COUNT', 1000);
  const latCount = parseEnv('BENCH_LAT_COUNT', 500);
  const msgCount = resolveMsgCount(size);

  const ctx = new zlink.Context();
  const a = new zlink.Socket(ctx, typeA);
  const b = new zlink.Socket(ctx, typeB);

  try {
    const ep = await endpointFor(transport, pattern.toLowerCase());
    a.bind(ep);
    b.connect(ep);
    await settle();

    const buf = Buffer.alloc(size, 'a');

    for (let i = 0; i < warmup; i++) {
      b.send(buf, zlink.SendFlag.NONE);
      a.recv(size, zlink.ReceiveFlag.NONE);
    }

    let t0 = process.hrtime.bigint();
    for (let i = 0; i < latCount; i++) {
      b.send(buf, zlink.SendFlag.NONE);
      const x = a.recv(size, zlink.ReceiveFlag.NONE);
      a.send(x, zlink.SendFlag.NONE);
      b.recv(size, zlink.ReceiveFlag.NONE);
    }
    const latUs = (Number(process.hrtime.bigint() - t0) / 1000.0) / (latCount * 2);

    t0 = process.hrtime.bigint();
    for (let i = 0; i < msgCount; i++) {
      b.send(buf, zlink.SendFlag.NONE);
    }
    for (let i = 0; i < msgCount; i++) {
      a.recv(size, zlink.ReceiveFlag.NONE);
    }
    const elapsedSec = Number(process.hrtime.bigint() - t0) / 1e9;
    const thr = msgCount / elapsedSec;

    printResult(pattern, transport, size, thr, latUs);
    return 0;
  } catch (_) {
    return 2;
  } finally {
    try { a.close(); } catch (_) {}
    try { b.close(); } catch (_) {}
    try { ctx.close(); } catch (_) {}
  }
}

async function runPubSub(transport, size) {
  const warmup = parseEnv('BENCH_WARMUP_COUNT', 1000);
  const msgCount = resolveMsgCount(size);

  const ctx = new zlink.Context();
  const pub = new zlink.Socket(ctx, zlink.SocketType.PUB);
  const sub = new zlink.Socket(ctx, zlink.SocketType.SUB);

  try {
    const ep = await endpointFor(transport, 'pubsub');
    sub.setSockOpt(zlink.SocketOption.SUBSCRIBE, Buffer.alloc(0));
    pub.bind(ep);
    sub.connect(ep);
    await settle();

    const buf = Buffer.alloc(size, 'a');
    for (let i = 0; i < warmup; i++) {
      pub.send(buf, zlink.SendFlag.NONE);
      sub.recv(size, zlink.ReceiveFlag.NONE);
    }

    const t0 = process.hrtime.bigint();
    for (let i = 0; i < msgCount; i++) {
      pub.send(buf, zlink.SendFlag.NONE);
    }
    for (let i = 0; i < msgCount; i++) {
      sub.recv(size, zlink.ReceiveFlag.NONE);
    }
    const elapsedSec = Number(process.hrtime.bigint() - t0) / 1e9;
    const thr = msgCount / elapsedSec;
    const latUs = (elapsedSec * 1e6) / msgCount;

    printResult('PUBSUB', transport, size, thr, latUs);
    return 0;
  } catch (_) {
    return 2;
  } finally {
    try { pub.close(); } catch (_) {}
    try { sub.close(); } catch (_) {}
    try { ctx.close(); } catch (_) {}
  }
}

async function runDealerRouter(transport, size) {
  const warmup = parseEnv('BENCH_WARMUP_COUNT', 1000);
  const latCount = parseEnv('BENCH_LAT_COUNT', 1000);
  const msgCount = resolveMsgCount(size);

  const ctx = new zlink.Context();
  const router = new zlink.Socket(ctx, zlink.SocketType.ROUTER);
  const dealer = new zlink.Socket(ctx, zlink.SocketType.DEALER);

  try {
    const ep = await endpointFor(transport, 'dealer-router');
    dealer.setSockOpt(zlink.SocketOption.ROUTING_ID, Buffer.from('CLIENT'));
    router.bind(ep);
    dealer.connect(ep);
    await settle();

    const buf = Buffer.alloc(size, 'a');

    for (let i = 0; i < warmup; i++) {
      dealer.send(buf, zlink.SendFlag.NONE);
      const rid = router.recv(256, zlink.ReceiveFlag.NONE);
      router.recv(size, zlink.ReceiveFlag.NONE);
      router.send(rid, zlink.SendFlag.SNDMORE);
      router.send(buf, zlink.SendFlag.NONE);
      dealer.recv(size, zlink.ReceiveFlag.NONE);
    }

    let t0 = process.hrtime.bigint();
    for (let i = 0; i < latCount; i++) {
      dealer.send(buf, zlink.SendFlag.NONE);
      const rid = router.recv(256, zlink.ReceiveFlag.NONE);
      router.recv(size, zlink.ReceiveFlag.NONE);
      router.send(rid, zlink.SendFlag.SNDMORE);
      router.send(buf, zlink.SendFlag.NONE);
      dealer.recv(size, zlink.ReceiveFlag.NONE);
    }
    const latUs = (Number(process.hrtime.bigint() - t0) / 1000.0) / (latCount * 2);

    t0 = process.hrtime.bigint();
    for (let i = 0; i < msgCount; i++) {
      dealer.send(buf, zlink.SendFlag.NONE);
    }
    for (let i = 0; i < msgCount; i++) {
      router.recv(256, zlink.ReceiveFlag.NONE);
      router.recv(size, zlink.ReceiveFlag.NONE);
    }
    const elapsedSec = Number(process.hrtime.bigint() - t0) / 1e9;
    const thr = msgCount / elapsedSec;

    printResult('DEALER_ROUTER', transport, size, thr, latUs);
    return 0;
  } catch (_) {
    return 2;
  } finally {
    try { router.close(); } catch (_) {}
    try { dealer.close(); } catch (_) {}
    try { ctx.close(); } catch (_) {}
  }
}

async function runRouterRouter(transport, size, usePoll) {
  const latCount = parseEnv('BENCH_LAT_COUNT', 1000);
  const msgCount = resolveMsgCount(size);

  const ctx = new zlink.Context();
  const router1 = new zlink.Socket(ctx, zlink.SocketType.ROUTER);
  const router2 = new zlink.Socket(ctx, zlink.SocketType.ROUTER);

  try {
    const ep = await endpointFor(transport, 'router-router');
    router1.setSockOpt(zlink.SocketOption.ROUTING_ID, Buffer.from('ROUTER1'));
    router2.setSockOpt(zlink.SocketOption.ROUTING_ID, Buffer.from('ROUTER2'));
    router1.setSockOpt(zlink.SocketOption.ROUTER_MANDATORY, intSockOpt(1));
    router2.setSockOpt(zlink.SocketOption.ROUTER_MANDATORY, intSockOpt(1));
    router1.bind(ep);
    router2.connect(ep);
    await settle();

    let connected = false;
    for (let i = 0; i < 100; i++) {
      try {
        router2.send(Buffer.from('ROUTER1'), zlink.SendFlag.SNDMORE | zlink.SendFlag.DONTWAIT);
        router2.send(Buffer.from('PING'), zlink.SendFlag.DONTWAIT);
      } catch (_) {
        await sleep(10);
        continue;
      }

      if (usePoll && !waitForInput(router1, 0)) {
        await sleep(10);
        continue;
      }

      try {
        router1.recv(256, zlink.ReceiveFlag.DONTWAIT);
        router1.recv(16, zlink.ReceiveFlag.DONTWAIT);
        connected = true;
        break;
      } catch (_) {
        await sleep(10);
      }
    }

    if (!connected) return 2;

    router1.send(Buffer.from('ROUTER2'), zlink.SendFlag.SNDMORE);
    router1.send(Buffer.from('PONG'), zlink.SendFlag.NONE);
    if (usePoll && !waitForInput(router2, 2000)) return 2;
    router2.recv(256, zlink.ReceiveFlag.NONE);
    router2.recv(16, zlink.ReceiveFlag.NONE);

    const buf = Buffer.alloc(size, 'a');

    let t0 = process.hrtime.bigint();
    for (let i = 0; i < latCount; i++) {
      router2.send(Buffer.from('ROUTER1'), zlink.SendFlag.SNDMORE);
      router2.send(buf, zlink.SendFlag.NONE);

      if (usePoll && !waitForInput(router1, 2000)) return 2;
      const rid = router1.recv(256, zlink.ReceiveFlag.NONE);
      router1.recv(size, zlink.ReceiveFlag.NONE);

      router1.send(rid, zlink.SendFlag.SNDMORE);
      router1.send(buf, zlink.SendFlag.NONE);

      if (usePoll && !waitForInput(router2, 2000)) return 2;
      router2.recv(256, zlink.ReceiveFlag.NONE);
      router2.recv(size, zlink.ReceiveFlag.NONE);
    }

    const latUs = (Number(process.hrtime.bigint() - t0) / 1000.0) / (latCount * 2);

    t0 = process.hrtime.bigint();
    for (let i = 0; i < msgCount; i++) {
      router2.send(Buffer.from('ROUTER1'), zlink.SendFlag.SNDMORE);
      router2.send(buf, zlink.SendFlag.NONE);
    }
    for (let i = 0; i < msgCount; i++) {
      if (usePoll && !waitForInput(router1, 2000)) return 2;
      router1.recv(256, zlink.ReceiveFlag.NONE);
      router1.recv(size, zlink.ReceiveFlag.NONE);
    }

    const elapsedSec = Number(process.hrtime.bigint() - t0) / 1e9;
    const thr = msgCount / elapsedSec;
    const pattern = usePoll ? 'ROUTER_ROUTER_POLL' : 'ROUTER_ROUTER';
    printResult(pattern, transport, size, thr, latUs);
    return 0;
  } catch (_) {
    return 2;
  } finally {
    try { router1.close(); } catch (_) {}
    try { router2.close(); } catch (_) {}
    try { ctx.close(); } catch (_) {}
  }
}

async function runStream(transport, size) {
  const warmup = parseEnv('BENCH_WARMUP_COUNT', 1000);
  const latCount = parseEnv('BENCH_LAT_COUNT', 500);
  const msgCount = resolveMsgCount(size);
  const ioTimeoutMs = parseEnv('BENCH_STREAM_TIMEOUT_MS', 5000);

  const ctx = new zlink.Context();
  const server = new zlink.Socket(ctx, zlink.SocketType.STREAM);
  const client = new zlink.Socket(ctx, zlink.SocketType.STREAM);

  try {
    const ep = await endpointFor(transport, 'stream');
    server.setSockOpt(zlink.SocketOption.SNDTIMEO, intSockOpt(ioTimeoutMs));
    server.setSockOpt(zlink.SocketOption.RCVTIMEO, intSockOpt(ioTimeoutMs));
    client.setSockOpt(zlink.SocketOption.SNDTIMEO, intSockOpt(ioTimeoutMs));
    client.setSockOpt(zlink.SocketOption.RCVTIMEO, intSockOpt(ioTimeoutMs));
    server.bind(ep);
    client.connect(ep);
    await settle();

    const serverClientId = streamExpectConnectEvent(server);
    const clientServerId = streamExpectConnectEvent(client);

    const buf = Buffer.alloc(size, 'a');
    const cap = Math.max(256, size);

    for (let i = 0; i < warmup; i++) {
      streamSend(client, clientServerId, buf);
      streamRecv(server, cap);
    }

    let t0 = process.hrtime.bigint();
    for (let i = 0; i < latCount; i++) {
      streamSend(client, clientServerId, buf);
      const rx = streamRecv(server, cap);
      streamSend(server, serverClientId, rx.data);
      streamRecv(client, cap);
    }
    const latUs = (Number(process.hrtime.bigint() - t0) / 1000.0) / (latCount * 2);

    t0 = process.hrtime.bigint();
    for (let i = 0; i < msgCount; i++) {
      streamSend(client, clientServerId, buf);
    }
    for (let i = 0; i < msgCount; i++) {
      streamRecv(server, cap);
    }
    const elapsedSec = Number(process.hrtime.bigint() - t0) / 1e9;
    const thr = msgCount / elapsedSec;

    printResult('STREAM', transport, size, thr, latUs);
    return 0;
  } catch (_) {
    return 2;
  } finally {
    try { server.close(); } catch (_) {}
    try { client.close(); } catch (_) {}
    try { ctx.close(); } catch (_) {}
  }
}

async function waitUntil(fn, timeoutMs, intervalMs = 10) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      if (fn()) return true;
    } catch (_) {
    }
    await sleep(intervalMs);
  }
  return false;
}

async function recvWithTimeout(socket, size, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      return socket.recv(size, zlink.ReceiveFlag.DONTWAIT);
    } catch (_) {
      await sleep(10);
    }
  }
  throw new Error('timeout');
}

async function gatewaySendWithRetry(gateway, service, parts, flags, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      gateway.send(service, parts, flags);
      return;
    } catch (_) {
      await sleep(10);
    }
  }
  throw new Error('timeout');
}

async function spotRecvWithTimeout(spot, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      return spot.recv(zlink.ReceiveFlag.DONTWAIT);
    } catch (_) {
      await sleep(10);
    }
  }
  throw new Error('timeout');
}

async function runGateway(transport, size) {
  const warmup = parseEnv('BENCH_WARMUP_COUNT', 200);
  const latCount = parseEnv('BENCH_LAT_COUNT', 200);
  const msgCount = resolveMsgCount(size);
  const suffix = `${Date.now()}-${Math.random().toString(16).slice(2)}`;

  const ctx = new zlink.Context();
  let registry;
  let discovery;
  let receiver;
  let router;
  let gateway;
  try {
    const regPub = `inproc://gw-pub-${suffix}`;
    const regRouter = `inproc://gw-router-${suffix}`;
    registry = new zlink.Registry(ctx);
    registry.setHeartbeat(5000, 60000);
    registry.setEndpoints(regPub, regRouter);
    registry.start();

    discovery = new zlink.Discovery(ctx, zlink.ServiceType.GATEWAY);
    discovery.connectRegistry(regPub);
    const service = 'svc';
    discovery.subscribe(service);

    receiver = new zlink.Receiver(ctx);
    const providerEp = await endpointFor(transport, 'gateway-provider');
    receiver.bind(providerEp);
    receiver.connectRegistry(regRouter);
    receiver.register(service, providerEp, 1);
    router = receiver.routerSocket();

    gateway = new zlink.Gateway(ctx, discovery);
    if (!(await waitUntil(() => discovery.receiverCount(service) > 0, 5000))) return 2;
    if (!(await waitUntil(() => gateway.connectionCount(service) > 0, 5000))) return 2;
    await settle();

    const payload = Buffer.alloc(size, 'a');
    for (let i = 0; i < warmup; i++) {
      await gatewaySendWithRetry(gateway, service, [payload], zlink.SendFlag.NONE, 5000);
      await recvWithTimeout(router, 256, 5000);
      await recvWithTimeout(router, Math.max(256, size), 5000);
    }

    let t0 = process.hrtime.bigint();
    for (let i = 0; i < latCount; i++) {
      await gatewaySendWithRetry(gateway, service, [payload], zlink.SendFlag.NONE, 5000);
      await recvWithTimeout(router, 256, 5000);
      await recvWithTimeout(router, Math.max(256, size), 5000);
    }
    const latUs = (Number(process.hrtime.bigint() - t0) / 1000.0) / latCount;

    let recvCount = 0;
    const receiverLoop = (async () => {
      for (let i = 0; i < msgCount; i++) {
        try {
          await recvWithTimeout(router, 256, 5000);
          await recvWithTimeout(router, Math.max(256, size), 5000);
        } catch (_) {
          break;
        }
        recvCount += 1;
      }
    })();

    let sent = 0;
    t0 = process.hrtime.bigint();
    for (let i = 0; i < msgCount; i++) {
      try {
        gateway.send(service, [payload], zlink.SendFlag.NONE);
      } catch (_) {
        break;
      }
      sent += 1;
      if ((i & 1023) === 0) {
        await Promise.resolve();
      }
    }
    await receiverLoop;
    const elapsedSec = Number(process.hrtime.bigint() - t0) / 1e9;
    const thr = (sent > 0 && recvCount > 0) ? (Math.min(sent, recvCount) / elapsedSec) : 0.0;
    printResult('GATEWAY', transport, size, thr, latUs);
    return 0;
  } catch (_) {
    return 2;
  } finally {
    try { if (gateway) gateway.close(); } catch (_) {}
    try { if (router) router.close(); } catch (_) {}
    try { if (receiver) receiver.close(); } catch (_) {}
    try { if (discovery) discovery.close(); } catch (_) {}
    try { if (registry) registry.close(); } catch (_) {}
    try { ctx.close(); } catch (_) {}
  }
}

async function runSpot(transport, size) {
  const warmup = parseEnv('BENCH_WARMUP_COUNT', 200);
  const latCount = parseEnv('BENCH_LAT_COUNT', 200);
  const msgCount = Math.min(resolveMsgCount(size), parseEnv('BENCH_SPOT_MSG_COUNT_MAX', 50000));

  const ctx = new zlink.Context();
  let nodePub;
  let nodeSub;
  let spotPub;
  let spotSub;
  try {
    nodePub = new zlink.SpotNode(ctx);
    nodeSub = new zlink.SpotNode(ctx);
    const endpoint = await endpointFor(transport, 'spot');
    nodePub.bind(endpoint);
    nodeSub.connectPeerPub(endpoint);
    spotPub = new zlink.Spot(nodePub);
    spotSub = new zlink.Spot(nodeSub);
    spotSub.subscribe('bench');
    await settle();

    const payload = Buffer.alloc(size, 'a');
    for (let i = 0; i < warmup; i++) {
      spotPub.publish('bench', [payload], zlink.SendFlag.NONE);
      await spotRecvWithTimeout(spotSub, 5000);
    }

    let t0 = process.hrtime.bigint();
    for (let i = 0; i < latCount; i++) {
      spotPub.publish('bench', [payload], zlink.SendFlag.NONE);
      await spotRecvWithTimeout(spotSub, 5000);
    }
    const latUs = (Number(process.hrtime.bigint() - t0) / 1000.0) / latCount;

    let recvCount = 0;
    const receiverLoop = (async () => {
      for (let i = 0; i < msgCount; i++) {
        try {
          await spotRecvWithTimeout(spotSub, 5000);
        } catch (_) {
          break;
        }
        recvCount += 1;
      }
    })();

    let sent = 0;
    t0 = process.hrtime.bigint();
    for (let i = 0; i < msgCount; i++) {
      try {
        spotPub.publish('bench', [payload], zlink.SendFlag.NONE);
      } catch (_) {
        break;
      }
      sent += 1;
      if ((i & 1023) === 0) {
        await Promise.resolve();
      }
    }
    await receiverLoop;
    const elapsedSec = Number(process.hrtime.bigint() - t0) / 1e9;
    const thr = (sent > 0 && recvCount > 0) ? (Math.min(sent, recvCount) / elapsedSec) : 0.0;
    printResult('SPOT', transport, size, thr, latUs);
    return 0;
  } catch (_) {
    return 2;
  } finally {
    try { if (spotSub) spotSub.close(); } catch (_) {}
    try { if (spotPub) spotPub.close(); } catch (_) {}
    try { if (nodeSub) nodeSub.close(); } catch (_) {}
    try { if (nodePub) nodePub.close(); } catch (_) {}
    try { ctx.close(); } catch (_) {}
  }
}

async function runPattern(pattern, transport, size) {
  const p = String(pattern).toUpperCase();
  if (p === 'PAIR') {
    return runPairLike('PAIR', zlink.SocketType.PAIR, zlink.SocketType.PAIR, transport, size);
  }
  if (p === 'PUBSUB') {
    return runPubSub(transport, size);
  }
  if (p === 'DEALER_DEALER') {
    return runPairLike('DEALER_DEALER', zlink.SocketType.DEALER, zlink.SocketType.DEALER, transport, size);
  }
  if (p === 'DEALER_ROUTER') {
    return runDealerRouter(transport, size);
  }
  if (p === 'ROUTER_ROUTER') {
    return runRouterRouter(transport, size, false);
  }
  if (p === 'ROUTER_ROUTER_POLL') {
    return runRouterRouter(transport, size, true);
  }
  if (p === 'STREAM') {
    return runStream(transport, size);
  }
  if (p === 'GATEWAY') {
    return runGateway(transport, size);
  }
  if (p === 'SPOT') {
    return runSpot(transport, size);
  }
  return 2;
}

async function runFromArgs(argv) {
  if (!argv || argv.length < 3) return 1;
  return runPattern(argv[0], argv[1], parseInt(argv[2], 10));
}

module.exports = { runPattern, runFromArgs };

if (require.main === module) {
  runFromArgs(process.argv.slice(2))
    .then((code) => process.exit(code))
    .catch(() => process.exit(2));
}
