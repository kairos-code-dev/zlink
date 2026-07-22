export interface CloseableResource {
  close(): void | Promise<void>;
}

/** Closes owned runtime resources once, in reverse acquisition order. */
export class EventLoopResourceStack {
  private readonly resources: CloseableResource[] = [];
  private closePromise?: Promise<void>;

  own<T extends CloseableResource>(resource: T): T {
    if (this.closePromise !== undefined) throw new Error('Resource stack is closing.');
    this.resources.push(resource);
    return resource;
  }

  close(): Promise<void> {
    if (this.closePromise !== undefined) return this.closePromise;
    const resources = this.resources.splice(0).reverse();
    this.closePromise = closeResources(resources);
    return this.closePromise;
  }
}

async function closeResources(resources: readonly CloseableResource[]): Promise<void> {
  const failures: unknown[] = [];
  for (const resource of resources) {
    try {
      await resource.close();
    } catch (error) {
      failures.push(error);
    }
  }
  if (failures.length > 0) {
    throw new AggregateError(failures, 'Event-loop resource cleanup failed.');
  }
}

export type EventLoopTask = () => void | Promise<void>;

/** Keeps infrastructure progress independent from awaiting application handlers. */
export class EventLoopWorkQueues {
  private readonly infrastructure: EventLoopTask[] = [];
  private readonly application: EventLoopTask[] = [];
  private infrastructureScheduled = false;
  private applicationScheduled = false;
  private accepting = true;

  constructor(
    private readonly infrastructureLimit: number,
    private readonly applicationLimit: number
  ) {
    if (infrastructureLimit < 1 || applicationLimit < 1) {
      throw new RangeError('Queue limits must be positive.');
    }
  }

  submitInfrastructure(task: EventLoopTask): boolean {
    if (!this.accepting || this.infrastructure.length >= this.infrastructureLimit) return false;
    this.infrastructure.push(task);
    this.scheduleInfrastructure();
    return true;
  }

  submitApplication(task: EventLoopTask): boolean {
    if (!this.accepting || this.application.length >= this.applicationLimit) return false;
    this.application.push(task);
    this.scheduleApplication();
    return true;
  }

  stopAdmission(): void {
    this.accepting = false;
  }

  private scheduleInfrastructure(): void {
    if (this.infrastructureScheduled) return;
    this.infrastructureScheduled = true;
    queueMicrotask(() => this.drainInfrastructure());
  }

  private drainInfrastructure(): void {
    this.infrastructureScheduled = false;
    const task = this.infrastructure.shift();
    if (task === undefined) return;
    try {
      const result = task();
      if (result instanceof Promise) void result.catch(() => undefined);
    } finally {
      if (this.infrastructure.length > 0) this.scheduleInfrastructure();
    }
  }

  private scheduleApplication(): void {
    if (this.applicationScheduled) return;
    this.applicationScheduled = true;
    queueMicrotask(() => void this.drainApplication());
  }

  private async drainApplication(): Promise<void> {
    try {
      const task = this.application.shift();
      if (task !== undefined) await task();
    } catch {
      // Handler failure is reported by the dispatch owner; it does not stop this queue.
    } finally {
      this.applicationScheduled = false;
      if (this.application.length > 0) this.scheduleApplication();
    }
  }
}
