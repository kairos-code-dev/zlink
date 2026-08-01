import {
  RecvFlags
} from '@zlink-systems/zlink';
import {
  operationRequiresReply,
  ReadyDomain,
  type ReadyRecord,
  type ReceiveRecord
} from '../foundation/service-runtime-contracts';
import type { ZLinkBackendMeshNode } from './contracts';
import type { ZLinkInboundDispatchBudget } from '../dispatch/inbound-dispatch-budget';

//  The pump awaits between the two reads and the budget can pause in that gap.
//  Reading through a function keeps the later check honest; an inline read
//  stays narrowed by the first one.
function budgetPaused(budget?: { readonly receivePaused: boolean }): boolean {
  return budget?.receivePaused === true;
}


export interface ZLinkMeshDispatchPumpOptions {
  readonly readyCapacity?: number;
  readonly messageCapacity?: number;
  readonly partCapacity?: number;
  readonly byteCapacity?: number;
  readonly dispatch: (owner: ReadyRecord, record: ReceiveRecord) => void | Promise<void>;
  readonly inboundDispatchBudget?: ZLinkInboundDispatchBudget;
  readonly reportError?: (error: unknown) => void;
}

export class ZLinkMeshDispatchPump {
  private pendingDomains: number = ReadyDomain.None;
  private scheduled = false;
  private disposed = false;
  private drainPromise?: Promise<void>;
  private readonly removeResumeListener?: () => void;

  constructor(
    private readonly node: ZLinkBackendMeshNode,
    private readonly options: ZLinkMeshDispatchPumpOptions
  ) {
    this.removeResumeListener = options.inboundDispatchBudget?.onResume(() => {
      if (this.disposed) return;
      this.pendingDomains |= ReadyDomain.Application;
      this.schedule();
    });
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
    this.removeResumeListener?.();
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
    const applicationBudget = domain === ReadyDomain.Application
      ? this.options.inboundDispatchBudget
      : undefined;
    if (budgetPaused(applicationBudget)) return;
    const readyBatch = this.node.createReadyBatch(this.options.readyCapacity ?? 32);
    const receiveBatch = this.node.createReceiveBatch(
      applicationBudget === undefined ? (this.options.messageCapacity ?? 64) : 1,
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
              if (budgetPaused(applicationBudget)) return;
              const received = claim.recvBatch(receiveBatch, RecvFlags.DontWait);
              if (!received.ok) {
                break;
              }
              for (const record of received.records) {
                const payloadBytes = BigInt(
                  record.parts.reduce((sum, part) => sum + part.size(), 0)
                );
                applicationBudget?.enqueue(payloadBytes);
                let started = false;
                let releaseCompletion: (() => void) | undefined;
                try {
                  releaseCompletion = operationRequiresReply(record.operationKind)
                    ? await applicationBudget?.acquireCompletionSend()
                    : undefined;
                  // A handler may synchronously submit an operation whose
                  // control/completion is owned by this same MeshNode.
                  this.scheduled = false;
                  this.drainPromise = undefined;
                  if (this.pendingDomains !== ReadyDomain.None) {
                    this.schedule();
                  }
                  applicationBudget?.start(payloadBytes);
                  started = true;
                  await this.options.dispatch(drained.records[index], record);
                  await record.onTerminalCompletion?.();
                } finally {
                  releaseCompletion?.();
                  if (started) {
                    applicationBudget?.complete(payloadBytes);
                  } else {
                    applicationBudget?.cancelQueued(payloadBytes);
                  }
                  for (const part of record.parts) {
                    part.close();
                  }
                }
              }
              // A continuously readable owner must not keep the timers phase
              // from running while the pump consumes successive batches.
              await yieldToTimers();
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
