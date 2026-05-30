// SPDX-License-Identifier: MPL-2.0

import type {
  MemberPeerEntry,
  RegistryServiceSummaryEntry,
  RegistryServiceSummaryFilter,
  RegistryStatus,
  RegistryTopologyEntry,
  RegistryTopologyFilter,
} from '../index';

/** A service registry that members publish to and peers query for topology. */
export interface Registry {
  /** Bind the registry's publish and router endpoints so members and peers can connect. */
  bind(pubEndpoint: string, routerEndpoint: string): void;
  /** Set the registry's identifier. */
  setId(id: number): void;
  /** Add a peer registry to federate with, by its publish endpoint. */
  addPeer(pubEndpoint: string): void;
  /** Set the member heartbeat interval and the drop timeout, both in ms. */
  setHeartbeat(intervalMs: number, timeoutMs: number): void;
  /** Set how often, in ms, the registry broadcasts its state. */
  setBroadcastInterval(intervalMs: number): void;
  /** Configure the registry as a TLS server; apply before `bind`. */
  setTlsServer(cert: string, key: string, requireClientCert?: boolean): void;
  /** Configure TLS for connections to peer registries. */
  setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
  /** Return a snapshot of the registry's current status. */
  status(): RegistryStatus;
  /** Return a per-service rollup of registered endpoints, optionally filtered. */
  serviceSummary(filter?: RegistryServiceSummaryFilter): RegistryServiceSummaryEntry[];
  /** Return the registry's topology entries, optionally filtered. */
  topology(filter?: RegistryTopologyFilter): RegistryTopologyEntry[];
  /** Return the member peers registered on `channelName`. */
  memberPeers(channelName: string): MemberPeerEntry[];
  /** Close the registry and release its resources. */
  close(): void;
}
