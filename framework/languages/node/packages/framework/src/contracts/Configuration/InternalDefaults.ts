import * as os from 'node:os';

/**
 * The default application listener bound used by the Framework stream parser.
 * This is an internal contract value; it is not a new registration option.
 */
export const DEFAULT_APPLICATION_LISTENER_MAX_MESSAGE_SIZE = 16_777_216;

/** Defaults shared by registration normalization and the runtime worker pool. */
export const DEFAULT_WORKER_MIN_THREADS = 0;
export const DEFAULT_WORKER_IDLE_TIMEOUT_MS = 30_000;
export const DEFAULT_WORKER_QUEUE_LENGTH = 1024;

export function defaultWorkerMaxThreads(): number {
  return Math.max(2, os.availableParallelism());
}
