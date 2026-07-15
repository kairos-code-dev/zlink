import * as os from 'node:os';
import { Worker } from 'node:worker_threads';
import type { ZLinkWorkerCall } from '../../contracts';
import { ZLinkFrameworkErrorKind, ZLinkFrameworkException } from '../../contracts';
import type { ZLinkWorkerOptions } from '../configuration';
import { ZLinkConfigurationException } from '../configuration';
import { createAbortError } from '../abort';
import { captureZLinkSpotSerialTurn, type ZLinkSpotSerialTurn } from '../execution';

export type ZLinkCpuWorkerWork<T> = (signal: AbortSignal) => T;
export type ZLinkIoWorkerWork<T> = (signal: AbortSignal) => Promise<T>;

export interface ZLinkWorkerRuntimeOptions {
  readonly maxThreads: number;
  readonly maxQueueLength: number;
}

export function resolveWorkerRuntimeOptions(options?: ZLinkWorkerOptions): ZLinkWorkerRuntimeOptions {
  return {
    maxThreads: options?.maxThreads ?? Math.max(2, os.availableParallelism()),
    maxQueueLength: options?.maxQueueLength ?? 1024
  };
}

interface ZLinkCpuJob<T> {
  readonly source: string;
  readonly timeoutMs?: number;
  readonly signal?: AbortSignal;
  readonly resolve: (value: T) => void;
  readonly reject: (error: unknown) => void;
  settled: boolean;
}

/** Runs synchronous CPU functions in a bounded set of worker threads. */
class ZLinkCpuWorkerPool {
  private readonly queue: ZLinkCpuJob<unknown>[] = [];
  private inFlight = 0;

  constructor(private readonly options: ZLinkWorkerRuntimeOptions) {}

  get pendingCount(): number {
    return this.queue.length;
  }

  get inFlightCount(): number {
    return this.inFlight;
  }

  schedule<T>(work: ZLinkCpuWorkerWork<T>, timeoutMs?: number, signal?: AbortSignal): Promise<T> {
    if (work.constructor.name === 'AsyncFunction') {
      return Promise.reject(new ZLinkConfigurationException(
        'runCpuWorker requires a synchronous function; use runIoWorker for async work.'
      ));
    }
    if (signal?.aborted === true) {
      return Promise.reject(createAbortError());
    }
    if (this.queue.length >= this.options.maxQueueLength) {
      return Promise.reject(workerQueueFull(this.options.maxQueueLength));
    }
    return new Promise<T>((resolve, reject) => {
      this.queue.push({
        source: work.toString(),
        timeoutMs,
        signal,
        resolve,
        reject,
        settled: false
      } as ZLinkCpuJob<unknown>);
      this.pump();
    });
  }

  private pump(): void {
    while (this.inFlight < this.options.maxThreads && this.queue.length > 0) {
      const job = this.queue.shift() as ZLinkCpuJob<unknown>;
      if (job.settled) continue;
      this.inFlight += 1;
      this.run(job).finally(() => {
        this.inFlight -= 1;
        this.pump();
      });
    }
  }

  private async run(job: ZLinkCpuJob<unknown>): Promise<void> {
    const abortState = new Int32Array(new SharedArrayBuffer(Int32Array.BYTES_PER_ELEMENT));
    const worker = new Worker(CPU_WORKER_SOURCE, {
      eval: true,
      workerData: { source: job.source, abortState: abortState.buffer }
    });
    let timeout: ReturnType<typeof setTimeout> | undefined;
    let abortListener: (() => void) | undefined;
    const settle = (complete: () => void): void => {
      if (job.settled) return;
      job.settled = true;
      if (timeout !== undefined) clearTimeout(timeout);
      if (job.signal !== undefined && abortListener !== undefined) {
        job.signal.removeEventListener('abort', abortListener);
      }
      complete();
    };
    if (job.timeoutMs !== undefined) {
      timeout = setTimeout(() => {
        Atomics.store(abortState, 0, 1);
        settle(() => job.reject(workerTimedOut(job.timeoutMs as number)));
      }, job.timeoutMs);
    }
    if (job.signal !== undefined) {
      abortListener = () => {
        Atomics.store(abortState, 0, 1);
        settle(() => job.reject(createAbortError()));
      };
      job.signal.addEventListener('abort', abortListener, { once: true });
    }
    worker.once('message', (message: CpuWorkerMessage) => {
      if (message.ok) {
        settle(() => job.resolve(message.value));
      } else {
        settle(() => job.reject(workerFailed(deserializeWorkerError(message.error))));
      }
    });
    worker.once('error', (error) => settle(() => job.reject(workerFailed(error))));
    worker.once('exit', (code) => {
      if (code !== 0) {
        settle(() => job.reject(workerFailed(new Error(`CPU worker exited with code ${code}.`))));
      }
    });
    await new Promise<void>((resolve) => worker.once('exit', resolve));
  }
}

