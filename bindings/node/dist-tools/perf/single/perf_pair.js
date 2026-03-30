// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist');
const { createPayload, latencyUsFromPayload, stampPayload } = require('../common/perf_metrics');
async function runPairBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    const server = new zlink.PairSocket(ctx);
    const client = new zlink.PairSocket(ctx);
    const endpoint = `inproc://perf-pair-${process.pid}-${msgSize}`;
    const payload = createPayload(msgSize);
    const latenciesUs = [];
    let measuring = false;
    let done = false;
    const startedAt = process.hrtime.bigint();
    const warmupNs = BigInt(Math.floor(options.warmup * 1_000_000_000));
    const activeNs = BigInt(Math.floor(options.duration * 1_000_000_000));
    try {
        server.bind(endpoint);
        client.connect(endpoint);
        server.recvHandler((_, parts) => {
            const elapsed = process.hrtime.bigint() - startedAt;
            if (elapsed >= warmupNs) {
                measuring = true;
            }
            if (measuring && elapsed < warmupNs + activeNs) {
                latenciesUs.push(latencyUsFromPayload(parts[0].toBuffer()));
            }
            if (elapsed >= warmupNs + activeNs) {
                done = true;
            }
        });
        while (!done) {
            stampPayload(payload);
            const result = client.trySend(payload);
            if (result !== zlink.SendResult.Sent) {
                await new Promise((resolve) => setImmediate(resolve));
                continue;
            }
            await new Promise((resolve) => setImmediate(resolve));
        }
        return latenciesUs;
    }
    finally {
        client.close();
        server.close();
        ctx.close();
    }
}
module.exports = { runPairBenchmark };
