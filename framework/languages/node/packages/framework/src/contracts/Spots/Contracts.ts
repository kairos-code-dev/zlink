import type { ZLinkActor } from '../Actors';
import type { ZLinkPublishCall, ZLinkRequestCall, ZLinkSendCall, ZLinkYieldRequestCall } from '../Channels';
import type { RoutingId, Type, ZLinkMessage } from '../Common';
import type { ZLinkSpotTimerHandler } from '../Handlers';
import type { ZLinkTimer, ZLinkTimerOptions } from '../Timers';
import type { ZLinkEntrySpot, ZLinkSpot } from './ZLinkSpot';

export interface ZLinkActorHandlerRegistry {
  addHandler(handlerType: Type): this;
}

export interface ZLinkSpotHandlerRegistry<TActor extends ZLinkActor = ZLinkActor> extends ZLinkActorHandlerRegistry {
  addPacket(handlerType: Type, packetName?: string): this;
  packet(packetName: string, handlerType: Type): this;
  addSubscribe(handlerType: Type, topic: string): this;
  subscribe(topic: string, handlerType: Type): this;
  addSpotHandler(handlerType: Type): this;
  actorSend(packetName: string, handlerType: Type, actorType?: Type<TActor>): this;
  actorRequest(packetName: string, handlerType: Type, actorType?: Type<TActor>): this;
}

/**
 * Fluent terminator surface for a `runWorker(...)` job.
 *
 * The job result is delivered back on the owning Spot serial executor:
 * `onCompleted(...)`/`onError(...)` callbacks are enqueued into the Spot
 * dispatch queue and never run inline in the worker completion turn.
 */
export interface ZLinkWorkerCall<T> {
  /**
   * Fails the caller with a `WorkerTimedOut` framework error after
   * `durationMs` and aborts the work's `AbortSignal`. A completion that
   * arrives after the timeout is dropped without invoking user callbacks.
   */
  timeoutMs(durationMs: number): ZLinkWorkerCall<T>;
  /**
   * Awaitable terminator (gated path). The returned promise settles in the
   * owning Spot serial order; a handler that awaits it keeps the Spot gate
   * until completion.
   */
  submit(signal?: AbortSignal): Promise<T>;
  /**
   * Yield terminator. Only use when the handler can safely let unrelated
   * Spot/Entry Spot work run while the worker result is pending.
   */
  yield(signal?: AbortSignal): Promise<T>;
  /**
   * Callback terminator (detached path, explicit interleaving opt-in).
   * `callback`/`onError` always re-enter the owning Spot serial executor,
   * so they must re-validate Spot state captured before submission.
   */
  onCompleted(
    callback: (result: T, signal?: AbortSignal) => void | Promise<void>,
    onError?: (error: unknown, signal?: AbortSignal) => void | Promise<void>,
    signal?: AbortSignal,
  ): void;
}

export interface ZLinkSpotCommonContext<
  TActor extends ZLinkActor = ZLinkActor,
  TSpot = ZLinkSpot<TActor>
> {
  readonly spotRid: RoutingId;
  readonly nodeRid: RoutingId;
  readonly routingId: RoutingId;
  readonly handlers: ZLinkSpotHandlerRegistry<TActor>;
  readonly outbound: ZLinkSpotOutbound;
  addTimer<THandler extends ZLinkSpotTimerHandler<TSpot>>(
    name: string,
    periodMs: number,
    handlerType: Type<THandler>,
    options?: ZLinkTimerOptions,
    signal?: AbortSignal
  ): Promise<ZLinkTimer>;
  /**
   * Schedules `work` off the Spot serial line as an asynchronous deferral.
   *
   * The closure-based `runWorker(...)` releases the Spot dispatch queue while
   * `work` runs, but it does not provide CPU thread offload: `work` still
   * executes on the main event loop. Long synchronous CPU work must be split
   * into a separate service or process instead. `work` must not touch Spot
   * state; mutate Spot state only in the completion callback/continuation,
   * which runs back on this Spot's serial executor.
   */
  runWorker<T>(work: (signal: AbortSignal) => T | Promise<T>): ZLinkWorkerCall<T>;
}

export interface ZLinkSpotContext<
  TActor extends ZLinkActor = ZLinkActor,
  TSpot extends ZLinkSpot<TActor> = ZLinkSpot<TActor>
> extends ZLinkSpotCommonContext<TActor, TSpot> {
  /**
   * Schedules `work` off the Spot serial line as an asynchronous deferral.
   *
   * The closure-based `runWorker(...)` releases the Spot dispatch queue while
   * `work` runs, but it does not provide CPU thread offload: `work` still
   * executes on the main event loop. Long synchronous CPU work must be split
   * into a separate service or process instead. `work` must not touch Spot
   * state; mutate Spot state only in the completion callback/continuation,
   * which runs back on this Spot's serial executor.
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
   * Schedules `work` off the Entry Spot serial line as an asynchronous deferral.
   *
   * The closure-based `runWorker(...)` releases the Entry Spot dispatch queue
   * while `work` runs, but it does not provide CPU thread offload: `work`
   * still executes on the main event loop. Long synchronous CPU work must be
   * split into a separate service or process instead. `work` must not touch
   * Spot state; mutate Spot state only in the completion
   * callback/continuation, which runs back on this Entry Spot's serial
   * executor.
   */
  runWorker<T>(work: (signal: AbortSignal) => T | Promise<T>): ZLinkWorkerCall<T>;
  destroyActor(actor: TActor, signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSpotActorReplyOptions {
  metadata(key: string, value: string): this;
  compress(enabled?: boolean): this;
}

export interface ZLinkSpotOutbound {
  sendToSpot(spotRid: RoutingId, message: unknown): ZLinkSendCall;
  requestToSpot(spotRid: RoutingId, request: unknown): ZLinkYieldRequestCall;
  publish(topic: string, event: unknown): ZLinkPublishCall;
  sendToChannel(channelName: string, message: unknown): ZLinkSendCall;
  requestToChannel(channelName: string, request: unknown): ZLinkYieldRequestCall;
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
  executeOnSpot<TSpot extends ZLinkSpot, TResult>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    operation: (spot: TSpot) => TResult | Promise<TResult>,
    signal?: AbortSignal
  ): Promise<TResult>;
  find(spotRid: RoutingId, signal?: AbortSignal): Promise<ZLinkSpotInfo | null>;
  list(signal?: AbortSignal): Promise<readonly ZLinkSpotInfo[]>;
  close(spotRid: RoutingId, signal?: AbortSignal): Promise<boolean>;
}
