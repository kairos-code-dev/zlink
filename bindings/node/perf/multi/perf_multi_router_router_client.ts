// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist');
const {
  createMetricCollector,
  createPayload,
  createRunId,
  decodeMetricHeader,
  currentEpochUs,
  summarizeMetrics,
  stampPayload
} = require('../common/perf_metrics');

const SERVER_ID = Buffer.from('multi-router-router-server', 'ascii');
const { parseMultiArgs } = require('./perf_multi_common');
const { waitForConnectionReady } = require('./perf_multi_runtime');

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  const runId = createRunId();
  const collector = createMetricCollector({
    runId,
    msgSize: options.msgSize
  });
  const routers = [];
  const warmupPayloads = [];
  const activePayloads = [];

  try {
    for (let i = 0; i < options.clients; i += 1) {
      const router = new zlink.RouterSocket(ctx);
      router.setRoutingId(Buffer.from(`multi-router-client-${i}`, 'ascii'));
      routers.push(router);
      warmupPayloads.push(createPayload(options.msgSize));
      activePayloads.push(createPayload(options.msgSize));
    }
    for (const router of routers) {
      await waitForConnectionReady(router, () => router.connect(options.endpoint));
    }

    console.log('CLIENT_READY');

    const warmupUntilNs = process.hrtime.bigint()
      + BigInt(Math.floor(options.warmup * 1_000_000_000));
    let seq = 1n;
    while (process.hrtime.bigint() < warmupUntilNs) {
      for (let i = 0; i < routers.length; i += 1) {
        stampPayload(warmupPayloads[i], { phase: 0, runId, msgSize: options.msgSize, seq });
        routers[i].send(SERVER_ID, warmupPayloads[i]);
        routers[i].recv();
        seq += 1n;
      }
    }

    const stopAtNs = process.hrtime.bigint()
      + BigInt(Math.floor(options.duration * 1_000_000_000));
    while (process.hrtime.bigint() < stopAtNs) {
      for (let i = 0; i < routers.length; i += 1) {
        stampPayload(activePayloads[i], { phase: 1, runId, msgSize: options.msgSize, seq });
        routers[i].send(SERVER_ID, activePayloads[i]);
        const echoed = routers[i].recv();
        collector.record(decodeMetricHeader(echoed.parts[0].data), currentEpochUs());
        seq += 1n;
      }
    }

    const result = await collector.finish();
    const lines = summarizeMetrics(
      'MULTI_ROUTER_ROUTER',
      'tcp',
      options.msgSize,
      result.latenciesUs,
      options.duration
    );
    for (const line of lines) {
      console.log(line);
    }
  } finally {
    await collector.close();
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
