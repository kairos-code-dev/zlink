// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist');
const {
  attachCallbackCollector,
  driveSender,
  finishCollector
} = require('./perf_single_common');

async function runDealerRouterBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  const router = new zlink.RouterSocket(ctx);
  const dealer = new zlink.DealerSocket(ctx);
  const endpoint = `inproc://perf-dealer-router-${process.pid}-${msgSize}`;

  try {
    router.bind(endpoint);
    dealer.connect(endpoint);

    const state = attachCallbackCollector(
      (handler) => router.onReceive(handler),
      msgSize,
      options,
      (_, parts) => parts[0].toBuffer()
    );

    await driveSender((payload) => (
      dealer.trySend(payload) === zlink.SendResult.Sent
    ), state);
    return await finishCollector(state);
  } finally {
    dealer.close();
    router.close();
    ctx.close();
  }
}

module.exports = { runDealerRouterBenchmark };
