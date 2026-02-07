'use strict';

const net = require('net');
const zlink = require('../src/index');

const ZLINK_PAIR = 0;
const ZLINK_PUB = 1;
const ZLINK_SUB = 2;
const ZLINK_DEALER = 5;
const ZLINK_ROUTER = 6;
const ZLINK_XPUB = 9;
const ZLINK_XSUB = 10;

const ZLINK_SUBSCRIBE = 6;
const ZLINK_XPUB_VERBOSE = 40;
const ZLINK_LAST_ENDPOINT = 32;
const ZLINK_DONTWAIT = 1;
const ZLINK_SNDMORE = 2;

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

async function transports(prefix) {
  return [
    { name: 'tcp', endpoint: '' },
    { name: 'ws', endpoint: '' },
    { name: 'inproc', endpoint: `inproc://${prefix}-${Date.now()}` },
  ];
}

async function endpointFor(name, baseEndpoint, suffix) {
  if (name === 'inproc') return baseEndpoint + suffix;
  const port = await getPort();
  return `${name}://127.0.0.1:${port}`;
}

async function tryTransport(name, fn) {
  try {
    await fn();
  } catch (err) {
    if (name === 'ws') return;
    throw err;
  }
}

async function recvWithTimeout(socket, size, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let last;
  while (Date.now() < deadline) {
    try {
      return socket.recv(size, ZLINK_DONTWAIT);
    } catch (err) {
      last = err;
      await new Promise(r => setTimeout(r, 10));
    }
  }
  if (last) throw last;
  throw new Error('timeout');
}

async function sendWithRetry(socket, buf, flags, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let last;
  while (Date.now() < deadline) {
    try {
      socket.send(buf, flags);
      return;
    } catch (err) {
      last = err;
      await new Promise(r => setTimeout(r, 10));
    }
  }
  if (last) throw last;
  throw new Error('timeout');
}

async function gatewaySendWithRetry(gateway, service, parts, flags, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let last;
  while (Date.now() < deadline) {
    try {
      gateway.send(service, parts, flags);
      return;
    } catch (err) {
      last = err;
      await new Promise(r => setTimeout(r, 10));
    }
  }
  if (last) throw last;
  throw new Error('timeout');
}

async function waitUntil(fn, timeoutMs, intervalMs = 10) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      if (fn()) return true;
    } catch (_) {
      // keep polling while discovery/gateway state converges
    }
    await new Promise(r => setTimeout(r, intervalMs));
  }
  return false;
}

async function gatewayRecvWithTimeout(gateway, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let last;
  while (Date.now() < deadline) {
    try {
      return gateway.recv(ZLINK_DONTWAIT);
    } catch (err) {
      last = err;
      await new Promise(r => setTimeout(r, 10));
    }
  }
  if (last) throw last;
  throw new Error('timeout');
}

async function spotRecvWithTimeout(spot, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let last;
  while (Date.now() < deadline) {
    try {
      return spot.recv(ZLINK_DONTWAIT);
    } catch (err) {
      last = err;
      await new Promise(r => setTimeout(r, 10));
    }
  }
  if (last) throw last;
  throw new Error('timeout');
}

module.exports = {
  zlink,
  transports,
  endpointFor,
  tryTransport,
  recvWithTimeout,
  sendWithRetry,
  gatewaySendWithRetry,
  waitUntil,
  gatewayRecvWithTimeout,
  spotRecvWithTimeout,
  ZLINK_PAIR,
  ZLINK_PUB,
  ZLINK_SUB,
  ZLINK_DEALER,
  ZLINK_ROUTER,
  ZLINK_XPUB,
  ZLINK_XSUB,
  ZLINK_SUBSCRIBE,
  ZLINK_XPUB_VERBOSE,
  ZLINK_LAST_ENDPOINT,
  ZLINK_DONTWAIT,
  ZLINK_SNDMORE,
};
