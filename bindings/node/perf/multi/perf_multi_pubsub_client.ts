// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist');
const {
  createMetricCollector,
  decodeMetricHeader,
  summarizeMetrics
} = require('../common/perf_metrics');

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
  const subs = [];
  const collector = createMetricCollector({
    runId: 0,
    msgSize: options.msgSize
  });
  let stopResolve;
  const stopped = new Promise((resolve) => {
    stopResolve = resolve;
  });
  let stopCount = 0;
  let stop = false;

  try {
    for (let i = 0; i < options.clients; i += 1) {
      const sub = new zlink.SubSocket(ctx);
      sub.setSubscription('perf.topic');
      sub.connect(options.endpoint);
      (async () => {
        while (!stop) {
          const received = sub.trySubscribe();
          if (!received) {
            await new Promise((resolve) => setImmediate(resolve));
            continue;
          }
          const header = decodeMetricHeader(received.parts[0].toBuffer());
          if (!header) {
            continue;
          }
          if (header.phase === 1) {
            stopCount += 1;
            if (stopCount >= options.clients) {
              stopResolve();
            }
            continue;
          }
          collector.record(header, process.hrtime.bigint());
        }
      })();
      subs.push(sub);
    }

    console.log('CLIENT_READY');
    await Promise.race([
      stopped,
      new Promise((resolve) => setTimeout(resolve, Math.ceil((options.warmup + options.duration + 2) * 1000)))
    ]);
    stop = true;
    const result = await collector.finish();
    const resultLines = summarizeMetrics(
      'MULTI_PUBSUB',
      'tcp',
      options.msgSize,
      result.latenciesUs,
      options.duration
    );
    for (const line of resultLines) {
      console.log(line);
    }
  } finally {
    for (const sub of subs) {
      sub.close();
    }
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
