export interface ZLinkMessageMetadata {
  readonly values: ReadonlyMap<string, string>;
  find(key: string): string | undefined;
}

export interface ZLinkMessageMetadataPolicy {
  canForward(key: string): boolean;
}

class ImmutableZLinkMessageMetadata implements ZLinkMessageMetadata {
  readonly values: ReadonlyMap<string, string>;

  constructor(values: ReadonlyMap<string, string> | Readonly<Record<string, string>> = new Map()) {
    this.values = new Map(values instanceof Map ? values : Object.entries(values));
  }

  find(key: string): string | undefined {
    return this.values.get(key);
  }
}

export const ZLinkMessageMetadataEmpty: ZLinkMessageMetadata =
  Object.freeze(new ImmutableZLinkMessageMetadata());

export function zlinkMessageMetadata(
  values: ReadonlyMap<string, string> | Readonly<Record<string, string>>
): ZLinkMessageMetadata {
  return Object.freeze(new ImmutableZLinkMessageMetadata(values));
}