interface CpuWorkerMessage {
  readonly ok: boolean;
  readonly value?: unknown;
  readonly error?: { readonly name?: string; readonly message?: string; readonly stack?: string };
}

const CPU_WORKER_SOURCE = String.raw`
const { parentPort, workerData } = require('node:worker_threads');
const state = new Int32Array(workerData.abortState);
const signal = {
  get aborted() { return Atomics.load(state, 0) !== 0; },
  get reason() { return this.aborted ? new DOMException('The operation was aborted.', 'AbortError') : undefined; },
  throwIfAborted() { if (this.aborted) throw this.reason; },
  addEventListener() {},
  removeEventListener() {},
  dispatchEvent() { return false; },
  onabort: null
};
try {
  const work = (0, eval)('(' + workerData.source + ')');
  const value = work(signal);
  if (value !== null && (typeof value === 'object' || typeof value === 'function') && typeof value.then === 'function') {
    throw new TypeError('runCpuWorker function returned a Promise; use runIoWorker for async work.');
  }
  parentPort.postMessage({ ok: true, value });
} catch (error) {
  parentPort.postMessage({
    ok: false,
    error: {
      name: error && error.name,
      message: error && error.message ? error.message : String(error),
      stack: error && error.stack
    }
  });
}
`;

/** Runs asynchronous I/O functions without consuming a CPU worker thread. */
class ZLinkIoWorkerRuntime {
  schedule<T>(work: ZLinkIoWorkerWork<T>, timeoutMs?: number, signal?: AbortSignal): Promise<T> {
    if (signal?.aborted === true) {
      return Promise.reject(createAbortError());
    }
    return new Promise<T>((resolve, reject) => {
      const controller = new AbortController();
      let settled = false;
      let timeout: ReturnType<typeof setTimeout> | undefined;
      let abortListener: (() => void) | undefined;
      const settle = (complete: () => void): void => {
        if (settled) return;
        settled = true;
        if (timeout !== undefined) clearTimeout(timeout);
        if (signal !== undefined && abortListener !== undefined) {
          signal.removeEventListener('abort', abortListener);
        }
        complete();
      };
      if (timeoutMs !== undefined) {
        timeout = setTimeout(() => {
          controller.abort();
          settle(() => reject(workerTimedOut(timeoutMs)));
        }, timeoutMs);
      }
      if (signal !== undefined) {
        abortListener = () => {
          controller.abort();
          settle(() => reject(createAbortError()));
        };
        signal.addEventListener('abort', abortListener, { once: true });
      }
      Promise.resolve().then(() => work(controller.signal)).then(
        (value) => settle(() => resolve(value)),
        (error) => settle(() => reject(workerFailed(error)))
      );
    });
  }
}

export class ZLinkWorkerRuntime {
  private readonly cpu: ZLinkCpuWorkerPool;
  private readonly io = new ZLinkIoWorkerRuntime();

  constructor(options?: ZLinkWorkerOptions) {
    this.cpu = new ZLinkCpuWorkerPool(resolveWorkerRuntimeOptions(options));
  }

  get pendingCount(): number {
    return this.cpu.pendingCount;
  }

  get inFlightCount(): number {
    return this.cpu.inFlightCount;
  }

