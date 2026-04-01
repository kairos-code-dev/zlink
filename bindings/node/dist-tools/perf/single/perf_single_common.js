// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const { createMetricCollector, createPayload, createRunId, decodeMetricHeader, sleepImmediate, stampPayload } = require('../common/perf_metrics');
function attachCallbackCollector(registerHandler, msgSize, options, extractBuffer) {
    const startedAtNs = process.hrtime.bigint();
    const runId = createRunId();
    const collector = createMetricCollector({
        runId,
        msgSize
    });
    registerHandler((...args) => {
        const messageBuffer = extractBuffer(...args);
        const header = decodeMetricHeader(messageBuffer);
        collector.record(header, process.hrtime.bigint());
    });
    return {
        collector,
        payload: createPayload(msgSize),
        runId,
        msgSize,
        warmupUntilNs: startedAtNs
            + BigInt(Math.floor(options.warmup * 1_000_000_000)),
        stopAtNs: startedAtNs
            + BigInt(Math.floor((options.warmup + options.duration) * 1_000_000_000))
    };
}
async function driveSender(sendOnce, state) {
    let turns = 0;
    while (process.hrtime.bigint() < state.stopAtNs) {
        for (let i = 0; i < 256 && process.hrtime.bigint() < state.stopAtNs; i += 1) {
            stampPayload(state.payload, {
                phase: process.hrtime.bigint() < state.warmupUntilNs ? 2 : 0,
                runId: state.runId,
                msgSize: state.msgSize
            });
            const result = sendOnce(state.payload);
            if (result === false) {
                break;
            }
        }
        turns += 1;
        if ((turns & 0x03) === 0) {
            await sleepImmediate();
        }
    }
}
async function finishCollector(state) {
    const result = await state.collector.finish();
    return result.latenciesUs;
}
module.exports = {
    attachCallbackCollector,
    driveSender,
    finishCollector
};
