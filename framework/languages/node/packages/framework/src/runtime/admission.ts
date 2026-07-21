import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../contracts';

export class ZLinkRuntimeAdmissionGate {
  private readonly meshes = new Map<string, ZLinkMeshAdmissionState>();

  get acceptsNewWork(): boolean {
    return [...this.meshes.values()].every((state) => !state.sealed);
  }

  register(meshName: string): void {
    if (!this.meshes.has(meshName)) {
      this.meshes.set(meshName, { sealed: false, active: 0, waiters: [] });
    }
  }

  accepts(meshName: string): boolean {
    return !this.requireState(meshName).sealed;
  }

  pending(meshName: string): number {
    return this.requireState(meshName).active;
  }

  seal(meshName: string): void {
    this.requireState(meshName).sealed = true;
  }

  close(): void {
    for (const state of this.meshes.values()) state.sealed = true;
  }

  claim(meshName: string, operation: string): ZLinkApplicationWorkClaim {
    const state = this.requireState(meshName);
    if (!state.sealed) {
      state.active += 1;
      let closed = false;
      return {
        close: () => {
          if (closed) return;
          closed = true;
          state.active -= 1;
          if (state.active === 0) {
            for (const resolve of state.waiters.splice(0)) resolve();
          }
        }
      };
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.RequestRejected,
      `${operation} was rejected because the framework is draining.`
    );
  }

  async run<T>(meshName: string, operation: string, work: () => Promise<T>): Promise<T> {
    const claim = this.claim(meshName, operation);
    try {
      return await work();
    } finally {
      claim.close();
    }
  }

  awaitZero(meshName: string, signal?: AbortSignal): Promise<void> {
    const state = this.requireState(meshName);
    if (state.active === 0) return Promise.resolve();
    if (signal?.aborted === true) return Promise.reject(signal.reason);
    return new Promise<void>((resolve, reject) => {
      const complete = () => {
        signal?.removeEventListener('abort', abort);
        resolve();
      };
      const abort = () => {
        const index = state.waiters.indexOf(complete);
        if (index >= 0) state.waiters.splice(index, 1);
        reject(signal?.reason);
      };
      state.waiters.push(complete);
      signal?.addEventListener('abort', abort, { once: true });
    });
  }

  requireRequest(operation: string, meshName?: string): void {
    if (meshName === undefined) {
      if (this.acceptsNewWork) return;
    } else if (this.accepts(meshName)) {
      return;
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.RequestRejected,
      `${operation} was rejected because the framework is draining.`
    );
  }

  requireActorCreate(actorId: string, meshName?: string): void {
    if (meshName === undefined) {
      if (this.acceptsNewWork) return;
    } else if (this.accepts(meshName)) {
      return;
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorCreateRejected,
      `Actor '${actorId}' create request was rejected because the framework is draining.`
    );
  }

  private requireState(meshName: string): ZLinkMeshAdmissionState {
    const state = this.meshes.get(meshName);
    if (state !== undefined) return state;
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.RouteNotConnected,
      `RouteMesh '${meshName}' is not registered.`
    );
  }
}

export interface ZLinkApplicationWorkClaim {
  close(): void;
}

interface ZLinkMeshAdmissionState {
  sealed: boolean;
  active: number;
  readonly waiters: Array<() => void>;
}
