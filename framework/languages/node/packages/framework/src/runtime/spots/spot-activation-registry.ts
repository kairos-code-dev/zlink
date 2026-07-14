import type {
  RoutingId,
  Type,
  ZLinkSpot,
  ZLinkSpotCreateResult,
  ZLinkSpotInfo
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkSpotCreateState
} from '../../contracts';
import type { ZLinkSpotActivation } from './spot-activation-state';
import { ZLinkConfigurationException } from '../configuration';
import { createAbortError } from '../abort';
import { ZLinkSpotLifecycleMetrics } from './spot-lifecycle-metrics';
export { ZLinkSpotActivation } from './spot-activation-state';

interface PendingSpotActivation {
  readonly spotType: Type<ZLinkSpot>;
  readonly ready: Promise<ZLinkSpotCreateResult>;
}

export interface ZLinkSpotActivationOperation {
  readonly owner: boolean;
  readonly ready: Promise<ZLinkSpotCreateResult>;
}

export interface ZLinkSpotCloseOperation {
  readonly activation: ZLinkSpotActivation;
  readonly ready: Promise<void>;
  readonly started: boolean;
}

export class ZLinkSpotActivationRegistry {
  private nextId = 1;
  private readonly activations = new Map<string, ZLinkSpotActivation>();
  private readonly pending = new Map<string, PendingSpotActivation>();
  private readonly closing = new Map<string, ZLinkSpotCloseOperation>();
  private readonly failedClose = new Set<string>();
  private readonly emptyWaiters = new Set<() => void>();
  private readonly lifecycleMetrics: ZLinkSpotLifecycleMetrics;

  constructor(metrics?: import('../diagnostics').ZLinkRuntimeMetrics) {
    this.lifecycleMetrics = new ZLinkSpotLifecycleMetrics(metrics);
  }

  allocateSpotRid(): RoutingId {
    let spotRid: RoutingId;
    do {
      spotRid = `spot-${this.nextId}`;
      this.nextId += 1;
    } while (this.activations.has(spotActivationKey(spotRid)));
    return spotRid;
  }

  resolve(spotRid: RoutingId): ZLinkSpotActivation | undefined {
    const key = spotActivationKey(spotRid);
    return this.closing.has(key) || this.failedClose.has(key) ? undefined : this.activations.get(key);
  }

  has(spotRid: RoutingId): boolean {
    const key = spotActivationKey(spotRid);
    return !this.closing.has(key) && !this.failedClose.has(key) && this.activations.has(key);
  }

  list(): readonly ZLinkSpotInfo[] {
    return [...this.activations.values()]
      .filter((activation) => {
        const key = spotActivationKey(activation.spotRid);
        return !this.closing.has(key) && !this.failedClose.has(key);
      })
      .map((activation) => String(activation.spotRid))
      .sort((left, right) => left.localeCompare(right))
      .map((spotRid) => ({ spotRid }));
  }

  activeActivations(): readonly ZLinkSpotActivation[] {
    return [...this.activations.values()];
  }

  closingOperation(spotRid: RoutingId): ZLinkSpotCloseOperation | undefined {
    return this.closing.get(spotActivationKey(spotRid));
  }

  whenEmpty(signal?: AbortSignal): Promise<void> {
    if (this.isEmpty()) return Promise.resolve();
    if (signal?.aborted === true) return Promise.reject(createAbortError());
    return new Promise<void>((resolve, reject) => {
      const complete = () => {
        signal?.removeEventListener('abort', abort);
        resolve();
      };
      const abort = () => {
        this.emptyWaiters.delete(complete);
        reject(createAbortError());
      };
      this.emptyWaiters.add(complete);
      signal?.addEventListener('abort', abort, { once: true });
    });
  }

  register(activation: ZLinkSpotActivation): void {
    const key = spotActivationKey(activation.spotRid);
    if (this.activations.has(key)) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.SpotTypeMismatch,
        `Spot '${activation.spotRid}' was activated more than once.`
      );
    }
    this.activations.set(key, activation);
    this.lifecycleMetrics.opened('user');
  }

  startClose(
    spotRid: RoutingId,
    close: (activation: ZLinkSpotActivation) => Promise<void>,
    resourcesReleased: (activation: ZLinkSpotActivation) => boolean
  ): ZLinkSpotCloseOperation | undefined {
    const key = spotActivationKey(spotRid);
    const closing = this.closing.get(key);
    if (closing !== undefined) {
      return { ...closing, started: false };
    }
    const activation = this.activations.get(key);
    if (activation === undefined || !activation.canClose()) {
      return undefined;
    }
    const operation = {} as ZLinkSpotCloseOperation;
    let completed = false;
    const ready = Promise.resolve()
      .then(() => close(activation))
      .then(() => { completed = true; })
      .finally(() => {
        if (this.closing.get(key) === operation) {
          this.closing.delete(key);
          if (completed || resourcesReleased(activation)) {
            this.activations.delete(key);
            this.lifecycleMetrics.closed('user');
            this.failedClose.delete(key);
          } else {
            this.failedClose.add(key);
          }
          this.resolveEmptyWaiters();
        }
      });
    Object.assign(operation, { activation, ready, started: true });
    this.closing.set(key, operation);
    return operation;
  }

  getOrBegin(
    spotType: Type<ZLinkSpot>,
    spotRid: RoutingId,
    create: () => Promise<ZLinkSpotCreateResult>
  ): ZLinkSpotActivationOperation {
    const key = spotActivationKey(spotRid);
    const closing = this.closing.get(key);
    if (closing !== undefined) {
      return {
        owner: false,
        ready: closing.ready
          .catch(() => undefined)
          .then(() => this.getOrBegin(spotType, spotRid, create).ready)
      };
    }
    if (this.failedClose.has(key)) {
      return {
        owner: false,
        ready: Promise.reject(new ZLinkConfigurationException(
          `Spot '${spotRid}' cleanup has not completed.`
        ))
      };
    }
    const existing = this.activations.get(key);
    if (existing !== undefined) {
      if (existing.spotType !== spotType) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.SpotTypeMismatch,
          `Spot '${spotRid}' already exists with a different spot type.`
        );
      }
      return {
        owner: false,
        ready: Promise.resolve({ spotRid, state: ZLinkSpotCreateState.Existing })
      };
    }

    const pending = this.pending.get(key);
    if (pending === undefined) {
      const ready = Promise.resolve().then(create);
      const pending: PendingSpotActivation = { spotType, ready };
      this.pending.set(key, pending);
      const tracked = ready.finally(() => {
        if (this.pending.get(key) === pending) {
          this.pending.delete(key);
          this.resolveEmptyWaiters();
        }
      });
      return { owner: true, ready: tracked };
    }
    if (pending.spotType !== spotType) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.SpotTypeMismatch,
        `Spot '${spotRid}' is being created with a different spot type.`
      );
    }
    return {
      owner: false,
      ready: pending.ready.then((result) => result.state === ZLinkSpotCreateState.Created
        ? { spotRid, state: ZLinkSpotCreateState.Existing }
        : { spotRid, state: result.state, reply: result.reply })
    };
  }

  private isEmpty(): boolean {
    return this.activations.size === 0 && this.pending.size === 0 && this.closing.size === 0;
  }

  private resolveEmptyWaiters(): void {
    if (!this.isEmpty()) return;
    for (const resolve of this.emptyWaiters) resolve();
    this.emptyWaiters.clear();
  }
}

function spotActivationKey(spotRid: RoutingId): string {
  return String(spotRid);
}
