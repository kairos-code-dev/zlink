// SPDX-License-Identifier: MPL-2.0

import type { Received } from '../messaging';
import type { SendOperation } from '../messaging';
import type { RecvFlags } from './socket_constants';
import type { CommonSocketOptions } from './socket_options';
import type { ConnectableSocket } from './socket';

export interface PairSocket extends ConnectableSocket {
  readonly options: CommonSocketOptions;
  send(): SendOperation;
  recv(result: Received, flags?: RecvFlags): boolean;
  setSendReadyHandler(handler: () => void): void;
}
