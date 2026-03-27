// SPDX-License-Identifier: MPL-2.0

'use strict';

const { requireNative } = require('../native');

class MonitorSocket {
  constructor(handle) {
    this._native = handle;
  }

  recv() {
    return requireNative().monitorRecv(this._native);
  }

  snapshot() {
    return requireNative().monitorSnapshot(this._native);
  }

  close() {
    if (!this._native) return;
    requireNative().monitorClose(this._native);
    this._native = null;
  }
}

module.exports = {
  MonitorSocket
};
