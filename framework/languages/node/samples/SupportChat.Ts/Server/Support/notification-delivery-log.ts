type DeliveredFrame = {
  seq: number;
  actorId: string;
  packetName: string;
  payload: unknown;
};

type DeliveryWaiter = {
  actorId: string;
  afterSeq: number;
  resolve: (value: { delivered: DeliveredFrame[]; nextSeq: number }) => void;
};

const defaultWaitTimeoutMs = 100;

// SupportNotificationDeliveryLog buffers per-actor server push frames. The Session server
// pumps each bound actor's frames to its stream session. This is the node sample idiom for
// bound session push: the conversation event publisher appends here, the session relays out.
class SupportNotificationDeliveryLog {
  private readonly delivered: DeliveredFrame[] = [];
  private readonly waiters: DeliveryWaiter[] = [];
  private deliveredSeq = 0;

  append(actorId: string, packetName: string, payload: unknown): void {
    this.delivered.push({
      seq: ++this.deliveredSeq,
      actorId,
      packetName,
      payload
    });
    this.notifyWaiters();
  }

  async waitFor(
    actorId: string,
    afterSeq = 0,
    timeoutMs: number = defaultWaitTimeoutMs
  ): Promise<{ delivered: DeliveredFrame[]; nextSeq: number }> {
    const ready = this.deliveredFor(actorId, afterSeq);
    if (ready.delivered.length > 0) {
      return ready;
    }
    return await new Promise((resolve) => {
      let timeout: ReturnType<typeof setTimeout>;
      const waiter: DeliveryWaiter = {
        actorId,
        afterSeq,
        resolve: (value) => {
          clearTimeout(timeout);
          resolve(value);
        }
      };
      timeout = setTimeout(() => {
        const index = this.waiters.indexOf(waiter);
        if (index >= 0) {
          this.waiters.splice(index, 1);
        }
        resolve({ delivered: [], nextSeq: afterSeq });
      }, timeoutMs);
      this.waiters.push(waiter);
    });
  }

  private deliveredFor(actorId: string, afterSeq: number): { delivered: DeliveredFrame[]; nextSeq: number } {
    const delivered = this.delivered.filter((entry) => entry.actorId === actorId && entry.seq > afterSeq);
    return {
      delivered,
      nextSeq: delivered.at(-1)?.seq ?? afterSeq
    };
  }

  private notifyWaiters(): void {
    const pending = [];
    for (const waiter of this.waiters) {
      const ready = this.deliveredFor(waiter.actorId, waiter.afterSeq);
      if (ready.delivered.length === 0) {
        pending.push(waiter);
        continue;
      }
      waiter.resolve(ready);
    }
    this.waiters.length = 0;
    this.waiters.push(...pending);
  }
}

export { SupportNotificationDeliveryLog };
