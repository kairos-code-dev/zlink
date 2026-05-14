// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const net = require('node:net');
const tls = require('node:tls');
const { once } = require('node:events');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeader, currentEpochNs, summarizeMetrics, stampPayload } = require('../common/perf_metrics');
const { parseMultiArgs, resolveMultiConnectConcurrency } = require('./perf_multi_common');
const { resolveMultiLatencySampleCap } = require('./perf_multi_runtime');
function buildStreamPacketFrame(body) {
    const frame = Buffer.allocUnsafe(6 + body.length);
    frame.writeUInt16BE(0, 0);
    frame.writeUInt32BE(body.length, 2);
    body.copy(frame, 6);
    return frame;
}
function buildStreamPacketFrames(body) {
    return buildStreamPacketFrame(body);
}
function readFixedPayload(state, payloadSize) {
    const first = state.chunks[0];
    if (first.length === payloadSize) {
        state.chunks.shift();
        state.bufferedBytes -= payloadSize;
        return first;
    }
    if (first.length > payloadSize) {
        const payload = first.subarray(0, payloadSize);
        state.chunks[0] = first.subarray(payloadSize);
        state.bufferedBytes -= payloadSize;
        return payload;
    }
    const payload = Buffer.allocUnsafe(payloadSize);
    let copied = 0;
    while (copied < payloadSize) {
        const chunk = state.chunks[0];
        const remaining = payloadSize - copied;
        const take = Math.min(chunk.length, remaining);
        chunk.copy(payload, copied, 0, take);
        copied += take;
        if (take === chunk.length) {
            state.chunks.shift();
        }
        else {
            state.chunks[0] = chunk.subarray(take);
        }
    }
    state.bufferedBytes -= payloadSize;
    return payload;
}
function parseFixedPayloads(state, chunk, payloadSize) {
    if (chunk.length > 0) {
        state.chunks.push(chunk);
        state.bufferedBytes += chunk.length;
    }
    const payloads = [];
    while (state.bufferedBytes >= payloadSize) {
        payloads.push(readFixedPayload(state, payloadSize));
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
function createFrameReader(transport, payloadSize) {
    const state = {
        chunks: [],
        bufferedBytes: 0,
        pending: [],
        waiters: []
    };
    const flushWaiters = () => {
        while (state.pending.length > 0 && state.waiters.length > 0) {
            state.waiters.shift().resolve(state.pending.shift());
        }
    };
    const onData = (chunk) => {
        const payloads = parseFixedPayloads(state, normalizeBinaryChunk(chunk), payloadSize);
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
async function nextFrameWithTimeout(reader, timeoutMs) {
    let timeout = null;
    try {
        return await Promise.race([
            reader.nextFrame(),
            new Promise((resolve) => {
                timeout = setTimeout(() => resolve(null), timeoutMs);
            })
        ]);
    }
    finally {
        if (timeout) {
            clearTimeout(timeout);
        }
    }
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
            readers.push(createFrameReader(transport, options.msgSize));
            payloads.push(createPayload(options.msgSize));
        }
        if (transports.length !== options.clients) {
            throw new Error(`connect_ok mismatch: ${transports.length}/${options.clients}`);
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
        let fatalError = null;
        const inflight = new Array(transports.length).fill(false);
        const submitOne = (index) => {
            const currentSeq = seq;
            seq += 1n;
            inflight[index] = true;
            stampPayload(payloads[index], { phase: 1, runId, msgSize: options.msgSize, seq: currentSeq });
            transports[index].writeFrame(buildStreamPacketFrames(payloads[index]))
                .then(() => nextFrameWithTimeout(readers[index], Number(process.env.PERF_MULTI_STREAM_FRAME_TIMEOUT_MS ?? 1000)))
                .then((echoed) => {
                if (echoed) {
                    collector.record(decodeMetricHeader(echoed), currentEpochNs());
                }
            })
                .catch((error) => {
                fatalError = error;
            })
                .finally(() => {
                inflight[index] = false;
            });
        };
        while (currentEpochNs() < activeStopNs) {
            if (fatalError) {
                throw fatalError;
            }
            let submitted = false;
            for (let i = 0; i < transports.length; i += 1) {
                if (!inflight[i]) {
                    submitOne(i);
                    submitted = true;
                }
            }
            if (!submitted) {
                await new Promise((resolve) => setImmediate(resolve));
            }
        }
        while (inflight.some(Boolean)) {
            if (fatalError) {
                throw fatalError;
            }
            await new Promise((resolve) => setImmediate(resolve));
        }
        const result = await collector.finish();
        for (const metricLine of summarizeMetrics('MULTI_STREAM', options.transport, options.msgSize, result.latenciesNs, options.duration, 'current', result.accepted)) {
            console.log(metricLine);
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
