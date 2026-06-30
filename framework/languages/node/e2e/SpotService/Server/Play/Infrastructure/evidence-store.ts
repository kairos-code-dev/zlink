import fs from 'node:fs';
import path from 'node:path';

export class EvidenceStore {
  private readonly entries: string[] = [];
  private readonly waiters: (() => void)[] = [];

  constructor(readonly rid: string, private readonly file?: string) {
    if (this.file !== undefined) {
      fs.mkdirSync(path.dirname(this.file), { recursive: true });
      fs.writeFileSync(this.file, '');
    }
  }

  add(entry: string): void {
    this.entries.push(entry);
    if (this.file !== undefined) {
      fs.appendFileSync(this.file, `${entry}\n`);
    }
    const waiters = this.waiters.splice(0);
    for (const waiter of waiters) {
      waiter();
    }
  }

  snapshot(): string[] {
    return [...this.entries];
  }

  async waitUntil(predicate: (entries: readonly string[]) => boolean, timeoutMs: number): Promise<string[]> {
    const deadline = Date.now() + timeoutMs;
    while (true) {
      const snapshot = this.snapshot();
      if (predicate(snapshot)) {
        return snapshot;
      }
      const remaining = deadline - Date.now();
      if (remaining <= 0) {
        throw new Error('Timed out waiting for SpotService evidence.');
      }
      await new Promise<void>((resolve) => {
        const timer = setTimeout(resolve, Math.min(remaining, 100));
        this.waiters.push(() => {
          clearTimeout(timer);
          resolve();
        });
      });
    }
  }
}
