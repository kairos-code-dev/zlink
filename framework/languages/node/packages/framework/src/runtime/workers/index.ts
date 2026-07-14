import * as os from 'node:os';
import type { ZLinkWorkerCall } from '../../contracts';
import { ZLinkFrameworkErrorKind, ZLinkFrameworkException } from '../../contracts';
import type { ZLinkWorkerOptions } from '../configuration';
import { ZLinkConfigurationException } from '../configuration';
import { captureZLinkSpotSerialTurn, type ZLinkSpotSerialTurn } from '../execution';
import { createAbortError } from '../abort';

export type ZLinkWorkerWork<T> = (signal: AbortSignal) => T | Promise<T>;

export interface ZLinkSpotWorkerRuntimeOptions {
  readonly maxThreads: number;
  readonly maxQueueLength: number;
}

export function resolveWorkerRuntimeOptions(options?: ZLinkWorkerOptions): ZLinkSpotWorkerRuntimeOptions {
  return {
    maxThreads: options?.maxThreads ?? Math.max(2, os.availableParallelism() * 2),
    maxQueueLength: options?.maxQueueLength ?? 1024
  };
}

interface ZLinkWorkerJob {
  readonly run: () => Promise<void>;
  settled: boolean;
}

/**
 * Single bounded worker scheduler shared by the Spot `runWorker(...)` calls
 * of one framework runtime.
 *
 * This is the asynchronous-deferral projection of the elastic bounded worker
 * pool semantics: jobs run off the Spot serial line as macrotasks
 * (`setImmediate`), bounded by `maxThreads` concurrent in-flight jobs and a
 * `maxQueueLength` pending queue. It does not provide CPU thread offload;
 * job closures still execute on the main event loop.
 */
export class ZLinkSpotWorkerRuntime {
  private readonly options: ZLinkSpotWorkerRuntimeOptions;
  private readonly queue: ZLinkWorkerJob[] = [];
  private inFlight = 0;

  constructor(options?: ZLinkWorkerOptions) {
    this.options = resolveWorkerRuntimeOptions(options);
  }

  get pendingCount(): number {
    return this.queue.length;
  }

  get inFlightCount(): number {
    return this.inFlight;
  }

  schedule<T>(work: ZLinkWorkerWork<T>, timeoutMs?: number, signal?: AbortSignal): Promise<T> {
    if (signal?.aborted === true) {
      return Promise.reject(createAbortError());
    }
    if (this.queue.length >= this.options.maxQueueLength) {
      return Promise.reject(new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.WorkerQueueFull,
        `Worker queue is full (maxQueueLength=${this.options.maxQueueLength}).`,
        true
      ));
    }

    return new Promise<T>((resolve, reject) => {
      const controller = new AbortController();
      let timeout: ReturnType<typeof setTimeout> | undefined;
      let abortListener: (() => void) | undefined;
      const job: ZLinkWorkerJob = {
        settled: false,
        run: async () => {
          if (job.settled) {
            return;
          }
          try {
            const result = await work(controller.signal);
            settle(() => resolve(result));
          } catch (error) {
            settle(() => reject(new ZLinkFrameworkException(
              ZLinkFrameworkErrorKind.WorkerFailed,
              'Worker job failed.',
              false,
              error
            )));
          }
        }
      };
      const settle = (complete: () => void): void => {
        if (job.settled) {
          // Late completion after timeout/abort: drop without user callbacks.
          return;
        }
        job.settled = true;
        if (timeout !== undefined) {
          clearTimeout(timeout);
        }
        if (signal !== undefined && abortListener !== undefined) {
          signal.removeEventListener('abort', abortListener);
        }
        complete();
      };
      if (timeoutMs !== undefined) {
        timeout = setTimeout(() => {
          const fail = (): void => reject(new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.WorkerTimedOut,
            `Worker job timed out after ${timeoutMs}ms.`,
            true
          ));
          settle(fail);
          controller.abort();
        }, timeoutMs);
      }
      if (signal !== undefined) {
        abortListener = () => {
          settle(() => reject(createAbortError()));
          controller.abort();
        };
        signal.addEventListener('abort', abortListener, { once: true });
      }
      this.queue.push(job);
      this.pump();
    });
  }

  private pump(): void {
    while (this.inFlight < this.options.maxThreads && this.queue.length > 0) {
      const job = this.queue.shift() as ZLinkWorkerJob;
      this.inFlight += 1;
      setImmediate(() => {
        void job.run().finally(() => {
          this.inFlight -= 1;
          this.pump();
        });
      });
    }
  }
}

