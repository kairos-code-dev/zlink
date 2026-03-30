// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist');
const { latencyUsFromPayload, payloadPhase, summarizeMetrics } = require('../common/perf_metrics');
function parseArgs(argv) {
    const options = { endpoint: '', msgSize: 256, duration: 2, clients: 1 };
    for (let i = 0; i < argv.length; i += 1) {
        if (argv[i] === '--endpoint') {
            options.endpoint = argv[++i];
        }
        else if (argv[i] === '--msg-size') {
            options.msgSize = Number(argv[++i]);
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
    const subs = [];
    const latenciesUs = [];
    let stopCount = 0;
    try {
        for (let i = 0; i < options.clients; i += 1) {
            const sub = new zlink.SubSocket(ctx);
            const monitor = sub.monitorOpen(zlink.MonitorEvent.SUB_DELIVERY_READY_CHANGED);
            sub.setSubscription('perf.topic');
            sub.connect(options.endpoint);
            while (true) {
                const event = monitor.recv();
                if (event.event === zlink.MonitorEvent.SUB_DELIVERY_READY_CHANGED && event.value >= 1) {
                    break;
                }
            }
            monitor.close();
            sub.subscribeHandler((_, __, parts) => {
                const payload = parts[0].toBuffer();
                const phase = payloadPhase(payload);
                if (phase === 0) {
                    latenciesUs.push(latencyUsFromPayload(payload));
                }
                else if (phase === 1) {
                    stopCount += 1;
                }
            });
            subs.push(sub);
        }
        console.log('CLIENT_READY');
        while (stopCount < 1) {
            await new Promise((resolve) => setImmediate(resolve));
        }
        const resultLines = summarizeMetrics('MULTI_PUBSUB', 'tcp', options.msgSize, latenciesUs, options.duration);
        for (const line of resultLines) {
            console.log(line);
        }
    }
    finally {
        for (const sub of subs) {
            sub.close();
        }
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
