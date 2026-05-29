// SPDX-License-Identifier: MPL-2.0

import type { SubscriptionEvent, TopicMessage } from '../messaging';
import type { SendOperation } from '../messaging';
import type { SubscriptionEntry } from '../service';
import type { Discovery } from '../service';
import type { RecvFlags } from './socket_constants';
import type { PubSocketOptions, SubSocketOptions } from './socket_options';
import type { ConnectableSocket } from './socket';

export interface PubSocket extends ConnectableSocket {
  readonly options: PubSocketOptions;
  publish(topic: string): SendOperation;
  setSendReadyHandler(handler: () => void): void;
  attachDiscovery(discovery: Discovery): void;
}

export interface XPubSocket extends PubSocket {
  receiveSubscriptionEvent(result: SubscriptionEvent, flags?: RecvFlags): boolean;
}

export interface SubSocket extends ConnectableSocket {
  readonly options: SubSocketOptions;
  setSubscription(filter: string): void;
  unsetSubscription(filter: string): void;
  subscriptionAt(index: number): SubscriptionEntry | null;
  subscribe(result: TopicMessage, flags?: RecvFlags): boolean;
  attachDiscovery(discovery: Discovery): void;
}

export interface XSubSocket extends SubSocket {}