  scheduleCpu<T>(work: ZLinkCpuWorkerWork<T>, timeoutMs?: number, signal?: AbortSignal): Promise<T> {
    return this.cpu.schedule(work, timeoutMs, signal);
  }

  scheduleIo<T>(work: ZLinkIoWorkerWork<T>, timeoutMs?: number, signal?: AbortSignal): Promise<T> {
    return this.io.schedule(work, timeoutMs, signal);
  }
}

export interface ZLinkSpotSerialLike {
  execute<T>(operation: () => Promise<T> | T): Promise<T>;
  post<T>(operation: () => Promise<T> | T): Promise<T>;
  yieldPromise<T>(pending: Promise<T>): Promise<T>;
}

export class DefaultZLinkWorkerCall<T> implements ZLinkWorkerCall<T> {
  private selectedTimeoutMs: number | undefined;
  private terminatorSelected = false;
  private readonly turn: ZLinkSpotSerialTurn | undefined;

  constructor(
    private readonly serial: ZLinkSpotSerialLike,
    private readonly schedule: (timeoutMs?: number, signal?: AbortSignal) => Promise<T>
  ) {
    this.turn = captureZLinkSpotSerialTurn();
  }

  timeoutMs(durationMs: number): ZLinkWorkerCall<T> {
    this.selectedTimeoutMs = durationMs;
    return this;
  }

  submit(signal?: AbortSignal): Promise<T> {
    const pending = this.begin(signal);
    return this.turn === undefined ? deliverOnSerial(this.serial, pending) : pending;
  }

  yield(signal?: AbortSignal): Promise<T> {
    const pending = this.begin(signal);
    return this.turn === undefined
      ? deliverOnSerial(this.serial, pending)
      : this.turn.yieldPromise(pending);
  }

  private begin(signal?: AbortSignal): Promise<T> {
    if (this.terminatorSelected) {
      throw new ZLinkConfigurationException('A worker call can select only one terminator.');
    }
    this.terminatorSelected = true;
    return this.schedule(this.selectedTimeoutMs, signal);
  }
}

export function deliverOnSerial<T>(serial: ZLinkSpotSerialLike, pending: Promise<T>): Promise<T> {
  return new ZLinkSerialDeliveredPromise(serial, pending);
}

class ZLinkSerialDeliveredPromise<T> implements Promise<T> {
  readonly [Symbol.toStringTag] = 'Promise';

  constructor(private readonly serial: ZLinkSpotSerialLike, private readonly pending: Promise<T>) {}

  then<TResult1 = T, TResult2 = never>(
    onfulfilled?: ((value: T) => TResult1 | PromiseLike<TResult1>) | null,
    onrejected?: ((reason: unknown) => TResult2 | PromiseLike<TResult2>) | null
  ): Promise<TResult1 | TResult2> {
    return this.pending.then(
      (value) => this.serial.execute(() => onfulfilled === undefined || onfulfilled === null
        ? value as unknown as TResult1
        : Promise.resolve(onfulfilled(value))),
      (reason) => this.serial.execute(() => {
        if (onrejected === undefined || onrejected === null) throw reason;
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
      (value) => this.serial.execute(() => { onfinally?.(); return value; }),
      (reason) => this.serial.execute(() => { onfinally?.(); throw reason; })
    );
  }
}

function workerQueueFull(maxQueueLength: number): ZLinkFrameworkException {
  return new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.WorkerQueueFull,
    `CPU worker queue is full (maxQueueLength=${maxQueueLength}).`,
    true
  );
}

function workerTimedOut(timeoutMs: number): ZLinkFrameworkException {
  return new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.WorkerTimedOut,
    `Worker job timed out after ${timeoutMs}ms.`,
    true
  );
}

function workerFailed(cause: unknown): ZLinkFrameworkException {
  return new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.WorkerFailed,
    'Worker job failed.',
    false,
    cause
  );
}

function deserializeWorkerError(error: CpuWorkerMessage['error']): Error {
  const result = new Error(error?.message ?? 'CPU worker failed.');
  result.name = error?.name ?? 'Error';
  if (error?.stack !== undefined) result.stack = error.stack;
  return result;
}
