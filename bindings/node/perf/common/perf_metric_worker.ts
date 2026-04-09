// SPDX-License-Identifier: MPL-2.0

'use strict';

const { parentPort, workerData } = require('node:worker_threads');

const latenciesNs = [];
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
    if (message.phase !== 1) {
      return;
    }
    const receivedAtNs = BigInt(message.receivedAtNs);
    const sentTsNs = BigInt(message.sentTsNs);
    accepted += 1;
    latenciesNs.push(Number(receivedAtNs - sentTsNs));
    return;
  }

  if (message.type === 'finish') {
    parentPort.postMessage({
      latenciesNs,
      accepted,
      rejected
    });
  }
});
