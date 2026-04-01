// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist');
const { createPayload, stampPayload } = require('../common/perf_metrics');
const TOPIC = 'perf.topic';
const PUB_READY_EVENTS = zlink.ServiceMonitorEvent.SPOT_READY_CHANGED
    | zlink.ServiceMonitorEvent.SPOT_PUB_DELIVERY_READY_CHANGED
    | zlink.ServiceMonitorEvent.SPOT_FIRST_DELIVERY_READY_CHANGED
    | zlink.ServiceMonitorEvent.ERROR;
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
    const node = new zlink.SpotNode(ctx);
    const spot = new zlink.Spot(node);
    const warmupPayload = createPayload(options.msgSize);
    const activePayload = createPayload(options.msgSize);
    const stopPayload = createPayload(options.msgSize);
    let monitor = null;
    try {
        monitor = spot.openMonitor(PUB_READY_EVENTS);
        node.bind(options.endpoint);
        const bindDeadline = Date.now() + 5000;
        while (Date.now() < bindDeadline) {
            const snapshot = monitor.snapshot();
            if ((snapshot.stateFlags & zlink.MonitorState.BOUND_READY) !== 0) {
                break;
            }
            const event = monitor.tryRecv();
            if (event && event.eventType === zlink.ServiceMonitorEvent.ERROR) {
                throw new Error(`spot server bind monitor error: ${JSON.stringify(event)}`);
            }
            await new Promise((resolve) => setImmediate(resolve));
        }
        if ((monitor.snapshot().stateFlags & zlink.MonitorState.BOUND_READY) === 0) {
            throw new Error(`spot server bind ready timeout: ${JSON.stringify({
                snapshot: monitor.snapshot(),
                status: node.statusSnapshot(),
                peers: node.peersSnapshot(),
                subjects: node.subjectsSnapshot()
            })}`);
        }
        console.log(`READY,${options.endpoint}`);
        const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        for await (const line of rl) {
            if (line !== 'GO') {
                continue;
            }
            const deadline = Date.now() + 10000;
            while (Date.now() < deadline) {
                const snapshot = monitor.snapshot();
                if ((snapshot.stateFlags & zlink.MonitorState.SEND_READY) !== 0
                    && snapshot.readyCount >= options.clients) {
                    break;
                }
                const event = monitor.tryRecv();
                if (!event) {
                    await new Promise((resolve) => setImmediate(resolve));
                    continue;
                }
                if (event.eventType === zlink.ServiceMonitorEvent.ERROR) {
                    throw new Error(`spot server ready monitor error: ${JSON.stringify(event)}`);
                }
            }
            const snapshot = monitor.snapshot();
            if ((snapshot.stateFlags & zlink.MonitorState.SEND_READY) === 0
                || snapshot.readyCount < options.clients) {
                throw new Error(`spot server ready timeout: ${JSON.stringify({
                    snapshot,
                    expectedReadyCount: options.clients,
                    status: node.statusSnapshot(),
                    peers: node.peersSnapshot(),
                    subjects: node.subjectsSnapshot(),
                })}`);
            }
            const warmupUntil = process.hrtime.bigint() + BigInt(Math.floor(options.warmup * 1_000_000_000));
            while (process.hrtime.bigint() < warmupUntil) {
                stampPayload(warmupPayload, { phase: 2, runId: 0, msgSize: options.msgSize });
                spot.publish(TOPIC, warmupPayload);
            }
            const activeUntil = process.hrtime.bigint() + BigInt(Math.floor(options.duration * 1_000_000_000));
            while (process.hrtime.bigint() < activeUntil) {
                stampPayload(activePayload, { phase: 0, runId: 0, msgSize: options.msgSize });
                spot.publish(TOPIC, activePayload);
            }
            for (let i = 0; i < options.clients; i += 1) {
                stampPayload(stopPayload, { phase: 1, runId: 0, msgSize: options.msgSize });
                spot.publish(TOPIC, stopPayload);
            }
            break;
        }
    }
    finally {
        if (monitor) {
            monitor.close();
        }
        spot.close();
        node.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
