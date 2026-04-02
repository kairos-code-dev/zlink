// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist');
const { attachCallbackCollector, driveSender, finishCollector } = require('./perf_single_common');
async function runDealerDealerBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    const server = new zlink.DealerSocket(ctx);
    const client = new zlink.DealerSocket(ctx);
    const endpoint = `inproc://perf-dealer-dealer-${process.pid}-${msgSize}`;
    try {
        server.bind(endpoint);
        client.connect(endpoint);
        const state = attachCallbackCollector((handler) => server.onReceive(handler), msgSize, options, (_, parts) => parts[0].toBuffer());
        await driveSender((payload) => (client.trySend(payload) === zlink.SendResult.Sent), state);
        return await finishCollector(state);
    }
    finally {
        client.close();
        server.close();
        ctx.close();
    }
}
module.exports = { runDealerDealerBenchmark };
