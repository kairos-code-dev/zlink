// SPDX-License-Identifier: MPL-2.0

'use strict';

function callbackSendBurstLimit(msgSize) {
  const normalized = Math.max(1, Math.floor(262144 / Math.max(1, msgSize)));
  return Math.max(1, Math.min(256, normalized));
}

function callbackDrainTicks(msgSize) {
  return msgSize >= 65536 ? 8 : 1;
}

module.exports = {
  callbackDrainTicks,
  callbackSendBurstLimit
};
