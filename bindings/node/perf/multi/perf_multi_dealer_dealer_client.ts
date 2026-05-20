// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('@zlink-systems/zlink');
const { requireNative } = require('../../dist/zlink/runtime/native/native');
const {
  createPayload,
} = require('../common/perf_metrics');
const { configureTlsClient } = require('../common/perf_tls');
const { parseMultiArgs } = require('./perf_multi_common');
const {
  applyAutoHwmMsgUnit,
  applyContextPolicy,
  applySocketPolicy,
  emitMultiSocketHwmDetail,
  waitForConnectionReady
} = require('./perf_multi_runtime');
const { STOP_TOKEN_BYTES } = require('../perf_stop_token');

// MULTI_DEALER_DEALER client == SENDER (one DEALER socket per client).
//
// C parity: bindings/c/perf/multi/src/perf_multi_dealer_dealer_client.cpp
// is the SENDER. It creates one DEALER socket per client (connect),
// prints CLIENT_READY,<size>, waits START,<size> from stdin, runs the
// per-socket DONTWAIT send window (run_send_window ~142-265: send until
// the duration deadline, POLLOUT-wait pending sockets with `-1`), then
// sends exactly ONE blocking wire stop token per socket
// (run_single_size_case ~290-293 / send_stop_token ~114-140). The
// matching RECEIVER/MEASURER is perf_multi_dealer_dealer_server.cpp.
// Cross-checked against the already-fixed cpp
// bindings/cpp/perf/multi/src/perf_dealer_dealer_client.cpp.
// Handshake (PERF_MULTI § 1.5 / line 201): server READY,<endpoint> then
// client spawn; client prints CLIENT_READY,<size>; runner sends
// START,<size> to BOTH; sender runs the send window after START.
async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  applyContextPolicy(ctx, 'client', 'MULTI_DEALER_DEALER');
  const dealers = [];
  let rl = null;

  try {
    for (let i = 0; i < options.clients; i += 1) {
      const dealer = new zlink.DealerSocket(ctx);
      applySocketPolicy(dealer, { transport: options.transport });
      configureTlsClient(dealer, options.transport);
      dealers.push(dealer);
    }
    for (const dealer of dealers) {
      await waitForConnectionReady(dealer, () => dealer.connect(options.endpoint));
      applyAutoHwmMsgUnit(ctx, options.msgSize);
    }
    ctx.recalculateAutoHwm();
    for (const dealer of dealers) {
      emitMultiSocketHwmDetail(dealer, 'endpoint', options.transport, options.msgSize);
    }

    console.log(`CLIENT_READY,${options.msgSize}`);
    rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of rl) {
      if (line === `START,${options.msgSize}`) {
        break;
      }
      if (line === 'STOP' || line === 'QUIT') {
        return;
      }
    }

    const payloads = dealers.map(() => createPayload(options.msgSize));
    // C run_send_window (~187-257) and send_stop_token (~114-140):
    // keep the active loop inside native code so each DEALER socket follows
    // the same DONTWAIT send, pending POLLOUT wait, and blocking stop-token
    // sequence as the C perf client. This avoids per-message JS scheduling
    // jitter without changing the public socket API.
    requireNative().socketPerfDealerDealerSendLoop(
      dealers.map((dealer) => dealer.nativeHandle()),
      payloads,
      options.duration,
      options.msgSize,
      STOP_TOKEN_BYTES
    );
    console.log(`CLIENT_DONE,${options.msgSize}`);
  } finally {
    rl?.close();
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
