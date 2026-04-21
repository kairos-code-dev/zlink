// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeaderFromParts, currentEpochNs, sleepImmediate, summarizeMetrics, stampPayload } = require('../common/perf_metrics');
const { parseMultiArgs, resolveMultiSpotControlSettleMs, resolveMultiSpotReadySettleMs } = require('./perf_multi_common');
const { POLLIN, POLLOUT, applySocketPolicy, applyContextPolicy, createSocketEventWaiter, recvNoWait, resolveMultiLatencySampleCap, subscribeNoWait, trySendToSpot, trySocketPublish, waitForConnectionReady } = require('./perf_multi_runtime');
const CONTROL_TOPIC = 'perf.control';
const SERVER_NODE_ROUTING_ID = zlink.RoutingId.fromBytes(Buffer.from('PERF_SPOT_REQREP_NODE', 'ascii'));
const SERVER_SPOT_ROUTING_ID = zlink.RoutingId.fromBytes(Buffer.from('PERF_SPOT_REQREP_SPOT', 'ascii'));
function closeReceived(received) {
    if (received && typeof received.close === 'function') {
        received.close();
    }
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'client', 'MULTI_SPOT_REQREP');
    const controlPub = new zlink.PubSocket(ctx);
    const controlSub = new zlink.SubSocket(ctx);
    const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
    const controlSubWaiter = createSocketEventWaiter(controlSub, POLLIN);
    const routers = [];
    const payloads = [];
    const waiting = [];
    const sendPending = [];
    const poller = new zlink.Poller();
    let rl = null;
    try {
        applySocketPolicy(controlPub);
        applySocketPolicy(controlSub);
        controlPub.bind(options.controlEndpoint);
        console.log(`CLIENT_CONTROL_ENDPOINT,${options.controlEndpoint}`);
        controlSub.setSubscription(CONTROL_TOPIC);
        await waitForConnectionReady(controlSub, () => controlSub.connect(options.serverControlEndpoint));
        console.log(`CONTROL_CONNECTED,${options.serverControlEndpoint}`);
        for (let i = 0; i < options.clients; i += 1) {
            const router = new zlink.RouterSocket(ctx);
            applySocketPolicy(router);
            routers.push(router);
            payloads.push(createPayload(options.msgSize));
            waiting.push(false);
            sendPending.push(false);
        }
        for (let i = 0; i < routers.length; i += 1) {
            await waitForConnectionReady(routers[i], () => routers[i].connect(options.peerEndpoint));
            poller.addSocket(routers[i], POLLIN | POLLOUT, i);
        }
        const stabilizationDeadline = Date.now() + resolveMultiSpotReadySettleMs();
        while (Date.now() < stabilizationDeadline) {
            await sleepImmediate();
        }
        for (;;) {
            if (trySocketPublish(controlPub, CONTROL_TOPIC, Buffer.from('CONNECTED'))) {
                break;
            }
            await controlPubWaiter.wait(POLLOUT);
        }
        const controlSettleDeadline = Date.now() + resolveMultiSpotControlSettleMs();
        while (Date.now() < controlSettleDeadline) {
            await sleepImmediate();
        }
        for (;;) {
            if (trySocketPublish(controlPub, CONTROL_TOPIC, Buffer.from(`READY_COUNT,${options.msgSize},${routers.length}`))) {
                break;
            }
            await controlPubWaiter.wait(POLLOUT);
        }
        console.log(`CLIENT_READY,${options.msgSize}`);
        rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        let startRequested = false;
        let startBroadcast = false;
        (async () => {
            for await (const line of rl) {
                if (line === `START,${options.msgSize}`) {
                    startRequested = true;
                }
            }
        })();
        while (!(startRequested && startBroadcast)) {
            let drained = false;
            while (true) {
                const received = subscribeNoWait(controlSub);
                if (!received) {
                    break;
                }
                drained = true;
                const payloadText = received.parts[0].data().toString('utf8');
                if (payloadText === `START,${options.msgSize}`) {
                    startBroadcast = true;
                }
            }
            if (!(startRequested && startBroadcast) && !drained) {
                await controlSubWaiter.wait(POLLIN);
            }
        }
        const runId = createRunId(1);
        const activeStartNs = currentEpochNs();
        const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
        const collector = createMetricCollector({
            runId,
            msgSize: options.msgSize,
            activeStartNs,
            activeStopNs,
            roundTrip: true,
            sampleCap: resolveMultiLatencySampleCap()
        });
        let seq = 1n;
        const drainReply = (index) => {
            let progressed = false;
            while (true) {
                const received = recvNoWait(routers[index]);
                if (!received) {
                    break;
                }
                try {
                    waiting[index] = false;
                    collector.record(decodeMetricHeaderFromParts(received.parts), currentEpochNs());
                    progressed = true;
                }
                finally {
                    closeReceived(received);
                }
            }
            return progressed;
        };
        while (currentEpochNs() < activeStopNs) {
            let progressed = false;
            for (let i = 0; i < routers.length; i += 1) {
                if (waiting[i] || sendPending[i]) {
                    continue;
                }
                stampPayload(payloads[i], { phase: 1, runId, msgSize: options.msgSize, seq });
                if (!trySendToSpot(routers[i], SERVER_NODE_ROUTING_ID, SERVER_SPOT_ROUTING_ID, payloads[i])) {
                    sendPending[i] = true;
                    continue;
                }
                waiting[i] = true;
                seq += 1n;
                progressed = true;
            }
            for (let i = 0; i < routers.length; i += 1) {
                progressed = drainReply(i) || progressed;
            }
            if (progressed) {
                continue;
            }
            const ready = poller.waitAll(poller.size, 25);
            if (ready.length === 0) {
                await sleepImmediate();
                continue;
            }
            for (const event of ready) {
                const index = event.userData;
                if (!Number.isInteger(index)) {
                    continue;
                }
                if ((event.events & POLLOUT) !== 0) {
                    sendPending[index] = false;
                }
                if ((event.events & POLLIN) !== 0) {
                    drainReply(index);
                }
            }
        }
        const result = await collector.finish();
        for (const metricLine of summarizeMetrics('MULTI_SPOT_REQREP', options.transport, options.msgSize, result.latenciesNs, options.duration, 'current', result.accepted)) {
            console.log(metricLine);
        }
    }
    finally {
        rl?.close();
        poller.close();
        controlSubWaiter.close();
        controlPubWaiter.close();
        controlSub.close();
        controlPub.close();
        for (const router of routers) {
            router.close();
        }
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
