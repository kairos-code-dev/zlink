// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const { createPayload, createRunId, sleepImmediate, stampPayload } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { trySocketPublish } = require('./perf_multi_runtime');
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    const pub = new zlink.PubSocket(ctx);
    const payload = createPayload(options.msgSize);
    try {
        pub.bind(options.endpoint);
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
            let seq = 1n;
            while (process.hrtime.bigint() < activeStopNs) {
                stampPayload(payload, { phase: 1, runId, msgSize: options.msgSize, seq });
                if (trySocketPublish(pub, 'perf.topic', payload)) {
                    seq += 1n;
                    continue;
                }
                await sleepImmediate();
            }
            break;
        }
    }
    finally {
        pub.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
