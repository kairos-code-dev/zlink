import { ZLinkFrameworkInternalErrorKind, createInternalFrameworkException  } from '../framework-errors-internal';
import {
  ZLinkFrameworkException
} from '../../contracts';
import { createAbortError, throwIfAborted } from '../abort';

type ZLinkRequestSubmit<TReply> = (
  resolve: (reply: TReply) => void,
  reject: (error: unknown) => void
) => boolean;

interface ZLinkPendingSubmitOptions {
  readonly timeoutMs: number | undefined;
  readonly signal?: AbortSignal;
  readonly onDiscard?: () => void;
}

export class ZLinkAsyncSubmitter {
  private readonly queue: ZLinkPendingSubmit<unknown>[] = [];
  private readonly capacityWaiters: ZLinkPendingSubmit<unknown>[] = [];
  private readonly active = new Set<ZLinkPendingSubmit<unknown>>();
  private requestActive = false;
  private readonly timeoutMs: number | undefined;
  private readonly capacity: number;
  private readonly waiterCapacity: number;
  private readonly onCommandFailure: ((error: unknown) => void) | undefined;
  private readyRegistered = false;
  private readyCredit = false;
  private disposed = false;

  constructor(
    private readonly registerSendReady: (handler: () => void) => void,
    options: {
      readonly timeoutMs?: number;
      readonly capacity?: number;
      readonly waiterCapacity?: number;
      readonly onCommandFailure?: (error: unknown) => void;
    } = {}
  ) {
    this.timeoutMs = options.timeoutMs === -1 ? undefined : options.timeoutMs ?? 1000;
    this.capacity = options.capacity ?? 4096;
    this.waiterCapacity = options.waiterCapacity ?? this.capacity;
    this.onCommandFailure = options.onCommandFailure;
  }

  submitCommand(
    submit: () => boolean,
    signal?: AbortSignal,
    onDiscard?: () => void,
    timeoutMs?: number
  ): Promise<void> {
    const overflow = this.rejectHardOverflow<void>(signal, onDiscard);
    if (overflow !== undefined) return overflow;
    const pending = this.createPending<void>(
      () => submit(),
      true,
      signal,
      timeoutMs,
      onDiscard
    );
    this.ensureReadyHandler();
    if (this.trySubmitPending(pending)) {
      this.readyCredit = false;
      return pending.promise;
    }
    return this.enqueue(pending);
  }

  trySubmitCommand(submit: () => boolean, onDiscard?: () => void): boolean {
    if (this.disposed) {
      onDiscard?.();
      return false;
    }
    try {
      const accepted = submit();
      if (!accepted) {
        onDiscard?.();
      }
      return accepted;
    } catch (error) {
      throw discardBeforePending(error, onDiscard);
    }
  }

  submitCommandOneWay(submit: () => boolean, signal?: AbortSignal, onDiscard?: () => void): void {
    const overflow = this.rejectHardOverflow<void>(signal, onDiscard);
    if (overflow !== undefined) {
      void overflow.catch(() => undefined);
      throw hardOverflowError();
    }
    const pending = this.createPending<void>(
      () => submit(),
      true,
      signal,
      undefined,
      onDiscard
    );
    this.ensureReadyHandler();
    if (this.trySubmitPending(pending)) {
      this.readyCredit = false;
      this.finishOneWaySubmission(pending);
      return;
    }
    if (this.disposed) {
      this.rejectOneWaySubmission(pending, runtimeShutdownError());
    }
    if (this.pendingQueueLength() >= this.capacity) {
      this.capacityWaiters.push(pending as ZLinkPendingSubmit<unknown>);
      this.finishOneWaySubmission(pending);
      return;
    }
    this.queue.push(pending as ZLinkPendingSubmit<unknown>);
    this.consumeReadyCredit();
    this.finishOneWaySubmission(pending);
  }

