// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../core';
import type { RequestOperation } from '../messaging';
import type { Discovery } from '../service';
import type { PairSocket } from './pair_socket';
import type { DealerSocketOptions } from './socket_options';

export interface DealerSocket extends PairSocket {
  readonly options: DealerSocketOptions;
  setChannelName(channelName: string): void;
  getChannelName(): string;
  setRoutingId(routingId: RoutingId): void;
  getRoutingId(): RoutingId;
  attachDiscovery(discovery: Discovery): void;
  request(): RequestOperation;
}
