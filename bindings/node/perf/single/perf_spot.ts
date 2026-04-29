// SPDX-License-Identifier: MPL-2.0
'use strict';

import net from 'node:net';
const zlink = require('../../dist/canonical');
import {
  createPayload,
  createRunId,
  decodeMetricHeaderFromParts,
  summarizeMetrics,
  stampPayload
} from '../common/perf_metrics';
import {
  applyContextPolicy,
  applySocketPolicy,
  parseSingleBinaryArgs,
  resolveSingleIdleDrainMs,
  waitForPostReadySettle
} from './perf_single_common';

const SERVICE_TYPE_SPOT = 0x3002;
const SERVICE_NAME = 'perf.spot';
const TOPIC = 'perf.topic';

function trySpotPublish(spot: any, payload: Buffer): boolean {
  try {
    return spot.publish(SERVICE_NAME, TOPIC, payload, zlink.SendFlags.DontWait);
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

async function sleepMs(delayMs: number): Promise<void> {
  await new Promise((resolve) => setTimeout(resolve, Math.max(0, delayMs)));
}

async function runSpotBenchmark(msgSize: number, options: any) {
  const ctx = new zlink.Context();
  applyContextPolicy(ctx);
  const registry = new zlink.Registry(ctx);
  const discovery = new zlink.Discovery(ctx, SERVICE_TYPE_SPOT, SERVICE_NAME);
  const publisherNode = new zlink.SpotNode(ctx);
  const subscriberNode = new zlink.SpotNode(ctx);
  let publisher: any = null;
  let subscriber: any = null;

  try {
    const registryPub = `tcp://127.0.0.1:${await reservePort()}`;
    const registryRouter = `tcp://127.0.0.1:${await reservePort()}`;
    const publisherEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
    const subscriberEndpoint = `tcp://127.0.0.1:${await reservePort()}`;

    registry.bind(registryPub, registryRouter);
    discovery.connectRegistry(registryRouter);
    publisherNode.attachDiscovery(discovery);
    subscriberNode.attachDiscovery(discovery);
    publisherNode.bind(publisherEndpoint);
    subscriberNode.bind(subscriberEndpoint);

    publisher = publisherNode.createSpot();
    subscriber = subscriberNode.createSpot();
    applySocketPolicy(publisher, options);
    applySocketPolicy(subscriber, options);
    subscriber.setSubscription(TOPIC);

    const runId = createRunId(options.runId ?? 1);
    const payload = createPayload(msgSize);
    let seq = 1n;
    let probeReady = false;
    const activeDeadline = { value: 0 };
    const latencySampleCap = Number(process.env.PERF_SINGLE_LATENCY_SAMPLE_CAP ?? 200000);
    const latenciesNs: number[] = [];
    let accepted = 0;
    const collectReadable = (countActive: boolean) => {
      return drainSpot(subscriber, (received) => {
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
      await sleepMs(25);
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
      } else {
        await sleepMs(1);
      }
      collectReadable(true);
      await new Promise((resolve) => setImmediate(resolve));
    }

    stampPayload(payload, {
      phase: 2,
      runId,
      msgSize,
      seq
    });
    trySpotPublish(publisher, payload);

    const idleDeadline = Date.now() + Math.max(
      resolveSingleIdleDrainMs(options),
      Number(process.env.PERF_SINGLE_RCVTIMEO_MS ?? 200)
    );
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
  } finally {
    if (subscriber) {
      subscriber.close();
    }
    if (publisher) {
      publisher.close();
    }
    subscriberNode.close();
    publisherNode.close();
    discovery.close();
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