  submitRequest<TReply>(
    submit: ZLinkRequestSubmit<TReply>,
    signal?: AbortSignal,
    timeoutMs?: number,
    onDiscard?: () => void
  ): Promise<TReply> {
    const overflow = this.rejectHardOverflow<TReply>(signal, onDiscard, requestTimeoutError);
    if (overflow !== undefined) return overflow;
    const pending = this.createPending<TReply>(submit, false, signal, timeoutMs, onDiscard);
    this.ensureReadyHandler();
    if (!this.requestActive && this.trySubmitPending(pending)) {
      this.readyCredit = false;
      return pending.promise;
    }
    return this.enqueue(pending);
  }

  dispose(): void {
    if (this.disposed) {
      return;
    }
    this.disposed = true;
    this.readyCredit = false;
    const error = runtimeShutdownError();
    this.queue.length = 0;
    this.capacityWaiters.length = 0;
    for (const pending of this.active) {
      pending.reject(error);
    }
    this.active.clear();
  }

  rejectActive(error: unknown): void {
    for (const pending of this.active) {
      pending.reject(error);
    }
  }

  private createPending<TReply>(
    submit: ZLinkRequestSubmit<TReply>,
    completeOnAccepted: boolean,
    signal?: AbortSignal,
    timeoutMs?: number,
    onDiscard?: () => void
  ): ZLinkPendingSubmit<TReply> {
    if (this.disposed) {
      const error = runtimeShutdownError();
      throw discardBeforePending(error, onDiscard);
    }
    try {
      throwIfAborted(signal);
    } catch (error) {
      throw discardBeforePending(error, onDiscard);
    }
    let pending!: ZLinkPendingSubmit<TReply>;
    pending = new ZLinkPendingSubmit<TReply>(
      submit,
      completeOnAccepted,
      {
        timeoutMs: timeoutMs === -1 ? undefined : timeoutMs ?? this.timeoutMs,
        signal,
        onDiscard
      },
      () => this.completePending(pending as ZLinkPendingSubmit<unknown>)
    );
    this.active.add(pending as ZLinkPendingSubmit<unknown>);
    return pending;
  }

  private enqueue<TReply>(pending: ZLinkPendingSubmit<TReply>): Promise<TReply> {
    if (this.disposed) {
      pending.reject(runtimeShutdownError());
      return pending.promise;
    }
    if (this.queue.length >= this.capacity) {
      if (this.capacityWaiters.length >= this.waiterCapacity) {
        pending.reject(hardOverflowError());
        return pending.promise;
      }
      this.capacityWaiters.push(pending as ZLinkPendingSubmit<unknown>);
      return pending.promise;
    }

    this.queue.push(pending as ZLinkPendingSubmit<unknown>);
    this.consumeReadyCredit();
    return pending.promise;
  }

  private finishOneWaySubmission(pending: ZLinkPendingSubmit<void>): void {
    const submissionError = pending.takeSubmissionError();
    if (submissionError !== undefined) {
      void pending.promise.catch(() => undefined);
      throw submissionError;
    }
    void pending.promise.catch((error) => this.onCommandFailure?.(error));
  }

  private rejectOneWaySubmission(pending: ZLinkPendingSubmit<void>, error: unknown): never {
    pending.reject(error);
    void pending.promise.catch(() => undefined);
    throw error;
  }

  private ensureReadyHandler(): void {
    if (this.readyRegistered) {
      return;
    }
    this.readyRegistered = true;
    this.registerSendReady(() => this.onReady());
  }

  private onReady(): void {
    if (this.disposed) {
      return;
    }
    if (this.queue.length === 0 || (this.queue[0].isRequest && this.requestActive)) {
      this.readyCredit = true;
      return;
    }
    this.retryOne();
  }

  private consumeReadyCredit(): void {
    if (!this.readyCredit || this.queue.length === 0) {
      return;
    }
    if (this.queue[0].isRequest && this.requestActive) {
      return;
    }
    this.readyCredit = false;
    this.retryOne();
  }

