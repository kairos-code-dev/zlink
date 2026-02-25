'use strict';

const net = require('net');
const fs = require('fs');
const path = require('path');
const tls = require('tls');
const zlink = require('../../src/index');

const STOP_TOKEN = Buffer.from('__zlink_perf_stop__', 'utf8');
const MAX_STREAM_FRAME_BYTES = 16 * 1024 * 1024;

const ECHO_PATTERNS = new Set([
  'MULTI_DEALER_ROUTER',
  'MULTI_ROUTER_ROUTER',
  'MULTI_STREAM',
  'MULTI_STREAM_CALLBACK',
  'MULTI_STREAM_LEN32BE',
]);

function parseEnv(name, def) {
  let raw = process.env[name];
  if (!raw && name.startsWith('PERF_')) {
    raw = process.env[`PERF_${name.slice('PERF_'.length)}`];
  } else if (!raw && name.startsWith('PERF_')) {
    raw = process.env[`PERF_${name.slice('PERF_'.length)}`];
  }
  if (!raw) return def;
  const v = parseInt(raw, 10);
  return Number.isFinite(v) && v > 0 ? v : def;
}

function nowNs() {
  return process.hrtime.bigint();
}

function elapsedSec(startNs) {
  return Number(nowNs() - startNs) / 1e9;
}

function bandwidthMbps(pattern, throughput, size) {
  const mul = ECHO_PATTERNS.has(pattern) ? 2.0 : 1.0;
  return (throughput * size * mul) / 1_000_000.0;
}

function printClientResults(pattern, transport, size, throughput, latencyUs) {
  const bw = bandwidthMbps(pattern, throughput, size);
  console.log(`RESULT,current,${pattern},${transport},${size},throughput,${throughput}`);
  console.log(`RESULT,current,${pattern},${transport},${size},bandwidth,${bw}`);
  console.log(`RESULT,current,${pattern},${transport},${size},latency,${latencyUs}`);
}

function printServerMetrics(pattern, transport, size, cpuStart, wallStartNs) {
  const cpu = process.cpuUsage(cpuStart);
  const wallSec = Math.max(1e-9, elapsedSec(wallStartNs));
  const cpuSec = Math.max(0, (cpu.user + cpu.system) / 1_000_000.0);
  const ncpu = Math.max(1, (osCpusCount()));
  const cpuPct = (cpuSec / (wallSec * ncpu)) * 100.0;
  const memMb = process.memoryUsage().rss / (1024.0 * 1024.0);
  console.log(`RESULT,current,${pattern},${transport},${size},server_cpu_pct,${cpuPct}`);
  console.log(`RESULT,current,${pattern},${transport},${size},server_mem_mb,${memMb}`);
}

function osCpusCount() {
  const os = require('os');
  const cpus = os.cpus();
  return Array.isArray(cpus) && cpus.length > 0 ? cpus.length : 1;
}

function intSockOpt(value) {
  const buf = Buffer.alloc(4);
  buf.writeInt32LE(value, 0);
  return buf;
}

function getPort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.listen(0, '127.0.0.1', () => {
      const addr = server.address();
      server.close(() => resolve(addr.port));
    });
    server.on('error', reject);
  });
}

async function makeEndpoint(transport, bindPort) {
  const port = bindPort > 0 ? bindPort : await getPort();
  return `${transport}://127.0.0.1:${port}`;
}

function setupTlsServer(socket, transport) {
  if (transport !== 'tls' && transport !== 'wss') {
    return true;
  }
  const root = path.resolve(__dirname, '..', '..', '..', '..');
  const cert = path.join(root, 'core', 'tests', 'certs', 'gen', 'server.crt');
  const key = path.join(root, 'core', 'tests', 'certs', 'gen', 'server.key');
  if (!fs.existsSync(cert) || !fs.existsSync(key)) {
    return false;
  }
  try {
    socket.setSockOpt(zlink.SocketOption.TLS_CERT, Buffer.from(cert, 'utf8'));
    socket.setSockOpt(zlink.SocketOption.TLS_KEY, Buffer.from(key, 'utf8'));
    return true;
  } catch (_) {
    return false;
  }
}

