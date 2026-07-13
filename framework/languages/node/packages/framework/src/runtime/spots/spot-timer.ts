import type {
  RoutingId,
  Type,
  ZLinkEntrySpot,
  ZLinkProviderResolver,
  ZLinkRuntimeEventPublisher,
  ZLinkSpot,
  ZLinkSpotTimerHandler,
  ZLinkTimer,
  ZLinkTimerOptions,
  ZLinkTimerTick
} from '../../contracts';
import type {
  ZLinkEntrySpotTimerHandlerRegistration,
  ZLinkSpotTimerHandlerRegistration
} from '../../contracts/Configuration/RegistrationTypes';
import {
  ZLinkSpotEventKind,
  ZLinkTimerOverrunPolicy
} from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
import { throwIfAborted } from '../abort';
import { ZLinkSpotSerialExecutor } from './spot-serial-executor';
import { createProviderInstance } from './spot-provider';
import { createInboundFlow, runWithFlow } from '../diagnostics/flow-context';

type ZLinkTimerOwnerSpot = ZLinkSpot | ZLinkEntrySpot;
type ZLinkTimerFailureReporter = (
  tick: ZLinkTimerTick,
  cause: unknown,
  event?: ZLinkSpotEventKind.TimerHandlerFailed | ZLinkSpotEventKind.TimerStoppedAfterUnhandledException
) => Promise<void> | void;

export class ZLinkSpotTimerRegistry {
  private readonly timers = new Set<ZLinkTimer>();

  constructor(private readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics) {}

  async add<TSpot extends ZLinkTimerOwnerSpot, THandler extends ZLinkSpotTimerHandler<TSpot>>(
    name: string,
    periodMs: number,
    options: ZLinkTimerOptions | undefined,
    handlerType: Type<THandler>,
    serial: ZLinkSpotSerialExecutor,
    spot: TSpot,
    providerResolver?: ZLinkProviderResolver,
    signal?: AbortSignal,
    reportFailure?: ZLinkTimerFailureReporter
  ): Promise<ZLinkTimer> {
    validateTimer(name, periodMs, options);
    throwIfAborted(signal);
    const handler = await createProviderInstance(handlerType, providerResolver);
    const timer = new ZLinkManagedTimer(
      name,
      periodMs,
      normalizeTimerOptions(options),
      async (tick) => {
        this.metrics?.duration('zlink.spot.timer.tick.lateness', tick.delayMs / 1000);
        const timerFlow = createInboundFlow(undefined, 'Timer');
        await serial.execute(() => runWithFlow(timerFlow, () => handler.handle(spot, tick)));
      },
      reportFailure,
      () => !serial.isExecuting
    );
    this.timers.add(timer);
    return timer;
  }

  async dispose(): Promise<void> {
    const timers = [...this.timers];
    this.timers.clear();
    for (const timer of timers) {
      await timer.dispose();
    }
  }
}

export class ZLinkManagedTimer implements ZLinkTimer {
  private disposed = false;
  private readonly startedAtMs = Date.now();
  private deliveryIndex = 0n;
  private lastScheduledIndex = 0n;
  private timeout: NodeJS.Timeout | undefined;
  private running: Promise<void> = Promise.resolve();

  constructor(
    private readonly name: string,
    private readonly periodMs: number,
    private readonly options: Required<ZLinkTimerOptions>,
    private readonly onTick: (tick: ZLinkTimerTick) => Promise<void>,
    private readonly onFailure?: ZLinkTimerFailureReporter,
    private readonly shouldWaitForRunningOnCancel: () => boolean = () => true
  ) {
    this.scheduleNext();
  }

  get isDisposed(): boolean {
    return this.disposed;
  }

  async cancel(_signal?: AbortSignal): Promise<void> {
    if (this.disposed) {
      return;
    }
    this.disposed = true;
    if (this.timeout !== undefined) {
      clearTimeout(this.timeout);
      this.timeout = undefined;
    }
    if (this.shouldWaitForRunningOnCancel()) {
      await this.running;
    }
  }

