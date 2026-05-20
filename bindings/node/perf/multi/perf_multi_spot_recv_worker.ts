// SPDX-License-Identifier: MPL-2.0

'use strict';

const { parentPort, workerData } = require('node:worker_threads');
const zlink = require('@zlink-systems/zlink');
const { configureTlsClient } = require('../common/perf_tls');
const {
  createRunId,
  decodeMetricHeader,
  currentEpochNs,
  HEADER_SIZE,
  sleepImmediate
} = require('../common/perf_metrics');
const {
  applyAutoHwmMsgUnit,
  applyContextPolicy,
  applySpotNodeAdmission,
  emitMultiSocketHwmDetail
} = require('./perf_multi_runtime');

const TOPIC = 'bench';

function trySpotSubscribePayloadInto(spot, buffer) {
  try {
    return spot.subscribePayloadInto(buffer, zlink.RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError &&
        (error.result === zlink.RecvResult.NoData || error.internalErrno === 2)) {
      return null;
    }
    const text = String(error && error.message ? error.message : error);
    if (/Device or resource busy|resource busy/i.test(text)) {
      return null;
    }
    throw error;
  }
}

function closeQuietly(resource) {
  try {
    resource?.close();
  } catch {
    // Best-effort cleanup after the result has already been reported.
  }
}

function connectPeerIfNeeded(node, endpoint) {
  try {
    node.connectPeer(endpoint);
  } catch (error) {
    const text = String(error && error.message ? error.message : error);
    if (!/Device or resource busy|resource busy|already/i.test(text)) {
      throw error;
    }
  }
}

function resolveLatencySampleStride() {
  const configured = Number(process.env.PERF_MULTI_SPOT_LATENCY_SAMPLE_STRIDE);
  if (Number.isFinite(configured) && configured >= 1) {
    return Math.trunc(configured);
  }
  return 32;
}

function shouldSampleLatency(sampleIndex, stride) {
  return stride <= 1 || sampleIndex === 1 || (sampleIndex % stride) === 0;
}

async function waitForStart() {
  return new Promise((resolve) => {
    parentPort.once('message', (message) => resolve(message));
  });
}

async function main() {
  const options = workerData;
  const ctx = new zlink.Context();
  applyContextPolicy(ctx, 'client', 'MULTI_SPOT');
  const node = new zlink.SpotNode(ctx);
  const slots = [];

  try {
    configureTlsClient(node, options.transport);
    applyAutoHwmMsgUnit(ctx, options.msgSize);
    applySpotNodeAdmission(node);
    connectPeerIfNeeded(node, options.peerEndpoint);

    const payloadSize = Math.max(options.msgSize, HEADER_SIZE);
    for (let i = 0; i < options.clients; i += 1) {
      const spot = node.createSpot();
      spot.setSubscription(TOPIC);
      slots.push({ spot, buffer: Buffer.allocUnsafe(payloadSize) });
    }

    ctx.recalculateAutoHwm();
    if (options.workerIndex === 0) {
      emitMultiSocketHwmDetail(node, 'spotnode_data', options.transport, options.msgSize);
    }
    parentPort.postMessage({ type: 'ready' });

    const start = await waitForStart();
    const activeStartNs = BigInt(start.activeStartNs);
    const graceMultiplier = options.msgSize >= 262144
      ? 4
      : (options.msgSize >= 131072 ? 2 : 1);
    const fallbackDeadlineNs = activeStartNs
      + BigInt(Math.ceil(options.duration * 1000 * (1 + graceMultiplier)))
        * 1_000_000n;
    const expectedRunId = createRunId(1);
    const burstCap = 4096;
    const activeDurationNs = BigInt(Math.floor(options.duration * 1_000_000_000));
    const latencySampleStride = resolveLatencySampleStride();
    let senderWindowEndNs = 0n;
    let accepted = 0;
    const latenciesNs = [];

    while (currentEpochNs() < fallbackDeadlineNs) {
      let progressed = false;
      for (let i = 0; i < slots.length; i += 1) {
        const { spot, buffer } = slots[i];
        let drained = 0;
        while (drained < burstCap && currentEpochNs() < fallbackDeadlineNs) {
          const received = trySpotSubscribePayloadInto(spot, buffer);
          if (!received) {
            break;
          }
          drained += 1;
          progressed = true;
          const header = decodeMetricHeader(buffer.subarray(0, received.size));
          if (!header
              || (header.runId >>> 0) !== expectedRunId
              || (header.msgSize >>> 0) !== options.msgSize
              || header.phase !== 1) {
            continue;
          }
          const sentTsNs = BigInt(header.sentTsNs);
          if (sentTsNs <= 0n) {
            continue;
          }
          if (senderWindowEndNs === 0n) {
            senderWindowEndNs = sentTsNs + activeDurationNs;
          }
          if (sentTsNs > senderWindowEndNs) {
            continue;
          }
          const receivedAtNs = currentEpochNs();
          if (receivedAtNs < sentTsNs) {
            continue;
          }
          accepted += 1;
          if (shouldSampleLatency(accepted, latencySampleStride)) {
            latenciesNs.push(Number(receivedAtNs - sentTsNs));
          }
        }
      }
      if (!progressed) {
        await sleepImmediate();
      }
    }

    parentPort.postMessage({
      type: 'result',
      accepted,
      latenciesNs,
    });
  } finally {
    for (const slot of slots) {
      closeQuietly(slot.spot);
    }
    closeQuietly(node);
    closeQuietly(ctx);
  }
}

main().catch((error) => {
  parentPort.postMessage({
    type: 'error',
    error: String(error && error.stack ? error.stack : error)
  });
});
