import type { RoutingId } from '../../contracts/Common';
import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole,
  ZLinkLocationWriteIntent,
  type ZLinkLocationWriteResult,
  type ZLinkPeerLocation,
  type ZLinkPeerLocationKey
} from '../../contracts/Locations';

export interface ZLinkAutoConnectLocal {
  readonly autoConnectType: ZLinkLocationAutoConnectType;
  readonly meshName: string;
  readonly role: ZLinkLocationRole;
  readonly nodeRid?: RoutingId;
  readonly endpoint: string;
}

export interface ZLinkAutoConnectTarget {
  readonly targetKey: string;
  readonly nodeRid?: RoutingId;
  readonly role: ZLinkLocationRole;
  readonly endpoint: string;
  readonly metadata?: Readonly<Record<string, string>>;
  readonly ownerId?: string;
  readonly connectionKind?: 'spot-router' | 'spot-pub';
}

export interface IZLinkAutoConnectExecutor {
  connect(target: ZLinkAutoConnectTarget): boolean;
  disconnect(target: ZLinkAutoConnectTarget): void;
}

export interface IZLinkAutoConnectPeerPublisher {
  writePeer(
    peer: ZLinkPeerLocation,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult>;
  removePeer(
    key: ZLinkPeerLocationKey,
    generation: bigint,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult>;
}

export interface ZLinkAutoConnectEventSink {
  desiredSetChanged(change: {
    readonly autoConnectType: ZLinkLocationAutoConnectType;
    readonly meshName: string;
    readonly connectedEndpoints: readonly string[];
    readonly disconnectedEndpoints: readonly string[];
  }): void;
}