  dispose(): Promise<void> {
    return this.cancel();
  }

  private scheduleNext(): void {
    if (this.disposed) {
      return;
    }

    const delayMs = this.options.overrunPolicy === ZLinkTimerOverrunPolicy.DelayNextTick
      ? this.periodMs
      : Math.max(0, Number(this.lastScheduledIndex + 1n) * this.periodMs - this.elapsedMs());
    this.timeout = setTimeout(() => {
      this.timeout = undefined;
      this.running = this.fire().catch(() => undefined);
    }, delayMs);
  }

  private async fire(): Promise<void> {
    if (this.disposed) {
      return;
    }

    const scheduledIndex = this.selectScheduledIndex();
    const skippedTicks = scheduledIndex - this.lastScheduledIndex - 1n;
    const startedElapsedMs = this.elapsedMs();
    const scheduledElapsedMs = Number(scheduledIndex) * this.periodMs;
    this.deliveryIndex += 1n;
    const tick: ZLinkTimerTick = {
      name: this.name,
      deliveryIndex: this.deliveryIndex,
      scheduledIndex,
      periodMs: this.periodMs,
      scheduledAt: new Date(this.startedAtMs + scheduledElapsedMs),
      startedAt: new Date(this.startedAtMs + startedElapsedMs),
      scheduledElapsedMs,
      startedElapsedMs,
      delayMs: startedElapsedMs - scheduledElapsedMs,
      skippedTicks
    };

    let shouldContinue = true;
    try {
      await this.onTick(tick);
    } catch (cause) {
      await this.onFailure?.(tick, cause);
      shouldContinue = !this.options.stopOnUnhandledException;
      if (!shouldContinue) {
        await this.onFailure?.(tick, cause, ZLinkSpotEventKind.TimerStoppedAfterUnhandledException);
      }
    }

    this.lastScheduledIndex = scheduledIndex;
    if (!shouldContinue) {
      this.disposed = true;
      return;
    }
    this.scheduleNext();
  }

  private selectScheduledIndex(): bigint {
    if (this.options.overrunPolicy === ZLinkTimerOverrunPolicy.DelayNextTick) {
      return this.lastScheduledIndex + 1n;
    }

    const dueScheduledIndex = BigInt(Math.max(1, Math.floor(this.elapsedMs() / this.periodMs)));
    if (this.options.overrunPolicy === ZLinkTimerOverrunPolicy.SkipLateTicks) {
      return dueScheduledIndex;
    }

    const availableTicks = dueScheduledIndex - this.lastScheduledIndex;
    const maxCatchUpTicks = BigInt(this.options.maxCatchUpTicks);
    if (availableTicks > maxCatchUpTicks) {
      return dueScheduledIndex - maxCatchUpTicks + 1n;
    }

    return this.lastScheduledIndex + 1n;
  }

  private elapsedMs(): number {
    return Date.now() - this.startedAtMs;
  }
}

export function createTimerDiagnostics(
  sourceName: string,
  spotRid: RoutingId,
  isEntrySpot: boolean,
  timerName: string,
  handlerType: Type,
  publisher: ZLinkRuntimeEventPublisher | undefined
): ZLinkTimerFailureReporter | undefined {
  if (publisher === undefined) {
    return undefined;
  }
  return async (tick, cause, event = ZLinkSpotEventKind.TimerHandlerFailed) => {
    try {
      await publisher.publish({
        sourceName,
        timestamp: new Date(),
        event,
        timerDiagnostic: {
          spotRid,
          isEntrySpot,
          timerName,
          handlerType: handlerType.name,
          deliveryIndex: tick.deliveryIndex,
          scheduledIndex: tick.scheduledIndex,
          exceptionType: exceptionType(cause),
          exceptionMessage: exceptionMessage(cause)
        }
      });
    } catch {
    }
  };
}

interface ZLinkEntrySpotTimerRegistrationSet {
  readonly timerHandlers?: readonly ZLinkEntrySpotTimerHandlerRegistration[];
}

