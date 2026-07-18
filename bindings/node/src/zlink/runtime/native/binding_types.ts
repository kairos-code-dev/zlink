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
  ActorRefRaw
} from './binding_service_types';

export type NativeHandle = unknown;
export type NativeBuffer = Buffer;
export type NullableNativeHandle = NativeHandle | null;
export type NativeVersion = [number, number, number];
export type NativeRequestCallback = (
  result: number,
  replyParts: Buffer[] | null
) => void;

export type {
  ActorRefRaw,
  MonitorEventValueRaw,
  MonitorStatusRaw,
  NativeReceivedRaw,
  NativeTopicMessageRaw,
  SubscriptionEntry
};
