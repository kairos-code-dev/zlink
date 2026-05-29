// SPDX-License-Identifier: MPL-2.0

import { PubSocketOptions } from './socket_options';
import {
  PublisherSocket,
} from './socket_operations';
import {
  NativeSocketType,
  configCall,
  handlerCall,
  requireNative,
  type Context,
  type SocketSendReadyHandler,
} from './socket_runtime_support';

export class PubSocket extends PublisherSocket {
  readonly options: PubSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.PUB); this.options = PubSocketOptions.create(this); }
  setSendReadyHandler(handler: SocketSendReadyHandler): void {
    handlerCall('send-ready handler registration failed', () => {
      requireNative().socketSendReadyHandler(this.nativeHandle(), handler);
    });
  }
  attachDiscovery(discovery: { nativeHandle(): unknown }): void {
    configCall('socket discovery attachment failed', () => {
      requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle());
    });
  }
}
