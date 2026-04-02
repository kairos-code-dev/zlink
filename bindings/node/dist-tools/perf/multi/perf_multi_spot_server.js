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
function ensureMeshPubBudgetDefault() {
    if (!process.env.ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM) {
        process.env.ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM = '100';
    }
}
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
    ensureMeshPubBudgetDefault();
    const options = parseArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    const node = new zlink.SpotNode(ctx);
    const spot = new zlink.Spot(node);
    const warmupPayload = createPayload(options.msgSize);
    const activePayload = createPayload(options.msgSize);
    const stopPayload = createPayload(options.msgSize);
    let monitor = null;
    let readyCount = 0;
    let peersReady = false;
    const publishUntil = async (payload, phase, deadlineNs) => {
        while (process.hrtime.bigint() < deadlineNs) {
            stampPayload(payload, { phase, runId: 0, msgSize: options.msgSize });
            const result = spot.tryPublish(TOPIC, payload);
            await new Promise((resolve) => setImmediate(resolve));
        }
    };
    const publishStopFrames = async () => {
        let sent = 0;
        const deadline = Date.now() + 5000;
        while (sent < options.clients && Date.now() < deadline) {
            stampPayload(stopPayload, { phase: 1, runId: 0, msgSize: options.msgSize });
            const result = spot.tryPublish(TOPIC, stopPayload);
            if (result === zlink.SendResult.Sent) {
                sent += 1;
                continue;
            }
            await new Promise((resolve) => setImmediate(resolve));
        }
        if (sent < options.clients) {
            throw new Error(`spot server stop publish timeout: sent=${sent} expected=${options.clients}`);
        }
    };
    try {
        node.bind(options.endpoint);
        monitor = spot.monitorOpen(PUB_READY_EVENTS);
        spot.setLinger(0);
        spot.setSendHighWaterMark(100);
        spot.setSendTimeout(200);
        spot.setNoDrop(true);
        console.log(`READY,${options.endpoint}`);
        const waitForPeersReady = (async () => {
            const deadline = Date.now() + 10000;
            while (Date.now() < deadline) {
                const event = monitor.tryRecv();
                if (!event) {
                    await new Promise((resolve) => setImmediate(resolve));
                    continue;
                }
                if (event.eventType === zlink.ServiceMonitorEvent.ERROR) {
                    throw new Error(`spot server ready monitor error: ${JSON.stringify(event)}`);
                }
                if ((event.eventType === zlink.ServiceMonitorEvent.SPOT_PUB_DELIVERY_READY_CHANGED
                    || event.eventType === zlink.ServiceMonitorEvent.SPOT_FIRST_DELIVERY_READY_CHANGED)
                    && event.value > readyCount) {
                    readyCount = event.value;
                }
                if (readyCount >= options.clients) {
                    peersReady = true;
                    console.log(`PEERS_READY,${options.msgSize}`);
                    return;
                }
            }
            throw new Error(`spot server ready timeout: ${JSON.stringify({
                readyCount,
                snapshot: monitor.snapshot(),
                expectedReadyCount: options.clients,
                status: node.statusSnapshot(),
                peers: node.peersSnapshot(),
                subjects: node.subjectsSnapshot(),
            })}`);
        })();
        const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        for await (const line of rl) {
            if (line !== `START,${options.msgSize}` && line !== 'GO') {
                continue;
            }
            if (!peersReady) {
                await waitForPeersReady;
            }
            await publishUntil(warmupPayload, 2, process.hrtime.bigint() + BigInt(Math.floor(options.warmup * 1_000_000_000)));
            await publishUntil(activePayload, 0, process.hrtime.bigint() + BigInt(Math.floor(options.duration * 1_000_000_000)));
            await publishStopFrames();
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