function createMonitor(socket, acceptFallback) {
  const events = acceptFallback
    ? (zlink.MonitorEvent.CONNECTION_READY
      | zlink.MonitorEvent.ACCEPTED
      | zlink.MonitorEvent.CONNECTED)
    : (zlink.MonitorEvent.CONNECTION_READY | zlink.MonitorEvent.CONNECTED);
  return socket.monitorOpen(events);
}

function isMonitorReady(eventValue, acceptFallback) {
  if (eventValue === zlink.MonitorEvent.CONNECTION_READY) return true;
  if (!acceptFallback) return false;
  return eventValue === zlink.MonitorEvent.ACCEPTED
    || eventValue === zlink.MonitorEvent.CONNECTED;
}

function waitMonitorReady(monitor, timeoutMs, acceptFallback) {
  const poller = new zlink.Poller();
  poller.addSocket(monitor, zlink.PollEvent.POLLIN);
  const deadline = Date.now() + Math.max(1, timeoutMs);
  while (Date.now() < deadline) {
    const remain = Math.max(1, deadline - Date.now());
    const ready = poller.poll(remain);
    if (!Array.isArray(ready) || ready.length === 0) {
      continue;
    }
    const evt = monitor.recv(zlink.ReceiveFlag.NONE);
    const eventValue = Number(evt.event || 0);
    if (isMonitorReady(eventValue, acceptFallback)) {
      return true;
    }
  }
  return false;
}

function parseArgs(argv) {
  const out = {
    role: '',
    pattern: '',
    transport: '',
    size: 64,
    endpoint: '',
  };
  for (let i = 0; i < argv.length; i++) {
    const k = argv[i];
    if (k === '--role' && i + 1 < argv.length) {
      out.role = String(argv[++i]).toLowerCase();
    } else if (k === '--pattern' && i + 1 < argv.length) {
      out.pattern = String(argv[++i]).toUpperCase();
    } else if (k === '--transport' && i + 1 < argv.length) {
      out.transport = String(argv[++i]).toLowerCase();
    } else if (k === '--size' && i + 1 < argv.length) {
      const size = parseInt(argv[++i], 10);
      if (Number.isFinite(size) && size > 0) {
        out.size = size;
      }
    } else if (k === '--endpoint' && i + 1 < argv.length) {
      out.endpoint = String(argv[++i]);
    }
  }
  return out;
}

function printUnsupported(pattern, transport) {
  console.log(`UNSUPPORTED,node,${pattern},${transport}`);
}

function isStreamPattern(pattern) {
  return pattern === 'MULTI_STREAM'
    || pattern === 'MULTI_STREAM_CALLBACK'
    || pattern === 'MULTI_STREAM_LEN32BE';
}

function appendChunkAndCheckStop(stash, chunk) {
  if (!chunk || chunk.length === 0) {
    return false;
  }
  if (!stash.buf || stash.buf.length === 0) {
    stash.buf = Buffer.from(chunk);
  } else {
    stash.buf = Buffer.concat([stash.buf, chunk]);
  }

  while (stash.buf.length >= 4) {
    const bodyLen = stash.buf.readUInt32BE(0);
    if (!Number.isFinite(bodyLen) || bodyLen < 0 || bodyLen > MAX_STREAM_FRAME_BYTES) {
      stash.buf = Buffer.alloc(0);
      return false;
    }
    const frameLen = 4 + bodyLen;
    if (stash.buf.length < frameLen) {
      break;
    }
    const body = stash.buf.subarray(4, frameLen);
    stash.buf = stash.buf.subarray(frameLen);
    if (body.length === STOP_TOKEN.length && body.equals(STOP_TOKEN)) {
      return true;
    }
  }
  return false;
}

function parseRawEndpoint(endpoint) {
  const url = new URL(endpoint);
  const transport = String(url.protocol || '').replace(/:$/, '').toLowerCase();
  const host = String(url.hostname || '127.0.0.1');
  const port = parseInt(url.port || '0', 10);
  if (!Number.isFinite(port) || port <= 0) {
    throw new Error(`invalid endpoint: ${endpoint}`);
  }
  return { transport, host, port, url };
}

