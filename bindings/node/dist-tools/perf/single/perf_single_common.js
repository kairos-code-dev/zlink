// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist/canonical');
const { MonitorEvent, RecvFlags, RecvResult } = zlink;
const { sleepImmediate } = require('../common/perf_metrics');
const READY_EVENTS = new Set([MonitorEvent.CONNECTION_READY, MonitorEvent.CONNECTED]);
const POLLIN = 1;
function tryRecv(socket) {
    try {
        return socket.recv(RecvFlags.DontWait);
    }
    catch (error) {
        if (error instanceof zlink.RecvError && error.result === RecvResult.NoData) {
            return null;
        }
        throw error;
    }
}
function trySubscribe(socket) {
    try {
        return socket.subscribe(RecvFlags.DontWait);
    }
    catch (error) {
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
    }
    catch (error) {
        if (error instanceof zlink.SubmitError && error.result === zlink.SubmitResult.Backpressured) {
            return false;
        }
        throw error;
    }
}
function trySocketPublish(socket, topic, payload) {
    try {
        socket.publish(topic, payload, zlink.SendFlags.DontWait);
        return true;
    }
    catch (error) {
        if (error instanceof zlink.SubmitError && error.result === zlink.SubmitResult.Backpressured) {
            return false;
        }
        throw error;
    }
}
async function waitForConnectionReady(socket, connectFn = null, timeoutMs = 5000) {
    const monitor = socket.monitorOpen(MonitorEvent.CONNECTION_READY);
    const deadline = Date.now() + timeoutMs;
    try {
        if (typeof connectFn === 'function') {
            await connectFn();
        }
        while (Date.now() < deadline) {
            if (monitor.snapshot().isReady()) {
                const event = monitor.recv();
                if (READY_EVENTS.has(event.event)) {
                    return;
                }
            }
            await sleepImmediate();
        }
        throw new Error(`connection ready timeout after ${timeoutMs}ms`);
    }
    finally {
        monitor.close();
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
            }
            catch (error) {
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
                    const received = trySubscribe(socket);
                    if (!received) {
                        break;
                    }
                    onMessage(received);
                }
            }
            else {
                while (true) {
                    const received = tryRecv(socket);
                    if (!received) {
                        break;
                    }
                    onMessage(received);
                }
            }
        }
    }
    finally {
        poller.close();
    }
}
function drainRecvNow(socket, onMessage) {
    if (typeof socket.subscribe === 'function') {
        while (true) {
            const received = trySubscribe(socket);
            if (!received) {
                return;
            }
            onMessage(received);
        }
        return;
    }
    while (true) {
        const received = tryRecv(socket);
        if (!received) {
            break;
        }
        onMessage(received);
    }
}
module.exports = {
    drainRecvSocket,
    drainRecvNow,
    waitForConnectionReady,
    trySocketSend,
    trySocketPublish
};
