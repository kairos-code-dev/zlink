// SPDX-License-Identifier: MPL-2.0

import { RoutingId } from '../../../contracts';
import {
  AutoConnectType,
  type MemberPeerEntry,
  type RegistryServiceSummaryEntry,
  type RegistryStatus,
  type RegistryTopologyEntry,
  type RegistryTopologyFilter,
  type ServiceKindValue,
  type ServiceRoleValue,
  type SpotKindValue,
  type TopologySourceValue,
  type TopologyStateValue,
  type RegistryStateValue,
} from '../../../contracts/service';
import { normalizeRoutingId } from '../../core/routing_id';

export interface MemberPeerEntryRaw {
  autoConnectType: number;
  serviceRole: number;
  channelName: string;
  endpoint: string;
  routingId: Buffer;
  weight: number;
  value: number | bigint;
}

export interface RegistryTopologyEntryRaw {
  autoConnectType: number;
  routingId: Buffer;
  serviceKind: number;
  serviceRole: number;
  channelName: string;
  endpoint: string;
  source: number;
  state: number;
  desiredCount: number;
  readyCount: number;
  errorCode: number;
  lastReportedMs: number | bigint;
  spotKind: number;
}

export interface RegistryStatusRaw {
  registryId: number;
  bindEndpoint: string;
  state: number;
  topologyEntryCount: number;
  peerRegistryCount: number;
  connectedPeerRegistryCount: number;
  listSeq: number | bigint;
  lastError: number;
  lastChangedMs: number | bigint;
}

export interface RegistryServiceSummaryEntryRaw {
  autoConnectType: number;
  serviceRole: number;
  channelName: string;
  totalCount: number;
  connectingCount: number;
  readyCount: number;
  errorCount: number;
  stoppedCount: number;
  lastReportedMs: number | bigint;
}

export function mapMemberPeerEntry(entry: MemberPeerEntryRaw): MemberPeerEntry {
  return {
    autoConnectType: entry.autoConnectType as AutoConnectType,
    serviceRole: entry.serviceRole as ServiceRoleValue,
    channelName: entry.channelName,
    endpoint: entry.endpoint,
    routingId: RoutingId.from(entry.routingId),
    weight: entry.weight,
    value: BigInt(entry.value)
  };
}

export function mapRegistryTopologyEntry(entry: RegistryTopologyEntryRaw): RegistryTopologyEntry {
  return {
    autoConnectType: entry.autoConnectType as AutoConnectType,
    routingId: RoutingId.from(entry.routingId),
    serviceKind: entry.serviceKind as ServiceKindValue,
    serviceRole: entry.serviceRole as ServiceRoleValue,
    channelName: entry.channelName,
    endpoint: entry.endpoint,
    source: entry.source as TopologySourceValue,
    state: entry.state as TopologyStateValue,
    desiredCount: entry.desiredCount,
    readyCount: entry.readyCount,
    errorCode: entry.errorCode,
    lastReportedMs: BigInt(entry.lastReportedMs),
    spotKind: entry.spotKind as SpotKindValue
  };
}

export function mapRegistryStatus(entry: RegistryStatusRaw): RegistryStatus {
  return {
    registryId: entry.registryId,
    bindEndpoint: entry.bindEndpoint,
    state: entry.state as RegistryStateValue,
    topologyEntryCount: entry.topologyEntryCount,
    peerRegistryCount: entry.peerRegistryCount,
    connectedPeerRegistryCount: entry.connectedPeerRegistryCount,
    listSeq: BigInt(entry.listSeq),
    lastError: entry.lastError,
    lastChangedMs: BigInt(entry.lastChangedMs)
  };
}

export function mapRegistryServiceSummaryEntry(entry: RegistryServiceSummaryEntryRaw): RegistryServiceSummaryEntry {
  return {
    autoConnectType: entry.autoConnectType as AutoConnectType,
    serviceRole: entry.serviceRole as ServiceRoleValue,
    channelName: entry.channelName,
    totalCount: entry.totalCount,
    connectingCount: entry.connectingCount,
    readyCount: entry.readyCount,
    errorCount: entry.errorCount,
    stoppedCount: entry.stoppedCount,
    lastReportedMs: BigInt(entry.lastReportedMs)
  };
}

export function normalizeTopologyFilter(filter?: RegistryTopologyFilter): Record<string, unknown> | undefined {
  if (!filter) {
    return undefined;
  }
  return {
    autoConnectType: filter.autoConnectType,
    serviceKind: filter.serviceKind,
    serviceRole: filter.serviceRole,
    channelName: filter.channelName,
    routingId: filter.routingId ? normalizeRoutingId(filter.routingId, 'filter.routingId') : undefined,
    state: filter.state,
    source: filter.source
  };
}
