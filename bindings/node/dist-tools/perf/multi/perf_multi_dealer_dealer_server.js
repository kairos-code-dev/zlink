// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist');
const { createMetricCollector, decodeMetricHeader, summarizeMetrics } = require('../common/perf_metrics');
function parseArgs(argv) {
    const options = { endpoint: '', msgSize: 256, warmup: 1, duration: 2, clients: 1 };
    for (let i = 0; i < argv.length; i += 1) {
        if (argv[i] === '--endpoint') {
            options.endpoint = argv[++i];
        }
        else if (argv[i] === '--msg-size') {
            options.msgSize = Number(argv[++i]);
        }
        else if (argv[i] === '--warmup') {
            options.warmup = Number(argv[++i]);
        }
        else if (argv[i] === '--duration') {
            options.duration = Number(argv[++i]);
        }
        else if (argv[i] === '--clients') {
            options.clients = Number(argv[++i]);
        }
    }
    return options;
}
async function main() {
    const options = parseArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    const server = new zlink.DealerSocket(ctx);
    const collector = createMetricCollector({
        runId: 0,
        msgSize: options.msgSize
    });
    let stop = false;
    try {
        server.bind(options.endpoint);
        (async () => {
            while (!stop) {
                const received = server.tryRecv();
                if (!received) {
                    await new Promise((resolve) => setImmediate(resolve));
                    continue;
                }
                const header = decodeMetricHeader(received.parts[0].toBuffer());
                if (!header || header.phase === 1) {
                    continue;
                }
                collector.record(header, process.hrtime.bigint());
            }
        })();
        console.log(`READY,${options.endpoint}`);
        const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        for await (const line of rl) {
            if (line === 'STOP') {
                stop = true;
                break;
            }
        }
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
