// SPDX-License-Identifier: MPL-2.0

import { randomBytes } from 'node:crypto';
import { RuntimeContext as Context } from '../../core/context';
import { Actor } from './actor';
import { Spot } from './spot';
import { SpotNodePublisher, SpotRouteBridge } from './spot_route_bridge';
import { getNativeHandle, NativeHandle } from '../../handles/native_handle';
import { closeCall, configCall, connectCall } from '../../errors/native_errors';
import { validateCString } from '../../options/validation';
import { int32Buffer, readInt32Option } from '../../sockets/socket_options';
import { RuntimeSendOperation } from '../../sockets/socket_operation_builders';
import { normalizeRoutingId } from '../../core/routing_id';
import { requireNative } from '../../native/native';
import { normalizeOperationPayload } from '../../buffers/message_conversion';
import { RoutingId, type MessageLike } from '../../../contracts';
import type { AutoHwmProfileValue } from '../../../contracts/core';
import { SpotNodeMode, type ActorDestroyOperation, type ActorJoinEntrySpotOperation, type ActorJoinOperation, type ActorLeaveOperation, type ActorLookupOperation, type ActorRef, type SendOperation, type SpotNodeActorEntry, type SpotNodeModeValue, type SpotNodePeerEntry, type SpotNodePeerFilter, type SpotNodeSocketEntry, type SpotNodeSocketFilter, type SpotNodeSpotEntry, type SpotNodeStatus, type SpotNodeSubjectEntry, type SpotNodeSubjectFilter } from '../../../contracts/service';
import { SpotNodeOption } from './spot_options';
import { actorRefFromRaw, actorRefToRaw, spotNodeActorEntryFromRaw, spotNodeSpotEntryFromRaw } from './actor_models';
import { invokeActorBindRemoteSession, invokeActorDestroy, invokeActorJoin, invokeActorJoinEntrySpot, invokeActorLeave, invokeActorSendBoundSession, invokeRemoteActorGetRef } from './actor_invokers';
import { RuntimeActorDestroyOperation, RuntimeActorJoinEntrySpotOperation, RuntimeActorJoinOperation, RuntimeActorLeaveOperation, RuntimeActorLookupOperation } from './actor_operations';
import { mapSpotNodePeerEntry, mapSpotNodeSocketEntry, mapSpotNodeStatus, mapSpotNodeSubjectEntry, type ActorRefRaw, type SpotNodePeerEntryRaw, type SpotNodeSocketEntryRaw, type SpotNodeSpotGetOrNewRaw, type SpotNodeStatusRaw, type SpotNodeSubjectEntryRaw } from './spot_raw_models';

type SpotNodeSpotEntryRaw = Parameters<typeof spotNodeSpotEntryFromRaw>[0];
type SpotNodeActorEntryRaw = Parameters<typeof spotNodeActorEntryFromRaw>[0];

