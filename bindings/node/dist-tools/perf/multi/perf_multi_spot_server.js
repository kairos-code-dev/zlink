// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist');
const { createPayload, stampPayload } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const TOPIC = 'perf.topic';
function ensureMeshPubBudgetDefault() {
    if (!process.env.ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM) {
        process.env.ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM = '100';
    }
}
async function main() {
    ensureMeshPubBudgetDefault();
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    const node = new zlink.SpotNode(ctx);
    const spot = new zlink.Spot(node);
    const warmupPayload = createPayload(options.msgSize);
    const activePayload = createPayload(options.msgSize);
    const stopPayload = createPayload(options.msgSize);
    const publishUntil = async (payload, phase, deadlineNs) => {
        while (process.hrtime.bigint() < deadlineNs) {
            stampPayload(payload, { phase, runId: 0, msgSize: options.msgSize });
            spot.tryPublish(TOPIC, payload);
            await new Promise((resolve) => setImmediate(resolve));
        }
    };
    const publishStopFrames = async () => {
        let sent = 0;
        const deadline = Date.now() + 5000;
        while (sent < options.clients && Date.now() < deadline) {
            stampPayload(stopPayload, { phase: 1, runId: 0, msgSize: options.msgSize });
            const result = spot.tryPublish(TOPIC, stopPayload);
            if (result === zlink.SendResult.Sent) {
                sent += 1;
                await new Promise((resolve) => setImmediate(resolve));
                continue;
            }
            await new Promise((resolve) => setImmediate(resolve));
        }
        if (sent < options.clients) {
            throw new Error(`spot server stop publish timeout: sent=${sent} expected=${options.clients}`);
        }
        for (let i = 0; i < 4; i += 1) {
            await new Promise((resolve) => setImmediate(resolve));
        }
    };
    try {
        node.bind(options.endpoint);
        spot.setLinger(0);
        spot.setSendHighWaterMark(100);
        spot.setSendTimeout(200);
        spot.setNoDrop(true);
        console.log(`READY,${options.endpoint}`);
        const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        for await (const line of rl) {
            if (line !== `START,${options.msgSize}` && line !== 'GO') {
                continue;
            }
            await publishUntil(warmupPayload, 2, process.hrtime.bigint() + BigInt(Math.floor(options.warmup * 1_000_000_000)));
            await publishUntil(activePayload, 0, process.hrtime.bigint() + BigInt(Math.floor(options.duration * 1_000_000_000)));
            await publishStopFrames();
            break;
        }
    }
    finally {
        spot.close();
        node.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
