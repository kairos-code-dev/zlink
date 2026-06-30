import fs from 'node:fs';
import path from 'node:path';

export class EvidenceStore {
  readonly rid: string;
  private readonly entries: string[] = [];

  constructor(private readonly filePath?: string) {
    this.rid = process.env.ZLINK_E2E_RID ?? 'publisher';
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
}
