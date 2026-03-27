// SPDX-License-Identifier: MPL-2.0

'use strict';

const { requireNative } = require('../native');
const { NativeSocketHandle } = require('./native_socket_handle');
const { MonitorSocket } = require('./monitor_socket');
const { SocketOption } = require('./constants');
const { normalizeBufferLike } = require('./socket_support');

class BaseSocket {
  constructor(ctx, type) {
    this._handle = new NativeSocketHandle(requireNative().socketNew(ctx._native, type));
    this._socketType = type;
  }

  get _native() {
    return this._handle.value();
  }

  bind(endpoint) {
    requireNative().socketBind(this._native, endpoint);
  }

  connect(endpoint) {
    requireNative().socketConnect(this._native, endpoint);
  }

  setSockOpt(option, value) {
    requireNative().socketSetOpt(this._native, option | 0, normalizeBufferLike(value, 'value'));
  }

  getSockOpt(option) {
    return requireNative().socketGetOpt(this._native, option | 0);
  }

  setOption(option, value) {
    this.setSockOpt(option, value);
  }

  getOption(option) {
    return this.getSockOpt(option);
  }

  setRoutingId(routingId) {
    this.setSockOpt(SocketOption.ROUTING_ID, routingId);
  }

  getRoutingId() {
    return this.getSockOpt(SocketOption.ROUTING_ID);
  }

  monitorOpen(events) {
    return new MonitorSocket(requireNative().monitorOpen(this._native, events | 0));
  }

  close() {
    this._handle.close();
  }
}

module.exports = {
  BaseSocket
};
