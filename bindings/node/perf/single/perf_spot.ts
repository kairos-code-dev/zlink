// SPDX-License-Identifier: MPL-2.0
'use strict';

import net from 'node:net';
const zlink = require('@zlink-systems/zlink');
import {
  createPayload,
  createRunId,
  decodeMetricHeaderFromParts,
  sleepImmediate,
  summarizeMetrics,
  stampPayload
} from '../common/perf_metrics';
import {
  applyContextPolicy,
  applySpotNodeAdmission,
  parseSingleBinaryArgs,
  waitForPostReadySettle
} from './perf_single_common';
import {
  STOP_TOKEN_BYTES,
  isStopTokenParts
} from '../perf_stop_token';

const AUTO_CONNECT_SPOT_MESH = 5;
const CHANNEL_NAME = 'perf.spot';
const TOPIC = 'perf.topic';

function trySpotPublish(spot: any, payload: Buffer): boolean {
  try {
    return spot.publish(TOPIC)
      .message(payload)
      .flags(zlink.SendFlags.DontWait)
      .submit();
  } catch (error: any) {
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

async function publishStopToken(spot: any) {
  // PERF_SINGLE_TEST_POLICY § 1.4: emit the wire-level stop token. Spot
  // does not expose a POLLOUT poller wait, so we yield via setImmediate
  // through any transient backpressure. Bounded retry to avoid hangs.
  for (let retry = 0; retry < 100; retry += 1) {
    if (trySpotPublish(spot, STOP_TOKEN_BYTES)) {
      return;
    }
    await sleepImmediate();
  }
}

function trySpotSubscribe(spot: any) {
  try {
    return spot.subscribe(zlink.RecvFlags.DontWait);
  } catch (error: any) {
    if (error instanceof zlink.RecvError &&
        (error.result === zlink.RecvResult.NoData || error.internalErrno === 2)) {
      return null;
    }
    throw error;
  }
}

function drainSpot(spot: any, onMessage: (received: any) => void): boolean {
  let processed = false;
  while (true) {
    const received = trySpotSubscribe(spot);
    if (!received) {
      return processed;
    }
    try {
      onMessage(received);
      processed = true;
    } finally {
      received.close();
    }
  }
}

async function reservePort(): Promise<number> {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await new Promise((resolve) => server.once('listening', resolve));
  const { port } = server.address() as net.AddressInfo;
  await new Promise((resolve, reject) =>
    server.close((error) => (error ? reject(error) : resolve(undefined))));
  return port;
}


async function runSpotBenchmark(msgSize: number, options: any) {
  const ctx = new zlink.Context();
  applyContextPolicy(ctx);
  const registry = new zlink.Registry(ctx);
  const publisherDiscovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, CHANNEL_NAME);
  const subscriberDiscovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, CHANNEL_NAME);
  const publisherNode = new zlink.SpotNode(ctx);
  const subscriberNode = new zlink.SpotNode(ctx);
  let publisher: any = null;
  let subscriber: any = null;

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
    applySpotNodeAdmission(publisherNode, options);
    applySpotNodeAdmission(subscriberNode, options);
    publisherNode.bind(publisherEndpoint);
    subscriberNode.bind(subscriberEndpoint);
    publisherNode.attachDiscovery(publisherDiscovery);
    subscriberNode.attachDiscovery(subscriberDiscovery);
    subscriber.setSubscription(TOPIC);

    const runId = createRunId(options.runId ?? 1);
    const payload = createPayload(msgSize);
    let seq = 1n;
    let probeReady = false;
    let stopReceived = false;
    const activeDeadline = { value: 0 };
    const latencySampleCap = Number(process.env.PERF_SINGLE_LATENCY_SAMPLE_CAP ?? 200000);
    const latenciesNs: number[] = [];
    let accepted = 0;
    const collectReadable = (countActive: boolean) => {
      return drainSpot(subscriber, (received) => {
        // PERF_SINGLE_TEST_POLICY § 1.4: wire-level stop token terminates
        // the receiver loop. Returning here lets the outer loop observe
        // `stopReceived` without recording the sentinel as a payload.
        if (isStopTokenParts(received.parts)) {
          stopReceived = true;
          return;
        }
        const header = decodeMetricHeaderFromParts(received.parts);
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
      stampPayload(payload, {
        phase: 0,
        runId,
        msgSize,
        seq
      });
      if (trySpotPublish(publisher, payload)) {
        seq += 1n;
      }
      collectReadable(false);
      await sleepImmediate();
    }
    if (!probeReady) {
      throw new Error('spot ready probe timed out');
    }

    await waitForPostReadySettle(Number(process.env.PERF_SINGLE_SPOT_READY_SETTLE_MS ?? 1000));

    activeDeadline.value = Date.now() + options.duration * 1000;
    while (Date.now() < activeDeadline.value) {
      stampPayload(payload, {
        phase: 1,
        runId,
        msgSize,
        seq
      });
      if (trySpotPublish(publisher, payload)) {
        seq += 1n;
      }
      collectReadable(true);
      await sleepImmediate();
    }

    // PERF_SINGLE_TEST_POLICY § 1.4: signal phase end via wire stop token
    // and drain in-flight payloads until the receiver observes it. The
    // legacy phase-2 cooldown message + timer-based idle drain are no
    // longer needed — in-flight messages naturally precede the token.
    await publishStopToken(publisher);
    while (!stopReceived) {
      if (!collectReadable(false)) {
        await sleepImmediate();
      }
    }

    if (accepted <= 0) {
      throw new Error('spot benchmark produced no measured messages');
    }

    return {
      latenciesNs,
      accepted
    };
  } finally {
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
    const options = parseSingleBinaryArgs(process.argv.slice(2));
    const result = await runSpotBenchmark(options.msgSize, options);
    for (const line of summarizeMetrics(
      'SPOT',
      options.transport,
      options.msgSize,
      result.latenciesNs,
      options.duration,
      options.libName,
      result.accepted
    )) {
      console.log(line);
    }
  })().catch((error) => {
    console.error(error);
    process.exitCode = 1;
  });
}
