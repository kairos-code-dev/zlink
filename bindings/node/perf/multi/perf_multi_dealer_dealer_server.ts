// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../..');
const {
  createPayload,
  createRunId,
  stampPayload
} = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const {
  POLLOUT,
  applyAutoHwmMsgUnit,
  applyContextPolicy,
  applySocketPolicy,
  pollEvents,
  pollEventHas,
  resolveClientPollTimeoutMs,
  trySocketSend,
  waitForConnectionReadyCount
} = require('./perf_multi_runtime');

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  applyContextPolicy(ctx, 'server', 'MULTI_DEALER_DEALER');
  const server = new zlink.DealerSocket(ctx);
  const poller = new zlink.Poller();
  const payload = createPayload(options.msgSize);
  let rl = null;

  try {
    applySocketPolicy(server);
    server.bind(options.endpoint);
    applyAutoHwmMsgUnit(server, options.msgSize);
    ctx.recalculateAutoHwm();
    poller.add(server, pollEvents(POLLOUT));
    const readyBarrier = waitForConnectionReadyCount(server, options.clients);
    console.log(`READY,${options.endpoint}`);

    rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of rl) {
      if (line !== `START,${options.msgSize}`) {
        if (line === 'STOP' || line === 'QUIT') {
          break;
        }
        continue;
      }

      await readyBarrier;
      const clientPollTimeoutMs = resolveClientPollTimeoutMs();
      const runId = createRunId(1);
      const activeStopNs = process.hrtime.bigint() + BigInt(Math.floor(options.duration * 1_000_000_000));
      let pending = false;
      let seq = 1n;
      while (process.hrtime.bigint() < activeStopNs) {
        if (!pending) {
          stampPayload(payload, { phase: 1, runId, msgSize: options.msgSize, seq });
          if (trySocketSend(server, payload)) {
            seq += 1n;
            continue;
          }
          pending = true;
        }

        const nowNs = process.hrtime.bigint();
        const remainMs = Number((activeStopNs - nowNs) / 1_000_000n);
        const waitMs = Math.max(1, Math.min(clientPollTimeoutMs, remainMs));
        const ready = poller.wait(waitMs);
        if (!ready || !pollEventHas(ready, POLLOUT)) {
          continue;
        }
        pending = false;
      }
      let cooldownPending = true;
      stampPayload(payload, { phase: 2, runId, msgSize: options.msgSize, seq });
      while (cooldownPending) {
        if (trySocketSend(server, payload)) {
          cooldownPending = false;
          continue;
        }
        const ready = poller.wait(clientPollTimeoutMs);
        if (ready && pollEventHas(ready, POLLOUT)) {
          // will retry send on next iteration
        }
      }
      break;
    }
  } finally {
    rl?.close();
    poller.close();
    server.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
