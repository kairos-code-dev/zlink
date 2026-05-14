// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('@zlink-systems/zlink');
const {
  createPayload,
  createRunId,
  stampPayload
} = require('../common/perf_metrics');
const { configureTlsServer } = require('../common/perf_tls');
const { parseMultiArgs } = require('./perf_multi_common');
const {
  POLLOUT,
  applyAutoHwmMsgUnit,
  applyContextPolicy,
  applySocketPolicy,
  emitMultiSocketHwmDetail,
  pollEvents,
  pollEventHas,
  sendStopTokenOnce,
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
    configureTlsServer(server, options.transport);
    server.bind(options.endpoint);
    applyAutoHwmMsgUnit(server, options.msgSize);
    ctx.recalculateAutoHwm();
    emitMultiSocketHwmDetail(server, 'endpoint', options.transport, options.msgSize);
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

        // PERF_MULTI_TEST_POLICY § 1.3.1: signal-driven wait. Once the
        // duration deadline elapses we exit on the next loop iteration —
        // we do not need a timer-based bound because POLLOUT readiness
        // arrives quickly under the perf HWM/inflight regime.
        const ready = poller.wait(-1);
        if (!ready || !pollEventHas(ready, POLLOUT)) {
          continue;
        }
        pending = false;
        if (process.hrtime.bigint() >= activeStopNs) {
          break;
        }
      }
      // PERF_MULTI_TEST_POLICY § 1.3.1: signal phase end via the wire-level
      // stop token (retries through transient backpressure with a poller
      // POLLOUT wait — no 1-25 ms timer fallback).
      for (let i = 0; i < Math.max(options.clients * 16, options.clients); i += 1) {
        await sendStopTokenOnce(server, (bytes) => trySocketSend(server, bytes));
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
