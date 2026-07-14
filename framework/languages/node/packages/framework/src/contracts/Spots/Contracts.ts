import type { ZLinkActor } from '../Actors';
import type { ZLinkPublishCall, ZLinkRequestCall, ZLinkSendCall } from '../Channels';
import type { RoutingId, Type, ZLinkMessage } from '../Common';
import type { ZLinkSpotTimerHandler } from '../Handlers';
import type { ZLinkTimer, ZLinkTimerOptions } from '../Timers';
import type { ZLinkEntrySpot, ZLinkSpot } from './ZLinkSpot';
import type { SpotHandle } from './SpotHandle';

export interface ZLinkActorHandlerRegistry {
  addHandler<THandler>(handlerType: Type<THandler>): this;
  addActorPacket<THandler, TActor extends ZLinkActor>(
    handlerType: Type<THandler>,
    actorType: Type<TActor>
  ): this;
}

export interface ZLinkActorTransferAdapter<TActor extends ZLinkActor> {
  transferOut(actor: TActor): Promise<ZLinkMessage>;
  transferIn(actorId: string, state: ZLinkMessage): Promise<TActor>;
}

export interface ZLinkSpotHandlerRegistry extends ZLinkActorHandlerRegistry {
  addPacket<THandler>(handlerType: Type<THandler>): this;
  addSubscribe<THandler>(handlerType: Type<THandler>, topic: string): this;
}

export interface ZLinkWorkerCall<T> {
  timeoutMs(durationMs: number): ZLinkWorkerCall<T>;
  submit(signal?: AbortSignal): Promise<T>;
  yield(signal?: AbortSignal): Promise<T>;
}

export interface ZLinkSpotCommonContext<
  TActor extends ZLinkActor = ZLinkActor,
  TSpot = ZLinkSpot<TActor>
> {
  readonly spotRid: RoutingId;
  readonly nodeRid: RoutingId;
  readonly routingId: RoutingId;
  readonly handlers: ZLinkSpotHandlerRegistry;
  readonly outbound: ZLinkSpotOutbound;
  addTimer<THandler extends ZLinkSpotTimerHandler<TSpot>>(
    name: string,
    periodMs: number,
    handlerType: Type<THandler>,
    options?: ZLinkTimerOptions,
    signal?: AbortSignal
  ): Promise<ZLinkTimer>;
  /**
   * Schedules `work` as an asynchronous deferral on the main event loop.
   *
   * Await `submit()` to keep the current Spot turn until `work` completes, or
   * await `yield()` to let another turn run while waiting.
   * This does not provide CPU thread offload. `work` must not touch Spot state;
   * mutate Spot state only after the awaited terminator completes.
   */
  runWorker<T>(work: (signal: AbortSignal) => T | Promise<T>): ZLinkWorkerCall<T>;
}

export interface ZLinkSpotContext<
  TActor extends ZLinkActor = ZLinkActor,
  TSpot extends ZLinkSpot<TActor> = ZLinkSpot<TActor>
> extends ZLinkSpotCommonContext<TActor, TSpot> {
  /**
   * Schedules `work` as an asynchronous deferral on the main event loop.
   *
   * Await `submit()` to keep the current Spot turn until `work` completes, or
   * await `yield()` to let another turn run while waiting.
   * This does not provide CPU thread offload. `work` must not touch Spot state;
   * mutate Spot state only after the awaited terminator completes.
   */
  runWorker<T>(work: (signal: AbortSignal) => T | Promise<T>): ZLinkWorkerCall<T>;
  leaveActor(actor: TActor, signal?: AbortSignal): Promise<void>;
  close(signal?: AbortSignal): Promise<boolean>;
}

export interface ZLinkEntrySpotContext<
  TActor extends ZLinkActor = ZLinkActor,
  TEntrySpot extends ZLinkEntrySpot<TActor> = ZLinkEntrySpot<TActor>
> extends ZLinkSpotCommonContext<TActor, TEntrySpot> {
  /**
   * Schedules `work` as an asynchronous deferral on the main event loop.
   *
   * Await `submit()` to keep the current Entry Spot turn until `work`
   * completes, or await `yield()` to let another turn run while waiting.
   * This does not provide CPU thread offload. `work` must not touch Spot state;
   * mutate Spot state only after the awaited terminator completes.
   */
  runWorker<T>(work: (signal: AbortSignal) => T | Promise<T>): ZLinkWorkerCall<T>;
  destroyActor(actor: TActor, signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSpotActorReplyOptions {
  metadata(key: string, value: string): this;
  compress(enabled?: boolean): this;
}

export interface ZLinkSpotOutbound {
  sendToSpot(spot: SpotHandle, message: unknown): ZLinkSendCall;
  requestToSpot(spot: SpotHandle, request: unknown): ZLinkRequestCall;
  publish(topic: string, event: unknown): ZLinkPublishCall;
  sendToChannel(channelName: string, message: unknown): ZLinkSendCall;
  requestToChannel(channelName: string, request: unknown): ZLinkRequestCall;
}

export enum ZLinkSpotCreateState {
  Existing = 'existing',
  Created = 'created',
  Rejected = 'rejected'
}

export interface ZLinkSpotCreateResult {
  readonly spotRid: RoutingId;
  readonly state: ZLinkSpotCreateState;
  readonly reply?: unknown;
}

export interface ZLinkSpotInfo {
  readonly spotRid: RoutingId;
}

export interface ZLinkSpotManager {
  create<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult>;
  create<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    request: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult>;
  create<TSpot extends ZLinkSpot, TRequest>(
    spotType: Type<TSpot>,
    request: TRequest,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult>;
  getOrCreate<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult>;
  getOrCreate<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    request: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult>;
  getOrCreate<TSpot extends ZLinkSpot, TRequest>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    request: TRequest,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult>;
  find(spotRid: RoutingId, signal?: AbortSignal): Promise<ZLinkSpotInfo | null>;
  list(signal?: AbortSignal): Promise<readonly ZLinkSpotInfo[]>;
  close(spotRid: RoutingId, signal?: AbortSignal): Promise<boolean>;
}
