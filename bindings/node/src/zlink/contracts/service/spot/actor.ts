// SPDX-License-Identifier: MPL-2.0

import type { RecvFlags } from '../../sockets/socket_constants';
import type {
  ActorJoinOperation,
  ActorLeaveOperation,
  ActorPart,
  ActorRef,
  SendOperation,
} from '../index';
import type { Spot } from './spot';

export interface Actor {
  ref(): ActorRef;
  readonly actorRef: ActorRef;
  join(spot: Spot): ActorJoinOperation;
  leave(spot: Spot): ActorLeaveOperation;
  recvPart(flags?: RecvFlags): ActorPart | null;
  sendBoundSession(): SendOperation;
  closeBoundSession(timeoutMs?: number): void;
  close(timeoutMs?: number): void;
}
