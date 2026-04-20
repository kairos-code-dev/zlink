// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist/canonical');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeader, currentEpochNs, sleepImmediate, stampPayload } = require('../common/perf_metrics');
const { applySocketPolicy, benchmarkEndpoint, drainRecvSocket, waitForConnectionReady, } = require('./perf_single_common');
const RECEIVER_ID = Buffer.from('router-perf-receiver', 'ascii');
const SENDER_ID = Buffer.from('router-perf-sender', 'ascii');
const RECEIVER_ROUTING_ID = zlink.RoutingId.fromBytes(RECEIVER_ID);
const SENDER_ROUTING_ID = zlink.RoutingId.fromBytes(SENDER_ID);
function partStrings(received) {
    return received.parts.map((part) => part.data().toString());
}
async function handshake(receiver, sender) {
    sender.send(RECEIVER_ROUTING_ID, Buffer.from('PING'));
    const ping = receiver.recv();
    if (ping.routingId === null || partStrings(ping).join(',') !== 'PING') {
        throw new Error('router-router handshake receive failed');
    }
    receiver.send(SENDER_ROUTING_ID, Buffer.from('PONG'));
    const pong = sender.recv();
    if (pong.routingId === null || partStrings(pong).join(',') !== 'PONG') {
        throw new Error('router-router handshake reply failed');
    }
}
async function runRouterRouterBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    const receiver = new zlink.RouterSocket(ctx);
    const sender = new zlink.RouterSocket(ctx);
    const endpoint = await benchmarkEndpoint(options.transport, `router-router-${msgSize}`);
    try {
        applySocketPolicy(receiver);
        applySocketPolicy(sender);
        receiver.setRoutingId(RECEIVER_ROUTING_ID);
        sender.setRoutingId(SENDER_ROUTING_ID);
        receiver.bind(endpoint);
        await waitForConnectionReady(sender, () => sender.connect(endpoint));
        await handshake(receiver, sender);
        const activeStartNs = currentEpochNs();
        const activeStopNs = activeStartNs
            + BigInt(Math.floor(options.duration * 1_000_000_000));
        const runId = createRunId(options.runId ?? 1);
        const collector = createMetricCollector({
            runId,
            msgSize,
            activeStartNs,
            activeStopNs,
            sampleCap: Number(process.env.PERF_SINGLE_LATENCY_SAMPLE_CAP ?? 200000)
        });
        const payload = createPayload(msgSize);
        let seq = 1n;
        let stop = false;
        const recvTask = drainRecvSocket(receiver, (received) => {
            const header = decodeMetricHeader(received.parts[0].data());
            collector.record(header, currentEpochNs());
        }, () => stop);
        while (currentEpochNs() < activeStopNs) {
            for (let i = 0; i < 256 && currentEpochNs() < activeStopNs; i += 1) {
                stampPayload(payload, {
                    phase: 1,
                    runId,
                    msgSize,
                    seq
                });
                sender.send(RECEIVER_ROUTING_ID, payload);
                seq += 1n;
            }
            if (currentEpochNs() < activeStopNs) {
                await sleepImmediate();
            }
        }
        stampPayload(payload, { phase: 2, runId, msgSize, seq });
        sender.send(RECEIVER_ROUTING_ID, payload);
        const drainDeadlineNs = activeStopNs + 250000000n;
        while (currentEpochNs() < drainDeadlineNs) {
            await sleepImmediate();
        }
        stop = true;
        await recvTask;
        const result = await collector.finish();
        return result.latenciesNs;
    }
    finally {
        sender.close();
        receiver.close();
        ctx.close();
    }
}
module.exports = { runRouterRouterBenchmark };
