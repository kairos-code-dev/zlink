import { ZLinkConfigurationException } from '../configuration';

type ZLinkRequestSubmit<TReply> = (
  resolve: (reply: TReply) => void,
  reject: (error: unknown) => void
) => boolean;

interface ZLinkPendingSubmitOptions {
  readonly timeoutMs: number;
  readonly signal?: AbortSignal;
}

export class ZLinkAsyncSubmitter {
  private readonly queue: ZLinkPendingSubmit<unknown>[] = [];
  private readonly active = new Set<ZLinkPendingSubmit<unknown>>();
  private readonly timeoutMs: number;
  private readonly capacity: number;
  private readyRegistered = false;
  private disposed = false;

  constructor(
    private readonly registerSendReady: (handler: () => void) => void,
    options: { readonly timeoutMs?: number; readonly capacity?: number } = {}
  ) {
    this.timeoutMs = options.timeoutMs ?? 200;
    this.capacity = options.capacity ?? 4096;
  }

  submitCommand(submit: () => boolean, signal?: AbortSignal): Promise<void> {
    const pending = this.createPending<void>(
      () => submit(),
      true,
      signal
    );
    if (pending.trySubmit()) {
      return pending.promise;
    }
    return this.enqueue(pending);
  }

  submitRequest<TReply>(submit: ZLinkRequestSubmit<TReply>, signal?: AbortSignal): Promise<TReply> {
    const pending = this.createPending<TReply>(submit, false, signal);
    if (pending.trySubmit()) {
      return pending.promise;
    }
    return this.enqueue(pending);
  }

  dispose(): void {
    if (this.disposed) {
      return;
    }
    this.disposed = true;
    const error = new ZLinkConfigurationException('ZLink async submitter is disposed.');
    this.queue.length = 0;
    for (const pending of this.active) {
      pending.reject(error);
    }
    this.active.clear();
  }

  private createPending<TReply>(
    submit: ZLinkRequestSubmit<TReply>,
    completeOnAccepted: boolean,
    signal?: AbortSignal
  ): ZLinkPendingSubmit<TReply> {
    if (this.disposed) {
      throw new ZLinkConfigurationException('ZLink async submitter is disposed.');
    }
    throwIfAborted(signal);
    const pending = new ZLinkPendingSubmit<TReply>(
      submit,
      completeOnAccepted,
      {
        timeoutMs: this.timeoutMs,
        signal
      }
    );
    this.active.add(pending as ZLinkPendingSubmit<unknown>);
    pending.promise.then(
      () => this.active.delete(pending as ZLinkPendingSubmit<unknown>),
      () => this.active.delete(pending as ZLinkPendingSubmit<unknown>)
    );
    return pending;
  }

  private enqueue<TReply>(pending: ZLinkPendingSubmit<TReply>): Promise<TReply> {
    if (this.disposed) {
      pending.reject(new ZLinkConfigurationException('ZLink async submitter is disposed.'));
      return pending.promise;
    }
    if (this.queue.length >= this.capacity) {
      pending.reject(new ZLinkConfigurationException('ZLink async submit queue is full.'));
      return pending.promise;
    }

    this.ensureReadyHandler();
    this.queue.push(pending as ZLinkPendingSubmit<unknown>);
    this.drain();
    return pending.promise;
  }

  private ensureReadyHandler(): void {
    if (this.readyRegistered) {
      return;
    }
    this.readyRegistered = true;
    this.registerSendReady(() => this.drain());
  }

  private drain(): void {
    if (this.disposed) {
      return;
    }
    while (this.queue.length > 0) {
      const pending = this.queue[0];
      if (!pending.trySubmit()) {
        return;
      }
      this.queue.shift();
    }
  }
}

class ZLinkPendingSubmit<TReply> {
  readonly promise: Promise<TReply>;
  private resolvePromise!: (reply: TReply) => void;
  private rejectPromise!: (error: unknown) => void;
  private readonly signal: AbortSignal | undefined;
  private readonly abortHandler: (() => void) | undefined;
  private readonly timeout: ReturnType<typeof setTimeout>;
  private completed = false;

  constructor(
    private readonly submit: ZLinkRequestSubmit<TReply>,
    private readonly completeOnAccepted: boolean,
    options: ZLinkPendingSubmitOptions
  ) {
    this.signal = options.signal;
    this.promise = new Promise<TReply>((resolve, reject) => {
      this.resolvePromise = resolve;
      this.rejectPromise = reject;
    });
    this.timeout = setTimeout(
      () => this.reject(new ZLinkConfigurationException('ZLink async submit timed out.')),
      options.timeoutMs
    );
    this.timeout.unref?.();
    if (options.signal !== undefined) {
      this.abortHandler = () => this.reject(new Error('The operation was aborted.'));
      options.signal.addEventListener('abort', this.abortHandler, { once: true });
    }
  }

  trySubmit(): boolean {
    if (this.completed) {
      return true;
    }
    let accepted: boolean;
    try {
      accepted = this.submit(
        (reply) => this.resolve(reply),
        (error) => this.reject(error)
      );
    } catch (error) {
      this.reject(error);
      return true;
    }
    if (accepted && this.completeOnAccepted) {
      this.resolve(undefined as TReply);
    }
    return accepted;
  }

  resolve(reply: TReply): void {
    if (this.completed) {
      return;
    }
    this.completed = true;
    this.cleanup();
    this.resolvePromise(reply);
  }

  reject(error: unknown): void {
    if (this.completed) {
      return;
    }
    this.completed = true;
    this.cleanup();
    this.rejectPromise(error);
  }

  private cleanup(): void {
    clearTimeout(this.timeout);
    if (this.signal !== undefined && this.abortHandler !== undefined) {
      this.signal.removeEventListener('abort', this.abortHandler);
    }
  }
}

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted) {
    throw new Error('The operation was aborted.');
  }
}
