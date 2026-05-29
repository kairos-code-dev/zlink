// SPDX-License-Identifier: MPL-2.0

import { PubSocketOptions } from './socket_options';
import {
  NativeSocketType,
  PublisherSocket,
  requireNative,
  configCall,
  handlerCall,
  type Context,
  type SocketSendReadyHandler,
} from './socket_operations';

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
