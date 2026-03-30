// SPDX-License-Identifier: MPL-2.0

import { RecvSocket } from './recv_socket';
import { SocketOption } from './constants';
import { normalizeBufferLike } from './socket_support';
import type { BufferLike } from '../buffer_like';

export class SubscriberSocket extends RecvSocket {
  protected constructor(ctx: import('../index').Context, type: number) {
    super(ctx, type);
  }

  subscribe(filter: BufferLike | string): void {
    this.setSockOpt(SocketOption.SUBSCRIBE, normalizeBufferLike(filter, 'filter'));
  }

  unsubscribe(filter: BufferLike | string): void {
    this.setSockOpt(SocketOption.UNSUBSCRIBE, normalizeBufferLike(filter, 'filter'));
  }
}
