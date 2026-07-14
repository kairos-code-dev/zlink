// The wire contract from scenario §7, mirrored from the language-neutral definition.
// The field names are the contract: every language server encodes exactly these.
// Only the messages the browser exchanges appear here — server-internal messages
// (§7.3) never reach the client.

export interface PlayerView {
  PlayerId: string;
  X: number;
  Y: number;
  ZoneId: string;
  IsBot: boolean;
}

export interface NodeView {
  NodeId: string;
  Registered: boolean;
  Connected: boolean;
  Maintenance: boolean;
  Zones: string[];
  PlayerCount: number;
}

// --- §7.1 game — browser <-> Gateway ---------------------------------------

export interface JoinWorldReq {
  PlayerId: string;
}

export interface JoinWorldRes {
  PlayerId: string;
  ZoneId: string;
  NodeId: string;
  X: number;
  Y: number;
  Error?: string | null;
}

export interface MoveMsg {
  X: number;
  Y: number;
}

export interface ZoneStateNotify {
  ZoneId: string;
  Tick: number;
  Players: PlayerView[];
}

export interface ZoneChangedNotify {
  PlayerId: string;
  ZoneId: string;
  NodeId: string;
  Transferred: boolean;
}

export interface WorldAnnounceNotify {
  AnnouncementId: string;
  Text: string;
}

export interface MoveRejectedNotify {
  Reason: MoveRejectReason;
  X: number;
  Y: number;
}

export type MoveRejectReason =
  | 'OutOfRange'
  | 'TooFar'
  | 'DiagonalCrossing'
  | 'ZoneMaintenance';

// --- §7.2 ops console — browser <-> Ops -------------------------------------

export type WatchNodesReq = Record<string, never>;

export interface WatchNodesRes {
  Nodes: NodeView[];
}

export interface NodeStatusNotify {
  NodeId: string;
  Registered: boolean;
  Connected: boolean;
  Maintenance: boolean;
  Zones: string[];
  PlayerCount: number;
}

export interface NodeAlertNotify {
  NodeId: string;
  Kind: NodeAlertKind;
  Detail: string;
  OccurredAt: string;
}

export type NodeAlertKind = 'TimerHandlerFailed' | 'PeersChanged';

export interface AnnounceWorldReq {
  Text: string;
}

export interface AnnounceWorldRes {
  AnnouncementId: string;
}

export interface SetMaintenanceReq {
  NodeId: string;
  Enabled: boolean;
}

export interface SetMaintenanceRes {
  NodeId: string;
  Enabled: boolean;
  Zones: string[];
  Error?: string | null;
}

export interface NodeDiagnosticsReq {
  NodeId: string;
}

export interface NodeDiagnosticsRes {
  NodeId: string;
  Zones: string[];
  PlayerCount: number;
  Maintenance: boolean;
  Error?: string | null;
}

/** A request whose target node is not connected returns this in `Error`. */
export const NODE_UNAVAILABLE = 'NodeUnavailable';

/** Packet names are the contract type names; the servers register them the same way. */
export const Packets = {
  JoinWorldReq: 'JoinWorldReq',
  JoinWorldRes: 'JoinWorldRes',
  MoveMsg: 'MoveMsg',
  ZoneStateNotify: 'ZoneStateNotify',
  ZoneChangedNotify: 'ZoneChangedNotify',
  WorldAnnounceNotify: 'WorldAnnounceNotify',
  MoveRejectedNotify: 'MoveRejectedNotify',
  WatchNodesReq: 'WatchNodesReq',
  WatchNodesRes: 'WatchNodesRes',
  NodeStatusNotify: 'NodeStatusNotify',
  NodeAlertNotify: 'NodeAlertNotify',
  AnnounceWorldReq: 'AnnounceWorldReq',
  AnnounceWorldRes: 'AnnounceWorldRes',
  SetMaintenanceReq: 'SetMaintenanceReq',
  SetMaintenanceRes: 'SetMaintenanceRes',
  NodeDiagnosticsReq: 'NodeDiagnosticsReq',
  NodeDiagnosticsRes: 'NodeDiagnosticsRes',
} as const;
