// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../../core';
import type {
  ActorRoute,
  MemberPeerEntry,
  SpotRoute,
} from '../index';

/** A discovery service that learns peer routes from a registry and resolves spots and actors for a fixed channel. */
export interface Discovery {
  /** The auto-connect topology this service uses. */
  readonly autoConnectType: number;
  /** The logical channel name this service serves. */
  readonly channelName: string;
  /** Connect to a registry's publish endpoint to receive route updates. */
  connectRegistry(endpoint: string): void;
  /** Set the application-defined value advertised for this peer. */
  setValue(value: number): void;
  /** Return the application-defined value advertised for this peer. */
  getValue(): number;
  /** Resolve the route to the spot identified by `spotRid`. */
  resolveSpot(spotRid: RoutingId): SpotRoute;
  /** Resolve the route to the actor identified by `actorId`. */
  resolveActor(actorId: string): ActorRoute;
  /** Whether spot ownership changes are synchronized across peers. */
  spotOwnerSyncEnabled: boolean;
  /** Whether actor routes are synchronized across peers. */
  actorRouteSyncEnabled: boolean;
  /** Return the registry member peers currently known to this service. */
  memberPeers(): MemberPeerEntry[];
  /** Configure TLS for the registry connection; apply before `connectRegistry`. */
  setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
  /** Close the discovery service and release its resources. */
  close(): void;
}
