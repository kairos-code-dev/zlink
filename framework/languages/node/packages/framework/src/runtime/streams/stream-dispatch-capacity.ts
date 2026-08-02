import type { ZLinkApplicationWorkClaim } from '../admission';

/**
 * Bounds framework work items whose payload size is zero and therefore cannot
 * be bounded by the application byte budget.
 */
export const ZLINK_STREAM_MAX_IN_FLIGHT_DISPATCHES = 1024;

export class ZLinkStreamDispatchCapacity {
  private active = 0;
  private readonly waiters: Array<() => void> = [];

  get receivePaused(): boolean {
    return this.active >= ZLINK_STREAM_MAX_IN_FLIGHT_DISPATCHES;
  }

  tryClaim(): ZLinkApplicationWorkClaim | undefined {
    if (this.receivePaused) {
      return undefined;
    }
    this.active += 1;
    let released = false;
    return {
      close: () => {
        if (released) {
          return;
        }
        released = true;
        this.active -= 1;
        if (this.waiters.length > 0 && !this.receivePaused) {
          this.waiters.shift()?.();
        }
      }
    };
  }

  waitUntilResumed(signal?: AbortSignal): Promise<void> {
    if (!this.receivePaused || signal?.aborted === true) {
      return Promise.resolve();
    }
    return new Promise<void>((resolve) => {
      let waiting = true;
      const finish = (): void => {
        if (!waiting) {
          return;
        }
        waiting = false;
        const index = this.waiters.indexOf(finish);
        if (index >= 0) {
          this.waiters.splice(index, 1);
        }
        signal?.removeEventListener('abort', finish);
        resolve();
      };
      this.waiters.push(finish);
      signal?.addEventListener('abort', finish, { once: true });
      if (!this.receivePaused) {
        finish();
      }
    });
  }
}
