// SPDX-License-Identifier: MPL-2.0

import type { RecvFlags } from '../../sockets/socket_constants';
import type {
  ActorJoinOp,
  ActorLeaveOp,
  ActorPart,
  ActorRef,
  SendOp,
} from '../index';
import type { Spot } from './spot';

export interface Actor {
  ref(): ActorRef;
  readonly actorRef: ActorRef;
  join(spot: Spot): ActorJoinOp;
  leave(spot: Spot): ActorLeaveOp;
  recvPart(flags?: RecvFlags): ActorPart | null;
  sendBoundSession(): SendOp;
  closeBoundSession(timeoutMs?: number): void;
  close(timeoutMs?: number): void;
}
