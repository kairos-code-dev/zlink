// SPDX-License-Identifier: MPL-2.0

'use strict';

const { requireNative } = require('../native');

class NativeSocketHandle {
  constructor(nativeHandle, own = true) {
    this._native = nativeHandle;
    this._own = own === true;
  }

  value() {
    return this._native;
  }

  streamDetach() {
    if (!this._native) return;
    requireNative().socketStreamDetach(this._native);
  }

  close() {
    if (!this._native) return;
    try {
      this.streamDetach();
    } catch (_) {}
    if (this._own) requireNative().socketClose(this._native);
    this._native = null;
  }
}

module.exports = {
  NativeSocketHandle
};
