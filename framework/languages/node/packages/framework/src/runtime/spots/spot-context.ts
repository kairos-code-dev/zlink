import type {
  RoutingId,
  Type,
  ZLinkActor,
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkProviderResolver,
  ZLinkRuntimeEventPublisher,
  ZLinkSpot,
  ZLinkSpotContext,
  ZLinkSpotHandlerRegistry,
  ZLinkSpotOutbound,
  ZLinkSpotTimerHandler,
  ZLinkTimerOptions
} from '../../contracts';
import type { ZLinkWorkerCall } from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
import { DefaultZLinkWorkerCall, ZLinkWorkerRuntime } from '../workers';
import {
  createTimerDiagnostics,
  type ZLinkSpotTimerRegistry
} from './spot-timer';
import type { ZLinkSpotSerialExecutor } from './spot-serial-executor';

interface ZLinkEntrySpotContextOptions {
  readonly nativeSpotRid: RoutingId;
  readonly nodeRid: RoutingId;
  readonly handlers: ZLinkSpotHandlerRegistry;
  readonly outbound: ZLinkSpotOutbound;
  readonly timers: ZLinkSpotTimerRegistry;
  readonly serial: ZLinkSpotSerialExecutor;
  readonly getEntrySpot: () => ZLinkEntrySpot;
  readonly spotNodeName: string;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
  readonly workerRuntime: ZLinkWorkerRuntime;
  readonly destroyActor?: (
    nodeRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ) => Promise<void>;
}

interface ZLinkSpotContextOptions {
  readonly meshName: string;
  readonly spotRid: RoutingId;
  readonly handlers: ZLinkSpotHandlerRegistry;
  readonly outbound: ZLinkSpotOutbound;
  readonly timers: ZLinkSpotTimerRegistry;
  readonly serial: ZLinkSpotSerialExecutor;
  readonly getSpot: () => ZLinkSpot | undefined;
  readonly nodeRid?: RoutingId;
  readonly nodeRidProvider?: () => RoutingId | undefined;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
  readonly workerRuntime: ZLinkWorkerRuntime;
  readonly close: (signal?: AbortSignal) => Promise<boolean>;
}

export function createEntrySpotContext(options: ZLinkEntrySpotContextOptions): ZLinkEntrySpotContext {
  return {
    meshName: options.spotNodeName,
    spotRid: options.nativeSpotRid,
    nodeRid: options.nodeRid,
    routingId: options.nativeSpotRid,
    handlers: options.handlers,
    outbound: options.outbound,
    destroyActor(actor: ZLinkActor, signal?: AbortSignal) {
      if (options.destroyActor === undefined) {
        throw new ZLinkConfigurationException('Entry Spot actor destroy runtime is not started.');
      }
      return options.destroyActor(options.nodeRid, actor, signal);
    },
    addTimer<THandler extends ZLinkSpotTimerHandler<ZLinkEntrySpot>>(
      name: string,
      periodMs: number,
      handlerType: Type<THandler>,
      timerOptions?: ZLinkTimerOptions,
      signal?: AbortSignal
    ) {
      return options.timers.add(
        name,
        periodMs,
        timerOptions,
        handlerType,
        options.serial,
        options.getEntrySpot(),
        options.providerResolver,
        signal,
        createTimerDiagnostics(
          options.spotNodeName,
          options.nodeRid,
          true,
          name,
          handlerType,
          options.runtimeEventPublisher
        )
      );
    },
    runCpuWorker<T>(work: (signal: AbortSignal) => T): ZLinkWorkerCall<T> {
      return new DefaultZLinkWorkerCall(options.serial, (timeoutMs, signal) =>
        options.workerRuntime.scheduleCpu(work, timeoutMs, signal));
    },
    runIoWorker<T>(work: (signal: AbortSignal) => Promise<T>): ZLinkWorkerCall<T> {
      return new DefaultZLinkWorkerCall(options.serial, (timeoutMs, signal) =>
        options.workerRuntime.scheduleIo(work, timeoutMs, signal));
    }
  };
}

export function createSpotContext(options: ZLinkSpotContextOptions): ZLinkSpotContext {
  return {
    meshName: options.meshName,
    spotRid: options.spotRid,
    get nodeRid() {
      return options.nodeRidProvider?.() ?? options.nodeRid ?? '';
    },
    routingId: options.spotRid,
    handlers: options.handlers,
    outbound: options.outbound,
    close: options.close,
    addTimer: <THandler extends ZLinkSpotTimerHandler<ZLinkSpot>>(
      name: string,
      periodMs: number,
      handlerType: Type<THandler>,
      timerOptions?: ZLinkTimerOptions,
      signal?: AbortSignal
    ) => {
      const spot = options.getSpot();
      if (spot === undefined) {
        throw new ZLinkConfigurationException('Spot timer cannot be registered before spot activation.');
      }
      return options.timers.add(
        name,
        periodMs,
        timerOptions,
        handlerType,
        options.serial,
        spot,
        options.providerResolver,
        signal,
        createTimerDiagnostics(String(options.spotRid), options.spotRid, false, name, handlerType, options.runtimeEventPublisher)
      );
    },
    runCpuWorker: <T>(work: (signal: AbortSignal) => T): ZLinkWorkerCall<T> =>
      new DefaultZLinkWorkerCall(options.serial, (timeoutMs, signal) =>
        options.workerRuntime.scheduleCpu(work, timeoutMs, signal)),
    runIoWorker: <T>(work: (signal: AbortSignal) => Promise<T>): ZLinkWorkerCall<T> =>
      new DefaultZLinkWorkerCall(options.serial, (timeoutMs, signal) =>
        options.workerRuntime.scheduleIo(work, timeoutMs, signal))
  };
}
