import { ZLinkApplicationHwmProfile } from '../../contracts';
import type { ZLinkInboundDispatchStatus } from '../../contracts';
import type { ZLinkInboundDispatchOptionValues } from
  '../../contracts/Configuration/InboundDispatch';
import { ZLinkConfigurationException } from '../configuration';
import { readFileSync } from 'node:fs';
import { totalmem } from 'node:os';
import { join } from 'node:path';

const PROFILE_PERCENT = new Map<ZLinkApplicationHwmProfile, bigint>([
  [ZLinkApplicationHwmProfile.Compact, 2n],
  [ZLinkApplicationHwmProfile.LowLatency, 5n],
  [ZLinkApplicationHwmProfile.Balanced, 10n],
  [ZLinkApplicationHwmProfile.Throughput, 20n]
]);
const COMPLETION_SEND_LIMIT = 65_536n;

export class ZLinkInboundDispatchBudget {
  private queuedBytes = 0n;
  private activeBytes = 0n;
  private pendingCompletionSends = 0n;
  private activeCompletionSends = 0n;
  private readonly completionWaiters: Array<() => void> = [];
  private readonly resumeListeners = new Set<() => void>();

  constructor(readonly applicationHwmBytes: bigint) {}

  get receivePaused(): boolean {
    return this.applicationHwmBytes > 0n
      && this.pendingPayloadBytes >= this.applicationHwmBytes;
  }

  get pendingPayloadBytes(): bigint {
    return this.queuedBytes + this.activeBytes;
  }

  enqueue(payloadBytes: bigint): void {
    this.queuedBytes += requirePayloadBytes(payloadBytes);
  }

  start(payloadBytes: bigint): void {
    const bytes = requirePayloadBytes(payloadBytes);
    if (bytes > this.queuedBytes) {
      throw new Error('Inbound dispatch byte accounting underflow.');
    }
    this.queuedBytes -= bytes;
    this.activeBytes += bytes;
  }

  cancelQueued(payloadBytes: bigint): void {
    const wasPaused = this.receivePaused;
    const bytes = requirePayloadBytes(payloadBytes);
    if (bytes > this.queuedBytes) {
      throw new Error('Inbound dispatch queued byte accounting underflow.');
    }
    this.queuedBytes -= bytes;
    const stillPaused = this.receivePaused;
    if (wasPaused && !stillPaused) {
      for (const listener of this.resumeListeners) listener();
    }
  }

  complete(payloadBytes: bigint): void {
    const wasPaused = this.receivePaused;
    const bytes = requirePayloadBytes(payloadBytes);
    if (bytes > this.activeBytes) {
      throw new Error('Inbound dispatch active byte accounting underflow.');
    }
    this.activeBytes -= bytes;
    const stillPaused = this.receivePaused;
    if (wasPaused && !stillPaused) {
      for (const listener of this.resumeListeners) listener();
    }
  }

  onResume(listener: () => void): () => void {
    this.resumeListeners.add(listener);
    return () => this.resumeListeners.delete(listener);
  }

  async waitUntilResumed(signal?: AbortSignal): Promise<void> {
    if (!this.receivePaused || signal?.aborted === true) return;
    await new Promise<void>((resolve) => {
      const finish = () => {
        removeResume();
        signal?.removeEventListener('abort', finish);
        resolve();
      };
      const removeResume = this.onResume(finish);
      signal?.addEventListener('abort', finish, { once: true });
      if (!this.receivePaused) finish();
    });
  }

