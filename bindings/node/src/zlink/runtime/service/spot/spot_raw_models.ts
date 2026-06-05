// SPDX-License-Identifier: MPL-2.0

import { RoutingId } from '../../../contracts';
import type { MonitorStatusRaw } from '../../eventing/monitor_raw';
import type { MessageSnapshot } from '../../messaging/message_snapshot';
import type {
  SpotNodePeerEntry,
  SpotNodeSocketEntry,
  SpotNodeSocketOwnerValue,
  SpotNodeStateValue,
  SpotNodeStatus,
  SpotNodeSubjectEntry,
  SpotPeerKindValue,
  SpotPeerSourceValue,
  SpotPeerStateValue,
  SpotRoleValue
} from '../../../contracts/service';
import type { SocketTypeValue } from '../../../contracts/sockets/socket_constants';
import { materializeMonitorStatus } from '../../eventing/monitor_status';
import type {
  ActorJoinInfoRaw,
  ActorPartRaw,
  ActorRefRaw,
  SpotActorLifecycleInfoRaw
} from './actor_models';

export interface SpotRoutedRaw {
  sourceRid?: Buffer | null;
  spotRid?: Buffer | null;
  requestSeq?: bigint | null;
  parts: MessageSnapshot[];
}

export interface SpotDispatchRaw {
  event: number;
  subjectKind: number;
  subjectHandle: bigint;
  actorParts?: ActorPartRaw[];
}

export interface SpotActorJoinRecvRaw {
  info: ActorJoinInfoRaw;
  message: MessageSnapshot;
}

export interface SpotActorLifecycleRaw {
  kind: number;
  info: SpotActorLifecycleInfoRaw;
}

export interface SpotNodeStatusRaw {
  channelName: string;
  localEndpoint: string;
  nodeRoutingId?: Buffer | null;
  state: number;
  configuredPeerCount: number;
  activePeerCount: number;
  connectedPeerCount: number;
  subjectCount: number;
  readySubjectCount: number;
  disconnectedSubTargetCount?: number;
  disconnectedRoutedTargetCount?: number;
  lastError: number;
  lastChangedMs: number | bigint;
}

export interface SpotNodePeerEntryRaw {
  channelName: string;
  localEndpoint: string;
  peerEndpoint: string;
  source: number;
  kind: number;
  state: number;
  weight: number;
  connectedSinceMs: number | bigint;
  lastChangedMs: number | bigint;
}

export interface SpotNodeSubjectEntryRaw {
  role: number;
  subject: string;
  subjectKind: number;
  readyPeerCount: number;
  activePeerCount: number;
  lastChangedMs: number | bigint;
}

export interface SpotNodeSocketEntryRaw {
  owner: number;
  ownerId: number | bigint;
  ownerName: string;
  socketName: string;
  socketType: number;
  autoHwmVisible: boolean;
  snapshot: MonitorStatusRaw;
}

export interface SpotNodeSpotGetOrNewRaw {
  spot: unknown;
  created: boolean;
}

export type { ActorRefRaw };

export function mapSpotNodeStatus(entry: SpotNodeStatusRaw, fallbackRoutingId: RoutingId): SpotNodeStatus {
  const nodeRoutingId = entry.nodeRoutingId
    ? RoutingId.from(entry.nodeRoutingId)
    : fallbackRoutingId;
  return {
    channelName: entry.channelName,
    localEndpoint: entry.localEndpoint,
    nodeRoutingId,
    state: entry.state as SpotNodeStateValue,
    configuredPeerCount: entry.configuredPeerCount,
    activePeerCount: entry.activePeerCount,
    connectedPeerCount: entry.connectedPeerCount,
    subjectCount: entry.subjectCount,
    readySubjectCount: entry.readySubjectCount,
    disconnectedSubTargetCount: entry.disconnectedSubTargetCount ?? 0,
    disconnectedRoutedTargetCount: entry.disconnectedRoutedTargetCount ?? 0,
    lastError: entry.lastError,
    lastChangedMs: BigInt(entry.lastChangedMs)
  };
}

export function mapSpotNodePeerEntry(entry: SpotNodePeerEntryRaw): SpotNodePeerEntry {
  return {
    channelName: entry.channelName,
    localEndpoint: entry.localEndpoint,
    peerEndpoint: entry.peerEndpoint,
    source: entry.source as SpotPeerSourceValue,
    kind: entry.kind as SpotPeerKindValue,
    state: entry.state as SpotPeerStateValue,
    weight: entry.weight,
    connectedSinceMs: BigInt(entry.connectedSinceMs),
    lastChangedMs: BigInt(entry.lastChangedMs)
  };
}

export function mapSpotNodeSubjectEntry(entry: SpotNodeSubjectEntryRaw): SpotNodeSubjectEntry {
  return {
    role: entry.role as SpotRoleValue,
    subject: entry.subject,
    subjectKind: entry.subjectKind,
    readyPeerCount: entry.readyPeerCount,
    activePeerCount: entry.activePeerCount,
    lastChangedMs: BigInt(entry.lastChangedMs)
  };
}

export function mapSpotNodeSocketEntry(entry: SpotNodeSocketEntryRaw): SpotNodeSocketEntry {
  return {
    owner: entry.owner as SpotNodeSocketOwnerValue,
    ownerId: BigInt(entry.ownerId),
    ownerName: entry.ownerName,
    socketName: entry.socketName,
    socketType: entry.socketType as SocketTypeValue,
    autoHwmVisible: Boolean(entry.autoHwmVisible),
    snapshot: materializeMonitorStatus(entry.snapshot)
  };
}
