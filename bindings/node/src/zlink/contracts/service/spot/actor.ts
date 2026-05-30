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

/** A handle to a local actor: join/leave spots, receive its messages, and send to its bound session. */
export interface Actor {
  /** Return this actor's reference. */
  ref(): ActorRef;
  /** This actor's reference. */
  readonly actorRef: ActorRef;
  /** Begin joining the actor to `spot`; submit the returned operation to apply it. */
  join(spot: Spot): ActorJoinOperation;
  /** Begin leaving `spot`; submit the returned operation to apply it. */
  leave(spot: Spot): ActorLeaveOperation;
  /** Receive the next message part for this actor, or null when none is available. */
  recvPart(flags?: RecvFlags): ActorPart | null;
  /** Begin a send to the actor's bound session; parts are consumed on a successful submit. */
  sendBoundSession(): SendOperation;
  /** Close the actor's bound session, waiting up to `timeoutMs` for it to drain. */
  closeBoundSession(timeoutMs?: number): void;
  /** Close the actor, waiting up to `timeoutMs` for it to drain. */
  close(timeoutMs?: number): void;
}
