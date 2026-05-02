// SPDX-License-Identifier: MPL-2.0
'use strict';
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
const node_net_1 = __importDefault(require("node:net"));
const zlink = require('../../dist/canonical');
const perf_metrics_1 = require("../common/perf_metrics");
const perf_single_common_1 = require("./perf_single_common");
const AUTO_CONNECT_SPOT_MESH = 5;
const SERVICE_NAME = 'perf.spot';
const TOPIC = 'perf.topic';
function trySpotPublish(spot, payload) {
    try {
        return spot.publish(SERVICE_NAME, TOPIC, payload, zlink.SendFlags.DontWait);
    }
    catch (error) {
        if (error instanceof zlink.SubmitError &&
            error.result === zlink.SubmitResult.Backpressured) {
            return false;
        }
        const text = String(error && error.message ? error.message : error);
        if ((error && error.code === 'EAGAIN') ||
            /Resource temporarily unavailable|temporarily unavailable|would block/i.test(text)) {
            return false;
        }
        throw error;
    }
}
function trySpotSubscribe(spot) {
    try {
        return spot.subscribe(zlink.RecvFlags.DontWait);
    }
    catch (error) {
        if (error instanceof zlink.RecvError &&
            (error.result === zlink.RecvResult.NoData || error.internalErrno === 2)) {
            return null;
        }
        throw error;
    }
}
function drainSpot(spot, onMessage) {
    let processed = false;
    while (true) {
        const received = trySpotSubscribe(spot);
        if (!received) {
            return processed;
        }
        try {
            onMessage(received);
            processed = true;
        }
        finally {
            received.close();
        }
    }
}
async function reservePort() {
    const server = node_net_1.default.createServer();
    server.listen(0, '127.0.0.1');
    await new Promise((resolve) => server.once('listening', resolve));
    const { port } = server.address();
    await new Promise((resolve, reject) => server.close((error) => (error ? reject(error) : resolve(undefined))));
    return port;
}
async function sleepMs(delayMs) {
    await new Promise((resolve) => setTimeout(resolve, Math.max(0, delayMs)));
}
async function runSpotBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    (0, perf_single_common_1.applyContextPolicy)(ctx);
    const registry = new zlink.Registry(ctx);
    const publisherDiscovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, SERVICE_NAME);
    const subscriberDiscovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, SERVICE_NAME);
    const publisherNode = new zlink.SpotNode(ctx);
    const subscriberNode = new zlink.SpotNode(ctx);
    let publisher = null;
    let subscriber = null;
    try {
        const registryPub = `tcp://127.0.0.1:${await reservePort()}`;
        const registryRouter = `tcp://127.0.0.1:${await reservePort()}`;
        const publisherEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
        const subscriberEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
        publisher = publisherNode.createSpot();
        subscriber = subscriberNode.createSpot();
        publisherNode.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('z-node-perf-spot-publisher')));
        subscriberNode.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('a-node-perf-spot-subscriber')));
        publisher.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('z-node-perf-spot-publisher-spot')));
        subscriber.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('a-node-perf-spot-subscriber-spot')));
        registry.bind(registryPub, registryRouter);
        registry.setBroadcastInterval(50);
        publisherDiscovery.connectRegistry(registryRouter);
        subscriberDiscovery.connectRegistry(registryRouter);
        (0, perf_single_common_1.applySpotNodeAdmission)(publisherNode, options);
        (0, perf_single_common_1.applySpotNodeAdmission)(subscriberNode, options);
        publisherNode.bind(publisherEndpoint);
        subscriberNode.bind(subscriberEndpoint);
        publisherNode.attachDiscovery(publisherDiscovery);
        subscriberNode.attachDiscovery(subscriberDiscovery);
        publisher.setLinger(Number(process.env.PERF_SINGLE_LINGER_MS ?? 0));
        subscriber.setLinger(Number(process.env.PERF_SINGLE_LINGER_MS ?? 0));
        subscriber.setSubscription(TOPIC);
        const runId = (0, perf_metrics_1.createRunId)(options.runId ?? 1);
        const payload = (0, perf_metrics_1.createPayload)(msgSize);
        let seq = 1n;
        let probeReady = false;
        const activeDeadline = { value: 0 };
        const latencySampleCap = Number(process.env.PERF_SINGLE_LATENCY_SAMPLE_CAP ?? 200000);
        const latenciesNs = [];
        let accepted = 0;
        const collectReadable = (countActive) => {
            return drainSpot(subscriber, (received) => {
                const header = (0, perf_metrics_1.decodeMetricHeaderFromParts)(received.parts);
                if (!header) {
                    return;
                }
                if (!probeReady &&
                    header.phase === 0 &&
                    header.runId === runId &&
                    header.msgSize === msgSize) {
                    probeReady = true;
                    return;
                }
                if (!countActive ||
                    header.phase !== 1 ||
                    header.runId !== runId ||
                    header.msgSize !== msgSize ||
                    Date.now() > activeDeadline.value) {
                    return;
                }
                accepted += 1;
                if (latenciesNs.length < latencySampleCap) {
                    const nowNs = BigInt(Date.now()) * 1000000n;
                    const sentTsNs = BigInt(header.sentTsNs);
                    if (nowNs >= sentTsNs) {
                        latenciesNs.push(Number(nowNs - sentTsNs));
                    }
                }
            });
        };
        const readyDeadline = Date.now() + Number(process.env.PERF_CONNECT_READY_TIMEOUT_MS ?? 5000);
        while (!probeReady && Date.now() < readyDeadline) {
            (0, perf_metrics_1.stampPayload)(payload, {
                phase: 0,
                runId,
                msgSize,
                seq
            });
            if (trySpotPublish(publisher, payload)) {
                seq += 1n;
            }
            collectReadable(false);
            await sleepMs(25);
        }
        if (!probeReady) {
            throw new Error('spot ready probe timed out');
        }
        await (0, perf_single_common_1.waitForPostReadySettle)(Number(process.env.PERF_SINGLE_SPOT_READY_SETTLE_MS ?? 1000));
        activeDeadline.value = Date.now() + options.duration * 1000;
        while (Date.now() < activeDeadline.value) {
            (0, perf_metrics_1.stampPayload)(payload, {
                phase: 1,
                runId,
                msgSize,
                seq
            });
            if (trySpotPublish(publisher, payload)) {
                seq += 1n;
            }
            else {
                await sleepMs(1);
            }
            collectReadable(true);
            await new Promise((resolve) => setImmediate(resolve));
        }
        (0, perf_metrics_1.stampPayload)(payload, {
            phase: 2,
            runId,
            msgSize,
            seq
        });
        trySpotPublish(publisher, payload);
        const idleDeadline = Date.now() + Math.max((0, perf_single_common_1.resolveSingleIdleDrainMs)(options), Number(process.env.PERF_SINGLE_RCVTIMEO_MS ?? 200));
        while (Date.now() < idleDeadline) {
            if (!collectReadable(false)) {
                await sleepMs(1);
            }
        }
        if (accepted <= 0) {
            throw new Error('spot benchmark produced no measured messages');
        }
        return {
            latenciesNs,
            accepted
        };
    }
    finally {
        if (subscriber) {
            subscriber.close();
        }
        if (publisher) {
            publisher.close();
        }
        subscriberNode.close();
        publisherNode.close();
        subscriberDiscovery.close();
        publisherDiscovery.close();
        registry.close();
        ctx.close();
    }
}
module.exports = { runSpotBenchmark };
if (require.main === module) {
    (async () => {
        const options = (0, perf_single_common_1.parseSingleBinaryArgs)(process.argv.slice(2));
        const result = await runSpotBenchmark(options.msgSize, options);
        for (const line of (0, perf_metrics_1.summarizeMetrics)('SPOT', options.transport, options.msgSize, result.latenciesNs, options.duration, options.libName, result.accepted)) {
            console.log(line);
        }
    })().catch((error) => {
        console.error(error);
        process.exitCode = 1;
    });
}
