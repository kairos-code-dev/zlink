// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeader, sleepImmediate, stampPayload } = require('../common/perf_metrics');
const { callbackDrainTicks, callbackSendBurstLimit } = require('./perf_callback_policy');
const RECEIVER_ID = Buffer.from('router-perf-receiver', 'ascii');
const SENDER_ID = Buffer.from('router-perf-sender', 'ascii');
function partStrings(received) {
    return received.parts.map((part) => part.data.toString());
}
async function handshake(receiver, sender) {
    sender.send(RECEIVER_ID, Buffer.from('PING'));
    const ping = receiver.recv();
    if (ping.routingId === null || partStrings(ping).join(',') !== 'PING') {
        throw new Error('router-router handshake receive failed');
    }
    receiver.send(SENDER_ID, Buffer.from('PONG'));
    const pong = sender.recv();
    if (pong.routingId === null || partStrings(pong).join(',') !== 'PONG') {
        throw new Error('router-router handshake reply failed');
    }
}
async function runRouterRouterBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    const receiver = new zlink.RouterSocket(ctx);
    const sender = new zlink.RouterSocket(ctx);
    const endpoint = `inproc://perf-router-router-${process.pid}-${msgSize}`;
    try {
        receiver.setRoutingId(RECEIVER_ID);
        sender.setRoutingId(SENDER_ID);
        receiver.bind(endpoint);
        sender.connect(endpoint);
        await handshake(receiver, sender);
        const startedAtNs = process.hrtime.bigint();
        const runId = createRunId();
        const collector = createMetricCollector({ runId, msgSize });
        const payload = createPayload(msgSize);
        const sendBurstLimit = callbackSendBurstLimit(msgSize);
        const drainTicks = callbackDrainTicks(msgSize);
        const warmupUntilNs = startedAtNs
            + BigInt(Math.floor(options.warmup * 1_000_000_000));
        const stopAtNs = startedAtNs
            + BigInt(Math.floor((options.warmup + options.duration) * 1_000_000_000));
        receiver.onReceive((_, parts) => {
            const messageBuffer = parts[0].data;
            const header = decodeMetricHeader(messageBuffer);
            collector.record(header, process.hrtime.bigint());
        });
        let turns = 0;
        while (process.hrtime.bigint() < stopAtNs) {
            for (let i = 0; i < sendBurstLimit && process.hrtime.bigint() < stopAtNs; i += 1) {
                stampPayload(payload, {
                    phase: process.hrtime.bigint() < warmupUntilNs ? 2 : 0,
                    runId,
                    msgSize
                });
                if (sender.trySend(RECEIVER_ID, payload) !== zlink.SendResult.Sent) {
                    break;
                }
            }
            turns += 1;
            if (msgSize >= 65536 || (turns & 0x03) === 0) {
                await sleepImmediate();
            }
        }
        for (let i = 0; i < drainTicks; i += 1) {
            await sleepImmediate();
        }
        const result = await collector.finish();
        return result.latenciesUs;
    }
    finally {
        sender.close();
        receiver.close();
        ctx.close();
    }
}
module.exports = { runRouterRouterBenchmark };
