// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('@zlink-systems/zlink');
const { createPayload, createRunId, stampPayload } = require('../common/perf_metrics');
const { configureTlsClient } = require('../common/perf_tls');
const { parseMultiArgs } = require('./perf_multi_common');
const { POLLOUT, applyAutoHwmMsgUnit, applyContextPolicy, applySocketPolicy, emitMultiSocketHwmDetail, pollEvents, pollEventHas, trySocketSend, waitForConnectionReady } = require('./perf_multi_runtime');
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
            applyAutoHwmMsgUnit(dealer, options.msgSize);
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
        const runId = createRunId(1);
        const payloads = dealers.map(() => createPayload(options.msgSize));
        const poller = new zlink.Poller();
        try {
            for (let i = 0; i < dealers.length; i += 1) {
                poller.add(dealers[i], pollEvents(POLLOUT), i);
            }
            // C run_send_window (~187-257): per-socket DONTWAIT send loop; a
            // socket that backpressures is marked pending; pending sockets are
            // POLLOUT-waited with `-1` (signal-driven, no timer fallback) and
            // cleared on POLLOUT. The duration deadline (application clock)
            // bounds the active window — PERF_MULTI § 1.3.1.
            const activeStopNs = process.hrtime.bigint()
                + BigInt(Math.floor(options.duration * 1_000_000_000));
            const pending = new Array(dealers.length).fill(false);
            let seq = 1n;
            while (process.hrtime.bigint() < activeStopNs) {
                let pendingCount = 0;
                for (let i = 0; i < dealers.length; i += 1) {
                    if (pending[i]) {
                        pendingCount += 1;
                        continue;
                    }
                    while (process.hrtime.bigint() < activeStopNs) {
                        stampPayload(payloads[i], {
                            phase: 1,
                            runId,
                            msgSize: options.msgSize,
                            seq
                        });
                        if (trySocketSend(dealers[i], payloads[i])) {
                            seq += 1n;
                            continue;
                        }
                        pending[i] = true;
                        pendingCount += 1;
                        break;
                    }
                }
                if (process.hrtime.bigint() >= activeStopNs || pendingCount === 0) {
                    continue;
                }
                const ready = poller.waitMany(poller.size, -1);
                for (const event of ready) {
                    if (!pollEventHas(event, POLLOUT)) {
                        continue;
                    }
                    const index = event.tag ?? event.userData;
                    if (Number.isInteger(index)) {
                        pending[index] = false;
                    }
                }
            }
            // C send_stop_token (~114-140) / run_single_size_case (~290-293):
            // exactly ONE wire stop token per socket. C's loop retries through
            // transient backpressure (EAGAIN/EWOULDBLOCK/ETIMEDOUT) until the
            // token is accepted (deadline ignored). DONTWAIT send + POLLOUT
            // `-1` wait keeps it purely signal-driven (PERF_MULTI § 1.3.1) — a
            // plain blocking submit only respects SNDTIMEO and would fail.
            for (let i = 0; i < dealers.length; i += 1) {
                while (!trySocketSend(dealers[i], STOP_TOKEN_BYTES)) {
                    const ready = poller.waitMany(poller.size, -1);
                    for (const event of ready) {
                        if (!pollEventHas(event, POLLOUT)) {
                            continue;
                        }
                        const index = event.tag ?? event.userData;
                        if (Number.isInteger(index)) {
                            pending[index] = false;
                        }
                    }
                }
            }
        }
        finally {
            poller.close();
        }
        console.log(`CLIENT_DONE,${options.msgSize}`);
    }
    finally {
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
