import fs from 'node:fs';
import path from 'node:path';

export class EvidenceStore {
  readonly rid: string;
  private readonly entries: string[] = [];
  private readonly waiters = new Set<() => void>();

  constructor(private readonly filePath?: string) {
    this.rid = process.env.ZLINK_E2E_RID ?? 'subscriber';
    if (filePath !== undefined && filePath.length > 0) {
      fs.mkdirSync(path.dirname(filePath), { recursive: true });
      fs.writeFileSync(filePath, '');
    }
  }

  add(entry: string): void {
    this.entries.push(entry);
    if (this.filePath !== undefined && this.filePath.length > 0) {
      fs.appendFileSync(this.filePath, `${entry}\n`);
    }
    for (const waiter of [...this.waiters]) {
      waiter();
    }
  }

  snapshot(): readonly string[] {
    return [...this.entries];
  }

  clear(): void {
    this.entries.length = 0;
    if (this.filePath !== undefined && this.filePath.length > 0) {
      fs.writeFileSync(this.filePath, '');
    }
  }

  async waitUntil(predicate: (snapshot: readonly string[]) => boolean, timeoutMs: number): Promise<readonly string[]> {
    const deadline = Date.now() + timeoutMs;
    while (true) {
      const snapshot = this.snapshot();
      if (predicate(snapshot)) {
        return snapshot;
      }
      const remaining = deadline - Date.now();
      if (remaining <= 0) {
        throw new Error('Timed out waiting for PubSub evidence.');
      }
      await new Promise<void>((resolve) => {
        let timer: NodeJS.Timeout;
        const done = () => {
          clearTimeout(timer);
          this.waiters.delete(done);
          resolve();
        };
        timer = setTimeout(done, Math.min(remaining, 250));
        this.waiters.add(done);
      });
    }
  }
}
