'use strict';

const fs = require('fs');
const net = require('net');
const zlink = require('../src/index');

const ipcPaths = new Set();
let ipcCleanupHooked = false;

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

function registerIpcEndpoint(endpoint) {
  const prefix = 'ipc://';
  if (!endpoint.startsWith(prefix)) return;
  const path = endpoint.slice(prefix.length);
  if (!path.startsWith('/')) return;

  ipcPaths.add(path);
  try {
    fs.unlinkSync(path);
  } catch (err) {
    if (!err || err.code !== 'ENOENT') {
      // ignore cleanup failures during bench setup/teardown
    }
  }

  if (ipcCleanupHooked) return;
  ipcCleanupHooked = true;
  process.on('exit', () => {
    for (const ipcPath of ipcPaths) {
      try {
        fs.unlinkSync(ipcPath);
      } catch (err) {
        if (!err || err.code !== 'ENOENT') {
          // ignore cleanup failures during process exit
        }
      }
    }
  });
}

async function endpointFor(transport, name) {
  if (transport === 'inproc') {
    return `inproc://bench-${name}-${Date.now()}`;
  }
  if (transport === 'ipc') {
    const port = await getPort();
    const endpoint = `ipc:///tmp/zlink-bench-${name}-${port}.sock`;
    registerIpcEndpoint(endpoint);
    return endpoint;
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

function waitForInput(socket, timeoutMs, poller = null) {
  const p = poller || new zlink.Poller();
  if (!poller) {
    p.addSocket(socket, zlink.PollEvent.POLLIN);
  }
  const evs = p.poll(timeoutMs);
  return evs.some((ev) => (ev & zlink.PollEvent.POLLIN) !== 0);
}

function streamExpectConnectEvent(socket) {
  const rid = Buffer.alloc(256);
  const payload = Buffer.alloc(16);
  for (let i = 0; i < 64; i++) {
    const ridLen = socket.recvMsgInto(rid, zlink.ReceiveFlag.NONE);
    const payloadLen = socket.recvMsgInto(payload, zlink.ReceiveFlag.NONE);
    if (payloadLen === 1 && payload[0] === 0x01) {
      return rid.subarray(0, ridLen);
    }
  }
  throw new Error('invalid STREAM connect event');
}

function streamSend(socket, rid, payload) {
  socket.send(rid, zlink.SendFlag.SNDMORE);
  socket.send(payload, zlink.SendFlag.NONE);
}

function streamRecvInto(socket, ridBuffer, dataBuffer) {
  const ridLen = socket.recvMsgInto(ridBuffer, zlink.ReceiveFlag.NONE);
  const dataLen = socket.recvMsgInto(dataBuffer, zlink.ReceiveFlag.NONE);
  return { ridLen, dataLen };
}

const STREAM_MAX_FRAME_BYTES = 16 * 1024 * 1024;

function encodeLen32BeFrame(payload) {
  const frame = Buffer.alloc(payload.length + 4);
  frame.writeUInt32BE(payload.length, 0);
  payload.copy(frame, 4);
  return frame;
}

class StreamLen32BeStash {
  constructor() {
    this._buf = Buffer.alloc(512);
    this._start = 0;
    this._end = 0;
  }

  _size() {
    return this._end - this._start;
  }

  _ensure(extra) {
    if ((this._buf.length - this._end) >= extra) return;

    if (this._start > 0) {
      this._buf.copy(this._buf, 0, this._start, this._end);
      this._end -= this._start;
      this._start = 0;
      if ((this._buf.length - this._end) >= extra) return;
    }

    let cap = this._buf.length;
    while ((cap - this._end) < extra) {
      cap = Math.max(cap * 2, this._end + extra);
    }
    const next = Buffer.alloc(cap);
    this._buf.copy(next, 0, this._start, this._end);
    this._end -= this._start;
    this._start = 0;
    this._buf = next;
  }

  append(chunk) {
    if (!chunk || chunk.length === 0) return;
    this._ensure(chunk.length);
    chunk.copy(this._buf, this._end);
    this._end += chunk.length;
  }

  tryReadPayload(outPayload) {
    const avail = this._size();
    if (avail < 4) return -1;

    const bodyLen = this._buf.readUInt32BE(this._start);
    if (bodyLen > STREAM_MAX_FRAME_BYTES) {
      this._start = 0;
      this._end = 0;
      return -1;
    }

    const frameLen = 4 + bodyLen;
    if (avail < frameLen) return -1;
    if (bodyLen > outPayload.length) {
      throw new Error('len32be payload buffer too small');
    }

    if (bodyLen > 0) {
      this._buf.copy(outPayload, 0, this._start + 4, this._start + frameLen);
    }

    this._start += frameLen;
    if (this._start === this._end) {
      this._start = 0;
      this._end = 0;
    } else if (this._start > 4096 && (this._start * 2) >= this._end) {
      this._buf.copy(this._buf, 0, this._start, this._end);
      this._end -= this._start;
      this._start = 0;
    }
    return bodyLen;
  }
}

function streamRecvLen32BePayload(socket, expectedRid, ridBuffer, chunkBuffer,
  stash, payloadBuffer) {
  while (true) {
    const { ridLen, dataLen: chunkLen } = streamRecvInto(
      socket, ridBuffer, chunkBuffer
    );
    if (expectedRid && expectedRid.length > 0) {
      if (ridLen !== expectedRid.length) {
        continue;
      }
      if (!ridBuffer.subarray(0, ridLen).equals(expectedRid)) {
        continue;
      }
    }
    if (chunkLen > 0) {
      stash.append(chunkBuffer.subarray(0, chunkLen));
    }
    const payloadLen = stash.tryReadPayload(payloadBuffer);
    if (payloadLen >= 0) {
      return payloadLen;
    }
  }
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
    const recvA = Buffer.alloc(Math.max(1, size));
    const recvB = Buffer.alloc(Math.max(1, size));

    for (let i = 0; i < warmup; i++) {
      b.send(buf, zlink.SendFlag.NONE);
      a.recvInto(recvA, zlink.ReceiveFlag.NONE);
    }

    let t0 = process.hrtime.bigint();
    for (let i = 0; i < latCount; i++) {
      b.send(buf, zlink.SendFlag.NONE);
      const n = a.recvInto(recvA, zlink.ReceiveFlag.NONE);
      a.send(recvA.subarray(0, n), zlink.SendFlag.NONE);
      b.recvInto(recvB, zlink.ReceiveFlag.NONE);
    }
    const latUs = (Number(process.hrtime.bigint() - t0) / 1000.0) / (latCount * 2);

    t0 = process.hrtime.bigint();
    for (let i = 0; i < msgCount; i++) {
      b.send(buf, zlink.SendFlag.NONE);
    }
    for (let i = 0; i < msgCount; i++) {
      a.recvInto(recvA, zlink.ReceiveFlag.NONE);
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
    const recvBuf = Buffer.alloc(Math.max(1, size));
    for (let i = 0; i < warmup; i++) {
      pub.send(buf, zlink.SendFlag.NONE);
      sub.recvInto(recvBuf, zlink.ReceiveFlag.NONE);
    }

    const t0 = process.hrtime.bigint();
    for (let i = 0; i < msgCount; i++) {
      pub.send(buf, zlink.SendFlag.NONE);
    }
    for (let i = 0; i < msgCount; i++) {
      sub.recvInto(recvBuf, zlink.ReceiveFlag.NONE);
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
    const ridBuf = Buffer.alloc(256);
    const dataBuf = Buffer.alloc(Math.max(1, size));

    for (let i = 0; i < warmup; i++) {
      dealer.send(buf, zlink.SendFlag.NONE);
      const ridLen = router.recvInto(ridBuf, zlink.ReceiveFlag.NONE);
      router.recvInto(dataBuf, zlink.ReceiveFlag.NONE);
      router.sendFrom(ridBuf, ridLen, zlink.SendFlag.SNDMORE);
      router.send(buf, zlink.SendFlag.NONE);
      dealer.recvInto(dataBuf, zlink.ReceiveFlag.NONE);
    }

    let t0 = process.hrtime.bigint();
    for (let i = 0; i < latCount; i++) {
      dealer.send(buf, zlink.SendFlag.NONE);
      const ridLen = router.recvInto(ridBuf, zlink.ReceiveFlag.NONE);
      router.recvInto(dataBuf, zlink.ReceiveFlag.NONE);
      router.sendFrom(ridBuf, ridLen, zlink.SendFlag.SNDMORE);
      router.send(buf, zlink.SendFlag.NONE);
      dealer.recvInto(dataBuf, zlink.ReceiveFlag.NONE);
    }
    const latUs = (Number(process.hrtime.bigint() - t0) / 1000.0) / (latCount * 2);

    t0 = process.hrtime.bigint();
    for (let i = 0; i < msgCount; i++) {
      dealer.send(buf, zlink.SendFlag.NONE);
    }
    for (let i = 0; i < msgCount; i++) {
      router.recvInto(ridBuf, zlink.ReceiveFlag.NONE);
      router.recvInto(dataBuf, zlink.ReceiveFlag.NONE);
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
    const routingId1 = Buffer.from('ROUTER1');
    const routingId2 = Buffer.from('ROUTER2');
    const ping = Buffer.from('PING');
    const pong = Buffer.from('PONG');
    const ridBuf = Buffer.alloc(256);
    const ctrlBuf = Buffer.alloc(16);
    const dataBuf = Buffer.alloc(Math.max(1, size));
    let poller1 = null;
    let poller2 = null;

    const ep = await endpointFor(transport, 'router-router');
    router1.setSockOpt(zlink.SocketOption.ROUTING_ID, routingId1);
    router2.setSockOpt(zlink.SocketOption.ROUTING_ID, routingId2);
    router1.setSockOpt(zlink.SocketOption.ROUTER_MANDATORY, intSockOpt(1));
    router2.setSockOpt(zlink.SocketOption.ROUTER_MANDATORY, intSockOpt(1));
    router1.bind(ep);
    router2.connect(ep);
    await settle();

    if (usePoll) {
      poller1 = new zlink.Poller();
      poller1.addSocket(router1, zlink.PollEvent.POLLIN);
      poller2 = new zlink.Poller();
      poller2.addSocket(router2, zlink.PollEvent.POLLIN);
    }

    let connected = false;
    for (let i = 0; i < 100; i++) {
      try {
        router2.send(routingId1, zlink.SendFlag.SNDMORE | zlink.SendFlag.DONTWAIT);
        router2.send(ping, zlink.SendFlag.DONTWAIT);
      } catch (_) {
        await sleep(10);
        continue;
      }

      if (usePoll && !waitForInput(router1, 0, poller1)) {
        await sleep(10);
        continue;
      }

      try {
        router1.recvInto(ridBuf, zlink.ReceiveFlag.DONTWAIT);
        router1.recvInto(ctrlBuf, zlink.ReceiveFlag.DONTWAIT);
        connected = true;
        break;
      } catch (_) {
        await sleep(10);
      }
    }

    if (!connected) return 2;

    router1.send(routingId2, zlink.SendFlag.SNDMORE);
    router1.send(pong, zlink.SendFlag.NONE);
    if (usePoll && !waitForInput(router2, 2000, poller2)) return 2;
    router2.recvInto(ridBuf, zlink.ReceiveFlag.NONE);
    router2.recvInto(ctrlBuf, zlink.ReceiveFlag.NONE);

    const buf = Buffer.alloc(size, 'a');

    let t0 = process.hrtime.bigint();
    for (let i = 0; i < latCount; i++) {
      router2.send(routingId1, zlink.SendFlag.SNDMORE);
      router2.send(buf, zlink.SendFlag.NONE);

      if (usePoll && !waitForInput(router1, 2000, poller1)) return 2;
      const ridLen = router1.recvInto(ridBuf, zlink.ReceiveFlag.NONE);
      router1.recvInto(dataBuf, zlink.ReceiveFlag.NONE);

      router1.sendFrom(ridBuf, ridLen, zlink.SendFlag.SNDMORE);
      router1.send(buf, zlink.SendFlag.NONE);

      if (usePoll && !waitForInput(router2, 2000, poller2)) return 2;
      router2.recvInto(ridBuf, zlink.ReceiveFlag.NONE);
      router2.recvInto(dataBuf, zlink.ReceiveFlag.NONE);
    }

    const latUs = (Number(process.hrtime.bigint() - t0) / 1000.0) / (latCount * 2);

    t0 = process.hrtime.bigint();
    for (let i = 0; i < msgCount; i++) {
      router2.send(routingId1, zlink.SendFlag.SNDMORE);
      router2.send(buf, zlink.SendFlag.NONE);
    }
    if (usePoll) {
      let received = 0;
      while (received < msgCount) {
        if (!waitForInput(router1, 2000, poller1)) return 2;
        while (received < msgCount) {
          try {
            router1.recvInto(ridBuf, zlink.ReceiveFlag.DONTWAIT);
            router1.recvInto(dataBuf, zlink.ReceiveFlag.DONTWAIT);
            received++;
          } catch (_) {
            break;
          }
        }
      }
    } else {
      for (let i = 0; i < msgCount; i++) {
        router1.recvInto(ridBuf, zlink.ReceiveFlag.NONE);
        router1.recvInto(dataBuf, zlink.ReceiveFlag.NONE);
      }
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
  return runStreamMode(transport, size, false, 'STREAM', 'stream');
}

async function runStreamLen32Be(transport, size) {
  return runStreamMode(transport, size, true, 'STREAM_LEN32BE', 'stream-len32be');
}

async function runStreamMode(transport, size, len32beDispatch, patternName, endpointName) {
  const warmup = parseEnv('BENCH_WARMUP_COUNT', 1000);
  const latCount = parseEnv('BENCH_LAT_COUNT', 500);
  const msgCount = parseEnv('BENCH_MSG_COUNT', 10000);
  const inflightLimit = Math.max(1, parseEnv('BENCH_STREAM_INFLIGHT', 10));
  const ioTimeoutMs = parseEnv('BENCH_STREAM_TIMEOUT_MS', 5000);
  const drainTimeoutMs = parseEnv('BENCH_STREAM_DRAIN_TIMEOUT_MS',
    Math.max(ioTimeoutMs * 6, 30000));

  const ctx = new zlink.Context();
  const server = new zlink.Socket(ctx, zlink.SocketType.STREAM);
  const client = new zlink.Socket(ctx, zlink.SocketType.STREAM);
  let serverAttached = false;
  let clientAttached = false;
  let clientServerId = null;
  let clientSinkCount = 0;
  const clientRawStash = new StreamLen32BeStash();
  const clientPayload = Buffer.alloc(Math.max(1, size));

  try {
    const ep = await endpointFor(transport, endpointName);
    server.setSockOpt(zlink.SocketOption.SNDTIMEO, intSockOpt(ioTimeoutMs));
    server.setSockOpt(zlink.SocketOption.RCVTIMEO, intSockOpt(ioTimeoutMs));
    client.setSockOpt(zlink.SocketOption.SNDTIMEO, intSockOpt(ioTimeoutMs));
    client.setSockOpt(zlink.SocketOption.RCVTIMEO, intSockOpt(ioTimeoutMs));

    server.streamAttach((rid, packets) => {
      if (!rid || rid.length === 0 || !packets) {
        return 0;
      }
      for (const packet of packets) {
        if (!packet || packet.length === 0) {
          continue;
        }
        if (packet.length === 1 && (packet[0] === 0x00 || packet[0] === 0x01)) {
          continue;
        }
        server.streamSend(rid, packet, zlink.SendFlag.NONE);
      }
      return 0;
    }, len32beDispatch ? zlink.StreamDispatchMode.LEN32BE : zlink.StreamDispatchMode.NONE);
    serverAttached = true;

    client.streamAttach((rid, packets) => {
      if (!clientServerId && rid && rid.length > 0) {
        clientServerId = Buffer.from(rid);
      }
      if (!packets) {
        return 0;
      }
      if (len32beDispatch) {
        clientSinkCount += packets.length;
        return 0;
      }

      for (const packet of packets) {
        if (!packet || packet.length === 0) {
          continue;
        }
        if (packet.length === 1 && (packet[0] === 0x00 || packet[0] === 0x01)) {
          continue;
        }
        clientRawStash.append(packet);
        while (true) {
          const payloadLen = clientRawStash.tryReadPayload(clientPayload);
          if (payloadLen < 0) {
            break;
          }
          clientSinkCount += 1;
        }
      }
      return 0;
    }, len32beDispatch ? zlink.StreamDispatchMode.LEN32BE : zlink.StreamDispatchMode.NONE);
    clientAttached = true;

    server.bind(ep);
    client.connect(ep);
    await settle();

    const ridDeadline = Date.now() + ioTimeoutMs;
    while (Date.now() < ridDeadline) {
      const rid = client.streamPeerRoutingId(0);
      if (rid && rid.length > 0) {
        clientServerId = Buffer.from(rid);
        break;
      }
      if (clientServerId && clientServerId.length > 0) {
        break;
      }
      await sleep(1);
    }
    if (!clientServerId) {
      return 2;
    }

    const payload = Buffer.alloc(size, 'a');
    const sendBuf = len32beDispatch ? payload : encodeLen32BeFrame(payload);

    const waitReceived = async (target, timeoutMs) => {
      const deadline = Date.now() + Math.max(timeoutMs, 1);
      while (Date.now() < deadline) {
        if (clientSinkCount >= target) {
          return true;
        }
        await sleep(1);
      }
      return clientSinkCount >= target;
    };

    let expected = clientSinkCount;
    for (let i = 0; i < warmup; i++) {
      client.streamSend(clientServerId, sendBuf, zlink.SendFlag.NONE);
      expected += 1;
      if (!(await waitReceived(expected, ioTimeoutMs))) {
        return 2;
      }
    }

    let t0 = process.hrtime.bigint();
    for (let i = 0; i < latCount; i++) {
      client.streamSend(clientServerId, sendBuf, zlink.SendFlag.NONE);
      expected += 1;
      if (!(await waitReceived(expected, ioTimeoutMs))) {
        return 2;
      }
    }
    const latUs = (Number(process.hrtime.bigint() - t0) / 1000.0) / (latCount * 2);

    const recvBegin = clientSinkCount;
    let sent = 0;
    t0 = process.hrtime.bigint();
    for (let i = 0; i < msgCount; i++) {
      try {
        client.streamSend(clientServerId, sendBuf, zlink.SendFlag.NONE);
      } catch (_) {
        break;
      }
      sent++;
      const acked = Math.max(0, clientSinkCount - recvBegin);
      if ((sent - acked) >= inflightLimit) {
        const target = recvBegin + sent - inflightLimit + 1;
        if (!(await waitReceived(target, ioTimeoutMs))) {
          break;
        }
      }
    }

    if (sent > 0) {
      await waitReceived(recvBegin + sent, drainTimeoutMs);
    }

    const elapsedSec = Number(process.hrtime.bigint() - t0) / 1e9;
    const recvDelta = Math.max(0, clientSinkCount - recvBegin);
    const effective = Math.min(sent, recvDelta);
    const thr = effective > 0 && elapsedSec > 0 ? (effective / elapsedSec) : 0.0;
    if (effective <= 0) {
      return 2;
    }
    printResult(patternName, transport, size, thr, latUs);
    return 0;
  } catch (_) {
    return 2;
  } finally {
    if (clientAttached) {
      try { client.streamDetach(); } catch (_) {}
    }
    if (serverAttached) {
      try { server.streamDetach(); } catch (_) {}
    }
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

async function recvIntoWithTimeout(socket, buffer, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      return socket.recvInto(buffer, zlink.ReceiveFlag.DONTWAIT);
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
    const parts = [payload];
    const ridBuf = Buffer.alloc(256);
    const dataBuf = Buffer.alloc(Math.max(256, size));
    for (let i = 0; i < warmup; i++) {
      await gatewaySendWithRetry(gateway, service, parts, zlink.SendFlag.NONE, 5000);
      await recvIntoWithTimeout(router, ridBuf, 5000);
      await recvIntoWithTimeout(router, dataBuf, 5000);
    }

    let t0 = process.hrtime.bigint();
    for (let i = 0; i < latCount; i++) {
      await gatewaySendWithRetry(gateway, service, parts, zlink.SendFlag.NONE, 5000);
      await recvIntoWithTimeout(router, ridBuf, 5000);
      await recvIntoWithTimeout(router, dataBuf, 5000);
    }
    const latUs = (Number(process.hrtime.bigint() - t0) / 1000.0) / latCount;

    let recvCount = 0;
    let sent = 0;
    t0 = process.hrtime.bigint();
    const receiverLoop = (async () => {
      for (let i = 0; i < msgCount; i++) {
        try {
          await recvIntoWithTimeout(router, ridBuf, 5000);
          await recvIntoWithTimeout(router, dataBuf, 5000);
        } catch (_) {
          break;
        }
        recvCount += 1;
      }
    })();

    for (let i = 0; i < msgCount; i++) {
      try {
        gateway.send(service, parts, zlink.SendFlag.NONE);
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
    const parts = [payload];
    for (let i = 0; i < warmup; i++) {
      spotPub.publish('bench', parts, zlink.SendFlag.NONE);
      await spotRecvWithTimeout(spotSub, 5000);
    }

    let t0 = process.hrtime.bigint();
    for (let i = 0; i < latCount; i++) {
      spotPub.publish('bench', parts, zlink.SendFlag.NONE);
      await spotRecvWithTimeout(spotSub, 5000);
    }
    const latUs = (Number(process.hrtime.bigint() - t0) / 1000.0) / latCount;

    let recvCount = 0;
    let sent = 0;
    t0 = process.hrtime.bigint();
    const spotReceiverLoop = (async () => {
      for (let i = 0; i < msgCount; i++) {
        try {
          await spotRecvWithTimeout(spotSub, 5000);
        } catch (_) {
          break;
        }
        recvCount += 1;
      }
    })();

    for (let i = 0; i < msgCount; i++) {
      try {
        spotPub.publish('bench', parts, zlink.SendFlag.NONE);
      } catch (_) {
        break;
      }
      sent += 1;
      if ((i & 1023) === 0) {
        await Promise.resolve();
      }
    }
    await spotReceiverLoop;
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
  if (p === 'STREAM_LEN32BE') {
    return runStreamLen32Be(transport, size);
  }
  if (p === 'GATEWAY') {
    return runGateway(transport, size);
  }
  if (p === 'SPOT') {
    return runSpot(transport, size);
  }
  return 2;
}

function parsePatternArgs(expectedPattern, argv) {
  if (!argv || argv.length < 2) return null;
  const expected = String(expectedPattern).toUpperCase();
  let args = argv.slice();
  if (args.length >= 3 && String(args[0]).toUpperCase() === expected) {
    args = args.slice(1);
  }
  if (args.length < 2) return null;
  const transport = String(args[0]);
  const size = parseInt(args[1], 10);
  if (!Number.isFinite(size) || size <= 0) return null;
  return { transport, size };
}

async function runFromArgs(argv) {
  if (!argv || argv.length < 3) return 1;
  return runPattern(argv[0], argv[1], parseInt(argv[2], 10));
}

module.exports = {
  parsePatternArgs,
  runPairLike,
  runPubSub,
  runDealerRouter,
  runRouterRouter,
  runStream,
  runStreamLen32Be,
  runGateway,
  runSpot,
  runPattern,
  runFromArgs,
  zlink,
};

if (require.main === module) {
  runFromArgs(process.argv.slice(2))
    .then((code) => process.exit(code))
    .catch(() => process.exit(2));
}
