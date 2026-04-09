// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist');
const { PollEvent, MonitorEvent } = zlink;
const { sleepImmediate } = require('../common/perf_metrics');
const READY_EVENTS = new Set([MonitorEvent.CONNECTION_READY, MonitorEvent.CONNECTED]);

async function waitForConnectionReady(socket, connectFn = null, timeoutMs = 5000) {
  const monitor = socket.monitorOpen(MonitorEvent.CONNECTION_READY);
  const deadline = Date.now() + timeoutMs;
  try {
    if (typeof connectFn === 'function') {
      await connectFn();
    }
    while (Date.now() < deadline) {
      const event = monitor.tryRecv();
      if (event && READY_EVENTS.has(event.event)) {
        return;
      }
      await sleepImmediate();
    }
    throw new Error(`connection ready timeout after ${timeoutMs}ms`);
  } finally {
    monitor.close();
  }
}

async function drainRecvSocket(socket, onMessage, shouldStop, pollTimeoutMs = 25) {
  const poller = new zlink.Poller();
  poller.addSocket(socket, PollEvent.POLLIN);

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
    const socketKind = socket && socket.constructor ? socket.constructor.name : '';
    if (socketKind === 'StreamSocket' && typeof socket.recv === 'function') {
      const received = socket.recv();
      if (received) {
        onMessage(received);
      }
    } else if (typeof socket.trySubscribe === 'function') {
      while (true) {
        const received = socket.trySubscribe();
        if (!received) {
          break;
        }
        onMessage(received);
      }
    } else if (typeof socket.tryRecv === 'function') {
      while (true) {
        const received = socket.tryRecv();
        if (!received) {
          break;
        }
        onMessage(received);
      }
    } else {
      const received = socket.recv();
      if (received) {
        onMessage(received);
      }
    }
  }
}

module.exports = {
  drainRecvSocket,
  waitForConnectionReady
};