function maybeBuffer(data) {
  if (Buffer.isBuffer(data)) return data;
  if (data instanceof ArrayBuffer) return Buffer.from(data);
  if (ArrayBuffer.isView(data)) {
    return Buffer.from(data.buffer, data.byteOffset, data.byteLength);
  }
  if (typeof data === 'string') return Buffer.from(data, 'utf8');
  return null;
}

function encodeLen32Frame(payload) {
  const frame = Buffer.alloc(4 + payload.length);
  frame.writeUInt32BE(payload.length, 0);
  if (payload.length > 0) {
    payload.copy(frame, 4);
  }
  return frame;
}

function decodeLen32Frame(frame) {
  if (!frame || frame.length < 4) {
    throw new Error('invalid stream frame');
  }
  const declared = frame.readUInt32BE(0);
  if (!Number.isFinite(declared) || declared < 0 || declared > MAX_STREAM_FRAME_BYTES) {
    throw new Error('invalid stream frame size');
  }
  if (frame.length !== (4 + declared)) {
    throw new Error('stream frame size mismatch');
  }
  return frame.subarray(4);
}

class RawTransportStreamClient {
  constructor(endpoint, timeoutMs) {
    this._target = parseRawEndpoint(endpoint);
    this._timeoutMs = Math.max(1, timeoutMs | 0);
    this._socket = null;
    this._ws = null;
    this._recvBuf = Buffer.alloc(0);
    this._wsFrames = [];
    this._closed = false;
    this._error = null;
  }

  async connect() {
    if (this._target.transport === 'ws' || this._target.transport === 'wss') {
      return this._connectWs();
    }
    return this._connectTcpLike();
  }

  async sendPayload(payload) {
    const frame = encodeLen32Frame(payload);
    if (this._ws) {
      if (this._ws.readyState !== this._ws.OPEN) {
        throw new Error('websocket is not open');
      }
      this._ws.send(frame);
      return;
    }

    const sock = this._socket;
    if (!sock || this._closed) {
      throw new Error('socket is closed');
    }
    const ok = sock.write(frame);
    if (!ok) {
      await this._waitForSocketDrain();
    }
  }

  async recvPayload(timeoutMs) {
    if (this._ws) {
      return this._recvPayloadWs(timeoutMs);
    }
    return this._recvPayloadTcpLike(timeoutMs);
  }

  close() {
    this._closed = true;
    try {
      if (this._ws) this._ws.close();
    } catch (_) {}
    try {
      if (this._socket) this._socket.destroy();
    } catch (_) {}
  }

  async _connectTcpLike() {
    const { transport, host, port } = this._target;
    await new Promise((resolve, reject) => {
      let settled = false;
      let timer = null;
      const finish = (err) => {
        if (settled) return;
        settled = true;
        if (timer) clearTimeout(timer);
        if (err) reject(err);
        else resolve();
      };

      const connectHandler = () => finish(null);
      const opts = { host, port };
      const sock = transport === 'tls'
        ? tls.connect({ ...opts, rejectUnauthorized: false, servername: host }, connectHandler)
        : net.createConnection(opts, connectHandler);

      this._socket = sock;
      sock.setNoDelay(true);
      sock.on('data', (chunk) => {
        if (!chunk || chunk.length === 0) return;
        this._recvBuf = this._recvBuf.length === 0
          ? Buffer.from(chunk)
          : Buffer.concat([this._recvBuf, chunk]);
      });
      sock.on('close', () => {
        this._closed = true;
      });
      sock.on('end', () => {
        this._closed = true;
      });
      sock.on('error', (err) => {
        this._error = err || new Error('socket error');
        finish(this._error);
      });

      timer = setTimeout(() => {
        const err = new Error('connect timeout');
        this._error = err;
        try { sock.destroy(); } catch (_) {}
        finish(err);
      }, this._timeoutMs);
    });
  }

