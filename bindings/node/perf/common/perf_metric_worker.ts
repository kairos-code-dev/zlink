// SPDX-License-Identifier: MPL-2.0

'use strict';

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
    if (message.phase !== 1) {
      return;
    }
    const receivedAtUs = BigInt(message.receivedAtUs);
    const sentTsUs = BigInt(message.sentTsUs);
    accepted += 1;
    latenciesUs.push(Number(receivedAtUs - sentTsUs));
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
