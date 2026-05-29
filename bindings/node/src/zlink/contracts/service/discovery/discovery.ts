// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../../core';
import type {
  ActorRoute,
  MemberPeerEntry,
  SpotRoute,
} from '../index';

export interface Discovery {
  readonly autoConnectType: number;
  readonly channelName: string;
  connectRegistry(endpoint: string): void;
  setValue(value: number): void;
  getValue(): number;
  resolveSpot(spotRid: RoutingId): SpotRoute;
  resolveActor(actorId: string): ActorRoute;
  spotOwnerSyncEnabled: boolean;
  actorRouteSyncEnabled: boolean;
  memberPeers(): MemberPeerEntry[];
  setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
  close(): void;
}
