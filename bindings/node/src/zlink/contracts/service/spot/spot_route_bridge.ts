// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../../core';
import type { MessageLike } from '../../messaging';
import type { RouterSocket } from '../../sockets';
import type { RequestOperation, SendOperation } from './spot_operations';

export const SpotRouteBridgeEndpointCapabilities = {
  SpotRoute: 0x00000001,
  RouteOnly: 0x00000001
} as const;

export type SpotRouteBridgeEndpointCapabilitiesValue =
  typeof SpotRouteBridgeEndpointCapabilities[keyof typeof SpotRouteBridgeEndpointCapabilities];

export interface SpotRouteBridgeEndpointOptions {
  readonly capabilities?: SpotRouteBridgeEndpointCapabilitiesValue | number;
}

export interface SpotRouteBridge {
  attachRouterChannel(channelName: string, router: RouterSocket, options?: SpotRouteBridgeEndpointOptions): void;
  send(channelName: string, targetNodeRid: RoutingId, targetSpotRid: RoutingId): SendOperation;
  request(channelName: string, targetNodeRid: RoutingId, targetSpotRid: RoutingId): RequestOperation;
  handleRouterReceived(
    channelName: string,
    sourceNodeRid: RoutingId,
    requestSeq: bigint | number,
    parts: MessageLike | readonly MessageLike[]
  ): boolean;
  close(): void;
}

export interface SpotNodePublisher {
  publish(topic: string): SendOperation;
  close(): void;
}
