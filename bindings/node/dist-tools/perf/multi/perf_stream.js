// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const net = require('node:net');
const { once } = require('node:events');
const zlink = require('../../dist');
const { createPayload, latencyUsFromPayload, stampPayload } = require('../common/perf_metrics');
async function reservePort() {
    const server = net.createServer();
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const address = server.address();
    await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    return address.port;
}
function frame(buffer) {
    const framed = Buffer.allocUnsafe(buffer.length + 4);
    framed.writeUInt32BE(buffer.length, 0);
    buffer.copy(framed, 4);
    return framed;
}
function parseFrames(state, chunk) {
    state.buffer = Buffer.concat([state.buffer, chunk]);
    const payloads = [];
    while (state.buffer.length >= 4) {
        const payloadLength = state.buffer.readUInt32BE(0);
        const frameLength = payloadLength + 4;
        if (state.buffer.length < frameLength) {
            break;
        }
        payloads.push(state.buffer.subarray(4, frameLength));
        state.buffer = state.buffer.subarray(frameLength);
    }
    return payloads;
}
async function runStreamBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    const stream = new zlink.StreamSocket(ctx);
    const payload = createPayload(msgSize);
    const latenciesUs = [];
    const port = await reservePort();
    const endpoint = `tcp://127.0.0.1:${port}`;
    let client;
    let echoLoop;
    let stopEchoLoop = false;
    const startedAt = process.hrtime.bigint();
    const warmupNs = BigInt(Math.floor(options.warmup * 1_000_000_000));
    const activeNs = BigInt(Math.floor(options.duration * 1_000_000_000));
    const serverStates = new Map();
    const clientState = { buffer: Buffer.alloc(0) };
    const clientFrames = [];
    const clientWaiters = [];
    const pushClientFrame = (message) => {
        if (clientWaiters.length > 0) {
            const waiter = clientWaiters.shift();
            waiter(message);
            return;
        }
        clientFrames.push(message);
    };
    const readClientFrame = async () => {
        if (clientFrames.length > 0) {
            return clientFrames.shift();
        }
        return new Promise((resolve) => {
            clientWaiters.push(resolve);
        });
    };
    const processServerReceive = (received) => {
        const routingId = received.routingId;
        if (!routingId) {
            return;
        }
        const routingKey = routingId.toString('hex');
        let state = serverStates.get(routingKey);
        if (!state) {
            state = { buffer: Buffer.alloc(0) };
            serverStates.set(routingKey, state);
        }
        for (const part of received.parts) {
            const payloads = parseFrames(state, part.toBuffer());
            for (const payloadFrame of payloads) {
                stream.send(routingId, frame(payloadFrame));
            }
        }
    };
    try {
        stream.bind(endpoint);
        client = net.createConnection({ host: '127.0.0.1', port });
        await once(client, 'connect');
        client.on('data', (chunk) => {
            const payloads = parseFrames(clientState, chunk);
            for (const payloadFrame of payloads) {
                pushClientFrame(payloadFrame);
            }
        });
        if (options.recv === 'callback') {
            stream.onReceive((routingId, parts) => {
                processServerReceive({
                    routingId,
                    parts
                });
            });
        }
        else {
            echoLoop = (async () => {
                while (!stopEchoLoop) {
                    const received = stream.tryRecv();
                    if (!received) {
                        await new Promise((resolve) => setImmediate(resolve));
                        continue;
                    }
                    processServerReceive(received);
                }
            })();
        }
        const stopAtNs = warmupNs + activeNs;
        while (true) {
            stampPayload(payload);
            client.write(frame(payload));
            const echoedPayload = await readClientFrame();
            const elapsed = process.hrtime.bigint() - startedAt;
            if (elapsed >= warmupNs && elapsed < stopAtNs) {
                latenciesUs.push(latencyUsFromPayload(echoedPayload));
            }
            if (elapsed >= stopAtNs) {
                break;
            }
        }
        return latenciesUs;
    }
    finally {
        stopEchoLoop = true;
        if (echoLoop) {
            await echoLoop;
        }
        if (client) {
            client.destroy();
        }
        stream.close();
        ctx.close();
    }
}
module.exports = { runStreamBenchmark };
