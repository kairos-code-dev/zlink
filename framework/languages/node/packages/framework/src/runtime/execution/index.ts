export interface ZLinkRuntimeTaskFailure {
  readonly taskName: string;
  readonly error: unknown;
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
