declare const zlinkRelocationReferenceBrand: unique symbol;

export interface ZLinkRelocationReference {
  readonly value: string;
  readonly [zlinkRelocationReferenceBrand]: true;
}

export interface ZLinkRelocationStored {
  readonly reference: ZLinkRelocationReference;
  readonly checksumCrc32c: number;
  readonly expiresAt: Date;
  readonly storeNow: Date;
}

export type ZLinkRelocationReadResult =
  | { readonly kind: 'found'; readonly payload: Uint8Array }
  | { readonly kind: 'missing' };

export type ZLinkRelocationRenewResult =
  | { readonly kind: 'renewed'; readonly expiresAt: Date; readonly storeNow: Date }
  | { readonly kind: 'missing' };

export type ZLinkRelocationDeleteResult = 'deleted' | 'missing';

export interface ZLinkRelocationStore {
  putRelocation(
    payload: Uint8Array,
    retentionMs: number,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationStored>;
  getRelocation(
    reference: ZLinkRelocationReference,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationReadResult>;
  renewRelocation(
    reference: ZLinkRelocationReference,
    retentionMs: number,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationRenewResult>;
  deleteRelocation(
    reference: ZLinkRelocationReference,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationDeleteResult>;
}