  async _connectWs() {
    const WS = globalThis.WebSocket;
    if (typeof WS !== 'function') {
      throw new Error('WebSocket client is unavailable');
    }

    const { transport, url } = this._target;
    if (transport === 'wss') {
      process.env.NODE_TLS_REJECT_UNAUTHORIZED = '0';
    }
    await new Promise((resolve, reject) => {
      let settled = false;
      let timer = null;
      const finish = (err) => {
        if (settled) return;
        settled = true;
        if (timer) clearTimeout(timer);
        if (err) reject(err);
        else resolve();
      };

      const ws = new WS(url.toString());
      this._ws = ws;
      ws.binaryType = 'arraybuffer';
      ws.onopen = () => finish(null);
      ws.onmessage = (event) => {
        const buf = maybeBuffer(event.data);
        if (buf && buf.length > 0) {
          this._wsFrames.push(buf);
        }
      };
      ws.onerror = () => {
        const err = new Error('websocket error');
        this._error = err;
        finish(err);
      };
      ws.onclose = () => {
        this._closed = true;
      };

      timer = setTimeout(() => {
        const err = new Error('websocket connect timeout');
        this._error = err;
        try { ws.close(); } catch (_) {}
        finish(err);
      }, this._timeoutMs);
    });
  }

  async _waitForSocketDrain() {
    const sock = this._socket;
    if (!sock) return;
    await new Promise((resolve, reject) => {
      let timer = null;
      const onDrain = () => {
        cleanup();
        resolve();
      };
      const onErr = (err) => {
        cleanup();
        reject(err || new Error('socket write error'));
      };
      const cleanup = () => {
        sock.off('drain', onDrain);
        sock.off('error', onErr);
        if (timer) clearTimeout(timer);
      };

      sock.on('drain', onDrain);
      sock.on('error', onErr);
      timer = setTimeout(() => {
        cleanup();
        reject(new Error('socket drain timeout'));
      }, this._timeoutMs);
    });
  }

  async _recvPayloadWs(timeoutMs) {
    const deadline = Date.now() + Math.max(1, timeoutMs | 0);
    while (Date.now() < deadline) {
      if (this._error) throw this._error;
      if (this._wsFrames.length > 0) {
        const frame = this._wsFrames.shift();
        return decodeLen32Frame(frame);
      }
      if (this._closed) break;
      await new Promise((resolve) => setTimeout(resolve, 1));
    }
    throw new Error('recv timeout');
  }

  async _recvPayloadTcpLike(timeoutMs) {
    const deadline = Date.now() + Math.max(1, timeoutMs | 0);
    while (Date.now() < deadline) {
      if (this._error) throw this._error;
      if (this._recvBuf.length >= 4) {
        const declared = this._recvBuf.readUInt32BE(0);
        if (!Number.isFinite(declared) || declared < 0 || declared > MAX_STREAM_FRAME_BYTES) {
          throw new Error('invalid stream frame length');
        }
        const frameLen = 4 + declared;
        if (this._recvBuf.length >= frameLen) {
          const frame = this._recvBuf.subarray(0, frameLen);
          this._recvBuf = this._recvBuf.subarray(frameLen);
          return decodeLen32Frame(frame);
        }
      }
      if (this._closed) break;
      await new Promise((resolve) => setTimeout(resolve, 1));
    }
    throw new Error('recv timeout');
  }
}

