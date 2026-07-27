import type { ZLinkRuntimeEvent } from '../../contracts/Eventing/Contracts';
import type { ZLinkLocationAutoConnectType } from '../../contracts/Locations/Values';
import type {
  ZLinkActorLocation,
  ZLinkPeerLocation,
  ZLinkRouteLocation,
  ZLinkSpotLocation
} from '../../contracts/Locations/Rows';
import type {
  ZLinkActorLocationKey,
  ZLinkRouteLocationKey,
  ZLinkSpotLocationKey
} from '../../contracts/Locations/Keys';

export interface ZLinkLocationMonitoringRegistration { readonly sourceName: string; }

export enum ZLinkSocketNativeEventType {
  Connected = 0x0001,
  ConnectDelayed = 0x0002,
  ConnectRetried = 0x0004,
  Listening = 0x0008,
  BindFailed = 0x0010,
  Accepted = 0x0020,
  AcceptFailed = 0x0040,
  Closed = 0x0080,
  CloseFailed = 0x0100,
  Disconnected = 0x0200,
  MonitorStopped = 0x0400,
  HandshakeFailedNoDetail = 0x0800,
  ConnectionReady = 0x1000,
  HandshakeFailedProtocol = 0x2000,
  HandshakeFailedAuth = 0x4000,
  PeerAdmissionChanged = 0x8000
}

export enum ZLinkLocationPeerEventKind { RowUpdated = 0, RowRemoved = 1, DesiredSetChanged = 2 }
export enum ZLinkLocationSpotEventKind { RowUpdated = 0, RowRemoved = 1, ResolveMiss = 2 }
export enum ZLinkLocationActorEventKind { RowUpdated = 0, RowRemoved = 1, ResolveMiss = 2 }
export enum ZLinkLocationRouteEventKind { RowUpdated = 0, RowRemoved = 1, ResolveMiss = 2 }

export interface ZLinkAutoConnectDesiredSetChange {
  readonly autoConnectType: ZLinkLocationAutoConnectType;
  readonly meshName: string;
  readonly connectedEndpoints: readonly string[];
  readonly disconnectedEndpoints: readonly string[];
}

export type ZLinkLocationPeerEvent =
  | (ZLinkRuntimeEvent & { readonly event: ZLinkLocationPeerEventKind.RowUpdated; readonly key: string; readonly peer: ZLinkPeerLocation })
  | (ZLinkRuntimeEvent & { readonly event: ZLinkLocationPeerEventKind.RowRemoved; readonly key: string })
  | (ZLinkRuntimeEvent & { readonly event: ZLinkLocationPeerEventKind.DesiredSetChanged; readonly desiredSetChange: ZLinkAutoConnectDesiredSetChange });
export type ZLinkLocationSpotEvent = ZLinkRuntimeEvent & (
  | { readonly event: ZLinkLocationSpotEventKind.RowUpdated; readonly key: ZLinkSpotLocationKey; readonly spot: ZLinkSpotLocation }
  | { readonly event: ZLinkLocationSpotEventKind.RowRemoved | ZLinkLocationSpotEventKind.ResolveMiss; readonly key: ZLinkSpotLocationKey }
);
export type ZLinkLocationActorEvent = ZLinkRuntimeEvent & (
  | { readonly event: ZLinkLocationActorEventKind.RowUpdated; readonly key: ZLinkActorLocationKey; readonly actor: ZLinkActorLocation }
  | { readonly event: ZLinkLocationActorEventKind.RowRemoved | ZLinkLocationActorEventKind.ResolveMiss; readonly key: ZLinkActorLocationKey }
);
export type ZLinkLocationRouteEvent = ZLinkRuntimeEvent & (
  | { readonly event: ZLinkLocationRouteEventKind.RowUpdated; readonly key: ZLinkRouteLocationKey; readonly route: ZLinkRouteLocation }
  | { readonly event: ZLinkLocationRouteEventKind.RowRemoved | ZLinkLocationRouteEventKind.ResolveMiss; readonly key: ZLinkRouteLocationKey }
);
