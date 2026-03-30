// SPDX-License-Identifier: MPL-2.0

import { SocketType } from './constants';
import { SendSocket } from './send_socket';
import { DuplexSocket } from './duplex_socket';
import { SubscriberSocket } from './subscriber_socket';
import { RecvSocket } from './recv_socket';
import { normalizeMessagePayload, normalizeMultipart } from './socket_support';
import { requireNative } from '../native';
import type { Context } from '../index';
import type { MessageLike } from '../message';
import type { BufferLike } from '../buffer_like';

export class PubSocket extends SendSocket {
  constructor(ctx: Context) {
    super(ctx, SocketType.PUB);
  }
}

export class XPubSocket extends SendSocket {
  constructor(ctx: Context) {
    super(ctx, SocketType.XPUB);
  }
}

export class PairSocket extends DuplexSocket {
  constructor(ctx: Context) {
    super(ctx, SocketType.PAIR);
  }
}

export class DealerSocket extends DuplexSocket {
  constructor(ctx: Context) {
    super(ctx, SocketType.DEALER);
  }
}

export class RouterSocket extends RecvSocket {
  constructor(ctx: Context) {
    super(ctx, SocketType.ROUTER);
  }

  send(_: MessageLike, __?: number): never {
    throw new Error('RouterSocket.send is not available on the canonical API. Use sendTo(routingId, message).');
  }

  sendParts(_: readonly MessageLike[], __?: number): never {
    throw new Error('RouterSocket.sendParts is not available on the canonical API. Use sendPartsTo(routingId, parts).');
  }

  sendFrom(_: BufferLike, __: number, ___?: number): never {
    throw new Error('RouterSocket.sendFrom is not available on the canonical API. Use sendTo(routingId, message).');
  }

  sendTo(routingId: BufferLike, message: MessageLike, flags = 0): number {
    return requireNative().socketSendParts(
      this.nativeHandle(),
      [normalizeMessagePayload(routingId, 'routingId'), normalizeMessagePayload(message)],
      flags | 0
    ) as number;
  }

  sendPartsTo(routingId: BufferLike, parts: readonly MessageLike[], flags = 0): number {
    return requireNative().socketSendParts(
      this.nativeHandle(),
      [normalizeMessagePayload(routingId, 'routingId'), ...normalizeMultipart(parts)],
      flags | 0
    ) as number;
  }
}

export class StreamSocket extends DuplexSocket {
  constructor(ctx: Context) {
    super(ctx, SocketType.STREAM);
  }
}

export class SubSocket extends SubscriberSocket {
  constructor(ctx: Context) {
    super(ctx, SocketType.SUB);
  }
}

export class XSubSocket extends SubscriberSocket {
  constructor(ctx: Context) {
    super(ctx, SocketType.XSUB);
  }
}
