// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist/canonical');
const {
  createMetricCollector,
  createPayload,
  createRunId,
  decodeMetricHeader,
  currentEpochNs,
  sleepImmediate,
  summarizeMetrics,
  stampPayload
} = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const {
  POLLIN,
  POLLOUT,
  applyContextPolicy,
  applySocketPolicy,
  recvNoWait,
  resolveMultiLatencySampleCap,
  trySocketSend,
  waitForConnectionReady
} = require('./perf_multi_runtime');

const SERVER_ID = Buffer.from('multi-router-router-server', 'ascii');
const SERVER_ROUTING_ID = zlink.RoutingId.fromBytes(SERVER_ID);

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  applyContextPolicy(ctx, 'client', 'MULTI_ROUTER_ROUTER');
  const routers = [];
  const payloads = [];
  const waiting = [];
  const sendPending = [];
  const poller = new zlink.Poller();

  try {
    for (let i = 0; i < options.clients; i += 1) {
      const router = new zlink.RouterSocket(ctx);
      applySocketPolicy(router);
      router.setRoutingId(
        zlink.RoutingId.fromBytes(Buffer.from(`multi-router-client-${i}`, 'ascii'))
      );
      routers.push(router);
      payloads.push(createPayload(options.msgSize));
      waiting.push(false);
      sendPending.push(false);
    }
    for (let i = 0; i < routers.length; i += 1) {
      await waitForConnectionReady(routers[i], () => routers[i].connect(options.endpoint));
      poller.addSocket(routers[i], POLLIN | POLLOUT, i);
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
        const echoed = recvNoWait(routers[index]);
        if (!echoed) {
          break;
        }
        waiting[index] = false;
        collector.record(
          decodeMetricHeader(echoed.parts[0].data()),
          currentEpochNs()
        );
        progressed = true;
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
        if (!trySocketSend(routers[i], SERVER_ROUTING_ID, payloads[i])) {
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
    for (const metricLine of summarizeMetrics(
      'MULTI_ROUTER_ROUTER',
      options.transport,
      options.msgSize,
      result.latenciesNs,
      options.duration,
      'current',
      result.accepted
    )) {
      console.log(metricLine);
    }
  } finally {
    poller.close();
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
