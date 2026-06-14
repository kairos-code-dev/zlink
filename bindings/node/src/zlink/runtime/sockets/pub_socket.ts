// SPDX-License-Identifier: MPL-2.0

import { PubSocketOptions } from './socket_options';
import {
  PublisherSocket,
} from './socket_operations';
import { configureSocketChannelName } from './socket_base';
import type { RuntimeContext as Context } from '../core/context';
import { configCall, handlerCall } from '../errors/native_errors';
import { getNativeHandle, NativeHandle } from '../handles/native_handle';
import { requireNative } from '../native/native';
import { SocketType as NativeSocketType } from '../../contracts/sockets/socket_constants';
import type { SocketSendReadyHandler } from '../../contracts/service';

export class PubSocket extends PublisherSocket {
  readonly options: PubSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.PUB); this.options = PubSocketOptions.create(this); }
  setChannelName(channelName: string): void {
    configureSocketChannelName(this, channelName);
  }
  setSendReadyHandler(handler: SocketSendReadyHandler): void {
    handlerCall('send-ready handler registration failed', () => {
      requireNative().socketSendReadyHandler(getNativeHandle(this), handler);
    });
  }
  attachDiscovery(discovery: NativeHandle): void {
    configCall('socket discovery attachment failed', () => {
      requireNative().socketAttachDiscovery(getNativeHandle(this), getNativeHandle(discovery));
    });
  }
}