async function runServerRouter(pattern, transport, endpoint, size) {
  const cpuStart = process.cpuUsage();
  const wallStartNs = nowNs();
  const ctx = new zlink.Context();
  const sock = new zlink.Socket(ctx, zlink.SocketType.ROUTER);
  const monitor = createMonitor(sock, true);
  const rcvTimeoutMs = parseEnv('PERF_MULTI_RCVTIMEO_MS', 5000);
  const readyTimeoutMs = parseEnv('PERF_MULTI_CONNECT_READY_TIMEOUT_MS', 5000);
  const ridBuf = Buffer.alloc(256);
  const payloadBuf = Buffer.alloc(Math.max(size, STOP_TOKEN.length, 256));
  try {
    sock.setSockOpt(zlink.SocketOption.RCVTIMEO, intSockOpt(rcvTimeoutMs));
    if (!setupTlsServer(sock, transport)) {
      return 2;
    }
    sock.bind(endpoint);
    console.log(`READY,${endpoint}`);
    if (!waitMonitorReady(monitor, readyTimeoutMs, true)) {
      return 2;
    }
    while (true) {
      const ridLen = sock.recvInto(ridBuf, zlink.ReceiveFlag.NONE);
      const payloadLen = sock.recvInto(payloadBuf, zlink.ReceiveFlag.NONE);
      const msg = payloadBuf.subarray(0, Math.max(0, Math.min(payloadLen, payloadBuf.length)));
      if (msg.length === STOP_TOKEN.length && msg.equals(STOP_TOKEN)) {
        break;
      }
      sock.sendFrom(ridBuf, ridLen, zlink.SendFlag.SNDMORE);
      sock.send(msg, zlink.SendFlag.NONE);
    }
    printServerMetrics(pattern, transport, size, cpuStart, wallStartNs);
    return 0;
  } catch (_) {
    return 2;
  } finally {
    try { monitor.close(); } catch (_) {}
    try { sock.close(); } catch (_) {}
    try { ctx.close(); } catch (_) {}
  }
}

async function runServerDealer(pattern, transport, endpoint, size) {
  const cpuStart = process.cpuUsage();
  const wallStartNs = nowNs();
  const ctx = new zlink.Context();
  const sock = new zlink.Socket(ctx, zlink.SocketType.DEALER);
  const monitor = createMonitor(sock, true);
  const rcvTimeoutMs = parseEnv('PERF_MULTI_RCVTIMEO_MS', 5000);
  const readyTimeoutMs = parseEnv('PERF_MULTI_CONNECT_READY_TIMEOUT_MS', 5000);
  const payloadBuf = Buffer.alloc(Math.max(size, STOP_TOKEN.length, 256));
  try {
    sock.setSockOpt(zlink.SocketOption.RCVTIMEO, intSockOpt(rcvTimeoutMs));
    if (!setupTlsServer(sock, transport)) {
      return 2;
    }
    sock.bind(endpoint);
    console.log(`READY,${endpoint}`);
    if (!waitMonitorReady(monitor, readyTimeoutMs, true)) {
      return 2;
    }
    while (true) {
      const payloadLen = sock.recvInto(payloadBuf, zlink.ReceiveFlag.NONE);
      const msg = payloadBuf.subarray(0, Math.max(0, Math.min(payloadLen, payloadBuf.length)));
      if (msg.length === STOP_TOKEN.length && msg.equals(STOP_TOKEN)) {
        break;
      }
      sock.send(msg, zlink.SendFlag.NONE);
    }
    printServerMetrics(pattern, transport, size, cpuStart, wallStartNs);
    return 0;
  } catch (_) {
    return 2;
  } finally {
    try { monitor.close(); } catch (_) {}
    try { sock.close(); } catch (_) {}
    try { ctx.close(); } catch (_) {}
  }
}

