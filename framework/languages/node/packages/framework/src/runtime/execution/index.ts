import { AsyncLocalStorage } from 'node:async_hooks';

export interface ZLinkRuntimeTaskFailure {
  readonly taskName: string;
  readonly error: unknown;
}

interface ZLinkSpotSerialTurnContext {
  readonly executor: ZLinkSpotSerialExecutorLike;
  readonly turnId: number;
  readonly turn: ZLinkSpotSerialTurn;
}

interface ZLinkSpotSerialExecutorLike {
  readonly activeTurnId: number;
  post<T>(operation: () => Promise<T> | T): Promise<T>;
}

const spotSerialTurnStorage = new AsyncLocalStorage<ZLinkSpotSerialTurnContext>();

export class ZLinkSpotSerialTurn {
  private suspendedResolve: (() => void) | undefined;
  private suspendedPromise: Promise<void> | undefined;
  private suspendSignaled = false;
  private owner: Promise<unknown> | undefined;

  constructor(private readonly postResume: (turn: ZLinkSpotSerialTurn, resume: () => void) => boolean) {}

  get suspended(): Promise<void> {
    if (this.suspendSignaled) {
      return Promise.resolve();
    }
    this.suspendedPromise ??= new Promise((resolve) => {
      this.suspendedResolve = resolve;
    });
    return this.suspendedPromise;
  }

  bindOwner(owner: Promise<unknown>): void {
    this.owner = owner;
  }

  resetSuspension(): void {
    this.suspendSignaled = false;
    this.suspendedResolve = undefined;
    this.suspendedPromise = undefined;
  }

  async yieldPromise<T>(pending: Promise<T>): Promise<T> {
    this.signalSuspended();
    const result = await pending;
    await this.awaitResumePermit();
    return result;
  }

  async resumeOwnerUntilNextYield(): Promise<void> {
    const owner = this.owner;
    if (owner === undefined) {
      return;
    }
    await Promise.race([
      owner.then(() => undefined, () => undefined),
      this.suspended
    ]);
  }

  private signalSuspended(): void {
    if (this.suspendSignaled) {
      return;
    }
    this.suspendSignaled = true;
    this.suspendedResolve?.();
  }

  private awaitResumePermit(): Promise<void> {
    return new Promise((resolve, reject) => {
      if (!this.postResume(this, resolve)) {
        reject(new Error('ZLink Spot serial executor is closed.'));
      }
    });
  }
}

export function runZLinkSpotSerialTurn<T>(
  executor: ZLinkSpotSerialExecutorLike,
  turnId: number,
  turn: ZLinkSpotSerialTurn,
  operation: () => Promise<T> | T
): Promise<T> {
  return spotSerialTurnStorage.run(
    { executor, turnId, turn },
    async () => operation()
  );
}

export function captureZLinkSpotSerialTurn(
  executor?: ZLinkSpotSerialExecutorLike
): ZLinkSpotSerialTurn | undefined {
  const current = spotSerialTurnStorage.getStore();
  if (current === undefined) {
    return undefined;
  }
  if (executor !== undefined && current.executor !== executor) {
    return undefined;
  }
  return current.turn;
}

export interface ZLinkCapturedExecutionTurn {
  yieldPromise<T>(pending: Promise<T>): Promise<T>;
  post(callback: () => void): void;
}

export function captureZLinkExecutionTurn(): ZLinkCapturedExecutionTurn | undefined {
  const current = spotSerialTurnStorage.getStore();
  if (current === undefined) {
    return undefined;
  }
  return {
    yieldPromise: <T>(pending: Promise<T>) => current.turn.yieldPromise(pending),
    post: (callback: () => void) => { void current.executor.post(callback); }
  };
}

export function isCurrentZLinkSpotSerialTurn(executor: ZLinkSpotSerialExecutorLike): boolean {
  const current = spotSerialTurnStorage.getStore();
  return current !== undefined
    && current.executor === executor
    && current.turnId === executor.activeTurnId;
}

export type ZLinkRuntimeTaskFailureHandler = (failure: ZLinkRuntimeTaskFailure) => void;
export type ZLinkRuntimeTaskCallback = (signal: AbortSignal) => Promise<void> | void;

export class ZLinkRuntimeErrorSink {
  private readonly handlers = new Set<ZLinkRuntimeTaskFailureHandler>();

  onRuntimeTaskException(handler: ZLinkRuntimeTaskFailureHandler): () => void {
    this.handlers.add(handler);
    return () => {
      this.handlers.delete(handler);
    };
  }

  reportHandlerException(error: unknown): void {
    this.reportRuntimeTaskException('handler', error);
  }

  reportRuntimeTaskException(taskName: string, error: unknown): void {
    if (process.env.ZLINK_DEBUG_RUNTIME_TASKS === '1') {
      console.error(`[zlink-runtime-task] ${taskName}`, error);
    }
    for (const handler of this.handlers) {
      try {
        handler({ taskName, error });
      } catch {
        // Error sinks must not create a second unhandled failure path.
      }
    }
  }
}

export class ZLinkRuntimeTaskRunner {
  constructor(
    readonly errorSink: ZLinkRuntimeErrorSink,
    private readonly shutdownSignal: AbortSignal
  ) {}

  runDetached(taskName: string, callback: ZLinkRuntimeTaskCallback): void {
    void this.run(taskName, callback);
  }

  run(taskName: string, callback: ZLinkRuntimeTaskCallback): Promise<void> {
    return Promise.resolve()
      .then(() => callback(this.shutdownSignal))
      .catch((error) => {
        if (this.shutdownSignal.aborted && isCancellationError(error)) {
          return;
        }
        this.errorSink.reportRuntimeTaskException(taskName, error);
      });
  }

  reportErrorSinkFailure(_taskName: string, _error: unknown): void {
    // Mirrors the dotnet no-op: a broken error sink must not fail the runtime.
  }
}

export class ZLinkFrameworkRuntimeState {
  readonly abortController = new AbortController();
  readonly errorSink = new ZLinkRuntimeErrorSink();
  readonly taskRunner = new ZLinkRuntimeTaskRunner(this.errorSink, this.abortController.signal);
  readonly listenerTasks: Promise<void>[] = [];

  constructor(readonly context: { dispose(): Promise<void> }) {}

  async dispose(): Promise<void> {
    this.abortController.abort();
    await this.waitForListenerTasks();
    await this.context.dispose();
  }

  private async waitForListenerTasks(): Promise<void> {
    if (this.listenerTasks.length === 0) {
      return;
    }
    try {
      await Promise.all(this.listenerTasks);
    } catch (error) {
      if (!isCancellationError(error)) {
        this.errorSink.reportRuntimeTaskException('listener', error);
      }
    }
  }
}

function isCancellationError(error: unknown): boolean {
  return (
    error instanceof DOMException && error.name === 'AbortError'
  ) || (
    error instanceof Error && /aborted|cancelled|canceled/i.test(error.message)
  );
}
