// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist');
const {
  createMetricCollector,
  createPayload,
  createRunId,
  decodeMetricHeader,
  summarizeMetrics,
  stampPayload
} = require('../common/perf_metrics');

const SERVER_ID = Buffer.from('multi-router-router-server', 'ascii');

function parseArgs(argv) {
  const options = { endpoint: '', msgSize: 256, warmup: 1, duration: 2, clients: 1 };
  for (let i = 0; i < argv.length; i += 1) {
    if (argv[i] === '--endpoint') {
      options.endpoint = argv[++i];
    } else if (argv[i] === '--msg-size') {
      options.msgSize = Number(argv[++i]);
    } else if (argv[i] === '--warmup') {
      options.warmup = Number(argv[++i]);
    } else if (argv[i] === '--duration') {
      options.duration = Number(argv[++i]);
    } else if (argv[i] === '--clients') {
      options.clients = Number(argv[++i]);
    }
  }
  return options;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
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
      router.connect(options.endpoint);
      routers.push(router);
      warmupPayloads.push(createPayload(options.msgSize));
      activePayloads.push(createPayload(options.msgSize));
    }

    console.log('CLIENT_READY');

    const warmupUntilNs = process.hrtime.bigint()
      + BigInt(Math.floor(options.warmup * 1_000_000_000));
    while (process.hrtime.bigint() < warmupUntilNs) {
      for (let i = 0; i < routers.length; i += 1) {
        stampPayload(warmupPayloads[i], { phase: 2, runId, msgSize: options.msgSize });
        routers[i].send(SERVER_ID, warmupPayloads[i]);
        routers[i].receive();
      }
    }

    const stopAtNs = process.hrtime.bigint()
      + BigInt(Math.floor(options.duration * 1_000_000_000));
    while (process.hrtime.bigint() < stopAtNs) {
      for (let i = 0; i < routers.length; i += 1) {
        stampPayload(activePayloads[i], { phase: 0, runId, msgSize: options.msgSize });
        routers[i].send(SERVER_ID, activePayloads[i]);
        const echoed = routers[i].receive();
        collector.record(decodeMetricHeader(echoed.parts[0].toBuffer()), process.hrtime.bigint());
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