export class SpotNode extends NativeHandle {
  private readonly _spots = new Set<Spot>();
  private readonly _ctx: Context;
  private _nodeRoutingId: RoutingId;
  constructor(ctx: Context, mode: SpotNodeModeValue = SpotNodeMode.All) {
    super(requireNative().spotNodeNew(getNativeHandle(ctx), { mode: mode | 0 }));
    this._ctx = ctx;
    this._nodeRoutingId = RoutingId.from(randomBytes(16));
  }
  setPubBind(endpoint: string): void {
    const normalizedEndpoint = validateCString(endpoint, 'endpoint');
    configCall('spot node pub bind failed', () => {
      requireNative().spotNodeSetPubBind(this._native, normalizedEndpoint);
    });
  }
  setRouterBind(endpoint: string): void {
    const normalizedEndpoint = validateCString(endpoint, 'endpoint');
    configCall('spot node router bind endpoint failed', () => {
      requireNative().spotNodeSetRouterBind(this._native, normalizedEndpoint);
    });
  }
  connectPeer(endpoint: string): void {
    const normalizedEndpoint = validateCString(endpoint, 'endpoint');
    connectCall('spot node peer connect failed', () => {
      requireNative().spotNodeConnectPeerPub(this._native, normalizedEndpoint);
    });
  }
  connectPeerRid(targetNodeRid: RoutingId, endpoint: string): void {
    const normalizedTargetNodeRid = normalizeRoutingId(targetNodeRid);
    const normalizedEndpoint = validateCString(endpoint, 'endpoint');
    connectCall('spot node peer connect by routing id failed', () => {
      requireNative().spotNodeConnectPeerRidPub(
        this._native,
        normalizedTargetNodeRid,
        normalizedEndpoint
      );
    });
  }
  disconnectPeer(endpoint: string): void {
    const normalizedEndpoint = validateCString(endpoint, 'endpoint');
    connectCall('spot node peer disconnect failed', () => {
      requireNative().spotNodeDisconnectPeerPub(this._native, normalizedEndpoint);
    });
  }
  disconnectPeerRid(targetNodeRid: RoutingId): void {
    const normalizedTargetNodeRid = normalizeRoutingId(targetNodeRid);
    connectCall('spot node peer disconnect by routing id failed', () => {
      requireNative().spotNodeDisconnectPeerRidPub(
        this._native,
        normalizedTargetNodeRid
      );
    });
  }
  createRouteBridge(): SpotRouteBridge {
    return new SpotRouteBridge(this._ctx, this);
  }
  createPublisher(): SpotNodePublisher {
    return new SpotNodePublisher(this);
  }
  setRoutingId(routingId: RoutingId): void {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    configCall('spot node routing id set failed', () => {
      requireNative().handleSetRoutingId(this._native, normalizedRoutingId);
    });
    this._nodeRoutingId = routingId;
  }
  setPublisherRoutingId(routingId: RoutingId): void {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    configCall('spot node publisher routing id set failed', () => {
      requireNative().spotNodeSetPubRoutingId(this._native, normalizedRoutingId);
    });
  }
  setSubscriberRoutingId(routingId: RoutingId): void {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    configCall('spot node subscriber routing id set failed', () => {
      requireNative().spotNodeSetSubRoutingId(this._native, normalizedRoutingId);
    });
  }
  get routingId(): RoutingId {
    this._nodeRoutingId = RoutingId.from(configCall('spot node routing id get failed', () =>
      requireNative().handleGetRoutingId(this._native) as Buffer
    ));
    return this._nodeRoutingId;
  }
  setTlsServer(cert: string, key: string, requireClientCert: boolean = false): void {
    const normalizedCert = validateCString(cert, 'cert', Number.MAX_SAFE_INTEGER);
    const normalizedKey = validateCString(key, 'key', Number.MAX_SAFE_INTEGER);
    configCall('spot node TLS server configuration failed', () => {
      requireNative().spotNodeSetTlsServer(this._native, normalizedCert, normalizedKey, requireClientCert ? 1 : 0);
    });
  }
  setTlsClient(ca: string, hostname: string, trustSystem: boolean = false): void {
    const normalizedCa = validateCString(ca, 'ca', Number.MAX_SAFE_INTEGER);
    const normalizedHostname = validateCString(hostname, 'hostname', Number.MAX_SAFE_INTEGER);
    configCall('spot node TLS client configuration failed', () => {
      requireNative().spotNodeSetTlsClient(this._native, normalizedCa, normalizedHostname, trustSystem ? 1 : 0);
    });
  }
  private setOptionRaw(option: number, value: number): void {
    const buffer = int32Buffer(value, 'value');
    configCall('spot node option set failed', () => {
      requireNative().spotNodeSetOption(this._native, option, buffer);
    });
  }
  private getOptionRaw(option: number, name: string): number {
    return readInt32Option(configCall('spot node option get failed', () =>
      requireNative().spotNodeGetOption(this._native, option) as Buffer
    ), name);
  }
  get routerHwmProfile(): AutoHwmProfileValue { return this.getOptionRaw(SpotNodeOption.ROUTER_HWM_PROFILE, 'routerHwmProfile') as AutoHwmProfileValue; }
  set routerHwmProfile(value: AutoHwmProfileValue) { this.setOptionRaw(SpotNodeOption.ROUTER_HWM_PROFILE, value | 0); }
  get routerHwm(): number { return this.getOptionRaw(SpotNodeOption.ROUTER_HWM, 'routerHwm'); }
  set routerHwm(value: number) { this.setOptionRaw(SpotNodeOption.ROUTER_HWM, value | 0); }
  get pubsubHwmProfile(): AutoHwmProfileValue { return this.getOptionRaw(SpotNodeOption.PUBSUB_HWM_PROFILE, 'pubsubHwmProfile') as AutoHwmProfileValue; }
  set pubsubHwmProfile(value: AutoHwmProfileValue) { this.setOptionRaw(SpotNodeOption.PUBSUB_HWM_PROFILE, value | 0); }
  get pubsubHwm(): number { return this.getOptionRaw(SpotNodeOption.PUBSUB_HWM, 'pubsubHwm'); }
  set pubsubHwm(value: number) { this.setOptionRaw(SpotNodeOption.PUBSUB_HWM, value | 0); }
  get dispatchWorkersMin(): number { return this.getOptionRaw(SpotNodeOption.DISPATCH_WORKERS_MIN, 'dispatchWorkersMin'); }
  set dispatchWorkersMin(value: number) { this.setOptionRaw(SpotNodeOption.DISPATCH_WORKERS_MIN, value | 0); }
  get dispatchWorkersMax(): number { return this.getOptionRaw(SpotNodeOption.DISPATCH_WORKERS_MAX, 'dispatchWorkersMax'); }
  set dispatchWorkersMax(value: number) { this.setOptionRaw(SpotNodeOption.DISPATCH_WORKERS_MAX, value | 0); }
  createSpot(): Spot {
    const spot = Spot.create(this);
    this._spots.add(spot);
    try {
      const raw = requireNative().spotNodeStatus(this._native) as SpotNodeStatusRaw;
      if (raw?.nodeRoutingId) {
        this._nodeRoutingId = RoutingId.from(raw.nodeRoutingId);
      }
    } catch {
      // Ignore cache warm-up failure; status() will surface real errors later.
    }
    return spot;
  }
  entrySpot(): Spot {
    const spot = Spot.fromNative(this, configCall('spot node entry spot lookup failed', () =>
      requireNative().spotNodeEntrySpot(this._native)
    ));
    this._spots.add(spot);
    return spot;
  }
  spotLookup(spotRid: RoutingId): Spot | null {
    const normalizedSpotRid = normalizeRoutingId(spotRid, 'spotRid');
    const native = configCall('spot node spot lookup failed', () =>
      requireNative().spotNodeSpotLookup(this._native, normalizedSpotRid)
    );
    if (!native) return null;
    const spot = Spot.fromNative(this, native);
    this._spots.add(spot);
    return spot;
  }
  getOrCreateSpot(spotRid: RoutingId): { spot: Spot; created: boolean } {
    const normalizedSpotRid = normalizeRoutingId(spotRid, 'spotRid');
    const result = configCall('spot node spot get-or-create failed', () =>
      requireNative().spotNodeSpotGetOrNew(this._native, normalizedSpotRid)
    ) as SpotNodeSpotGetOrNewRaw;
    const spot = Spot.fromNative(this, result.spot);
    this._spots.add(spot);
    return { spot, created: !!result.created };
  }
  createActor(actorId: string, request?: MessageLike | readonly MessageLike[]): Actor {
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    const normalizedRequest = request === undefined ? undefined : normalizeOperationPayload(request);
    return Actor.create(
      this._native,
      actorRefFromRaw(configCall('spot node actor create failed', () =>
        requireNative().spotNodeActorNew(
          this._native,
          normalizedActorId,
          normalizedRequest
        ) as ActorRefRaw
      ))
    );
  }
  actorLookup(actorId: string): ActorRef {
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    return actorRefFromRaw(
      configCall('spot node actor lookup failed', () =>
        requireNative().spotNodeActorLookup(
          this._native,
          normalizedActorId
        ) as ActorRefRaw
      )
    );
  }
  remoteActorGetRef(targetNodeRid: RoutingId, actorId: string): ActorLookupOperation {
    const node = this._native;
    const normalizedNodeRid = normalizeRoutingId(targetNodeRid, 'targetNodeRid');
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    return new RuntimeActorLookupOperation((callback, timeoutMs) =>
      invokeRemoteActorGetRef(node, normalizedNodeRid, normalizedActorId, callback, timeoutMs),
    );
  }
  destroyActor(actor: ActorRef): ActorDestroyOperation {
    const node = this._native;
    const actorRaw = actorRefToRaw(actor);
    return new RuntimeActorDestroyOperation((callback, timeoutMs) =>
      invokeActorDestroy(node, actorRaw, callback, timeoutMs),
    );
  }
  joinActor(actor: ActorRef, destNodeRid: RoutingId, destSpotRid: RoutingId): ActorJoinOperation {
    const node = this._native;
    return new RuntimeActorJoinOperation((parts, callback, flags, timeoutMs) =>
      invokeActorJoin(node, actor, destNodeRid, destSpotRid, null, parts, callback, flags, timeoutMs),
    );
  }
  joinActorEntrySpot(actor: ActorRef, destNodeRid: RoutingId, request: MessageLike): ActorJoinEntrySpotOperation {
    const node = this._native;
    return new RuntimeActorJoinEntrySpotOperation((parts, callback, flags, timeoutMs) =>
      invokeActorJoinEntrySpot(node, actor, destNodeRid, parts, callback, flags, timeoutMs),
      request,
    );
  }
  leaveActor(actor: ActorRef, currentSpotRid: RoutingId): ActorLeaveOperation {
    const node = this._native;
    return new RuntimeActorLeaveOperation((callback, timeoutMs) =>
      invokeActorLeave(node, actor, currentSpotRid, callback, timeoutMs),
    );
  }
  sendActorBoundSession(actor: ActorRef): SendOperation {
    const node = this._native;
    return new RuntimeSendOperation((parts, flags) => invokeActorSendBoundSession(node, actor, parts, flags));
  }
  bindRemoteActorSession(actor: ActorRef, sourceNodeRid: RoutingId, sourceSessionRid: RoutingId): void {
    invokeActorBindRemoteSession(this._native, actor, sourceNodeRid, sourceSessionRid);
  }
  /** @internal */
  unregisterSpot(spot: Spot): void {
    this._spots.delete(spot);
  }
  status(): SpotNodeStatus {
    const raw = configCall('spot node status snapshot failed', () =>
      requireNative().spotNodeStatus(this._native) as SpotNodeStatusRaw
    );
    if (raw.nodeRoutingId) {
      this._nodeRoutingId = RoutingId.from(raw.nodeRoutingId);
    }
    return mapSpotNodeStatus(raw, this._nodeRoutingId);
  }
  peers(filter?: SpotNodePeerFilter): SpotNodePeerEntry[] {
    return (configCall('spot node peers snapshot failed', () =>
      filter === undefined
        ? requireNative().spotNodePeers(this._native) as SpotNodePeerEntryRaw[]
        : requireNative().spotNodePeersQuery(this._native, filter) as SpotNodePeerEntryRaw[]
    ))
      .map((entry) => mapSpotNodePeerEntry(entry));
  }
  subjects(filter?: SpotNodeSubjectFilter): SpotNodeSubjectEntry[] {
    return (configCall('spot node subjects snapshot failed', () =>
      requireNative().spotNodeSubjects(this._native, filter ?? undefined) as SpotNodeSubjectEntryRaw[]
    ))
      .map((entry) => mapSpotNodeSubjectEntry(entry));
  }
  internalSockets(filter?: SpotNodeSocketFilter): SpotNodeSocketEntry[] {
    return (configCall('spot node internal socket snapshot failed', () =>
      requireNative().spotNodeInternalSockets(this._native, filter ?? undefined) as SpotNodeSocketEntryRaw[]
    ))
      .map((entry) => mapSpotNodeSocketEntry(entry));
  }
  spots(): SpotNodeSpotEntry[] {
    return (configCall('spot node spots snapshot failed', () =>
      requireNative().spotNodeSpots(this._native) as SpotNodeSpotEntryRaw[]
    ))
      .map((entry) => spotNodeSpotEntryFromRaw(entry));
  }
  actors(): SpotNodeActorEntry[] {
    return (configCall('spot node actors snapshot failed', () =>
      requireNative().spotNodeActors(this._native) as SpotNodeActorEntryRaw[]
    ))
      .map((entry) => spotNodeActorEntryFromRaw(entry));
  }
  close(): void {
    if (!this._native) {
      return;
    }
    for (const spot of [...this._spots]) {
      spot.close();
    }
    closeCall('spot node close failed', () => {
      requireNative().spotNodeDestroy(this._native);
    });
    this._native = null;
  }
}
