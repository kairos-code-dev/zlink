// SPDX-License-Identifier: MPL-2.0

import { BaseSocket } from './base_socket';
import { recvInto, recvMessage, recvMsgInto } from './socket_support';
import type { BufferLike } from '../buffer_like';
import { Received } from '../message';

export class RecvSocket extends BaseSocket {
  protected constructor(ctx: import('../index').Context, type: number) {
    super(ctx, type);
  }

  /**
   * Canonical caller-provided storage recv. Pass a long-lived {@link Received}
   * and the binding refills its internal state in place each successful call.
   * Returns true on success, false when DONT_WAIT finds no data. See
   * doc/spec/bindings/README.md "Canonical Recv: Caller-Provided Storage".
   */
  recv(result: Received, flags: number = 0): boolean {
    const fresh = recvMessage(this, flags);
    if (fresh == null || !(fresh instanceof Received)) return false;
    (result as Received & { _adoptFrom: (s: Received) => void })._adoptFrom(fresh);
    return true;
  }

  recvInto(buffer: BufferLike, flags = 0): number {
    return recvInto(this, buffer, flags);
  }

  recvMsgInto(buffer: BufferLike, flags = 0): number {
    return recvMsgInto(this, buffer, flags);
  }
}
