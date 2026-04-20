// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const net = require('node:net');
const tls = require('node:tls');
const readline = require('node:readline');
const { once } = require('node:events');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeader, currentEpochNs, summarizeMetrics, stampPayload } = require('../common/perf_metrics');
const { parseMultiArgs, resolveMultiConnectConcurrency } = require('./perf_multi_common');
const { resolveMultiLatencySampleCap } = require('./perf_multi_runtime');
function buildPacketFrame(body) {
    const frame = Buffer.allocUnsafe(6 + body.length);
    frame.writeUInt16BE(0, 0);
    frame.writeUInt32BE(body.length, 2);
    body.copy(frame, 6);
    return frame;
}
function parseFrames(state, chunk) {
    state.buffer = Buffer.concat([state.buffer, chunk]);
    const payloads = [];
    while (state.buffer.length >= 6) {
        const headerSize = state.buffer.readUInt16BE(0);
        const bodySize = state.buffer.readUInt32BE(2);
        const frameLength = 6 + headerSize + bodySize;
        if (state.buffer.length < frameLength) {
            break;
        }
        payloads.push(state.buffer.subarray(6 + headerSize, frameLength));
        state.buffer = state.buffer.subarray(frameLength);
    }
    return payloads;
}
function normalizeBinaryChunk(data) {
    if (Buffer.isBuffer(data)) {
        return data;
    }
    if (data instanceof ArrayBuffer) {
        return Buffer.from(data);
    }
    if (ArrayBuffer.isView(data)) {
        return Buffer.from(data.buffer, data.byteOffset, data.byteLength);
    }
    if (typeof data === 'string') {
        return Buffer.from(data, 'utf8');
    }
    throw new Error(`unsupported stream payload type: ${typeof data}`);
}
function createFrameReader(transport) {
    const state = {
        buffer: Buffer.alloc(0),
        pending: [],
        waiters: []
    };
    const flushWaiters = () => {
        while (state.pending.length > 0 && state.waiters.length > 0) {
            state.waiters.shift().resolve(state.pending.shift());
        }
    };
    const onData = (chunk) => {
        const payloads = parseFrames(state, normalizeBinaryChunk(chunk));
        if (payloads.length > 0) {
            state.pending.push(...payloads);
            flushWaiters();
        }
    };
    const onClose = () => {
        const error = new Error('stream socket closed');
        while (state.waiters.length > 0) {
            state.waiters.shift().reject(error);
        }
    };
    transport.onData(onData);
    transport.onError(onClose);
    transport.onClose(onClose);
    return {
        async nextFrame() {
            if (state.pending.length > 0) {
                return state.pending.shift();
            }
            return new Promise((resolve, reject) => {
                state.waiters.push({ resolve, reject });
            });
        },
        close() {
            transport.offData(onData);
            transport.offError(onClose);
            transport.offClose(onClose);
        }
    };
}
async function connectTcpTransport(endpoint) {
    const url = new URL(endpoint);
    const socket = net.createConnection({
        host: url.hostname || '127.0.0.1',
        port: Number(url.port)
    });
    socket.setNoDelay(true);
    await once(socket, 'connect');
    return {
        writeFrame(frame) {
            if (socket.write(frame)) {
                return Promise.resolve();
            }
            return once(socket, 'drain').then(() => undefined);
        },
        destroy() {
            socket.destroy();
        },
        onData(listener) {
            socket.on('data', listener);
        },
        onError(listener) {
            socket.on('error', listener);
        },
        onClose(listener) {
            socket.on('close', listener);
        },
        offData(listener) {
            socket.off('data', listener);
        },
        offError(listener) {
            socket.off('error', listener);
        },
        offClose(listener) {
            socket.off('close', listener);
        }
    };
}
async function connectTlsTransport(endpoint) {
    const url = new URL(endpoint);
    const socket = tls.connect({
        host: url.hostname || '127.0.0.1',
        port: Number(url.port),
        rejectUnauthorized: false
    });
    socket.setNoDelay(true);
    await once(socket, 'secureConnect');
    return {
        writeFrame(frame) {
            if (socket.write(frame)) {
                return Promise.resolve();
            }
            return once(socket, 'drain').then(() => undefined);
        },
        destroy() {
            socket.destroy();
        },
        onData(listener) {
            socket.on('data', listener);
        },
        onError(listener) {
            socket.on('error', listener);
        },
        onClose(listener) {
            socket.on('close', listener);
        },
        offData(listener) {
            socket.off('data', listener);
        },
        offError(listener) {
            socket.off('error', listener);
        },
        offClose(listener) {
            socket.off('close', listener);
        }
    };
}
async function connectWebSocketTransport(endpoint) {
    const WebSocketCtor = globalThis.WebSocket;
    if (typeof WebSocketCtor !== 'function') {
        throw new Error('WebSocket client is unavailable in this Node runtime');
    }
    const socket = new WebSocketCtor(endpoint);
    socket.binaryType = 'arraybuffer';
    const dataListeners = new Map();
    const errorListeners = new Map();
    const closeListeners = new Map();
    await new Promise((resolve, reject) => {
        const onOpen = () => {
            socket.removeEventListener('error', onError);
            resolve();
        };
        const onError = (event) => {
            socket.removeEventListener('open', onOpen);
            reject(event?.error || new Error('websocket connect failed'));
        };
        socket.addEventListener('open', onOpen, { once: true });
        socket.addEventListener('error', onError, { once: true });
    });
    return {
        writeFrame(frame) {
            socket.send(frame);
            return Promise.resolve();
        },
        destroy() {
            socket.close();
        },
        onData(listener) {
            const wrapped = (event) => listener(event.data);
            dataListeners.set(listener, wrapped);
            socket.addEventListener('message', wrapped);
        },
        onError(listener) {
            const wrapped = (event) => listener(event);
            errorListeners.set(listener, wrapped);
            socket.addEventListener('error', wrapped);
        },
        onClose(listener) {
            const wrapped = () => listener();
            closeListeners.set(listener, wrapped);
            socket.addEventListener('close', wrapped);
        },
        offData(listener) {
            const wrapped = dataListeners.get(listener);
            if (wrapped) {
                socket.removeEventListener('message', wrapped);
                dataListeners.delete(listener);
            }
        },
        offError(listener) {
            const wrapped = errorListeners.get(listener);
            if (wrapped) {
                socket.removeEventListener('error', wrapped);
                errorListeners.delete(listener);
            }
        },
        offClose(listener) {
            const wrapped = closeListeners.get(listener);
            if (wrapped) {
                socket.removeEventListener('close', wrapped);
                closeListeners.delete(listener);
            }
        }
    };
}
async function connectStreamTransport(endpoint, transport) {
    if (transport === 'tcp') {
        return connectTcpTransport(endpoint);
    }
    if (transport === 'tls') {
        return connectTlsTransport(endpoint);
    }
    if (transport === 'ws' || transport === 'wss') {
        return connectWebSocketTransport(endpoint);
    }
    throw new Error(`unsupported stream transport: ${transport}`);
}
async function connectAllStreamTransports(endpoint, transport, clientCount) {
    const concurrency = resolveMultiConnectConcurrency(clientCount);
    const connected = [];
    for (let start = 0; start < clientCount; start += concurrency) {
        const batchSize = Math.min(concurrency, clientCount - start);
        const batch = await Promise.allSettled(Array.from({ length: batchSize }, async () => connectStreamTransport(endpoint, transport)));
        for (const item of batch) {
            if (item.status === 'fulfilled') {
                connected.push(item.value);
            }
        }
    }
    return connected;
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const runId = createRunId(1);
    const transports = [];
    const readers = [];
    const payloads = [];
    try {
        const connected = await connectAllStreamTransports(options.endpoint, options.transport, options.clients);
        for (const transport of connected) {
            transports.push(transport);
            readers.push(createFrameReader(transport));
            payloads.push(createPayload(options.msgSize));
        }
        if (transports.length !== options.clients) {
            throw new Error(`connect_ok mismatch: ${transports.length}/${options.clients}`);
        }
        console.log(`CLIENT_READY,${options.msgSize}`);
        const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        for await (const line of rl) {
            if (line !== `START,${options.msgSize}`) {
                continue;
            }
            const activeStartNs = currentEpochNs();
            const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
            const collector = createMetricCollector({
                runId,
                msgSize: options.msgSize,
                activeStartNs,
                activeStopNs,
                roundTrip: true,
                sampleCap: resolveMultiLatencySampleCap()
            });
            let seq = 1n;
            while (currentEpochNs() < activeStopNs) {
                for (let i = 0; i < transports.length; i += 1) {
                    stampPayload(payloads[i], { phase: 1, runId, msgSize: options.msgSize, seq });
                    await transports[i].writeFrame(buildPacketFrame(payloads[i]));
                    const echoed = await readers[i].nextFrame();
                    collector.record(decodeMetricHeader(echoed), currentEpochNs());
                    seq += 1n;
                }
            }
            const result = await collector.finish();
            for (const metricLine of summarizeMetrics('MULTI_STREAM', options.transport, options.msgSize, result.latenciesNs, options.duration, 'current', result.accepted)) {
                console.log(metricLine);
            }
            break;
        }
    }
    finally {
        for (const reader of readers) {
            reader.close();
        }
        for (const transport of transports) {
            transport.destroy();
        }
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
