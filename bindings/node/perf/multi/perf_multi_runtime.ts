// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist/canonical');
const { MonitorEvent, RecvFlags, RecvResult } = zlink;
const { sleepImmediate } = require('../common/perf_metrics');
const POLLIN = 1;
const POLLOUT = 2;

function integerEnv(name, fallback) {
  const raw = process.env[name];
  if (raw === undefined || raw === '') {
    return fallback;
  }
  const parsed = Number(raw);
  return Number.isFinite(parsed) ? Math.trunc(parsed) : fallback;
}

function applySocketPolicy(socket, options = {}) {
  const hwm = integerEnv('PERF_MULTI_HWM', 1000);
  const sendHwm = integerEnv('PERF_MULTI_SNDHWM', hwm);
  const recvHwm = integerEnv('PERF_MULTI_RCVHWM', hwm);
  const sendTimeout = integerEnv('PERF_MULTI_SNDTIMEO_MS', 200);
  const recvTimeout = integerEnv('PERF_MULTI_RCVTIMEO_MS', 200);

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

async function waitForConnectionReady(
  socket,
  connectFn = null,
  timeoutMs = integerEnv('PERF_MULTI_CONNECT_READY_TIMEOUT_MS', 5000)
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

function trySocketSend(socket, ...args) {
  try {
    socket.send(...args, zlink.SendFlags.DontWait);
    return true;
  } catch (error) {
    if (error instanceof zlink.SubmitError && error.result === zlink.SubmitResult.Backpressured) {
      return false;
    }
    const text = String(error && error.message ? error.message : error);
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
    if (error instanceof zlink.SubmitError && error.result === zlink.SubmitResult.Backpressured) {
      return false;
    }
    const text = String(error && error.message ? error.message : error);
    if ((error && error.code === 'EAGAIN') || text.includes('Resource temporarily unavailable')) {
      return false;
    }
    throw error;
  }
}

async function drainRecvSocket(socket, onMessage, shouldStop, pollTimeoutMs = 25) {
  const poller = new zlink.Poller();
  poller.addSocket(socket, POLLIN);

  try {
    while (!shouldStop()) {
      let ready = null;
      try {
        ready = poller.wait(pollTimeoutMs);
      } catch (error) {
        const text = String(error && error.message ? error.message : error);
        if ((error && error.code === 'EAGAIN') || text.includes('Resource temporarily unavailable')) {
          await sleepImmediate();
          continue;
        }
        throw error;
      }
      if (!ready) {
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

module.exports = {
  POLLIN,
  POLLOUT,
  applySocketPolicy,
  drainRecvSocket,
  recvNoWait,
  subscribeNoWait,
  trySocketPublish,
  trySocketSend,
  waitForConnectionReady
};
