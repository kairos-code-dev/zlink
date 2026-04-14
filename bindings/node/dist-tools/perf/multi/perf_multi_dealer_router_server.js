// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const { parseMultiArgs } = require('./perf_multi_common');
const DEBUG = process.env.PERF_DEBUG === '1';
const POLLIN = 1;
function tryRouterRecv(router) {
    try {
        return router.recv(zlink.RecvFlags.DontWait);
    }
    catch (error) {
        if (error instanceof zlink.RecvError
            && error.result === zlink.RecvResult.NoData) {
            return null;
        }
        throw error;
    }
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2), { msgSize: undefined, warmup: undefined, duration: undefined, clients: undefined });
    const ctx = new zlink.Context();
    const router = new zlink.RouterSocket(ctx);
    const poller = new zlink.Poller();
    let stop = false;
    let handled = 0;
    try {
        router.bind(options.endpoint);
        poller.addSocket(router, POLLIN);
        console.log(`READY,${options.endpoint}`);
        const receiveLoop = (async () => {
            while (!stop) {
                const ready = poller.wait(25);
                if (!ready) {
                    await new Promise((resolve) => setImmediate(resolve));
                    continue;
                }
                while (true) {
                    const received = tryRouterRecv(router);
                    if (!received) {
                        break;
                    }
                    handled += 1;
                    if (DEBUG && (handled <= 8 || (handled % 100) === 0)) {
                        console.error(`dealer-router-server recv handled=${handled}`);
                    }
                    if (received.routingId) {
                        router.send(received.routingId, received.parts.map((part) => part.data()));
                        if (DEBUG && (handled <= 8 || (handled % 100) === 0)) {
                            console.error(`dealer-router-server sent handled=${handled}`);
                        }
                    }
                }
            }
        })();
        const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        for await (const line of rl) {
            if (line === 'STOP') {
                stop = true;
                break;
            }
        }
        await receiveLoop;
    }
    finally {
        if (DEBUG) {
            console.error(`dealer-router-server summary handled=${handled}`);
        }
        poller.close();
        router.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
