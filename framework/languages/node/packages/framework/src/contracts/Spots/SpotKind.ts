export enum ZLinkSpotKind {
  Invalid = 'invalid',
  Entry = 'entry',
  User = 'user'
}

export function zlinkSpotKindToWire(kind: ZLinkSpotKind): number {
  switch (kind) {
    case ZLinkSpotKind.Entry: return 1;
    case ZLinkSpotKind.User: return 2;
    default: return 0;
  }
}

export function zlinkSpotKindFromWire(value: number): ZLinkSpotKind {
  switch (value) {
    case 1: return ZLinkSpotKind.Entry;
    case 2: return ZLinkSpotKind.User;
    default: return ZLinkSpotKind.Invalid;
  }
}
