// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist');
const { createPayload, setPayloadPhase, stampPayload } = require('../common/perf_metrics');
function parseArgs(argv) {
    const options = { endpoint: '', msgSize: 256, warmup: 1, duration: 2 };
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
    }
    return options;
}
async function main() {
    const options = parseArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    const pub = new zlink.PubSocket(ctx);
    const payload = createPayload(options.msgSize);
    const monitor = pub.monitorOpen(zlink.MonitorEvent.PUB_DELIVERY_READY_CHANGED);
    try {
        pub.bind(options.endpoint);
        console.log(`READY,${options.endpoint}`);
        const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        for await (const line of rl) {
            if (line !== 'GO') {
                continue;
            }
            while (true) {
                const event = monitor.recv();
                if (event.event === zlink.MonitorEvent.PUB_DELIVERY_READY_CHANGED && event.value >= 1) {
                    break;
                }
            }
            const sendLoop = async (seconds, phase) => {
                const until = process.hrtime.bigint() + BigInt(Math.floor(seconds * 1_000_000_000));
                while (process.hrtime.bigint() < until) {
                    stampPayload(payload);
                    setPayloadPhase(payload, phase);
                    pub.publish('perf.topic', payload);
                    await new Promise((resolve) => setImmediate(resolve));
                }
            };
            await sendLoop(options.warmup, 2);
            await sendLoop(options.duration, 0);
            stampPayload(payload);
            setPayloadPhase(payload, 1);
            pub.publish('perf.topic', payload);
            break;
        }
    }
    finally {
        monitor.close();
        pub.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
