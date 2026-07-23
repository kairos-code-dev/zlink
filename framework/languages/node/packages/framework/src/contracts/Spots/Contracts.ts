import type { ZLinkActor } from '../Actors';
import type { ZLinkPublishCall, ZLinkRequestCall, ZLinkSendCall } from '../Channels';
import type {
  RoutingId,
  Type,
  ZLinkMessage,
  ZLinkMessageMetadata
} from '../Common';
import type {
  ZLinkAffinityKey,
  ZLinkPlacementProfile
} from '../Locations';
import type { ZLinkSubmitResult } from '../RouteMesh';
import type { ZLinkSpotTimerHandler } from '../Handlers';
import type { ZLinkTimer, ZLinkTimerOptions } from '../Timers';
import type { ZLinkEntrySpot, ZLinkInstanceSpot, ZLinkSpot } from './ZLinkSpot';

export interface ZLinkActorHandlerRegistry {
  addHandler<THandler>(handlerType: Type<THandler>): this;
}

export interface ZLinkActorTransferAdapter<TActor extends ZLinkActor> {
  transferOut(actor: TActor): Promise<ZLinkMessage>;
  transferIn(actorId: string, state: ZLinkMessage): Promise<TActor>;
}

export interface ZLinkSpotHandlerRegistry {
  addPacket<THandler>(handlerType: Type<THandler>): this;
  addSubscribe<THandler>(handlerType: Type<THandler>, channelName: string, topic: string): this;
}

export interface ZLinkInstanceSpotHandlerRegistry {
  addPacket<THandler>(handlerType: Type<THandler>): this;
}

export interface ZLinkWorkerCall<T> {
  timeoutMs(durationMs: number): ZLinkWorkerCall<T>;
  submit(signal?: AbortSignal): void;
  async(signal?: AbortSignal): Promise<T>;
  yield(signal?: AbortSignal): Promise<T>;
}

export interface ZLinkSpotCommonContext<
  TActor extends ZLinkActor = ZLinkActor,
  TSpot = ZLinkSpot<TActor>
> {
  readonly meshName: string;
  readonly spotRid: RoutingId;
  readonly nodeRid: RoutingId;
  readonly routingId: RoutingId;
  readonly outbound: ZLinkSpotOutbound;
  addTimer<THandler extends ZLinkSpotTimerHandler<TSpot>>(
    name: string,
    periodMs: number,
    handlerType: Type<THandler>,
    options?: ZLinkTimerOptions,
    signal?: AbortSignal
  ): Promise<ZLinkTimer>;
  /**
   * Runs synchronous CPU work on the bounded worker-thread pool.
   * The function is serialized into an isolated worker, so it must be self-contained
   * and return a value supported by the structured clone algorithm.
   */
  runCpuWorker<T>(work: (signal: AbortSignal) => T): ZLinkWorkerCall<T>;
  /** Runs asynchronous I/O work without occupying a CPU worker thread. */
  runIoWorker<T>(work: (signal: AbortSignal) => Promise<T>): ZLinkWorkerCall<T>;
}

export interface ZLinkSpotContext<
  TActor extends ZLinkActor = ZLinkActor,
  TSpot extends ZLinkSpot<TActor> = ZLinkSpot<TActor>
> extends ZLinkSpotCommonContext<TActor, TSpot> {
  readonly handlers: ZLinkSpotHandlerRegistry;
  close(signal?: AbortSignal): Promise<boolean>;
}

export interface ZLinkInstanceSpotContext
  extends ZLinkSpotCommonContext<ZLinkActor, ZLinkInstanceSpot> {
  readonly handlers: ZLinkInstanceSpotHandlerRegistry;
  close(signal?: AbortSignal): Promise<boolean>;
}

export interface ZLinkEntrySpotContext<
  TActor extends ZLinkActor = ZLinkActor,
  TEntrySpot extends ZLinkEntrySpot<TActor> = ZLinkEntrySpot<TActor>
> extends ZLinkSpotCommonContext<TActor, TEntrySpot> {
  readonly handlers: ZLinkSpotHandlerRegistry;
  destroyActor(actor: TActor, signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSpotActorReplyOptions {
  compress(enabled?: boolean): this;
}

export interface ZLinkSpotOutbound {
  sendToSpot(spotRid: SpotRid, message: unknown): ZLinkSpotSendCall;
  requestToSpot(spotRid: SpotRid, request: unknown): ZLinkSpotRequestCall;
  publish(channelName: string, topic: string, event: unknown): ZLinkPublishCall;
  sendToChannel(channelName: string, message: unknown): ZLinkSendCall;
  requestToChannel(channelName: string, request: unknown): ZLinkRequestCall;
}

export type SpotRid = RoutingId;

export interface SpotRef {
  readonly spotRid: SpotRid;
  readonly objectGeneration: bigint;
  readonly meshName: string;
  readonly nodeRid: RoutingId;
}

export interface ZLinkSpotSendCall {
  metadata(key: string, value: string): this;
  metadata(metadata: ZLinkMessageMetadata): this;
  instanceSpot(): this;
  instanceSpot(instanceSpotType: string): this;
  inMesh(meshName: string): this;
  placementProfile(placementProfile: ZLinkPlacementProfile): this;
  affinityKey(affinityKey: ZLinkAffinityKey): this;
  submit(signal?: AbortSignal): Promise<ZLinkSubmitResult>;
}

export interface ZLinkSpotRequestCall {
  metadata(key: string, value: string): this;
  metadata(metadata: ZLinkMessageMetadata): this;
  instanceSpot(): this;
  instanceSpot(instanceSpotType: string): this;
  inMesh(meshName: string): this;
  placementProfile(placementProfile: ZLinkPlacementProfile): this;
  affinityKey(affinityKey: ZLinkAffinityKey): this;
  timeout(timeoutMs: number): this;
  submit<TReply>(signal?: AbortSignal): Promise<TReply>;
  yield<TReply>(signal?: AbortSignal): Promise<TReply>;
}

export enum ZLinkSpotCreateState {
  Existing = 'existing',
  Created = 'created',
  Rejected = 'rejected'
}

export interface ZLinkSpotCreateResult {
  readonly spot: SpotRef;
  readonly state: ZLinkSpotCreateState;
  readonly reply?: unknown;
}

export interface ZLinkSpotInfo {
  readonly spotRid: RoutingId;
}

export interface ZLinkSpotManager {
  create(spotType: string): ZLinkSpotCreateCall;
  getOrCreate(spotRid: SpotRid, spotType: string): ZLinkSpotGetOrCreateCall;
  find(spotRid: SpotRid, signal?: AbortSignal): Promise<SpotRef | undefined>;
  close(spot: SpotRef, signal?: AbortSignal): Promise<boolean>;
}

export interface ZLinkSpotCreateCall {
  inMesh(meshName: string): this;
  request(request: unknown): this;
  placementProfile(placementProfile: ZLinkPlacementProfile): this;
  affinityKey(affinityKey: ZLinkAffinityKey): this;
  timeout(timeoutMs: number): this;
  submit(signal?: AbortSignal): Promise<ZLinkSpotCreateResult>;
}

export interface ZLinkSpotGetOrCreateCall extends ZLinkSpotCreateCall {}
