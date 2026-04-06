// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist');
const {
  createMetricCollector,
  createPayload,
  createRunId,
  decodeMetricHeader,
  sleepImmediate,
  stampPayload
} = require('../common/perf_metrics');
const {
  callbackDrainTicks,
  callbackSendBurstLimit
} = require('./perf_callback_policy');

async function runPubSubBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  const pub = new zlink.PubSocket(ctx);
  const sub = new zlink.SubSocket(ctx);
  const endpoint = `inproc://perf-pubsub-${process.pid}-${msgSize}`;
  const topic = 'perf:pubsub';

  try {
    pub.bind(endpoint);
    sub.connect(endpoint);
    sub.setSubscription(topic);

    const startedAtNs = process.hrtime.bigint();
    const runId = createRunId();
    const collector = createMetricCollector({ runId, msgSize });
    const payload = createPayload(msgSize);
    const sendBurstLimit = callbackSendBurstLimit(msgSize);
    const drainTicks = callbackDrainTicks(msgSize);
    const warmupUntilNs = startedAtNs
      + BigInt(Math.floor(options.warmup * 1_000_000_000));
    const stopAtNs = startedAtNs
      + BigInt(Math.floor((options.warmup + options.duration) * 1_000_000_000));

    sub.onSubscribe((_, __, parts) => {
      const messageBuffer = parts[0].toBuffer();
      const header = decodeMetricHeader(messageBuffer);
      collector.record(header, process.hrtime.bigint());
    });

    let turns = 0;
    while (process.hrtime.bigint() < stopAtNs) {
      for (
        let i = 0;
        i < sendBurstLimit && process.hrtime.bigint() < stopAtNs;
        i += 1
      ) {
        stampPayload(payload, {
          phase: process.hrtime.bigint() < warmupUntilNs ? 2 : 0,
          runId,
          msgSize
        });
        if (pub.tryPublish(topic, payload) !== zlink.SendResult.Sent) {
          break;
        }
      }
      turns += 1;
      if (msgSize >= 65536 || (turns & 0x03) === 0) {
        await sleepImmediate();
      }
    }

    for (let i = 0; i < drainTicks; i += 1) {
      await sleepImmediate();
    }

    const result = await collector.finish();
    return result.latenciesUs;
  } finally {
    sub.close();
    pub.close();
    ctx.close();
  }
}

module.exports = { runPubSubBenchmark };
