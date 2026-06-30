import fs from 'node:fs';
import path from 'node:path';

export class EvidenceStore {
  readonly rid: string;
  private readonly entries: string[] = [];
  private readonly waiters: Array<() => void> = [];

  constructor(private readonly filePath?: string) {
    this.rid = process.env.ZLINK_E2E_RID ?? 'node';
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
    const waiters = this.waiters.splice(0);
    for (const wake of waiters) {
      wake();
    }
  }

  snapshot(): string[] {
    return [...this.entries];
  }

  clear(): void {
    this.entries.length = 0;
    if (this.filePath !== undefined && this.filePath.length > 0) {
      fs.writeFileSync(this.filePath, '');
    }
  }

  async waitUntil(predicate: (line: string) => boolean, timeoutMs: number): Promise<string[]> {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      const snapshot = this.snapshot();
      if (snapshot.some(predicate)) {
        return snapshot;
      }
      await new Promise<void>((resolve) => {
        const timer = setTimeout(resolve, Math.min(250, Math.max(1, deadline - Date.now())));
        this.waiters.push(() => {
          clearTimeout(timer);
          resolve();
        });
      });
    }
    return this.snapshot();
  }
}