export interface ZLinkSpotSerialLike {
  /** Runs in serial order; re-entrant from within the active turn. */
  execute<T>(operation: () => Promise<T> | T): Promise<T>;
  /** Always enqueues as a new serial turn, never runs inline in a turn. */
  post<T>(operation: () => Promise<T> | T): Promise<T>;
  /** Suspends the current Spot turn until `pending` completes and resumes it through the serial queue. */
  yieldPromise<T>(pending: Promise<T>): Promise<T>;
}

/**
 * `runWorker(...)` builder bound to one owning Spot serial executor.
 *
 * Awaiting `submit()` keeps a captured Spot turn. Awaiting `yield()` suspends
 * that turn and resumes its continuation through the owning serial queue.
 */
export class DefaultZLinkWorkerCall<T> implements ZLinkWorkerCall<T> {
  private selectedTimeoutMs: number | undefined;
  private terminatorSelected = false;
  private readonly yieldTurn: ZLinkSpotSerialTurn | undefined;

  constructor(
    private readonly worker: ZLinkSpotWorkerRuntime,
    private readonly serial: ZLinkSpotSerialLike,
    private readonly work: ZLinkWorkerWork<T>
  ) {
    this.yieldTurn = captureZLinkSpotSerialTurn();
  }

  timeoutMs(durationMs: number): ZLinkWorkerCall<T> {
    this.selectedTimeoutMs = durationMs;
    return this;
  }

  submit(signal?: AbortSignal): Promise<T> {
    this.claimTerminator();
    const pending = this.worker.schedule(this.work, this.selectedTimeoutMs, signal);
    return this.yieldTurn === undefined
      ? deliverOnSerial(this.serial, pending)
      : pending;
  }

  yield(signal?: AbortSignal): Promise<T> {
    this.claimTerminator();
    const pending = this.worker.schedule(this.work, this.selectedTimeoutMs, signal);
    return this.yieldTurn === undefined
      ? deliverOnSerial(this.serial, pending)
      : this.yieldTurn.yieldPromise(pending);
  }

  private claimTerminator(): void {
    if (this.terminatorSelected) {
      throw new ZLinkConfigurationException(
        'A runWorker call can select only one terminator.'
      );
    }
    this.terminatorSelected = true;
  }
}

/**
 * Settles `pending` back on the owning Spot serial executor so detached
 * continuations land on the Spot queue. When the completion is awaited from
 * within the still-active originating turn (gated awaitable path), the
 * re-entrant `execute` delivers it as part of that turn instead of
 * deadlocking on the queue.
 */
export function deliverOnSerial<T>(serial: ZLinkSpotSerialLike, pending: Promise<T>): Promise<T> {
  return new ZLinkSerialDeliveredPromise(serial, pending);
}

class ZLinkSerialDeliveredPromise<T> implements Promise<T> {
  readonly [Symbol.toStringTag] = 'Promise';

  constructor(
    private readonly serial: ZLinkSpotSerialLike,
    private readonly pending: Promise<T>
  ) {}

  then<TResult1 = T, TResult2 = never>(
    onfulfilled?: ((value: T) => TResult1 | PromiseLike<TResult1>) | null,
    onrejected?: ((reason: unknown) => TResult2 | PromiseLike<TResult2>) | null
  ): Promise<TResult1 | TResult2> {
    return this.pending.then(
      (value) => this.serial.execute(() => onfulfilled === undefined || onfulfilled === null
        ? value as unknown as TResult1
        : Promise.resolve(onfulfilled(value))),
      (reason) => this.serial.execute(() => {
        if (onrejected === undefined || onrejected === null) {
          throw reason;
        }
        return Promise.resolve(onrejected(reason));
      })
    );
  }

  catch<TResult = never>(
    onrejected?: ((reason: unknown) => TResult | PromiseLike<TResult>) | null
  ): Promise<T | TResult> {
    return this.then(undefined, onrejected);
  }

  finally(onfinally?: (() => void) | null): Promise<T> {
    return this.then(
      (value) => this.serial.execute(() => {
        onfinally?.();
        return value;
      }),
      (reason) => this.serial.execute(() => {
        onfinally?.();
        throw reason;
      })
    );
  }
}
