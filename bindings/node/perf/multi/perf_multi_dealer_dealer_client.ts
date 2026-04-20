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
  trySocketSend,
  waitForConnectionReady
} = require('./perf_multi_runtime');

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  const dealers = [];
  const payloads = [];

  try {
    for (let i = 0; i < options.clients; i += 1) {
      const dealer = new zlink.DealerSocket(ctx);
      dealers.push(dealer);
      payloads.push(createPayload(options.msgSize));
    }
    for (const dealer of dealers) {
      await waitForConnectionReady(dealer, () => dealer.connect(options.endpoint));
    }
    console.log(`CLIENT_READY,${options.msgSize}`);

    const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    let active = false;
    for await (const line of rl) {
      if (line === `PHASE_ACTIVE,${options.msgSize}`) {
        active = true;
        break;
      }
    }
    if (!active) {
      throw new Error('dealer-dealer client missing PHASE_ACTIVE');
    }

    const runId = createRunId(1);
    const activeStopNs = process.hrtime.bigint() + BigInt(Math.floor(options.duration * 1_000_000_000));
    let seq = 1n;
    while (process.hrtime.bigint() < activeStopNs) {
      let progressed = false;
      for (let i = 0; i < dealers.length; i += 1) {
        stampPayload(payloads[i], { phase: 1, runId, msgSize: options.msgSize, seq });
        if (!trySocketSend(dealers[i], payloads[i])) {
          continue;
        }
        progressed = true;
        seq += 1n;
      }
      if (!progressed) {
        await sleepImmediate();
      }
    }
  } finally {
    for (const dealer of dealers) {
      dealer.close();
    }
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
