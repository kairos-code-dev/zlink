// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const { createMetricCollector, createRunId, decodeMetricHeader, currentEpochNs, summarizeMetrics } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { drainRecvSocket } = require('./perf_multi_runtime');
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    const server = new zlink.DealerSocket(ctx);
    const runId = createRunId(1);
    let collector = null;
    let stop = false;
    try {
        server.bind(options.endpoint);
        const recvTask = drainRecvSocket(server, (received) => {
            if (!collector) {
                return;
            }
            collector.record(decodeMetricHeader(received.parts[0].data()), currentEpochNs());
        }, () => stop);
        console.log(`READY,${options.endpoint}`);
        const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        for await (const line of rl) {
            if (line === `START,${options.msgSize}`) {
                const activeStartNs = process.hrtime.bigint();
                const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
                collector = createMetricCollector({
                    runId,
                    msgSize: options.msgSize,
                    activeStartNs,
                    activeStopNs
                });
                continue;
            }
            if (line === 'STOP') {
                stop = true;
                break;
            }
        }
        await recvTask;
        const result = collector ? await collector.finish() : { latenciesNs: [] };
        for (const line of summarizeMetrics('MULTI_DEALER_DEALER', 'tcp', options.msgSize, result.latenciesNs, options.duration)) {
            console.log(line);
        }
    }
    finally {
        server.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