  async acquireCompletionSend(signal?: AbortSignal): Promise<() => void> {
    this.pendingCompletionSends += 1n;
    let admitted = false;
    try {
      if (this.activeCompletionSends >= COMPLETION_SEND_LIMIT) {
        await new Promise<void>((resolve, reject) => {
          const wake = () => {
            signal?.removeEventListener('abort', abort);
            resolve();
          };
          const abort = () => {
            const index = this.completionWaiters.indexOf(wake);
            if (index >= 0) this.completionWaiters.splice(index, 1);
            reject(signal?.reason ?? new Error('Completion send admission was cancelled.'));
          };
          this.completionWaiters.push(wake);
          signal?.addEventListener('abort', abort, { once: true });
          if (signal?.aborted === true) abort();
        });
      }
      this.activeCompletionSends += 1n;
      admitted = true;
      let released = false;
      return () => {
        if (released) return;
        released = true;
        this.activeCompletionSends -= 1n;
        this.pendingCompletionSends -= 1n;
        this.completionWaiters.shift()?.();
      };
    } finally {
      if (!admitted) this.pendingCompletionSends -= 1n;
    }
  }

  snapshot(): ZLinkInboundDispatchStatus {
    return {
      applicationHwmBytes: this.applicationHwmBytes,
      pendingPayloadBytes: this.pendingPayloadBytes,
      queuedPayloadBytes: this.queuedBytes,
      activePayloadBytes: this.activeBytes,
      applicationReceivePaused: this.receivePaused,
      pendingCompletionSends: this.pendingCompletionSends,
      completionSendLimit: COMPLETION_SEND_LIMIT
    };
  }
}

export function resolveApplicationHwm(
  options: ZLinkInboundDispatchOptionValues
): bigint {
  const configured = options.applicationHwmBytes;
  if (configured !== undefined) {
    if (configured < 0n) {
      throw new ZLinkConfigurationException(
        'Application HWM bytes must be zero or positive.'
      );
    }
    return configured;
  }
  //  Spec 06: configured limit, then the container/cgroup limit, then total
  //  physical memory. Total, not free, so Auto stays deterministic.
  const memoryLimit = options.processMemoryLimitBytes
    ?? detectFiniteProcessMemoryLimit()
    ?? totalPhysicalMemory();
  if (memoryLimit <= 0n) {
    throw new ZLinkConfigurationException(
      'Application HWM Auto sizing could not read the total physical memory of this host.'
    );
  }
  const percent = PROFILE_PERCENT.get(options.applicationHwmProfile);
  if (percent === undefined) {
    throw new ZLinkConfigurationException(
      'Application HWM profile is not supported.'
    );
  }
  const resolved = memoryLimit * percent / 100n;
  if (resolved <= 0n) {
    throw new ZLinkConfigurationException(
      'Application HWM Auto sizing produced a non-positive byte limit.'
    );
  }
  return resolved;
}

function detectFiniteProcessMemoryLimit(): bigint | undefined {
  //  Node reports 0 when the process is not memory constrained, so only a
  //  positive safe integer names a real limit.
  const constrained = process.constrainedMemory();
  if (Number.isSafeInteger(constrained) && constrained > 0) {
    return BigInt(constrained);
  }
  const cgroupPath = currentCgroupPath();
  return readFiniteLimit(join('/sys/fs/cgroup', cgroupPath, 'memory.max'))
    ?? readFiniteLimit(join('/sys/fs/cgroup/memory', cgroupPath, 'memory.limit_in_bytes'));
}

function totalPhysicalMemory(): bigint {
  const total = totalmem();
  return Number.isSafeInteger(total) && total > 0 ? BigInt(total) : 0n;
}

function currentCgroupPath(): string {
  try {
    const unified = readFileSync('/proc/self/cgroup', 'utf8')
      .split(/\r?\n/u)
      .find((line) => line.startsWith('0::'));
    return unified?.slice(3).replace(/^\/+/u, '') ?? '';
  } catch {
    return '';
  }
}

function readFiniteLimit(path: string): bigint | undefined {
  try {
    const value = readFileSync(path, 'utf8').trim();
    if (value === '' || value === 'max') return undefined;
    const parsed = BigInt(value);
    return parsed > 0n && parsed < (1n << 63n) ? parsed : undefined;
  } catch {
    return undefined;
  }
}

function requirePayloadBytes(value: bigint): bigint {
  if (value < 0n) throw new RangeError('Payload bytes cannot be negative.');
  return value;
}
