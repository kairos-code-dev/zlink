// SPDX-License-Identifier: MPL-2.0

'use strict';

const net = require('node:net');
const { once } = require('node:events');
const zlink = require('../../dist/canonical');
const {
  createMetricCollector,
  createPayload,
  createRunId,
  decodeMetricHeader,
  currentEpochNs,
  sleepImmediate,
  stampPayload
} = require('../common/perf_metrics');

const TOPIC = 'perf.topic';
const SERVICE_TYPE_SPOT = 0x3002;
const SERVICE_NAME = 'perf.spot';
const DEBUG = process.env.PERF_DEBUG === '1';
const POLLIN = 1;

async function reserveTcpEndpoint() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const address = server.address();
  await new Promise((resolve, reject) => {
    server.close((error) => {
      if (error) {
        reject(error);
        return;
      }
      resolve();
    });
  });
  return `tcp://127.0.0.1:${address.port}`;
}

function trySpotSubscribe(spot) {
  try {
    return spot.subscribe(zlink.RecvFlags.DontWait);
  } catch (error) {
    if (
      error instanceof zlink.RecvError
      && error.result === zlink.RecvResult.NoData
    ) {
      return null;
    }
    throw error;
  }
}

function trySpotPublish(spot, serviceName, topic, payload) {
  try {
    spot.publish(serviceName, topic, payload, zlink.SendFlags.DontWait);
    return true;
  } catch (error) {
    if (
      error instanceof zlink.SubmitError
      && error.result === zlink.SubmitResult.Backpressured
    ) {
      return false;
    }
    throw error;
  }
}

async function runSpotBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const discovery = new zlink.Discovery(ctx, SERVICE_TYPE_SPOT, SERVICE_NAME);
  let spot = null;
  const poller = new zlink.Poller();

  try {
    node.attachDiscovery(discovery);
    spot = node.createSpot();
    if (DEBUG) {
      console.error('spot setup start');
    }
    spot.setSubscription(TOPIC);
    if (DEBUG) {
      console.error('spot subscription set');
    }
    poller.addSocket(spot, POLLIN);
    await new Promise((resolve) => setTimeout(resolve, 100));

    const startedAtNs = process.hrtime.bigint();
    const runId = createRunId();
    const collector = createMetricCollector({ runId, msgSize });
    const payload = createPayload(msgSize);
    let seq = 1n;
    let ready = false;
    const readyDeadline = Date.now() + 10_000;
    while (!ready && Date.now() < readyDeadline) {
      stampPayload(payload, {
        phase: 0,
        runId,
        msgSize,
        seq
      });
      if (trySpotPublish(spot, SERVICE_NAME, TOPIC, payload)) {
        seq += 1n;
      }
      while (true) {
        const received = trySpotSubscribe(spot);
        if (!received) {
          break;
        }
        const firstPart = received.parts[0];
        if (!firstPart) {
          continue;
        }
        const header = decodeMetricHeader(firstPart.data());
        if (header) {
          ready = true;
          collector.record(header, currentEpochNs());
        }
      }
      await sleepImmediate();
    }
    if (DEBUG) {
      console.error(`spot ready=${ready} seq=${seq.toString()}`);
    }

    const warmupUntilNs =
      startedAtNs + BigInt(Math.floor(options.warmup * 1_000_000_000));
    const stopAtNs =
      startedAtNs
      + BigInt(Math.floor((options.warmup + options.duration) * 1_000_000_000));

    while (process.hrtime.bigint() < stopAtNs) {
      for (let i = 0; i < 256 && process.hrtime.bigint() < stopAtNs; i += 1) {
        stampPayload(payload, {
          phase: process.hrtime.bigint() < warmupUntilNs ? 0 : 1,
          runId,
          msgSize,
          seq
        });
        if (!trySpotPublish(spot, SERVICE_NAME, TOPIC, payload)) {
          break;
        }
        seq += 1n;
      }
      let readySockets = [];
      try {
        readySockets = poller.poll(50);
      } catch (error) {
        const text = String(error && error.message ? error.message : error);
        if ((error && error.code === 'EAGAIN') || text.includes('Resource temporarily unavailable')) {
          await sleepImmediate();
          continue;
        }
        throw error;
      }
      if (readySockets.length !== 0) {
        while (true) {
          const received = trySpotSubscribe(spot);
          if (!received) {
            break;
          }
          const firstPart = received.parts[0];
          if (!firstPart) {
            continue;
          }
          const header = decodeMetricHeader(firstPart.data());
          collector.record(header, currentEpochNs());
        }
      }
      if ((Number(seq) & 0x03) === 0) {
        await sleepImmediate();
      }
    }

    for (let i = 0; i < 4; i += 1) {
      while (true) {
        const received = trySpotSubscribe(spot);
        if (!received) {
          break;
        }
        const firstPart = received.parts[0];
        if (!firstPart) {
          continue;
        }
        const header = decodeMetricHeader(firstPart.data());
        collector.record(header, currentEpochNs());
      }
      await sleepImmediate();
    }
    const result = await collector.finish();
    if (DEBUG) {
      console.error(`spot accepted=${result.accepted} rejected=${result.rejected}`);
    }
    return result.latenciesNs;
  } finally {
    poller.close();
    if (spot) {
      spot.close();
    }
    discovery.close();
    ctx.close();
  }
}

module.exports = { runSpotBenchmark };