async function runServerStreamRecv(pattern, transport, endpoint, size) {
  const cpuStart = process.cpuUsage();
  const wallStartNs = nowNs();
  const ctx = new zlink.Context();
  const sock = new zlink.Socket(ctx, zlink.SocketType.STREAM);
  const monitor = createMonitor(sock, true);
  const rcvTimeoutMs = parseEnv('PERF_MULTI_RCVTIMEO_MS', 5000);
  const sndTimeoutMs = parseEnv('PERF_MULTI_SNDTIMEO_MS', 5000);
  const readyTimeoutMs = parseEnv('PERF_MULTI_CONNECT_READY_TIMEOUT_MS', 5000);
  let attached = false;

  const stashByRid = new Map();
  const ridKey = (rid) => Buffer.from(rid).toString('hex');
  const getStash = (rid) => {
    const key = ridKey(rid);
    const cur = stashByRid.get(key);
    if (cur) return cur;
    const created = { buf: Buffer.alloc(0) };
    stashByRid.set(key, created);
    return created;
  };

  const tryReadFrame = (stash) => {
    if (!stash || !stash.buf || stash.buf.length < 4) {
      return null;
    }
    const bodyLen = stash.buf.readUInt32BE(0);
    if (!Number.isFinite(bodyLen) || bodyLen < 0 || bodyLen > MAX_STREAM_FRAME_BYTES) {
      stash.buf = Buffer.alloc(0);
      return null;
    }
    const frameLen = 4 + bodyLen;
    if (stash.buf.length < frameLen) {
      return null;
    }
    const frame = stash.buf.subarray(0, frameLen);
    stash.buf = stash.buf.subarray(frameLen);
    return frame;
  };

  try {
    sock.setSockOpt(zlink.SocketOption.SNDTIMEO, intSockOpt(sndTimeoutMs));
    sock.setSockOpt(zlink.SocketOption.RCVTIMEO, intSockOpt(rcvTimeoutMs));
    if (!setupTlsServer(sock, transport)) {
      return 2;
    }
    sock.bind(endpoint);
    console.log(`READY,${endpoint}`);
    if (!waitMonitorReady(monitor, readyTimeoutMs, true)) {
      return 2;
    }

    let stop = false;
    let callbackFailed = false;

    sock.streamAttach((rid, packets) => {
      if (!rid || rid.length === 0 || !packets) {
        return 0;
      }

      const stash = getStash(rid);
      for (const packet of packets) {
        if (!packet || packet.length === 0) {
          continue;
        }
        if (packet.length === 1 && (packet[0] === 0x00 || packet[0] === 0x01)) {
          continue;
        }

        stash.buf = stash.buf.length === 0
          ? Buffer.from(packet)
          : Buffer.concat([stash.buf, packet]);

        while (true) {
          const frame = tryReadFrame(stash);
          if (!frame) break;
          const body = frame.subarray(4);
          if (body.length === STOP_TOKEN.length && body.equals(STOP_TOKEN)) {
            stop = true;
            break;
          }
          try {
            sock.streamSend(rid, frame, zlink.SendFlag.NONE);
          } catch (_) {
            callbackFailed = true;
            stop = true;
            return 1;
          }
        }
      }
      return 0;
    }, zlink.StreamDispatchMode.NONE);
    attached = true;

    const deadline = Date.now() + Math.max(rcvTimeoutMs * 12, 60000);
    while (!stop && Date.now() < deadline) {
      await new Promise((resolve) => setTimeout(resolve, 1));
    }
    if (!stop || callbackFailed) {
      return 2;
    }

    printServerMetrics(pattern, transport, size, cpuStart, wallStartNs);
    return 0;
  } catch (_) {
    return 2;
  } finally {
    if (attached) {
      try { sock.streamDetach(); } catch (_) {}
    }
    try { monitor.close(); } catch (_) {}
    try { sock.close(); } catch (_) {}
    try { ctx.close(); } catch (_) {}
  }
}

