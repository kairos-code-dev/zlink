// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist');
const { createMetricCollector, decodeMetricHeader, currentEpochUs, summarizeMetrics } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { drainRecvSocket } = require('./perf_multi_runtime');
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    const server = new zlink.DealerSocket(ctx);
    const collector = createMetricCollector({
        runId: 0,
        msgSize: options.msgSize
    });
    let stop = false;
    try {
        server.bind(options.endpoint);
        const recvTask = drainRecvSocket(server, (received) => {
            const header = decodeMetricHeader(received.parts[0].data);
            collector.record(header, currentEpochUs());
        }, () => stop);
        console.log(`READY,${options.endpoint}`);
        const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        for await (const line of rl) {
            if (line === 'STOP') {
                stop = true;
                break;
            }
        }
        await recvTask;
        const result = await collector.finish();
        const resultLines = summarizeMetrics('MULTI_DEALER_DEALER', 'tcp', options.msgSize, result.latenciesUs, options.duration);
        for (const lineOut of resultLines) {
            console.log(lineOut);
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
