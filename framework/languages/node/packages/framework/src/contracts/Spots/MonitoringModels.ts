import type { RoutingId } from '../Common';

export enum ZLinkSpotNodeState { Idle = 1, Connecting = 2, PartialReady = 3, Ready = 4, Error = 5 }
export enum ZLinkSpotPeerSource { Manual = 1, Discovery = 2, Mixed = 3 }
export enum ZLinkSpotPeerKind { RouteMesh = 1 }
export enum ZLinkSpotPeerState { Configured = 1, Connecting = 2, Connected = 3 }
export enum ZLinkSubjectKind { None = 0, Topic = 1, Pattern = 2 }
export enum ZLinkSpotRole { Pub = 1, Sub = 2 }

export interface ZLinkSpotNodeStatus {
  readonly channelName: string;
  readonly localEndpoint: string;
  readonly nodeRoutingId?: RoutingId;
  readonly state: ZLinkSpotNodeState;
  readonly configuredPeerCount: number;
  readonly activePeerCount: number;
  readonly connectedPeerCount: number;
  readonly subjectCount: number;
  readonly readySubjectCount: number;
  readonly lastError: number;
  readonly lastChangedMs: bigint;
}

export interface ZLinkSpotNodePeerEntry {
  readonly channelName: string;
  readonly localEndpoint: string;
  readonly peerEndpoint: string;
  readonly source: ZLinkSpotPeerSource;
  readonly kind: ZLinkSpotPeerKind;
  readonly state: ZLinkSpotPeerState;
  readonly weight: number;
  readonly connectedSinceMs: bigint;
  readonly lastChangedMs: bigint;
}

export interface ZLinkSpotNodeSubjectEntry {
  readonly role: ZLinkSpotRole;
  readonly subject: string;
  readonly subjectKind: ZLinkSubjectKind;
  readonly readyPeerCount: number;
  readonly activePeerCount: number;
  readonly lastChangedMs: bigint;
}