async function runServerStreamCallback(pattern, transport, endpoint, size, dispatchMode) {
  const cpuStart = process.cpuUsage();
  const wallStartNs = nowNs();
  const ctx = new zlink.Context();
  const sock = new zlink.Socket(ctx, zlink.SocketType.STREAM);
  const monitor = createMonitor(sock, true);
  const rcvTimeoutMs = parseEnv('PERF_MULTI_RCVTIMEO_MS', 5000);
  const sndTimeoutMs = parseEnv('PERF_MULTI_SNDTIMEO_MS', 5000);
  const readyTimeoutMs = parseEnv('PERF_MULTI_CONNECT_READY_TIMEOUT_MS', 5000);
  const frameStash = { buf: Buffer.alloc(0) };
  let attached = false;

  try {
    sock.setSockOpt(zlink.SocketOption.SNDTIMEO, intSockOpt(sndTimeoutMs));
    sock.setSockOpt(zlink.SocketOption.RCVTIMEO, intSockOpt(rcvTimeoutMs));
    if (!setupTlsServer(sock, transport)) {
      return 2;
    }
    sock.bind(endpoint);
    console.log(`READY,${endpoint}`);
    if (!waitMonitorReady(monitor, readyTimeoutMs, true)) {
      return 2;
    }

    let stop = false;
    let callbackFailed = false;

    sock.streamAttach((rid, packets) => {
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

        let stopDetected = false;
        if (dispatchMode === zlink.StreamDispatchMode.LEN32BE) {
          stopDetected = packet.length === STOP_TOKEN.length && packet.equals(STOP_TOKEN);
        } else {
          stopDetected = appendChunkAndCheckStop(frameStash, packet);
        }
        if (stopDetected) {
          stop = true;
          continue;
        }

        try {
          sock.streamSend(rid, packet, zlink.SendFlag.NONE);
        } catch (_) {
          callbackFailed = true;
          stop = true;
          return 1;
        }
      }
      return 0;
    }, dispatchMode);
    attached = true;

    const deadline = Date.now() + Math.max(rcvTimeoutMs * 12, 60000);
    while (!stop && Date.now() < deadline) {
      await new Promise((resolve) => setTimeout(resolve, 1));
    }
    if (!stop || callbackFailed) {
      return 2;
    }

    printServerMetrics(pattern, transport, size, cpuStart, wallStartNs);
    return 0;
  } catch (_) {
    return 2;
  } finally {
    if (attached) {
      try { sock.streamDetach(); } catch (_) {}
    }
    try { monitor.close(); } catch (_) {}
    try { sock.close(); } catch (_) {}
    try { ctx.close(); } catch (_) {}
  }
}

async function runClientEchoDealer(pattern, transport, endpoint, size) {
  const ctx = new zlink.Context();
  const sock = new zlink.Socket(ctx, zlink.SocketType.DEALER);
  const monitor = createMonitor(sock, false);
  const sndTimeoutMs = parseEnv('PERF_MULTI_SNDTIMEO_MS', 5000);
  const rcvTimeoutMs = parseEnv('PERF_MULTI_RCVTIMEO_MS', 5000);
  const readyTimeoutMs = parseEnv('PERF_MULTI_CONNECT_READY_TIMEOUT_MS', 5000);
  const warmupS = Math.max(0, parseEnv('PERF_MULTI_WARMUP_SECONDS', 3));
  const durationS = Math.max(1, parseEnv('PERF_MULTI_DURATION_SECONDS', 5));
  const latCount = Math.max(1, parseEnv('PERF_LAT_COUNT', 200));
  const payload = Buffer.alloc(size, 'a');
  const recvBuf = Buffer.alloc(Math.max(size, STOP_TOKEN.length, 256));
  try {
    sock.setSockOpt(zlink.SocketOption.SNDTIMEO, intSockOpt(sndTimeoutMs));
    sock.setSockOpt(zlink.SocketOption.RCVTIMEO, intSockOpt(rcvTimeoutMs));
    sock.connect(endpoint);
    if (!waitMonitorReady(monitor, readyTimeoutMs, false)) {
      return 2;
    }

    const warmupDeadline = Date.now() + warmupS * 1000;
    while (Date.now() < warmupDeadline) {
      sock.send(payload, zlink.SendFlag.NONE);
      sock.recvInto(recvBuf, zlink.ReceiveFlag.NONE);
    }

    const latStart = nowNs();
    for (let i = 0; i < latCount; i++) {
      sock.send(payload, zlink.SendFlag.NONE);
      sock.recvInto(recvBuf, zlink.ReceiveFlag.NONE);
    }
    const latUs = (Number(nowNs() - latStart) / 1000.0) / (latCount * 2);

    let ops = 0;
    const t0 = nowNs();
    const deadline = Date.now() + durationS * 1000;
    while (Date.now() < deadline) {
      sock.send(payload, zlink.SendFlag.NONE);
      sock.recvInto(recvBuf, zlink.ReceiveFlag.NONE);
      ops += 1;
    }
    const thr = ops / Math.max(1e-9, elapsedSec(t0));
    sock.send(STOP_TOKEN, zlink.SendFlag.NONE);
    printClientResults(pattern, transport, size, thr, latUs);
    return 0;
  } catch (_) {
    return 2;
  } finally {
    try { monitor.close(); } catch (_) {}
    try { sock.close(); } catch (_) {}
    try { ctx.close(); } catch (_) {}
  }
}

