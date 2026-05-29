// SPDX-License-Identifier: MPL-2.0

import { RuntimeContext as Context } from '../../core/context';
import { NativeHandle } from '../../handles/native_handle';
import { requireNative } from '../../native/native';
import { closeCall, configCall, connectCall } from '../../errors/native_errors';
import { validateCString } from '../../options/validation';
import { boolBuffer, readInt32Option } from '../../sockets/socket_options';
import { normalizeRoutingId } from '../../core/routing_id';
import { RoutingId } from '../../../contracts';
import { SocketOption } from '../../options/option_mapping';
import {
  type ActorRef,
  type ActorRoute,
  type MemberPeerEntry,
  type SpotKindValue,
  type SpotRoute,
  type AutoConnectType,
} from '../../../contracts/service';
import { mapMemberPeerEntry, type MemberPeerEntryRaw } from '../registry/registry_support';

export interface DiscoveryActorRefRaw {
  nodeRid: Buffer;
  actorId: string;
  generation: bigint | number;
}

export interface DiscoveryActorRouteRaw {
  actor: DiscoveryActorRefRaw;
  currentSpotRid: Buffer;
  currentSpotKind: number;
}

export interface DiscoverySpotRouteRaw {
  spotRid: Buffer;
  ownerNodeRid: Buffer;
  spotKind: number;
}

function actorRefFromRaw(raw: DiscoveryActorRefRaw): ActorRef {
  return {
    nodeRid: RoutingId.from(raw.nodeRid),
    actorId: raw.actorId,
    generation: BigInt(raw.generation)
  };
}

function actorRouteFromRaw(raw: DiscoveryActorRouteRaw): ActorRoute {
  return {
    actor: actorRefFromRaw(raw.actor),
    currentSpotRid: RoutingId.from(raw.currentSpotRid),
    currentSpotKind: raw.currentSpotKind as SpotKindValue
  };
}

export class Discovery extends NativeHandle {
  readonly autoConnectType: AutoConnectType;
  readonly channelName: string;

  constructor(ctx: Context, autoConnectType: AutoConnectType, channelName: string) {
    if (typeof channelName !== 'string' || channelName.length === 0) throw new TypeError('Discovery channelName must be a non-empty string');
    validateCString(channelName, 'channelName');
    super(requireNative().discoveryNew(ctx.nativeHandle(), autoConnectType, channelName));
    this.autoConnectType = autoConnectType;
    this.channelName = channelName;
  }

  /** @internal */
  nativeHandle(): unknown { return this._native; }

  connectRegistry(endpoint: string): void {
    const normalizedEndpoint = validateCString(endpoint, 'endpoint');
    connectCall('discovery registry connect failed', () => {
      requireNative().discoveryConnectRegistry(this._native, normalizedEndpoint);
    });
  }

  setValue(value: number): void {
    configCall('discovery value set failed', () => {
      requireNative().discoverySetValue(this._native, value | 0);
    });
  }

  getValue(): number {
    return configCall('discovery value get failed', () =>
      requireNative().discoveryGetValue(this._native) as number
    );
  }

  resolveSpot(spotRid: RoutingId): SpotRoute {
    const normalizedSpotRid = normalizeRoutingId(spotRid);
    const raw = configCall('discovery spot resolve failed', () =>
      requireNative().discoveryResolveSpot(this._native, normalizedSpotRid)
    );
    return {
      spotRid: RoutingId.from(raw.spotRid),
      ownerNodeRid: RoutingId.from(raw.ownerNodeRid),
      spotKind: raw.spotKind as SpotKindValue
    };
  }

  resolveActor(actorId: string): ActorRoute {
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    return actorRouteFromRaw(
      configCall('discovery actor resolve failed', () =>
        requireNative().discoveryResolveActor(
          this._native,
          normalizedActorId
        )
      )
    );
  }

  get spotOwnerSyncEnabled(): boolean {
    return readInt32Option(
      configCall('discovery spot-owner sync option get failed', () =>
        requireNative().socketGetOpt(this._native, SocketOption.DISCOVERY_SPOT_OWNER_SYNC) as Buffer
      ),
      'spotOwnerSyncEnabled'
    ) !== 0;
  }

  set spotOwnerSyncEnabled(enabled: boolean) {
    const value = boolBuffer(enabled);
    configCall('discovery spot-owner sync option set failed', () => {
      requireNative().socketSetOpt(
        this._native,
        SocketOption.DISCOVERY_SPOT_OWNER_SYNC,
        value
      );
    });
  }

  get actorRouteSyncEnabled(): boolean {
    return readInt32Option(
      configCall('discovery actor-route sync option get failed', () =>
        requireNative().socketGetOpt(this._native, SocketOption.DISCOVERY_ACTOR_ROUTE_SYNC) as Buffer
      ),
      'actorRouteSyncEnabled'
    ) !== 0;
  }

  set actorRouteSyncEnabled(enabled: boolean) {
    const value = boolBuffer(enabled);
    configCall('discovery actor-route sync option set failed', () => {
      requireNative().socketSetOpt(
        this._native,
        SocketOption.DISCOVERY_ACTOR_ROUTE_SYNC,
        value
      );
    });
  }

  memberPeers(): MemberPeerEntry[] {
    return (configCall('discovery member peer query failed', () =>
      requireNative().discoveryGetProviders(this._native) as MemberPeerEntryRaw[]
    ))
      .map(mapMemberPeerEntry);
  }

  setTlsClient(ca: string, hostname: string, trustSystem: boolean = false): void {
    const normalizedCa = validateCString(ca, 'ca', Number.MAX_SAFE_INTEGER);
    const normalizedHostname = validateCString(hostname, 'hostname', Number.MAX_SAFE_INTEGER);
    configCall('discovery TLS client configuration failed', () => {
      requireNative().discoverySetTlsClient(this._native, normalizedCa, normalizedHostname, trustSystem ? 1 : 0);
    });
  }

  close(): void {
    if (this._native) {
      closeCall('discovery close failed', () => {
        requireNative().discoveryDestroy(this._native);
      });
      this._native = null;
    }
  }
}
