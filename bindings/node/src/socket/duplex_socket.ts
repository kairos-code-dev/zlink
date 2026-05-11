// SPDX-License-Identifier: MPL-2.0

import { SendSocket } from './send_socket';
import { recvInto, recvMessage, recvMsgInto } from './socket_support';
import type { BufferLike } from '../buffer_like';
import { Received } from '../message';

export class DuplexSocket extends SendSocket {
  protected constructor(ctx: import('../index').Context, type: number) {
    super(ctx, type);
  }

  /**
   * Canonical caller-provided storage recv. See doc/spec/bindings/README.md.
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
