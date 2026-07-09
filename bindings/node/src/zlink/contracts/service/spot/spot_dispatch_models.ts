// SPDX-License-Identifier: MPL-2.0

import type { Received } from '../../messaging';
import type { Timer } from '../../eventing';
import type { RecvFlags } from '../../sockets/socket_constants';
import type { ActorPart, ActorRef } from './actor_models';

/** The kind of readable event surfaced by a spot dispatch. */
export const SpotDispatchEvent = Object.freeze({
  SubscribeReadable: 1,
  RoutedReadable: 2,
  TimerReadable: 3,
  ChannelReplyReadable: 4,
  ActorReadable: 5,
  ActorJoinReadable: 6,
  ActorLifecycleReadable: 7
} as const);

/** The kind of readable event surfaced by a spot dispatch. */
export type SpotDispatchEvent = typeof SpotDispatchEvent[keyof typeof SpotDispatchEvent];

/** What kind of subject a spot dispatch event concerns. */
export const SpotDispatchSubjectKind = Object.freeze({
  Spot: 1,
  Timer: 2,
  ChannelDealer: 3,
  Actor: 4
} as const);

/** What kind of subject a spot dispatch event concerns. */
export type SpotDispatchSubjectKind = typeof SpotDispatchSubjectKind[keyof typeof SpotDispatchSubjectKind];

/** One active subscription: a topic filter and whether it is a pattern. */
export interface SubscriptionEntry {
  readonly filter: string;
  readonly isPattern: boolean;
}

/** The event and context passed to an on-dispatch-event callback. */
export interface SpotDispatchInfo {
  readonly event: SpotDispatchEvent;
  readonly subjectKind: SpotDispatchSubjectKind;
  readonly timer: Timer | null;
  readonly actorRef: ActorRef | null;
  readonly routed: Received | null;
  recvActorPart(flags?: RecvFlags): ActorPart | null;
}

/** Invoked for each spot dispatch event. */
export type SpotDispatchEventHandler = (info: SpotDispatchInfo) => void;