async function runClientEchoRawStream(pattern, transport, endpoint, size) {
  const sndTimeoutMs = parseEnv('PERF_MULTI_SNDTIMEO_MS', 5000);
  const warmupS = Math.max(0, parseEnv('PERF_MULTI_WARMUP_SECONDS', 3));
  const durationS = Math.max(1, parseEnv('PERF_MULTI_DURATION_SECONDS', 5));
  const latCount = Math.max(1, parseEnv('PERF_LAT_COUNT', 200));
  const payload = Buffer.alloc(size, 'a');
  const client = new RawTransportStreamClient(endpoint, sndTimeoutMs);

  try {
    await client.connect();

    const warmupDeadline = Date.now() + warmupS * 1000;
    while (Date.now() < warmupDeadline) {
      await client.sendPayload(payload);
      const echoed = await client.recvPayload(sndTimeoutMs);
      if (!echoed || echoed.length !== payload.length) {
        return 2;
      }
    }

    const latStart = nowNs();
    for (let i = 0; i < latCount; i++) {
      await client.sendPayload(payload);
      const echoed = await client.recvPayload(sndTimeoutMs);
      if (!echoed || echoed.length !== payload.length) {
        return 2;
      }
    }
    const latUs = (Number(nowNs() - latStart) / 1000.0) / (latCount * 2);

    let ops = 0;
    const t0 = nowNs();
    const deadline = Date.now() + durationS * 1000;
    while (Date.now() < deadline) {
      await client.sendPayload(payload);
      const echoed = await client.recvPayload(sndTimeoutMs);
      if (!echoed || echoed.length !== payload.length) {
        break;
      }
      ops += 1;
    }
    if (ops <= 0) {
      return 2;
    }
    const thr = ops / Math.max(1e-9, elapsedSec(t0));
    await client.sendPayload(STOP_TOKEN);
    printClientResults(pattern, transport, size, thr, latUs);
    return 0;
  } catch (_) {
    return 2;
  } finally {
    try { client.close(); } catch (_) {}
  }
}

async function runWithArgs(argv) {
  const args = parseArgs(argv);
  if (!args.role || !args.pattern || !args.transport || args.size <= 0) {
    return 1;
  }

  if (args.role === 'server') {
    const bindPort = parseEnv('PERF_MULTI_SERVER_BIND_PORT', 0);
    const endpoint = await makeEndpoint(args.transport, bindPort);
    if (args.pattern === 'MULTI_DEALER_ROUTER'
      || args.pattern === 'MULTI_ROUTER_ROUTER') {
      return runServerRouter(args.pattern, args.transport, endpoint, args.size);
    }
    if (args.pattern === 'MULTI_STREAM') {
      return runServerStreamRecv(args.pattern, args.transport, endpoint, args.size);
    }
    if (args.pattern === 'MULTI_STREAM_CALLBACK') {
      return runServerStreamCallback(
        args.pattern,
        args.transport,
        endpoint,
        args.size,
        zlink.StreamDispatchMode.NONE
      );
    }
    if (args.pattern === 'MULTI_STREAM_LEN32BE') {
      return runServerStreamCallback(
        args.pattern,
        args.transport,
        endpoint,
        args.size,
        zlink.StreamDispatchMode.LEN32BE
      );
    }
    return runServerDealer(args.pattern, args.transport, endpoint, args.size);
  }

  if (args.role === 'client') {
    if (!args.endpoint) return 1;
    if (isStreamPattern(args.pattern)) {
      return runClientEchoRawStream(
        args.pattern, args.transport, args.endpoint, args.size
      );
    }
    return runClientEchoDealer(
      args.pattern, args.transport, args.endpoint, args.size
    );
  }

  return 1;
}

async function run() {
  return runWithArgs(process.argv.slice(2));
}

if (require.main === module) {
  run()
    .then((rc) => process.exit(rc))
    .catch(() => process.exit(2));
}

module.exports = {
  runWithArgs,
};
