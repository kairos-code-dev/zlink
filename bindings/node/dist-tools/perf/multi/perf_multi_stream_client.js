// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const net = require('node:net');
const readline = require('node:readline');
const { once } = require('node:events');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeader, currentEpochNs, summarizeMetrics, stampPayload } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
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
function createFrameReader(socket) {
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
        const payloads = parseFrames(state, chunk);
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
    socket.on('data', onData);
    socket.on('error', onClose);
    socket.on('close', onClose);
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
            socket.off('data', onData);
            socket.off('error', onClose);
            socket.off('close', onClose);
        }
    };
}
async function connectStreamSocket(endpoint) {
    const url = new URL(endpoint);
    const socket = net.createConnection({
        host: url.hostname || '127.0.0.1',
        port: Number(url.port)
    });
    socket.setNoDelay(true);
    await once(socket, 'connect');
    return socket;
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const runId = createRunId(1);
    const sockets = [];
    const readers = [];
    const payloads = [];
    try {
        const connected = await Promise.all(Array.from({ length: options.clients }, async () => connectStreamSocket(options.endpoint)));
        for (const socket of connected) {
            sockets.push(socket);
            readers.push(createFrameReader(socket));
            payloads.push(createPayload(options.msgSize));
        }
        if (sockets.length !== options.clients) {
            throw new Error(`connect_ok mismatch: ${sockets.length}/${options.clients}`);
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
                roundTrip: true
            });
            let seq = 1n;
            while (currentEpochNs() < activeStopNs) {
                for (let i = 0; i < sockets.length; i += 1) {
                    stampPayload(payloads[i], { phase: 1, runId, msgSize: options.msgSize, seq });
                    if (!sockets[i].write(buildPacketFrame(payloads[i]))) {
                        await once(sockets[i], 'drain');
                    }
                    const echoed = await readers[i].nextFrame();
                    collector.record(decodeMetricHeader(echoed), currentEpochNs());
                    seq += 1n;
                }
            }
            const result = await collector.finish();
            for (const metricLine of summarizeMetrics('MULTI_STREAM', options.transport, options.msgSize, result.latenciesNs, options.duration)) {
                console.log(metricLine);
            }
            break;
        }
    }
    finally {
        for (const reader of readers) {
            reader.close();
        }
        for (const socket of sockets) {
            socket.destroy();
        }
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
