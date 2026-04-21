// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist/canonical');
const {
  createMetricCollector,
  createPayload,
  createRunId,
  decodeMetricHeaderFromParts,
  currentEpochNs,
  sleepImmediate,
  summarizeMetrics,
  stampPayload
} = require('../common/perf_metrics');
const {
  applyContextPolicy,
  applySocketPolicy,
  benchmarkEndpoint,
  configureTlsClient,
  configureTlsServer,
  parseSingleBinaryArgs,
  resolveSingleLatencySampleCap,
  waitForPostReadySettle
} = require('./perf_single_common');

const POLLIN = 1;

function tryRecvRouted(spot) {
  try {
    return spot.recvRouted(zlink.RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
      return null;
    }
    throw error;
  }
}

function tryRecvRouter(router) {
  try {
    return router.recv(zlink.RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
      return null;
    }
    throw error;
  }
}

function closeReceived(received) {
  if (received && typeof received.close === 'function') {
    received.close();
  }
}

async function awaitReply(requester, poller, deadlineNs) {
  while (currentEpochNs() < deadlineNs) {
    const remainingNs = deadlineNs - currentEpochNs();
    const timeoutMs = Math.max(
      1,
      Math.min(Number.MAX_SAFE_INTEGER, Number(remainingNs / 1_000_000n))
    );
    let ready = [];
    try {
      ready = poller.poll(timeoutMs);
    } catch (error) {
      const text = String(error && error.message ? error.message : error);
      if ((error && error.code === 'EAGAIN') || text.includes('Resource temporarily unavailable')) {
        await sleepImmediate();
        continue;
      }
      throw error;
    }
    if (!Array.isArray(ready) || ready.length === 0) {
      await sleepImmediate();
      continue;
    }
    while (true) {
      const received = tryRecvRouter(requester);
      if (!received) {
        break;
      }
      try {
        const header = decodeMetricHeaderFromParts(received.parts);
        if (header) {
          return header;
        }
      } finally {
        closeReceived(received);
      }
    }
  }
  throw new Error('spot reqrep reply timed out');
}

async function waitForProbeReady(requester, replierNode, replier, payload, runId, msgSize, seqRef, poller) {
  const readyTimeoutMs = Number(process.env.PERF_CONNECT_READY_TIMEOUT_MS ?? 5000);
  const deadlineNs = currentEpochNs() + BigInt(Math.max(1, readyTimeoutMs)) * 1_000_000n;

  while (currentEpochNs() < deadlineNs) {
    stampPayload(payload, {
      phase: 0,
      runId,
      msgSize,
      seq: seqRef.current
    });
    requester.sendToSpot(replierNode.routingId, replier.routingId, payload);
    seqRef.current += 1n;
    let reply = null;
    try {
      reply = await awaitReply(requester, poller, deadlineNs);
    } catch (error) {
      if (currentEpochNs() >= deadlineNs) {
        throw error;
      }
      await sleepImmediate();
      continue;
    }
    if (
      reply.phase === 0
      && reply.runId === runId
      && reply.msgSize === msgSize
    ) {
      return;
    }
    await sleepImmediate();
  }

  throw new Error('spot reqrep probe-ready timeout');
}

async function runSpotReqRepBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  applyContextPolicy(ctx);
  const requester = new zlink.RouterSocket(ctx);
  const requesterPoller = new zlink.Poller();
  const replierNode = new zlink.SpotNode(ctx);
  const replier = replierNode.createSpot();
  const endpoint = await benchmarkEndpoint(options.transport, `spot-reqrep-${msgSize}`);

  try {
    applySocketPolicy(requester, options);
    applySocketPolicy(replier, options);
    configureTlsServer(replierNode, options.transport);
    configureTlsClient(requester, options.transport);
    replierNode.bind(endpoint);
    requester.connect(endpoint);
    requesterPoller.addSocket(requester, POLLIN);

    replier.onDispatchEvent(() => {
      while (true) {
        const received = tryRecvRouted(replier);
        if (!received) {
          return;
        }
        try {
          received.reply(received.parts);
        } finally {
          received.close();
        }
      }
    });

    const runId = createRunId(options.runId ?? 1);
    const payload = createPayload(msgSize);
    const seqRef = { current: 1n };
    await waitForProbeReady(
      requester,
      replierNode,
      replier,
      payload,
      runId,
      msgSize,
      seqRef,
      requesterPoller
    );
    await waitForPostReadySettle(Number(process.env.PERF_SINGLE_SPOT_READY_SETTLE_MS ?? 1000));

    const activeStartNs = currentEpochNs();
    const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
    const collector = createMetricCollector({
      runId,
      msgSize,
      activeStartNs,
      activeStopNs,
      roundTrip: true,
      sampleCap: resolveSingleLatencySampleCap()
    });

    while (currentEpochNs() < activeStopNs) {
      stampPayload(payload, {
        phase: 1,
        runId,
        msgSize,
        seq: seqRef.current
      });
      requester.sendToSpot(replierNode.routingId, replier.routingId, payload);
      seqRef.current += 1n;
      collector.record(
        await awaitReply(
          requester,
          requesterPoller,
          activeStopNs + 2_000_000_000n
        ),
        currentEpochNs()
      );
    }

    stampPayload(payload, {
      phase: 2,
      runId,
      msgSize,
      seq: seqRef.current
    });
    requester.sendToSpot(replierNode.routingId, replier.routingId, payload);
    await awaitReply(
      requester,
      requesterPoller,
      currentEpochNs() + 2_000_000_000n
    );
    return collector.finish();
  } finally {
    requesterPoller.close();
    replier.close();
    replierNode.close();
    requester.close();
    ctx.close();
  }
}

module.exports = { runSpotReqRepBenchmark };

if (require.main === module) {
  (async () => {
    const options = parseSingleBinaryArgs(process.argv.slice(2));
    const result = await runSpotReqRepBenchmark(options.msgSize, options);
    for (const line of summarizeMetrics(
      'SPOT_REQREP',
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
