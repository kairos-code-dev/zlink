// SPDX-License-Identifier: MPL-2.0

import type { SubscriptionEvent, TopicMessage } from '../messaging';
import type { Discovery } from '../service';
import type { RecvFlags } from './socket_constants';
import type { SendOp } from './socket_operations';
import type { PubSocketOptions, SubSocketOptions } from './socket_options';
import type { Socket } from './socket';

export interface PubSocket extends Socket {
  readonly options: PubSocketOptions;
  publish(topic: string): SendOp;
  setSendReadyHandler(handler: () => void): void;
  attachDiscovery(discovery: Discovery): void;
}

export interface XPubSocket extends PubSocket {
  receiveSubscriptionEvent(result: SubscriptionEvent, flags?: RecvFlags): boolean;
}

export interface SubSocket extends Socket {
  readonly options: SubSocketOptions;
  setSubscription(filter: string): void;
  unsetSubscription(filter: string): void;
  subscriptionAt(index: number): string;
  subscribe(result: TopicMessage, flags?: RecvFlags): boolean;
  attachDiscovery(discovery: Discovery): void;
}

export interface XSubSocket extends SubSocket {}
