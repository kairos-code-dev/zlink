export type ServiceMailboxDomain = 'application' | 'infrastructure';

export interface ServiceMailboxRecord {
  readonly owner: string;
  readonly domain: ServiceMailboxDomain;
  readonly parts: readonly Uint8Array[];
  readonly sourceRoutingId?: string;
  readonly requestSequence?: bigint;
  readonly correlation?: bigint;
  readonly stateful?: unknown;
}

export interface ServiceMailboxClaim {
  readonly owner: string;
  readonly domain: ServiceMailboxDomain;
  readonly serial: bigint;
  readonly records: readonly ServiceMailboxRecord[];
}

interface OwnerQueue {
  readonly records: ServiceMailboxRecord[];
  bytes: number;
  claimed: boolean;
  claimSerial: bigint;
}

interface DomainState {
  readonly owners: Map<string, OwnerQueue>;
  readonly ready: string[];
  readonly indexed: Set<string>;
  messages: number;
  bytes: number;
  readonly messageBudget: number;
  readonly byteBudget: number;
}

export interface ServiceMailboxLimits {
  readonly applicationMessages: number;
  readonly applicationBytes: number;
  readonly infrastructureMessages: number;
  readonly infrastructureBytes: number;
}

/** Bounded level-triggered queues with one active application claim per owner. */
export class ServiceMailbox {
  private readonly application: DomainState;
  private readonly infrastructure: DomainState;
  private nextClaimSerial = 1n;
  private closed = false;

  constructor(limits: ServiceMailboxLimits) {
    this.application = createDomain(limits.applicationMessages, limits.applicationBytes);
    this.infrastructure = createDomain(limits.infrastructureMessages, limits.infrastructureBytes);
  }

  tryEnqueue(record: ServiceMailboxRecord): boolean {
    if (record.owner.length === 0 || record.parts.length === 0) {
      throw new TypeError('Mailbox records require an owner and retained payload.');
    }
    const target = this.domain(record.domain);
    const bytes = retainedBytes(record);
    if (
      this.closed
      || target.messages >= target.messageBudget
      || bytes > target.byteBudget - target.bytes
    ) {
      return false;
    }
    let queue = target.owners.get(record.owner);
    if (queue === undefined) {
      queue = { records: [], bytes: 0, claimed: false, claimSerial: 0n };
      target.owners.set(record.owner, queue);
    }
    const retained = retainRecord(record);
    queue.records.push(retained);
    queue.bytes += bytes;
    target.messages++;
    target.bytes += bytes;
    if (!queue.claimed && !target.indexed.has(record.owner)) {
      target.indexed.add(record.owner);
      target.ready.push(record.owner);
    }
    return true;
  }

  tryClaim(
    domain: ServiceMailboxDomain,
    messageBudget: number,
    byteBudget: number
  ): ServiceMailboxClaim | undefined {
    requirePositive(messageBudget, 'messageBudget');
    requirePositive(byteBudget, 'byteBudget');
    const source = this.domain(domain);
    for (;;) {
      const owner = source.ready.shift();
      if (owner === undefined) return undefined;
      source.indexed.delete(owner);
      const queue = source.owners.get(owner);
      if (queue === undefined || queue.claimed || queue.records.length === 0) continue;

      queue.claimed = true;
      const serial = this.nextClaimSerial++;
      queue.claimSerial = serial;
      const records: ServiceMailboxRecord[] = [];
      let bytes = 0;
      while (queue.records.length > 0 && records.length < messageBudget) {
        const next = queue.records[0]!;
        const nextBytes = retainedBytes(next);
        if (records.length > 0 && nextBytes > byteBudget - bytes) break;
        queue.records.shift();
        queue.bytes -= nextBytes;
        source.messages--;
        source.bytes -= nextBytes;
        bytes += nextBytes;
        records.push(next);
      }
      return { owner, domain, serial, records };
    }
  }

  release(claim: ServiceMailboxClaim): boolean {
    const target = this.domain(claim.domain);
    const queue = target.owners.get(claim.owner);
    if (queue === undefined || !queue.claimed || queue.claimSerial !== claim.serial) return false;
    queue.claimed = false;
    queue.claimSerial = 0n;
    if (queue.records.length === 0) {
      target.owners.delete(claim.owner);
    } else if (!target.indexed.has(claim.owner)) {
      target.indexed.add(claim.owner);
      target.ready.push(claim.owner);
    }
    return true;
  }

  close(): void {
    this.closed = true;
  }

  pendingMessages(domain: ServiceMailboxDomain): number {
    return this.domain(domain).messages;
  }

  pendingBytes(domain: ServiceMailboxDomain): number {
    return this.domain(domain).bytes;
  }

  private domain(domain: ServiceMailboxDomain): DomainState {
    return domain === 'application' ? this.application : this.infrastructure;
  }
}

function createDomain(messageBudget: number, byteBudget: number): DomainState {
  requirePositive(messageBudget, 'messageBudget');
  requirePositive(byteBudget, 'byteBudget');
  return {
    owners: new Map(),
    ready: [],
    indexed: new Set(),
    messages: 0,
    bytes: 0,
    messageBudget,
    byteBudget
  };
}

function retainRecord(record: ServiceMailboxRecord): ServiceMailboxRecord {
  return {
    ...record,
    parts: record.parts.map(part => Buffer.from(part))
  };
}

function retainedBytes(record: ServiceMailboxRecord): number {
  return record.parts.reduce((sum, part) => sum + part.byteLength, 0);
}

function requirePositive(value: number, field: string): void {
  if (!Number.isSafeInteger(value) || value < 1) {
    throw new RangeError(`${field} must be a positive safe integer.`);
  }
}
