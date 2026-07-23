import { createHash } from 'node:crypto';

export class RedisStoreKeys {
  private readonly authorityDomain: string;

  constructor(private readonly keyPrefix: string) {
    if (keyPrefix.includes('{') || keyPrefix.includes('}')) {
      throw new Error('Redis location keyPrefix must not contain hash-tag braces.');
    }
    this.authorityDomain = `${keyPrefix}:{zlink-location-v1}`;
  }

  rowHash(tag: string, rowKey: string): string {
    return this.rowHashPrefix(tag) + rowKey;
  }

  rowHashPrefix(tag: string): string {
    return `${this.keyPrefix}:row:${tag}:`;
  }

  generation(tag: string, rowKey: string): string {
    return `${this.keyPrefix}:gen:${tag}:${rowKey}`;
  }

  kindIndex(tag: string): string {
    return `${this.keyPrefix}:keys:${tag}`;
  }

  ownerIndexPrefix(tag: string): string {
    return `${this.keyPrefix}:own:${tag}:`;
  }

  lease(ownerId: string): string {
    return `${this.authorityDomain}:owner-lease:${digest(ownerId)}`;
  }

  leasePrefix(): string {
    return `${this.authorityDomain}:owner-lease:`;
  }

  leaseGenerationCounter(): string {
    return this.counter();
  }

  schema(): string {
    return `${this.authorityDomain}:schema`;
  }

  configuredPrefixPattern(): string {
    return `${this.keyPrefix}:*`;
  }

  counter(): string {
    return `${this.authorityDomain}:counter`;
  }

  descriptorMesh(canonicalKey: string): string {
    return `${this.authorityDomain}:descriptor:mesh:${digest(canonicalKey)}`;
  }

  descriptorAdmissionMesh(canonicalKey: string): string {
    return `${this.authorityDomain}:descriptor-admission:mesh:${digest(canonicalKey)}`;
  }

  descriptorMeshIndex(): string {
    return `${this.authorityDomain}:descriptor:mesh:index`;
  }

  descriptorMeshOwnerIndex(ownerId: string, leaseGeneration: string): string {
    return `${this.authorityDomain}:descriptor:mesh:owner:${digest(`${ownerId}\0${leaseGeneration}`)}`;
  }

  authorityCurrent(authorityKey: string): string {
    return `${this.authorityDomain}:authority:current:${digest(authorityKey)}`;
  }

  authorityHistory(authorityKey: string): string {
    return `${this.authorityDomain}:authority:history:${digest(authorityKey)}`;
  }

  authorityHistoryRevisions(authorityKey: string): string {
    return `${this.authorityDomain}:authority:history-revisions:${digest(authorityKey)}`;
  }

  authorityKeyIndex(): string {
    return `${this.authorityDomain}:authority:key-index`;
  }

  authorityIndexGc(): string {
    return `${this.authorityDomain}:authority:index-gc`;
  }

  membershipCurrent(): string {
    return `${this.authorityDomain}:membership:current`;
  }

  membershipHistory(authorityKey: string): string {
    return `${this.authorityDomain}:membership:history:${digest(authorityKey)}`;
  }

  membershipHistoryRevisions(authorityKey: string): string {
    return `${this.authorityDomain}:membership:history-revisions:${digest(authorityKey)}`;
  }

  capacityNode(phase: 'active' | 'pending'): string {
    return `${this.authorityDomain}:capacity:node:${phase}`;
  }

  capacityType(phase: 'active' | 'pending'): string {
    return `${this.authorityDomain}:capacity:type:${phase}`;
  }

  creation(reservationId: string): string {
    return `${this.authorityDomain}:creation:${compactId(reservationId)}`;
  }

  relocation(fenceId: string): string {
    return `${this.authorityDomain}:relocation:${compactId(fenceId)}`;
  }

  aggregate(aggregateId: string, generation: string): string {
    return `${this.authorityDomain}:aggregate:${compactId(aggregateId)}:${generation}`;
  }

  scan(scanId: string): string {
    return `${this.authorityDomain}:scan:${compactId(scanId)}`;
  }

  scansExpiry(): string {
    return `${this.authorityDomain}:scans:expiry`;
  }

  scansWatermark(): string {
    return `${this.authorityDomain}:scans:watermark`;
  }

  meshRowPrefix(): string {
    return `${this.authorityDomain}:descriptor:mesh:`;
  }

  routingIdAllocationGroup(groupName: string): string {
    return `${this.keyPrefix}:ridalloc:${groupName}`;
  }

  actorTransfer(actorRowKey: string, transferId: string): string {
    return `${this.keyPrefix}:transfer:${actorRowKey}:${transferId}`;
  }

  actorTransferByActor(actorRowKey: string): string {
    return `${this.keyPrefix}:transfer-by-actor:${actorRowKey}`;
  }

  stamp(tag: string, meshName: string | undefined): string {
    return meshName === undefined
      ? `${this.keyPrefix}:stamp:${tag}`
      : `${this.keyPrefix}:stamp:${tag}:${meshName}`;
  }
}

function digest(value: string): string {
  return createHash('sha256').update(value, 'utf8').digest('hex');
}

function compactId(value: string): string {
  const compact = value.replaceAll('-', '').toLowerCase();
  if (!/^[0-9a-f]{32}$/.test(compact)) {
    throw new TypeError('Redis provider IDs must be non-zero 128-bit values.');
  }
  if (/^0+$/.test(compact)) {
    throw new TypeError('Redis provider IDs must be non-zero 128-bit values.');
  }
  return compact;
}
