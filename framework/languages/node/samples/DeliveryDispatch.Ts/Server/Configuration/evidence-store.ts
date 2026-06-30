import fs from 'node:fs';
import path from 'node:path';
import type { DeliveryStatus, DeliveryStatusReq } from '../../Shared/Contracts/messages';

class EvidenceStore {
  private readonly events: DeliveryStatusReq[] = [];
  private readonly filePath: string | undefined;

  constructor() {
    const workDir = process.env.DELIVERYDISPATCH_WORK_DIR;
    this.filePath = workDir === undefined ? undefined : path.join(workDir, 'deliverydispatch-evidence.jsonl');
    if (this.filePath !== undefined) {
      fs.mkdirSync(path.dirname(this.filePath), { recursive: true });
    }
  }

  append(event: DeliveryStatusReq): void {
    this.events.push(event);
    if (this.filePath !== undefined) {
      fs.appendFileSync(this.filePath, `${JSON.stringify(event)}\n`);
    }
  }

  hasSequence(deliveryId: string, ...statuses: DeliveryStatus[]): boolean {
    const observed = this.readEvents()
      .filter((event) => event.deliveryId === deliveryId)
      .map((event) => event.status);
    let index = 0;
    for (const status of observed) {
      if (status === statuses[index]) {
        index += 1;
      }
      if (index === statuses.length) {
        return true;
      }
    }
    return false;
  }

  readLines(): string[] {
    return this.readEvents().map((event) =>
      `${event.deliveryId}:${event.status}:${event.courierId ?? '-'}:${event.occurredAt}`
    );
  }

  private readEvents(): DeliveryStatusReq[] {
    if (this.filePath === undefined || !fs.existsSync(this.filePath)) {
      return this.events;
    }
    return fs.readFileSync(this.filePath, 'utf8')
      .split('\n')
      .filter((line) => line.length > 0)
      .map((line) => JSON.parse(line) as DeliveryStatusReq);
  }
}

export { EvidenceStore };
