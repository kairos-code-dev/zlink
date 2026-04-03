// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist');
const {
  attachCallbackCollector,
  driveSender,
  finishCollector
} = require('./perf_single_common');

async function runSpotBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const spot = new zlink.Spot(node);
  const topic = 'perf:spot';

  try {
    const state = attachCallbackCollector(
      (handler) => spot.onSubscribe(handler),
      msgSize,
      options,
      (_, __, parts) => parts[0].toBuffer()
    );

    spot.setSubscription(topic);
    await driveSender((payload) => (
      spot.tryPublish(topic, payload) === zlink.SendResult.Sent
    ), state);
    return await finishCollector(state);
  } finally {
    spot.close();
    node.close();
    ctx.close();
  }
}

module.exports = { runSpotBenchmark };
