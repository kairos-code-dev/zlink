// SPDX-License-Identifier: MPL-2.0

import { PubSocketOptions } from './socket_options';
import {
  PublisherSocket,
} from './socket_operations';
import {
  NativeSocketType,
  RecvFlags,
  SubscriptionEvent,
  handlerCall,
  requireNative,
  recvNativeError,
  wrapRoutingId,
  type Context,
  type SocketSendReadyHandler,
} from './socket_runtime_support';

export class XPubSocket extends PublisherSocket {
  readonly options: PubSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.XPUB); this.options = PubSocketOptions.create(this); }
  receiveSubscriptionEvent(result: SubscriptionEvent, flags?: RecvFlags): boolean;
  receiveSubscriptionEvent(resultOrFlags: SubscriptionEvent | RecvFlags = RecvFlags.None,
                           maybeFlags: RecvFlags = RecvFlags.None): SubscriptionEvent | null | boolean {
    const hasResult = resultOrFlags instanceof SubscriptionEvent;
    const flags = hasResult ? maybeFlags : resultOrFlags as RecvFlags;
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().socketTrySubscriptionEvent(this.nativeHandle()) as { routingId?: Buffer | null; topic: string; subscribed: boolean } | null
        : requireNative().socketSubscriptionEvent(this.nativeHandle(), flags | 0) as { routingId?: Buffer | null; topic: string; subscribed: boolean } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'subscription event recv failed');
    }
    if (!raw) {
      return hasResult ? false : null;
    }
    const event = SubscriptionEvent.create(raw.topic, raw.subscribed, wrapRoutingId(raw.routingId ?? null));
    if (hasResult) {
      resultOrFlags.adoptFrom(event);
      return true;
    }
    return event;
  }
  setSendReadyHandler(handler: SocketSendReadyHandler): void {
    handlerCall('send-ready handler registration failed', () => {
      requireNative().socketSendReadyHandler(this.nativeHandle(), handler);
    });
  }
}
