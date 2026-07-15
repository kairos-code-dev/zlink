import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
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
  private queueOffset = 0;
  private readonly active = new Set<ZLinkPendingSubmit<unknown>>();
  private requestActive = false;
  private readonly timeoutMs: number | undefined;
  private readonly capacity: number;
  private readonly onCommandFailure: ((error: unknown) => void) | undefined;
  private readyRegistered = false;
  private disposed = false;

  constructor(
    private readonly registerSendReady: (handler: () => void) => void,
    options: {
      readonly timeoutMs?: number;
      readonly capacity?: number;
      readonly onCommandFailure?: (error: unknown) => void;
    } = {}
  ) {
    this.timeoutMs = options.timeoutMs === -1 ? undefined : options.timeoutMs ?? 1000;
    this.capacity = options.capacity ?? 4096;
    this.onCommandFailure = options.onCommandFailure;
  }

  submitCommand(submit: () => boolean, signal?: AbortSignal, onDiscard?: () => void): Promise<void> {
    const pending = this.createPending<void>(
      () => submit(),
      true,
      signal,
      undefined,
      onDiscard
    );
    if (this.pendingQueueLength() === 0 && this.trySubmitPending(pending)) {
      return pending.promise;
    }
    return this.enqueue(pending);
  }

  submitCommandOneWay(submit: () => boolean, signal?: AbortSignal, onDiscard?: () => void): void {
    const pending = this.createPending<void>(
      () => submit(),
      true,
      signal,
      undefined,
      onDiscard
    );
    if (this.pendingQueueLength() === 0 && this.trySubmitPending(pending)) {
      this.finishOneWaySubmission(pending);
      return;
    }
    if (this.disposed) {
      this.rejectOneWaySubmission(pending, new ZLinkConfigurationException('ZLink async submitter is disposed.'));
    }
    if (this.pendingQueueLength() >= this.capacity) {
      this.rejectOneWaySubmission(pending, new ZLinkConfigurationException('ZLink async submit queue is full.'));
    }
    this.ensureReadyHandler();
    this.queue.push(pending as ZLinkPendingSubmit<unknown>);
    this.drain();
    this.finishOneWaySubmission(pending);
  }

  submitRequest<TReply>(
    submit: ZLinkRequestSubmit<TReply>,
    signal?: AbortSignal,
    timeoutMs?: number,
    onDiscard?: () => void
  ): Promise<TReply> {
    const pending = this.createPending<TReply>(submit, false, signal, timeoutMs, onDiscard);
    if (this.pendingQueueLength() === 0 && this.trySubmitPending(pending)) {
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
    this.queueOffset = 0;
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
      const error = new ZLinkConfigurationException('ZLink async submitter is disposed.');
      throw discardBeforePending(error, onDiscard);
    }
    try {
      throwIfAborted(signal);
    } catch (error) {
      throw discardBeforePending(error, onDiscard);
    }
    const pending = new ZLinkPendingSubmit<TReply>(
      submit,
      completeOnAccepted,
      {
        timeoutMs: timeoutMs === -1 ? undefined : timeoutMs ?? this.timeoutMs,
        signal,
        onDiscard
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
    if (this.pendingQueueLength() >= this.capacity) {
      pending.reject(new ZLinkConfigurationException('ZLink async submit queue is full.'));
      return pending.promise;
    }

    this.ensureReadyHandler();
    this.queue.push(pending as ZLinkPendingSubmit<unknown>);
    this.drain();
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
    this.registerSendReady(() => this.drain());
  }

  private drain(): void {
    if (this.disposed) {
      return;
    }
    while (this.queueOffset < this.queue.length) {
      const pending = this.queue[this.queueOffset];
      if (!this.trySubmitPending(pending)) {
        return;
      }
      this.queueOffset += 1;
    }
    this.compactQueue();
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
    this.drain();
  }

  private pendingQueueLength(): number {
    return this.queue.length - this.queueOffset;
  }

  private compactQueue(): void {
    if (this.queueOffset === 0) {
      return;
    }
    if (this.queueOffset === this.queue.length) {
      this.queue.length = 0;
      this.queueOffset = 0;
      return;
    }
    if (this.queueOffset >= 64 && this.queueOffset * 2 >= this.queue.length) {
      this.queue.splice(0, this.queueOffset);
      this.queueOffset = 0;
    }
  }
}

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
  private completed = false;
  private accepted = false;
  private submitting = false;
  private deferredSettlement: { readonly reply?: TReply; readonly error?: unknown } | undefined;
  private submissionError: unknown;

  constructor(
    private readonly submit: ZLinkRequestSubmit<TReply>,
    private readonly completeOnAccepted: boolean,
    private readonly options: ZLinkPendingSubmitOptions
  ) {
    this.signal = options.signal;
    this.promise = new Promise<TReply>((resolve, reject) => {
      this.resolvePromise = resolve;
      this.rejectPromise = reject;
    });
    if (options.timeoutMs !== undefined) {
      this.timeout = setTimeout(
        () => this.reject(new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.RequestFailed,
          'ZLink async submit timed out.'
        )),
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
