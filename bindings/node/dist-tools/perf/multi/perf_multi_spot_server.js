// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const { createPayload, createRunId, sleepImmediate, stampPayload } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { POLLIN, POLLOUT, applyContextPolicy, applySocketPolicy, createSocketEventWaiter, subscribeNoWait, trySocketPublish, waitForConnectionReady } = require('./perf_multi_runtime');
const TOPIC = 'perf.topic';
const CONTROL_TOPIC = 'perf.control';
const SERVICE_NAME = 'perf.spot';
function trySpotPublish(spot, serviceName, topic, payload) {
    return trySocketPublish({
        publish(currentTopic, currentPayload, flags) {
            return spot.publish(serviceName, currentTopic, currentPayload, flags);
        }
    }, topic, payload);
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'server', 'MULTI_SPOT');
    const node = new zlink.SpotNode(ctx);
    const dealer = new zlink.DealerSocket(ctx);
    const controlPub = new zlink.PubSocket(ctx);
    const controlSub = new zlink.SubSocket(ctx);
    const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
    const controlSubWaiter = createSocketEventWaiter(controlSub, POLLIN);
    let spot = null;
    let spotWaiter = null;
    const payload = createPayload(options.msgSize);
    let readyCount = 0;
    let connected = false;
    let startRequested = false;
    let stopRequested = false;
    let connectedControlEndpoint = '';
    let rl = null;
    try {
        node.attachChannelDealerManual(SERVICE_NAME, dealer);
        applySocketPolicy(dealer);
        node.bind(options.peerEndpoint);
        spot = node.createSpot();
        applySocketPolicy(spot, {
            noDrop: Number(process.env.PERF_MULTI_SPOT_XPUB_NODROP ?? 1) !== 0
        });
        spotWaiter = createSocketEventWaiter(spot, POLLOUT);
        applySocketPolicy(controlPub);
        applySocketPolicy(controlSub);
        controlPub.bind(options.controlEndpoint);
        controlSub.setSubscription(CONTROL_TOPIC);
        console.log(`READY,${options.endpoint}`);
        console.log(`CONTROL_READY,${options.controlEndpoint}`);
        rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        (async () => {
            for await (const line of rl) {
                if (line === `START,${options.msgSize}`) {
                    startRequested = true;
                }
                else if (line.startsWith('CONNECT_CONTROL,')) {
                    const clientEndpoint = line.slice('CONNECT_CONTROL,'.length).trim();
                    if (!clientEndpoint || clientEndpoint === connectedControlEndpoint) {
                        continue;
                    }
                    await waitForConnectionReady(controlSub, () => controlSub.connect(clientEndpoint));
                    connectedControlEndpoint = clientEndpoint;
                }
                else if (line === 'STOP' || line === 'QUIT') {
                    stopRequested = true;
                    break;
                }
            }
        })();
        while (!stopRequested && !(connected && readyCount >= options.clients && startRequested)) {
            let drained = false;
            while (true) {
                const received = subscribeNoWait(controlSub);
                if (!received) {
                    break;
                }
                drained = true;
                const payloadText = received.parts[0].data().toString('utf8');
                if (payloadText === 'CONNECTED') {
                    connected = true;
                    continue;
                }
                if (payloadText.startsWith(`READY_COUNT,${options.msgSize},`)) {
                    readyCount = Number(payloadText.split(',')[2]);
                }
            }
            if (!(connected && readyCount >= options.clients && startRequested) && !drained) {
                await controlSubWaiter.wait(POLLIN);
            }
        }
        if (stopRequested) {
            return;
        }
        for (;;) {
            if (trySocketPublish(controlPub, CONTROL_TOPIC, Buffer.from(`START,${options.msgSize}`))) {
                break;
            }
            await controlPubWaiter.wait(POLLOUT);
        }
        const runId = createRunId(1);
        const activeStopNs = process.hrtime.bigint() + BigInt(Math.floor(options.duration * 1_000_000_000));
        let seq = 1n;
        while (process.hrtime.bigint() < activeStopNs) {
            stampPayload(payload, { phase: 1, runId, msgSize: options.msgSize, seq });
            if (trySpotPublish(spot, SERVICE_NAME, TOPIC, payload)) {
                seq += 1n;
                continue;
            }
            await spotWaiter.wait(POLLOUT);
        }
        stampPayload(payload, { phase: 2, runId, msgSize: options.msgSize, seq });
        for (;;) {
            if (trySpotPublish(spot, SERVICE_NAME, TOPIC, payload)) {
                break;
            }
            await spotWaiter.wait(POLLOUT);
        }
    }
    finally {
        rl?.close();
        if (spotWaiter) {
            spotWaiter.close();
        }
        controlSubWaiter.close();
        controlPubWaiter.close();
        if (spot) {
            spot.close();
        }
        controlPub.close();
        controlSub.close();
        dealer.close();
        node.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
