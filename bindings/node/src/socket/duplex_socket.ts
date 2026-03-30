// SPDX-License-Identifier: MPL-2.0

import { SendSocket } from './send_socket';
import { recvInto, recvMessage, recvMsgInto } from './socket_support';
import type { BufferLike } from '../buffer_like';
import type { Received } from '../message';

export class DuplexSocket extends SendSocket {
  protected constructor(ctx: import('../index').Context, type: number) {
    super(ctx, type);
  }

  recv(flags?: number): Received;
  recv(size: number, flags: number): Buffer;
  recv(arg0 = 0, arg1?: number): Received | Buffer {
    return recvMessage(this, arg0, arg1);
  }

  recvInto(buffer: BufferLike, flags = 0): number {
    return recvInto(this, buffer, flags);
  }

  recvMsgInto(buffer: BufferLike, flags = 0): number {
    return recvMsgInto(this, buffer, flags);
  }
}