  private retryOne(): void {
    if (this.disposed || this.queue.length === 0) {
      return;
    }
    const pending = this.queue[0];
    if (!this.trySubmitPending(pending)) {
      return;
    }
    this.removeQueued(pending);
  }

  private trySubmitPending<TReply>(pending: ZLinkPendingSubmit<TReply>): boolean {
    if (pending.isRequest && this.requestActive) {
      return false;
    }
    if (pending.isRequest) {
      this.requestActive = true;
    }
    const accepted = pending.trySubmit();
    if (!pending.isRequest) {
      return accepted;
    }
    if (!accepted) {
      this.requestActive = false;
      return false;
    }
    pending.promise.then(
      () => this.finishRequest(),
      () => this.finishRequest()
    );
    return true;
  }

  private finishRequest(): void {
    this.requestActive = false;
    this.consumeReadyCredit();
  }

  private pendingQueueLength(): number {
    return this.queue.length;
  }

  private rejectHardOverflow<TReply>(
    signal: AbortSignal | undefined,
    onDiscard: (() => void) | undefined,
    overflowError: () => Error = hardOverflowError
  ): Promise<TReply> | undefined {
    if (this.queue.length < this.capacity
      || this.capacityWaiters.length < this.waiterCapacity) {
      return undefined;
    }
    try {
      if (this.disposed) {
        throw runtimeShutdownError();
      }
      throwIfAborted(signal);
    } catch (error) {
      return Promise.reject(discardBeforePending(error, onDiscard));
    }
    try {
      onDiscard?.();
    } catch (error) {
      return Promise.reject(error);
    }
    return Promise.reject(overflowError());
  }

  private completePending(pending: ZLinkPendingSubmit<unknown>): void {
    this.active.delete(pending);
    this.removeQueued(pending);
    this.removeCapacityWaiter(pending);
    this.admitCapacityWaiters();
  }

  private removeQueued(pending: ZLinkPendingSubmit<unknown>): void {
    const index = this.queue.indexOf(pending);
    if (index >= 0) this.queue.splice(index, 1);
  }

  private removeCapacityWaiter(pending: ZLinkPendingSubmit<unknown>): void {
    const index = this.capacityWaiters.indexOf(pending);
    if (index >= 0) this.capacityWaiters.splice(index, 1);
  }

  private admitCapacityWaiters(): void {
    while (!this.disposed && this.queue.length < this.capacity && this.capacityWaiters.length > 0) {
      const pending = this.capacityWaiters.shift()!;
      if (!this.active.has(pending)) continue;
      this.queue.push(pending);
    }
    this.consumeReadyCredit();
  }
}

function hardOverflowError(): ZLinkFrameworkException {
  return createInternalFrameworkException(
    ZLinkFrameworkInternalErrorKind.DeadlineExceeded,
    'ZLink async submit timed out because its bounded admission waiters are full.',
    true
  );
}

function runtimeShutdownError(): ZLinkFrameworkException {
  return createInternalFrameworkException(
    ZLinkFrameworkInternalErrorKind.RuntimeShutdown,
    'ZLink async submitter rejected the operation because the runtime is shutting down.'
  );
}

function submitTimeoutError(): ZLinkFrameworkException {
  return createInternalFrameworkException(
    ZLinkFrameworkInternalErrorKind.DeadlineExceeded,
    'ZLink async submit timed out.',
    true
  );
}

function requestTimeoutError(): ZLinkFrameworkException {
  return createInternalFrameworkException(
    ZLinkFrameworkInternalErrorKind.RequestFailed,
    'ZLink async submit timed out.'
  );
}

export { ZLinkMeshSubmitterRegistry } from './mesh-submitters';

function discardBeforePending(error: unknown, onDiscard: (() => void) | undefined): unknown {
  if (onDiscard === undefined) return error;
  try {
    onDiscard();
    return error;
  } catch (discardError) {
    return new AggregateError([error, discardError], 'Queued message discard failed.');
  }
}

