// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../../core';
import type { MessageLike } from '../../messaging';
import type { DealerSocket, RouterSocket } from '../../sockets';
import type { RequestOperation, SendOperation } from './spot_operations';

export const SpotRouteBridgeEndpointCapabilities = {
  SpotRoute: 0x00000001,
  ChannelInbound: 0x00000002,
  RouteOnly: 0x00000001,
  RouteWithChannelInbound: 0x00000003
} as const;

export type SpotRouteBridgeEndpointCapabilitiesValue =
  typeof SpotRouteBridgeEndpointCapabilities[keyof typeof SpotRouteBridgeEndpointCapabilities];

export interface SpotRouteBridgeEndpointOptions {
  readonly capabilities?: SpotRouteBridgeEndpointCapabilitiesValue | number;
}

export interface SpotRouteBridge {
  attachDealerChannel(channelName: string, dealer: DealerSocket, options?: SpotRouteBridgeEndpointOptions): void;
  attachRouterChannel(channelName: string, router: RouterSocket, options?: SpotRouteBridgeEndpointOptions): void;
  setTargetNode(channelName: string, targetNodeRid: RoutingId): void;
  send(channelName: string, targetSpotRid: RoutingId): SendOperation;
  request(channelName: string, targetSpotRid: RoutingId): RequestOperation;
  handleRouterReceived(
    channelName: string,
    sourceNodeRid: RoutingId,
    parts: MessageLike | readonly MessageLike[],
    requestSeq?: bigint | number
  ): boolean;
  close(): void;
}

export interface SpotNodePublisher {
  publish(topic: string): SendOperation;
  close(): void;
}
