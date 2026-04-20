// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const {
  createPayload,
  createRunId,
  sleepImmediate,
  stampPayload
} = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const {
  POLLOUT,
  applySocketPolicy,
  trySocketPublish
} = require('./perf_multi_runtime');

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  const pub = new zlink.PubSocket(ctx);
  const poller = new zlink.Poller();
  const payload = createPayload(options.msgSize);

  try {
    applySocketPolicy(pub, {
      noDrop: Number(process.env.PERF_MULTI_PUBSUB_XPUB_NODROP ?? 1) !== 0
    });
    pub.bind(options.endpoint);
    poller.addSocket(pub, POLLOUT);
    console.log(`READY,${options.endpoint}`);

    const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of rl) {
      if (line !== `START,${options.msgSize}`) {
        if (line === 'STOP') {
          break;
        }
        continue;
      }

      const runId = createRunId(1);
      const activeStopNs = process.hrtime.bigint() + BigInt(Math.floor(options.duration * 1_000_000_000));
      let seq = 1n;
      let pending = false;
      while (process.hrtime.bigint() < activeStopNs) {
        if (!pending) {
          stampPayload(payload, { phase: 1, runId, msgSize: options.msgSize, seq });
          if (trySocketPublish(pub, 'perf.topic', payload)) {
            seq += 1n;
            continue;
          }
          pending = true;
        }

        const ready = poller.wait(25);
        if (!ready || (ready.events & POLLOUT) === 0) {
          await sleepImmediate();
          continue;
        }
        pending = false;
      }
      break;
    }
  } finally {
    poller.close();
    pub.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
