// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const { sleepImmediate } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { POLLIN, POLLOUT, applyContextPolicy, applySocketPolicy, createSocketEventWaiter, subscribeNoWait, trySocketPublish, waitForConnectionReady } = require('./perf_multi_runtime');
const CONTROL_TOPIC = 'perf.control';
const SERVICE_NAME = 'perf.spot';
const SERVER_NODE_ROUTING_ID = zlink.RoutingId.fromBytes(Buffer.from('PERF_SPOT_REQREP_NODE', 'ascii'));
const SERVER_SPOT_ROUTING_ID = zlink.RoutingId.fromBytes(Buffer.from('PERF_SPOT_REQREP_SPOT', 'ascii'));
function tryRecvRouted(spot) {
    try {
        return spot.recvRouted(zlink.RecvFlags.DontWait);
    }
    catch (error) {
        if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
            return null;
        }
        throw error;
    }
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'server', 'MULTI_SPOT_REQREP');
    const node = new zlink.SpotNode(ctx);
    const dealer = new zlink.DealerSocket(ctx);
    const controlPub = new zlink.PubSocket(ctx);
    const controlSub = new zlink.SubSocket(ctx);
    const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
    const controlSubWaiter = createSocketEventWaiter(controlSub, POLLIN);
    let spot = null;
    let readyCount = 0;
    let connected = false;
    let startRequested = false;
    let stop = false;
    let connectedControlEndpoint = '';
    let rl = null;
    try {
        node.attachChannelDealerManual(SERVICE_NAME, dealer);
        applySocketPolicy(dealer);
        node.setRoutingId(SERVER_NODE_ROUTING_ID);
        node.bind(options.peerEndpoint);
        spot = node.createSpot();
        applySocketPolicy(spot);
        spot.setRoutingId(SERVER_SPOT_ROUTING_ID);
        spot.onDispatchEvent(() => {
            while (true) {
                const received = tryRecvRouted(spot);
                if (!received) {
                    return;
                }
                try {
                    received.reply(received.parts);
                }
                finally {
                    received.close();
                }
            }
        });
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
                    stop = true;
                }
            }
        })();
        while (!stop && !(connected && readyCount >= options.clients && startRequested)) {
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
        if (stop) {
            return;
        }
        for (;;) {
            if (trySocketPublish(controlPub, CONTROL_TOPIC, Buffer.from(`START,${options.msgSize}`))) {
                break;
            }
            await controlPubWaiter.wait(POLLOUT);
        }
        while (!stop) {
            await sleepImmediate();
        }
    }
    finally {
        rl?.close();
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
