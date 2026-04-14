// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist/canonical');
const { createPayload, sleepImmediate, stampPayload } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { waitForConnectionReady } = require('./perf_multi_runtime');
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    const clients = [];
    const warmupPayloads = [];
    const activePayloads = [];
    try {
        for (let i = 0; i < options.clients; i += 1) {
            const dealer = new zlink.DealerSocket(ctx);
            clients.push(dealer);
            warmupPayloads.push(createPayload(options.msgSize));
            activePayloads.push(createPayload(options.msgSize));
        }
        for (const dealer of clients) {
            await waitForConnectionReady(dealer, () => dealer.connect(options.endpoint));
        }
        console.log('CLIENT_READY');
        const warmupUntil = process.hrtime.bigint() + BigInt(Math.floor(options.warmup * 1_000_000_000));
        let seq = 1n;
        while (process.hrtime.bigint() < warmupUntil) {
            for (let i = 0; i < clients.length; i += 1) {
                stampPayload(warmupPayloads[i], { phase: 0, runId: 0, msgSize: options.msgSize, seq });
                clients[i].send(warmupPayloads[i]);
                seq += 1n;
            }
            await sleepImmediate();
        }
        const activeUntil = process.hrtime.bigint() + BigInt(Math.floor(options.duration * 1_000_000_000));
        while (process.hrtime.bigint() < activeUntil) {
            for (let i = 0; i < clients.length; i += 1) {
                stampPayload(activePayloads[i], { phase: 1, runId: 0, msgSize: options.msgSize, seq });
                clients[i].send(activePayloads[i]);
                seq += 1n;
            }
            await sleepImmediate();
        }
    }
    finally {
        for (const dealer of clients) {
            dealer.close();
        }
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
