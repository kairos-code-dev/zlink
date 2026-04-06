// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../dist');
const { createPayload, sleepImmediate, stampPayload } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');

async function waitPubReady(pub) {
  const monitor = pub.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);
  try {
    while (true) {
      const event = monitor.recv();
      if (event.event === zlink.MonitorEvent.CONNECTION_READY) {
        return;
      }
    }
  } finally {
    monitor.close();
  }
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  const pub = new zlink.PubSocket(ctx);
  const warmupPayload = createPayload(options.msgSize);
  const activePayload = createPayload(options.msgSize);
  const stopPayload = createPayload(options.msgSize);

  try {
    pub.bind(options.endpoint);
    console.log(`READY,${options.endpoint}`);
    const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of rl) {
      if (line !== 'GO') {
        continue;
      }

      await waitPubReady(pub);

      const warmupUntil = process.hrtime.bigint() + BigInt(Math.floor(options.warmup * 1_000_000_000));
      while (process.hrtime.bigint() < warmupUntil) {
        stampPayload(warmupPayload, { phase: 2, runId: 0, msgSize: options.msgSize });
        pub.publish('perf.topic', warmupPayload);
        await sleepImmediate();
      }

      const activeUntil = process.hrtime.bigint() + BigInt(Math.floor(options.duration * 1_000_000_000));
      while (process.hrtime.bigint() < activeUntil) {
        stampPayload(activePayload, { phase: 0, runId: 0, msgSize: options.msgSize });
        pub.publish('perf.topic', activePayload);
        await sleepImmediate();
      }

      for (let i = 0; i < options.clients; i += 1) {
        stampPayload(stopPayload, { phase: 1, runId: 0, msgSize: options.msgSize });
        pub.publish('perf.topic', stopPayload);
      }
      break;
    }
  } finally {
    pub.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
