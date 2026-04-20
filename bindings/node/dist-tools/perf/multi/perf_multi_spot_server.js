// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const { createPayload, createRunId, sleepImmediate, stampPayload } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { applySocketPolicy, subscribeNoWait, trySocketPublish, waitForConnectionReady } = require('./perf_multi_runtime');
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
    const node = new zlink.SpotNode(ctx);
    const dealer = new zlink.DealerSocket(ctx);
    const controlPub = new zlink.PubSocket(ctx);
    const controlSub = new zlink.SubSocket(ctx);
    let spot = null;
    const payload = createPayload(options.msgSize);
    let readyCount = 0;
    let connected = false;
    let startRequested = false;
    let connectedControlEndpoint = '';
    try {
        node.attachChannelDealerManual(SERVICE_NAME, dealer);
        applySocketPolicy(dealer);
        node.bind(options.peerEndpoint);
        spot = node.createSpot();
        applySocketPolicy(spot, {
            noDrop: Number(process.env.PERF_MULTI_SPOT_XPUB_NODROP ?? 1) !== 0
        });
        applySocketPolicy(controlPub);
        applySocketPolicy(controlSub);
        controlPub.bind(options.controlEndpoint);
        controlSub.setSubscription(CONTROL_TOPIC);
        console.log(`READY,${options.endpoint}`);
        console.log(`CONTROL_READY,${options.controlEndpoint}`);
        const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
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
            }
        })();
        while (!(connected && readyCount >= options.clients && startRequested)) {
            while (true) {
                const received = subscribeNoWait(controlSub);
                if (!received) {
                    break;
                }
                const payloadText = received.parts[0].data().toString('utf8');
                if (payloadText === 'CONNECTED') {
                    connected = true;
                    continue;
                }
                if (payloadText.startsWith(`READY_COUNT,${options.msgSize},`)) {
                    readyCount = Number(payloadText.split(',')[2]);
                }
            }
            await sleepImmediate();
        }
        while (!trySocketPublish(controlPub, CONTROL_TOPIC, Buffer.from(`START,${options.msgSize}`))) {
            await sleepImmediate();
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
            await sleepImmediate();
        }
    }
    finally {
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