interface ZLinkUserSpotTimerRegistrationSet {
  readonly timerHandlers?: readonly ZLinkSpotTimerHandlerRegistration[];
}

export async function addEntrySpotTimerRegistrations(
  timers: ZLinkSpotTimerRegistry,
  entrySpotType: Type<ZLinkEntrySpot>,
  entrySpot: ZLinkEntrySpot,
  serial: ZLinkSpotSerialExecutor,
  registrations: ZLinkEntrySpotTimerRegistrationSet,
  options: {
    readonly providerResolver?: ZLinkProviderResolver;
    readonly spotNodeName: string;
    readonly nodeRid: RoutingId;
    readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
  }
): Promise<void> {
  for (const handler of registrations.timerHandlers ?? []) {
    if (handler.entrySpotType === entrySpotType) {
      await timers.add(
        handler.name,
        handler.periodMs,
        handler.options,
        handler.handlerType as Type<ZLinkSpotTimerHandler<ZLinkEntrySpot>>,
        serial,
        entrySpot,
        options.providerResolver,
        undefined,
        createTimerDiagnostics(
          options.spotNodeName,
          options.nodeRid,
          true,
          handler.name,
          handler.handlerType,
          options.runtimeEventPublisher
        )
      );
    }
  }
}

export async function addSpotTimerRegistrations(
  timers: ZLinkSpotTimerRegistry,
  spotType: Type<ZLinkSpot>,
  spotRid: RoutingId,
  spot: ZLinkSpot,
  serial: ZLinkSpotSerialExecutor,
  registrations: ZLinkUserSpotTimerRegistrationSet,
  options: {
    readonly providerResolver?: ZLinkProviderResolver;
    readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
    readonly signal?: AbortSignal;
  }
): Promise<void> {
  for (const handler of registrations.timerHandlers ?? []) {
    if (handler.spotType === spotType) {
      await timers.add(
        handler.name,
        handler.periodMs,
        handler.options,
        handler.handlerType as Type<ZLinkSpotTimerHandler<ZLinkSpot>>,
        serial,
        spot,
        options.providerResolver,
        options.signal,
        createTimerDiagnostics(
          String(spotRid),
          spotRid,
          false,
          handler.name,
          handler.handlerType,
          options.runtimeEventPublisher
        )
      );
    }
  }
}

function validateTimer(name: string, periodMs: number, options: ZLinkTimerOptions | undefined): void {
  if (name.trim().length === 0) {
    throw new ZLinkConfigurationException('SPOT timer name must not be empty.');
  }
  if (!Number.isFinite(periodMs) || periodMs <= 0) {
    throw new ZLinkConfigurationException('SPOT timer period must be greater than zero.');
  }
  if (
    options?.overrunPolicy !== undefined
    && !Object.values(ZLinkTimerOverrunPolicy).includes(options.overrunPolicy)
  ) {
    throw new ZLinkConfigurationException('SPOT timer overrun policy is not supported.');
  }
  if (
    options?.overrunPolicy === ZLinkTimerOverrunPolicy.CatchUpBounded
    && (options.maxCatchUpTicks === undefined || options.maxCatchUpTicks <= 0)
  ) {
    throw new ZLinkConfigurationException('SPOT timer MaxCatchUpTicks must be greater than zero.');
  }
}

function normalizeTimerOptions(options: ZLinkTimerOptions | undefined): Required<ZLinkTimerOptions> {
  return {
    overrunPolicy: options?.overrunPolicy ?? ZLinkTimerOverrunPolicy.SkipLateTicks,
    maxCatchUpTicks: options?.maxCatchUpTicks ?? 1,
    stopOnUnhandledException: options?.stopOnUnhandledException ?? false
  };
}

function exceptionType(cause: unknown): string {
  return cause instanceof Error ? cause.name : typeof cause;
}

function exceptionMessage(cause: unknown): string {
  return cause instanceof Error ? cause.message : String(cause);
}
