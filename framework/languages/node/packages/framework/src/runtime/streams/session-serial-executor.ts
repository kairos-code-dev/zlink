export class ZLinkStreamSessionSerialExecutor {
  private tail: Promise<void> | undefined;
  private closed = false;

  enqueue(work: () => Promise<void>): boolean {
    if (this.closed) {
      return false;
    }
    const previous = this.tail;
    const next = (previous === undefined ? runNow(work) : previous.then(work, work))
      .catch(() => {});
    this.tail = next;
    next.finally(() => {
      if (this.tail === next) {
        this.tail = undefined;
      }
    });
    return true;
  }

  run(work: () => Promise<void>): Promise<void> {
    return new Promise((resolve, reject) => {
      if (!this.enqueue(async () => {
        try {
          await work();
          resolve();
        } catch (error) {
          reject(error);
        }
      })) {
        reject(new Error('Session execution queue is closed.'));
      }
    });
  }

  async dispose(): Promise<void> {
    this.closed = true;
    await this.tail;
  }
}

async function runNow(work: () => Promise<void>): Promise<void> {
  await Promise.resolve();
  await work();
}
