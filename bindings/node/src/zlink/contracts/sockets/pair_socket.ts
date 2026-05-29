// SPDX-License-Identifier: MPL-2.0

import type { Received } from '../messaging';
import type { RecvFlags } from './socket_constants';
import type { SendOp } from './socket_operations';
import type { CommonSocketOptions } from './socket_options';
import type { Socket } from './socket';

export interface PairSocket extends Socket {
  readonly options: CommonSocketOptions;
  send(): SendOp;
  recv(result: Received, flags?: RecvFlags): boolean;
  setSendReadyHandler(handler: () => void): void;
}
