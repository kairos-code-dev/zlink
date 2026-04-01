// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const { parentPort, workerData } = require('node:worker_threads');
const latenciesUs = [];
const runId = workerData.runId >>> 0;
const msgSize = workerData.msgSize >>> 0;
let accepted = 0;
let rejected = 0;
parentPort.on('message', (message) => {
    if (message.type === 'sample') {
        if ((message.runId >>> 0) !== runId || (message.msgSize >>> 0) !== msgSize) {
            rejected += 1;
            return;
        }
        if (message.phase !== 0) {
            return;
        }
        const receivedAtNs = BigInt(message.receivedAtNs);
        const sentAtNs = BigInt(message.sentAtNs);
        accepted += 1;
        latenciesUs.push(Number(receivedAtNs - sentAtNs) / 1000);
        return;
    }
    if (message.type === 'finish') {
        parentPort.postMessage({
            latenciesUs,
            accepted,
            rejected
        });
    }
});
