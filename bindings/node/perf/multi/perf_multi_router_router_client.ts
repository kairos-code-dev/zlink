// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
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
  recvNoWait,
  trySocketSend,
  waitForConnectionReady
} = require('./perf_multi_runtime');

const SERVER_ID = Buffer.from('multi-router-router-server', 'ascii');
const SERVER_ROUTING_ID = zlink.RoutingId.fromBytes(SERVER_ID);

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  const routers = [];
  const payloads = [];
  const waiting = [];

  try {
    for (let i = 0; i < options.clients; i += 1) {
      const router = new zlink.RouterSocket(ctx);
      router.setRoutingId(
        zlink.RoutingId.fromBytes(Buffer.from(`multi-router-client-${i}`, 'ascii'))
      );
      routers.push(router);
      payloads.push(createPayload(options.msgSize));
      waiting.push(false);
    }
    for (const router of routers) {
      await waitForConnectionReady(router, () => router.connect(options.endpoint));
    }
    console.log(`CLIENT_READY,${options.msgSize}`);

    const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of rl) {
      if (line !== `START,${options.msgSize}`) {
        continue;
      }

      const runId = createRunId(1);
      const activeStartNs = process.hrtime.bigint();
      const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
      const collector = createMetricCollector({
        runId,
        msgSize: options.msgSize,
        activeStartNs,
        activeStopNs,
        roundTrip: true
      });
      let seq = 1n;

      while (process.hrtime.bigint() < activeStopNs) {
        let progressed = false;
        for (let i = 0; i < routers.length; i += 1) {
          if (waiting[i]) {
            continue;
          }
          stampPayload(payloads[i], { phase: 1, runId, msgSize: options.msgSize, seq });
          if (!trySocketSend(routers[i], SERVER_ROUTING_ID, payloads[i])) {
            continue;
          }
          waiting[i] = true;
          seq += 1n;
          progressed = true;
        }
        for (let i = 0; i < routers.length; i += 1) {
          const echoed = recvNoWait(routers[i]);
          if (!echoed) {
            continue;
          }
          waiting[i] = false;
          collector.record(
            decodeMetricHeader(echoed.parts[0].data()),
            currentEpochNs()
          );
          progressed = true;
        }
        if (!progressed) {
          await sleepImmediate();
        }
      }

      const drainDeadline = Date.now() + 2000;
      while (waiting.some(Boolean) && Date.now() < drainDeadline) {
        let progressed = false;
        for (let i = 0; i < routers.length; i += 1) {
          const echoed = recvNoWait(routers[i]);
          if (!echoed) {
            continue;
          }
          waiting[i] = false;
          collector.record(
            decodeMetricHeader(echoed.parts[0].data()),
            currentEpochNs()
          );
          progressed = true;
        }
        if (!progressed) {
          await sleepImmediate();
        }
      }

      const result = await collector.finish();
      for (const metricLine of summarizeMetrics(
        'MULTI_ROUTER_ROUTER',
        'tcp',
        options.msgSize,
        result.latenciesNs,
        options.duration
      )) {
        console.log(metricLine);
      }
      break;
    }
  } finally {
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