class ZLinkPendingSubmit<TReply> {
  readonly promise: Promise<TReply>;
  private resolvePromise!: (reply: TReply) => void;
  private rejectPromise!: (error: unknown) => void;
  private readonly signal: AbortSignal | undefined;
  private readonly abortHandler: (() => void) | undefined;
  private readonly timeout: ReturnType<typeof setTimeout> | undefined;
  private readonly deadlineMs: number | undefined;
  private completed = false;
  private accepted = false;
  private submitting = false;
  private deferredSettlement: { readonly reply?: TReply; readonly error?: unknown } | undefined;
  private submissionError: unknown;

  constructor(
    private readonly submit: ZLinkRequestSubmit<TReply>,
    private readonly completeOnAccepted: boolean,
    private readonly options: ZLinkPendingSubmitOptions,
    private readonly onSettled: () => void
  ) {
    this.signal = options.signal;
    this.deadlineMs = options.timeoutMs === undefined
      ? undefined
      : Date.now() + Math.max(0, options.timeoutMs);
    this.promise = new Promise<TReply>((resolve, reject) => {
      this.resolvePromise = resolve;
      this.rejectPromise = reject;
    });
    if (options.timeoutMs !== undefined) {
      this.timeout = setTimeout(
        () => this.reject(this.timeoutError()),
        options.timeoutMs
      );
    }
    if (options.signal !== undefined) {
      this.abortHandler = () => this.reject(createAbortError());
      options.signal.addEventListener('abort', this.abortHandler, { once: true });
    }
  }

  get isRequest(): boolean {
    return !this.completeOnAccepted;
  }

  trySubmit(): boolean {
    if (this.completed) {
      return true;
    }
    if (this.deadlineMs !== undefined && Date.now() >= this.deadlineMs) {
      this.reject(this.timeoutError());
      return true;
    }
    let accepted: boolean;
    try {
      this.submitting = true;
      accepted = this.submit(
        (reply) => this.resolve(reply),
        (error) => this.reject(error)
      );
    } catch (error) {
      this.submitting = false;
      this.submissionError = error;
      this.reject(error);
      return true;
    }
    this.submitting = false;
    this.accepted = accepted;
    const deferred = this.deferredSettlement;
    this.deferredSettlement = undefined;
    if (deferred !== undefined) {
      if ('error' in deferred) this.reject(deferred.error);
      else this.resolve(deferred.reply as TReply);
    }
    if (accepted && this.completeOnAccepted) {
      this.resolve(undefined as TReply);
    }
    return accepted;
  }

  private timeoutError(): ZLinkFrameworkException {
    return this.completeOnAccepted ? submitTimeoutError() : requestTimeoutError();
  }

  takeSubmissionError(): unknown {
    const error = this.submissionError;
    this.submissionError = undefined;
    return error;
  }

  resolve(reply: TReply): void {
    if (this.submitting) {
      this.deferredSettlement = { reply };
      return;
    }
    if (this.completed) {
      return;
    }
    this.completed = true;
    this.cleanup();
    this.onSettled();
    this.resolvePromise(reply);
  }

  reject(error: unknown): void {
    if (this.submitting) {
      this.deferredSettlement = { error };
      return;
    }
    if (this.completed) {
      return;
    }
    this.completed = true;
    let settlementError = error;
    if (!this.accepted && this.options.onDiscard !== undefined) {
      try {
        this.options.onDiscard();
      } catch (discardError) {
        settlementError = new AggregateError([error, discardError], 'Queued message discard failed.');
      }
    }
    this.cleanup();
    this.onSettled();
    this.rejectPromise(settlementError);
  }

  private cleanup(): void {
    if (this.timeout !== undefined) {
      clearTimeout(this.timeout);
    }
    if (this.signal !== undefined && this.abortHandler !== undefined) {
      this.signal.removeEventListener('abort', this.abortHandler);
    }
  }
}
