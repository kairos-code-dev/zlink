export class RedisStoreKeys {
  constructor(private readonly keyPrefix: string) {}

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
    return this.leasePrefix() + ownerId;
  }

  leasePrefix(): string {
    return `${this.keyPrefix}:lease:`;
  }

  leaseIndex(): string {
    return `${this.keyPrefix}:leases`;
  }

  leaseGenerationCounter(): string {
    return `${this.keyPrefix}:lease-generation`;
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
