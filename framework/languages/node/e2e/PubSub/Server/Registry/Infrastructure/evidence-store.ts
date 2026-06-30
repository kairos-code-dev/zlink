export class EvidenceStore {
  readonly rid: string;
  private readonly entries: string[] = [];

  constructor(rid: string) {
    this.rid = rid;
  }

  add(entry: string): void {
    this.entries.push(entry);
  }

  snapshot(): readonly string[] {
    return [...this.entries];
  }

  clear(): void {
    this.entries.length = 0;
  }
}
