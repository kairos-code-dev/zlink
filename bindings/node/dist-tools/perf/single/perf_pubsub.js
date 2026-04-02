// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist');
const { attachCallbackCollector, driveSender, finishCollector } = require('./perf_single_common');
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
        const state = attachCallbackCollector((handler) => sub.onSubscribe(handler), msgSize, options, (_, __, parts) => parts[0].toBuffer());
        await driveSender((payload) => (pub.tryPublish(topic, payload) === zlink.SendResult.Sent), state);
        return await finishCollector(state);
    }
    finally {
        sub.close();
        pub.close();
        ctx.close();
    }
}
module.exports = { runPubSubBenchmark };
