// SPDX-License-Identifier: MPL-2.0

import type {
  NativeReceivedRaw,
  NativeTopicMessageRaw
} from '../messaging/message_materializer';
import type {
  MonitorEventValueRaw,
  MonitorStatusRaw
} from '../eventing/monitor_raw';
import type {
  SubscriptionEntry
} from '../../contracts/service';
import type {
  ActorJoinEntrySpotResultRaw,
  ActorJoinResultRaw,
  ActorLookupResultRaw,
  ActorPartRaw,
  ActorRefRaw,
  SpotNodeActorEntryRaw,
  SpotNodeSpotEntryRaw
} from '../service/spot/actor_models';
import type {
  SpotActorJoinRecvRaw,
  SpotActorLifecycleRaw,
  SpotDispatchRaw,
  SpotNodePeerEntryRaw,
  SpotNodeSocketEntryRaw,
  SpotNodeSpotGetOrNewRaw,
  SpotNodeStatusRaw,
  SpotNodeSubjectEntryRaw,
  SpotRoutedRaw
} from '../service/spot/spot_raw_models';

export type NativeHandle = unknown;
export type NativeBuffer = Buffer;
export type NullableNativeHandle = NativeHandle | null;
export type NativeVersion = [number, number, number];
export type NativeRequestCallback = (
  result: number,
  replyParts: Buffer[] | null
) => void;
export type NativeActorJoinCallback = (
  result: ActorJoinResultRaw | null,
  replyParts: Buffer[] | null
) => void;
export type NativeActorEntryJoinCallback = (
  result: ActorJoinEntrySpotResultRaw | null,
  replyParts: Buffer[] | null
) => void;
export type NativeActorLookupCallback = (result: ActorLookupResultRaw) => void;

export type {
  ActorJoinEntrySpotResultRaw,
  ActorJoinResultRaw,
  ActorLookupResultRaw,
  ActorPartRaw,
  ActorRefRaw,
  MonitorEventValueRaw,
  MonitorStatusRaw,
  NativeReceivedRaw,
  NativeTopicMessageRaw,
  SpotActorJoinRecvRaw,
  SpotActorLifecycleRaw,
  SpotDispatchRaw,
  SpotNodeActorEntryRaw,
  SpotNodePeerEntryRaw,
  SpotNodeSocketEntryRaw,
  SpotNodeSpotEntryRaw,
  SpotNodeSpotGetOrNewRaw,
  SpotNodeStatusRaw,
  SpotNodeSubjectEntryRaw,
  SpotRoutedRaw,
  SubscriptionEntry
};
