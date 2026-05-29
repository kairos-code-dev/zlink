// SPDX-License-Identifier: MPL-2.0

import type {
  MemberPeerEntry,
  RegistryServiceSummaryEntry,
  RegistryServiceSummaryFilter,
  RegistryStatus,
  RegistryTopologyEntry,
  RegistryTopologyFilter,
} from '../index';

export interface Registry {
  bind(pubEndpoint: string, routerEndpoint: string): void;
  setId(id: number): void;
  addPeer(pubEndpoint: string): void;
  setHeartbeat(intervalMs: number, timeoutMs: number): void;
  setBroadcastInterval(intervalMs: number): void;
  setTlsServer(cert: string, key: string, requireClientCert?: boolean): void;
  setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
  status(): RegistryStatus;
  serviceSummary(filter?: RegistryServiceSummaryFilter): RegistryServiceSummaryEntry[];
  topology(filter?: RegistryTopologyFilter): RegistryTopologyEntry[];
  memberPeers(channelName: string): MemberPeerEntry[];
  close(): void;
}
