import {
  RecvFlags
} from '@zlink-systems/zlink';
import {
  ReadyDomain,
  type ReadyRecord,
  type ReceiveRecord
} from '../foundation/service-runtime-contracts';
import type { ZLinkBackendMeshNode } from './contracts';

export interface ZLinkMeshDispatchPumpOptions {
  readonly readyCapacity?: number;
  readonly messageCapacity?: number;
  readonly partCapacity?: number;
  readonly byteCapacity?: number;
  readonly dispatch: (owner: ReadyRecord, record: ReceiveRecord) => void | Promise<void>;
  readonly reportError?: (error: unknown) => void;
}

export class ZLinkMeshDispatchPump {
  private pendingDomains: number = ReadyDomain.None;
  private scheduled = false;
  private disposed = false;
  private drainPromise?: Promise<void>;

  constructor(
    private readonly node: ZLinkBackendMeshNode,
    private readonly options: ZLinkMeshDispatchPumpOptions
  ) {
  }

  start(): void {
    this.node.setReadyHandler((domains) => {
      if (this.disposed) {
        return ReadyDomain.None;
      }
      this.pendingDomains |= domains;
      this.schedule();
      return domains;
    });
  }

  async dispose(): Promise<void> {
    if (this.disposed) {
      return;
    }
    this.disposed = true;
    this.pendingDomains = ReadyDomain.None;
    await this.drainPromise;
  }

  private schedule(): void {
    if (this.scheduled) {
      return;
    }
    this.scheduled = true;
    const drain = yieldToEventLoop()
      .then(() => this.drain())
      .catch((error) => this.options.reportError?.(error));
    this.drainPromise = drain;
    void drain.finally(() => {
      if (this.drainPromise === drain) {
        this.drainPromise = undefined;
        this.scheduled = false;
        if (!this.disposed && this.pendingDomains !== ReadyDomain.None) {
          this.schedule();
        }
      }
    });
  }

  private async drain(): Promise<void> {
    while (!this.disposed) {
      const domains = this.pendingDomains;
      this.pendingDomains = ReadyDomain.None;
      if (domains === ReadyDomain.None) {
        return;
      }
      await this.drainDomains(domains);
    }
  }

  private async drainDomains(domains: number): Promise<void> {
    if ((domains & ReadyDomain.Infrastructure) !== 0) {
      await this.drainDomain(ReadyDomain.Infrastructure);
    }
    if ((domains & ReadyDomain.Application) !== 0) {
      await this.drainDomain(ReadyDomain.Application);
    }
  }

  private async drainDomain(domain: number): Promise<void> {
    const readyBatch = this.node.createReadyBatch(this.options.readyCapacity ?? 32);
    const receiveBatch = this.node.createReceiveBatch(
      this.options.messageCapacity ?? 64,
      this.options.partCapacity ?? 256,
      this.options.byteCapacity ?? (1 << 20)
    );
    try {
      for (;;) {
        readyBatch.reset();
        const drained = this.node.drainReady(domain, readyBatch, RecvFlags.DontWait);
        if (!drained.ok || drained.records.length === 0) {
          return;
        }
        for (let index = 0; index < drained.records.length; index += 1) {
          const claim = readyBatch.takeClaim(index);
          try {
            receiveBatch.reset();
            for (;;) {
              const received = claim.recvBatch(receiveBatch, RecvFlags.DontWait);
              if (!received.ok) {
                break;
              }
              for (const record of received.records) {
                try {
                  // A handler may synchronously submit an operation whose
                  // control/completion is owned by this same MeshNode.
                  this.scheduled = false;
                  this.drainPromise = undefined;
                  if (this.pendingDomains !== ReadyDomain.None) {
                    this.schedule();
                  }
                  await this.options.dispatch(drained.records[index], record);
                } finally {
                  for (const part of record.parts) {
                    part.close();
                  }
                }
              }
              await yieldToEventLoop();
              receiveBatch.reset();
            }
          } finally {
            claim.release();
          }
        }
        await yieldToTimers();
        if (!drained.hasResidue) {
          return;
        }
      }
    } finally {
      receiveBatch.close();
      readyBatch.close();
    }
  }
}

function yieldToEventLoop(): Promise<void> {
  return new Promise((resolve) => setImmediate(resolve));
}

function yieldToTimers(): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, 0));
}
