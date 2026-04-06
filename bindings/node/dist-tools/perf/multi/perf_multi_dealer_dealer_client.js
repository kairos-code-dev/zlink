// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist');
const { createPayload, sleepImmediate, stampPayload } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    const clients = [];
    const warmupPayloads = [];
    const activePayloads = [];
    const stopPayloads = [];
    try {
        for (let i = 0; i < options.clients; i += 1) {
            const dealer = new zlink.DealerSocket(ctx);
            dealer.connect(options.endpoint);
            clients.push(dealer);
            warmupPayloads.push(createPayload(options.msgSize));
            activePayloads.push(createPayload(options.msgSize));
            stopPayloads.push(createPayload(options.msgSize));
        }
        console.log('CLIENT_READY');
        const warmupUntil = process.hrtime.bigint() + BigInt(Math.floor(options.warmup * 1_000_000_000));
        while (process.hrtime.bigint() < warmupUntil) {
            for (let i = 0; i < clients.length; i += 1) {
                stampPayload(warmupPayloads[i], { phase: 2, runId: 0, msgSize: options.msgSize });
                clients[i].send(warmupPayloads[i]);
            }
            await sleepImmediate();
        }
        const activeUntil = process.hrtime.bigint() + BigInt(Math.floor(options.duration * 1_000_000_000));
        while (process.hrtime.bigint() < activeUntil) {
            for (let i = 0; i < clients.length; i += 1) {
                stampPayload(activePayloads[i], { phase: 0, runId: 0, msgSize: options.msgSize });
                clients[i].send(activePayloads[i]);
            }
            await sleepImmediate();
        }
        for (let i = 0; i < clients.length; i += 1) {
            stampPayload(stopPayloads[i], { phase: 1, runId: 0, msgSize: options.msgSize });
            clients[i].send(stopPayloads[i]);
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
