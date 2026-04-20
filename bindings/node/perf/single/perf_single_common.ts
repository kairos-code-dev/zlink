// SPDX-License-Identifier: MPL-2.0

'use strict';

const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const { once } = require('node:events');
const zlink = require('../../dist/canonical');
const {
  MonitorEvent,
  RecvFlags,
  RecvResult
} = zlink;
const {
  sleepImmediate
} = require('../common/perf_metrics');
const POLLIN = 1;

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const address = server.address();
  await new Promise((resolve, reject) => {
    server.close((error) => {
      if (error) {
        reject(error);
        return;
      }
      resolve();
    });
  });
  return address.port;
}

async function benchmarkEndpoint(transport, token) {
  if (transport === 'inproc') {
    return `inproc://perf-${token}-${process.pid}`;
  }
  if (transport === 'ipc') {
    return `ipc://${path.join(os.tmpdir(), `zlink-node-perf-${process.pid}-${token}.sock`)}`;
  }
  if (
    transport === 'tcp'
    || transport === 'tls'
    || transport === 'ws'
    || transport === 'wss'
  ) {
    return `${transport}://127.0.0.1:${await reservePort()}`;
  }
  throw new Error(`unsupported single transport: ${transport}`);
}

function integerEnv(name, fallback) {
  const raw = process.env[name];
  if (raw === undefined || raw === '') {
    return fallback;
  }
  const parsed = Number(raw);
  return Number.isFinite(parsed) ? Math.trunc(parsed) : fallback;
}

function applySocketPolicy(socket, options = {}) {
  const hwm = integerEnv('PERF_SINGLE_HWM', 1000);
  const sendHwm = integerEnv('PERF_SINGLE_SNDHWM', hwm);
  const recvHwm = integerEnv('PERF_SINGLE_RCVHWM', hwm);
  const sendTimeout = integerEnv('PERF_SINGLE_SNDTIMEO_MS', 200);
  const recvTimeout = integerEnv('PERF_SINGLE_RCVTIMEO_MS', 200);

  if (typeof socket.setSendHighWaterMark === 'function') {
    socket.setSendHighWaterMark(sendHwm);
  }
  if (typeof socket.setReceiveHighWaterMark === 'function') {
    socket.setReceiveHighWaterMark(recvHwm);
  }
  if (typeof socket.setSendTimeout === 'function') {
    socket.setSendTimeout(sendTimeout);
  }
  if (typeof socket.setReceiveTimeout === 'function') {
    socket.setReceiveTimeout(options.recvTimeout ?? recvTimeout);
  }
  if (typeof socket.setNoDrop === 'function' && options.noDrop !== undefined) {
    socket.setNoDrop(Boolean(options.noDrop));
  }
}

function recvNoWait(socket) {
  try {
    return socket.recv(RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === RecvResult.NoData) {
      return null;
    }
    throw error;
  }
}

function subscribeNoWait(socket) {
  try {
    return socket.subscribe(RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === RecvResult.NoData) {
      return null;
    }
    throw error;
  }
}

function trySocketSend(socket, ...args) {
  try {
    socket.send(...args, zlink.SendFlags.DontWait);
    return true;
  } catch (error) {
    const text = String(error && error.message ? error.message : error);
    if (error instanceof zlink.SubmitError && error.result === zlink.SubmitResult.Backpressured) {
      return false;
    }
    if ((error && error.code === 'EAGAIN') || text.includes('Resource temporarily unavailable')) {
      return false;
    }
    throw error;
  }
}

function trySocketPublish(socket, topic, payload) {
  try {
    socket.publish(topic, payload, zlink.SendFlags.DontWait);
    return true;
  } catch (error) {
    const text = String(error && error.message ? error.message : error);
    if (error instanceof zlink.SubmitError && error.result === zlink.SubmitResult.Backpressured) {
      return false;
    }
    if ((error && error.code === 'EAGAIN') || text.includes('Resource temporarily unavailable')) {
      return false;
    }
    throw error;
  }
}

async function waitForConnectionReady(
  socket,
  connectFn = null,
  timeoutMs = integerEnv('PERF_CONNECT_READY_TIMEOUT_MS', 5000)
) {
  const monitor = socket.monitorOpen(MonitorEvent.CONNECTION_READY);
  try {
    if (typeof connectFn === 'function') {
      await connectFn();
    }
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      try {
        const event = monitor.recv(RecvFlags.DontWait);
        if (event.event === MonitorEvent.CONNECTION_READY) {
          return;
        }
      } catch (error) {
        if (!(error instanceof zlink.RecvError && error.result === RecvResult.NoData)) {
          throw error;
        }
      }
      await sleepImmediate();
    }
    throw new Error(`connection ready timeout after ${timeoutMs}ms`);
  } finally {
    monitor.close();
  }
}

async function waitForPostReadySettle(timeoutMs) {
  const deadline = Date.now() + Math.max(0, timeoutMs | 0);
  while (Date.now() < deadline) {
    await sleepImmediate();
  }
}

async function drainRecvSocket(socket, onMessage, shouldStop, pollTimeoutMs = 25) {
  const poller = new zlink.Poller();
  poller.addSocket(socket, POLLIN);

  try {
    while (!shouldStop()) {
      let ready = [];
      try {
        ready = poller.poll(pollTimeoutMs);
      } catch (error) {
        const text = String(error && error.message ? error.message : error);
        if ((error && error.code === 'EAGAIN') || text.includes('Resource temporarily unavailable')) {
          await sleepImmediate();
          continue;
        }
        throw error;
      }
      if (ready.length === 0) {
        await sleepImmediate();
        continue;
      }
      if (typeof socket.subscribe === 'function') {
        while (true) {
          const received = subscribeNoWait(socket);
          if (!received) {
            break;
          }
          onMessage(received);
        }
      } else {
        while (true) {
          const received = recvNoWait(socket);
          if (!received) {
            break;
          }
          onMessage(received);
        }
      }
    }
  } finally {
    poller.close();
  }
}

function drainRecvNow(socket, onMessage) {
  if (typeof socket.subscribe === 'function') {
    while (true) {
      const received = subscribeNoWait(socket);
      if (!received) {
        return;
      }
      onMessage(received);
    }
    return;
  }
  while (true) {
    const received = recvNoWait(socket);
    if (!received) {
      break;
    }
    onMessage(received);
  }
}

module.exports = {
  applySocketPolicy,
  benchmarkEndpoint,
  drainRecvSocket,
  drainRecvNow,
  waitForPostReadySettle,
  waitForConnectionReady,
  trySocketSend,
  trySocketPublish
};
