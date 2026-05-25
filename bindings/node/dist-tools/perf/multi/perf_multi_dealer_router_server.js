// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('@zlink-systems/zlink');
const { configureTlsServer } = require('../common/perf_tls');
const { HEADER_SIZE } = require('../common/perf_metrics');
const { STOP_TOKEN_BYTES } = require('../perf_stop_token');
const { parseMultiArgs } = require('./perf_multi_common');
const { POLLIN, POLLOUT, applyAutoHwmMsgUnit, applyContextPolicy, applySocketPolicy, emitMultiSocketHwmDetail, pollEvents, pollEventHas, trySocketSend, waitPollerOne, waitForConnectionReadyCount } = require('./perf_multi_runtime');
function drainPending(router, pending) {
    while (pending.length > 0) {
        const reply = pending[0];
        if (!trySocketSend(router, reply.routingId, reply.payload)) {
            break;
        }
        pending.shift();
    }
}
function receiveAndQueueReplies(router, recvBuffer, pending) {
    while (true) {
        const received = router.recvInto(recvBuffer, zlink.RecvFlags.DontWait);
        if (received === null) {
            break;
        }
        if (!received.routingId || received.spotRid || received.requestSeq) {
            continue;
        }
        pending.push({
            routingId: received.routingId,
            payload: Buffer.from(recvBuffer.subarray(0, received.size))
        });
    }
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'server', 'MULTI_DEALER_ROUTER');
    const router = new zlink.RouterSocket(ctx);
    const poller = new zlink.Poller();
    const recvBuffer = Buffer.allocUnsafe(Math.max(options.msgSize, HEADER_SIZE, STOP_TOKEN_BYTES.length));
    const pending = [];
    let pollBuffer = null;
    let rl = null;
    let stop = false;
    try {
        applySocketPolicy(router);
        configureTlsServer(router, options.transport);
        router.bind(options.endpoint);
        applyAutoHwmMsgUnit(ctx, options.msgSize);
        ctx.recalculateAutoHwm();
        emitMultiSocketHwmDetail(router, 'endpoint', options.transport, options.msgSize);
        poller.add(router, pollEvents(POLLIN), 0);
        pollBuffer = new zlink.PollEvents(1);
        const readyBarrier = waitForConnectionReadyCount(router, options.clients);
        console.log(`READY,${options.endpoint}`);
        rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        (async () => {
            for await (const line of rl) {
                if (line === 'STOP' || line === 'QUIT') {
                    stop = true;
                    break;
                }
            }
        })();
        await readyBarrier;
        while (!stop) {
            poller.modify(router, pollEvents(POLLIN | POLLOUT));
            const ready = waitPollerOne(poller, pollBuffer, -1);
            if (!ready) {
                continue;
            }
            if (pollEventHas(ready, POLLOUT)) {
                drainPending(router, pending);
            }
            if (pollEventHas(ready, POLLIN)) {
                receiveAndQueueReplies(router, recvBuffer, pending);
                drainPending(router, pending);
            }
        }
    }
    finally {
        rl?.close();
        pollBuffer?.close();
        poller.close();
        router.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
