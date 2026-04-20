// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const { sleepImmediate } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { POLLIN, POLLOUT, recvNoWait, trySocketSend } = require('./perf_multi_runtime');
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    const router = new zlink.RouterSocket(ctx);
    const poller = new zlink.Poller();
    const pending = [];
    let stop = false;
    try {
        router.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('multi-router-router-server', 'ascii')));
        router.bind(options.endpoint);
        poller.addSocket(router, POLLIN);
        console.log(`READY,${options.endpoint}`);
        const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        (async () => {
            for await (const line of rl) {
                if (line === 'STOP') {
                    stop = true;
                    break;
                }
            }
        })();
        while (!stop) {
            poller.modifySocket(router, pending.length > 0 ? (POLLIN | POLLOUT) : POLLIN);
            const ready = poller.wait(25);
            if (!ready) {
                await sleepImmediate();
                continue;
            }
            if ((ready.events & POLLIN) !== 0) {
                while (true) {
                    const received = recvNoWait(router);
                    if (!received) {
                        break;
                    }
                    try {
                        if (!received.routingId) {
                            continue;
                        }
                        const reply = {
                            routingId: received.routingId,
                            parts: received.parts.map((part) => part.data())
                        };
                        if (pending.length === 0 && trySocketSend(router, reply.routingId, reply.parts)) {
                            continue;
                        }
                        pending.push(reply);
                    }
                    finally {
                        if (typeof received.close === 'function') {
                            received.close();
                        }
                    }
                }
            }
            if ((ready.events & POLLOUT) !== 0) {
                while (pending.length > 0) {
                    const current = pending[0];
                    if (!trySocketSend(router, current.routingId, current.parts)) {
                        break;
                    }
                    pending.shift();
                }
            }
        }
    }
    finally {
        poller.close();
        router.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
