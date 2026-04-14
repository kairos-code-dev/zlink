// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist/canonical');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeader, currentEpochNs, sleepImmediate, summarizeMetrics, stampPayload } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { waitForConnectionReady } = require('./perf_multi_runtime');
const DEBUG = process.env.PERF_DEBUG === '1';
function tryDealerSend(dealer, payload) {
    try {
        dealer.send(payload, zlink.SendFlags.DontWait);
        return true;
    }
    catch (error) {
        if (error instanceof zlink.SubmitError
            && error.result === zlink.SubmitResult.Backpressured) {
            return false;
        }
        throw error;
    }
}
function tryDealerRecv(dealer) {
    try {
        return dealer.recv(zlink.RecvFlags.DontWait);
    }
    catch (error) {
        if (error instanceof zlink.RecvError
            && error.result === zlink.RecvResult.NoData) {
            return null;
        }
        throw error;
    }
}
async function drainPendingReplies(dealers, waiting, onReceive, deadlineMs) {
    while (waiting.some((value) => value) && Date.now() < deadlineMs) {
        let progressed = false;
        for (let i = 0; i < dealers.length; i += 1) {
            const echoed = tryDealerRecv(dealers[i]);
            if (!echoed) {
                continue;
            }
            waiting[i] = false;
            progressed = true;
            onReceive(i, echoed);
        }
        if (!progressed) {
            await sleepImmediate();
        }
    }
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    const runId = createRunId();
    const collector = createMetricCollector({
        runId,
        msgSize: options.msgSize
    });
    const dealers = [];
    const warmupPayloads = [];
    const activePayloads = [];
    const waiting = [];
    const ready = [];
    let warmupSendCount = 0;
    let warmupRecvCount = 0;
    let activeSendCount = 0;
    let activeRecvCount = 0;
    let activeHeaderLogCount = 0;
    try {
        for (let i = 0; i < options.clients; i += 1) {
            const dealer = new zlink.DealerSocket(ctx);
            dealer.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from(`CLIENT-${i}`, 'ascii')));
            dealers.push(dealer);
            warmupPayloads.push(createPayload(options.msgSize));
            activePayloads.push(createPayload(options.msgSize));
            waiting.push(false);
            ready.push(false);
        }
        for (const dealer of dealers) {
            await waitForConnectionReady(dealer, () => dealer.connect(options.endpoint));
        }
        await new Promise((resolve) => setTimeout(resolve, 100));
        let seq = 1n;
        const readyDeadline = Date.now() + 10_000;
        while (ready.some((value) => !value) && Date.now() < readyDeadline) {
            for (let i = 0; i < dealers.length; i += 1) {
                if (ready[i] || waiting[i]) {
                    continue;
                }
                stampPayload(warmupPayloads[i], { phase: 0, runId, msgSize: options.msgSize, seq });
                if (!tryDealerSend(dealers[i], warmupPayloads[i])) {
                    continue;
                }
                waiting[i] = true;
                seq += 1n;
            }
            for (let i = 0; i < dealers.length; i += 1) {
                const echoed = tryDealerRecv(dealers[i]);
                if (!echoed) {
                    continue;
                }
                waiting[i] = false;
                ready[i] = true;
            }
            await sleepImmediate();
        }
        if (ready.some((value) => !value)) {
            throw new Error('dealer-router client ready gate timed out');
        }
        console.log('CLIENT_READY');
        const warmupUntilNs = process.hrtime.bigint()
            + BigInt(Math.floor(options.warmup * 1_000_000_000));
        while (process.hrtime.bigint() < warmupUntilNs) {
            for (let i = 0; i < dealers.length; i += 1) {
                if (waiting[i]) {
                    continue;
                }
                stampPayload(warmupPayloads[i], { phase: 0, runId, msgSize: options.msgSize, seq });
                if (!tryDealerSend(dealers[i], warmupPayloads[i])) {
                    continue;
                }
                waiting[i] = true;
                warmupSendCount += 1;
                if (DEBUG && seq <= 4n) {
                    console.error(`dealer-router-client warmup send seq=${seq.toString()} slot=${i}`);
                }
                seq += 1n;
            }
            for (let i = 0; i < dealers.length; i += 1) {
                const echoed = tryDealerRecv(dealers[i]);
                if (!echoed) {
                    continue;
                }
                waiting[i] = false;
                warmupRecvCount += 1;
                if (DEBUG && seq <= 8n) {
                    console.error(`dealer-router-client warmup recv slot=${i}`);
                }
            }
            await sleepImmediate();
        }
        await drainPendingReplies(dealers, waiting, () => { }, Date.now() + 2_000);
        const stopAtNs = process.hrtime.bigint()
            + BigInt(Math.floor(options.duration * 1_000_000_000));
        while (process.hrtime.bigint() < stopAtNs) {
            for (let i = 0; i < dealers.length; i += 1) {
                if (waiting[i]) {
                    continue;
                }
                stampPayload(activePayloads[i], { phase: 1, runId, msgSize: options.msgSize, seq });
                if (!tryDealerSend(dealers[i], activePayloads[i])) {
                    continue;
                }
                waiting[i] = true;
                activeSendCount += 1;
                if (DEBUG && seq <= 8n) {
                    console.error(`dealer-router-client active send seq=${seq.toString()} slot=${i}`);
                }
                seq += 1n;
            }
            for (let i = 0; i < dealers.length; i += 1) {
                const echoed = tryDealerRecv(dealers[i]);
                if (!echoed) {
                    continue;
                }
                waiting[i] = false;
                activeRecvCount += 1;
                if (DEBUG && seq <= 16n) {
                    console.error(`dealer-router-client active recv slot=${i}`);
                }
                const header = decodeMetricHeader(echoed.parts[0].data());
                if (DEBUG && activeHeaderLogCount < 8) {
                    activeHeaderLogCount += 1;
                    console.error(`dealer-router-client active header slot=${i} phase=${header ? header.phase : 'null'} runId=${header ? header.runId : 'null'} seq=${header ? header.seq.toString() : 'null'}`);
                }
                collector.record(header, currentEpochNs());
            }
            await sleepImmediate();
        }
        await drainPendingReplies(dealers, waiting, (_slot, echoed) => {
            collector.record(decodeMetricHeader(echoed.parts[0].data()), currentEpochNs());
        }, Date.now() + 2_000);
        const result = await collector.finish();
        if (DEBUG) {
            console.error(`dealer-router-client summary warmupSend=${warmupSendCount} warmupRecv=${warmupRecvCount} activeSend=${activeSendCount} activeRecv=${activeRecvCount} accepted=${result.accepted} rejected=${result.rejected} latencies=${result.latenciesNs.length}`);
        }
        const lines = summarizeMetrics('MULTI_DEALER_ROUTER', 'tcp', options.msgSize, result.latenciesNs, options.duration);
        for (const line of lines) {
            console.log(line);
        }
    }
    finally {
        await collector.close();
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
