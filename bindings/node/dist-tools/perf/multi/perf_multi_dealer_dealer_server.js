// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const { createPayload, createRunId, sleepImmediate, stampPayload } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { POLLOUT, applyContextPolicy, applySocketPolicy, trySocketSend } = require('./perf_multi_runtime');
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'server', 'MULTI_DEALER_DEALER');
    const server = new zlink.DealerSocket(ctx);
    const poller = new zlink.Poller();
    const payload = createPayload(options.msgSize);
    try {
        applySocketPolicy(server);
        server.bind(options.endpoint);
        poller.addSocket(server, POLLOUT);
        console.log(`READY,${options.endpoint}`);
        const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        for await (const line of rl) {
            if (line !== `START,${options.msgSize}`) {
                if (line === 'STOP') {
                    break;
                }
                continue;
            }
            const runId = createRunId(1);
            const activeStopNs = process.hrtime.bigint() + BigInt(Math.floor(options.duration * 1_000_000_000));
            let pending = false;
            let seq = 1n;
            while (process.hrtime.bigint() < activeStopNs) {
                if (!pending) {
                    stampPayload(payload, { phase: 1, runId, msgSize: options.msgSize, seq });
                    if (trySocketSend(server, payload)) {
                        seq += 1n;
                        continue;
                    }
                    pending = true;
                }
                const ready = poller.wait(25);
                if (!ready || (ready.events & POLLOUT) === 0) {
                    await sleepImmediate();
                    continue;
                }
                pending = false;
            }
            stampPayload(payload, { phase: 2, runId, msgSize: options.msgSize, seq });
            while (!trySocketSend(server, payload)) {
                const ready = poller.wait(25);
                if (!ready || (ready.events & POLLOUT) === 0) {
                    await sleepImmediate();
                }
            }
            break;
        }
    }
    finally {
        poller.close();
        server.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
