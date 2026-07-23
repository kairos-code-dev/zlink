import type {
  ZLinkAuthorityKey,
  ZLinkPlacementObjectKind
} from '../../contracts/Locations/Authority';

export function encodeAuthorityKey(
  kind: ZLinkPlacementObjectKind,
  globalId: string
): ZLinkAuthorityKey {
  const bytes = Buffer.from(globalId);
  if (bytes.byteLength < 1 || bytes.byteLength > 255) {
    throw new RangeError('Authority identity must contain 1..255 UTF-8 bytes.');
  }
  const discriminator = kind === 'actor' ? 'a' : 's';
  return {
    value: `zla1:${discriminator}:${bytes.byteLength}:${percentEncode(bytes)}`
  } as ZLinkAuthorityKey;
}

function percentEncode(bytes: Uint8Array): string {
  let result = '';
  for (const byte of bytes) {
    result += isUnreserved(byte)
      ? String.fromCharCode(byte)
      : `%${byte.toString(16).toUpperCase().padStart(2, '0')}`;
  }
  return result;
}

function isUnreserved(byte: number): boolean {
  return byte >= 0x41 && byte <= 0x5a
    || byte >= 0x61 && byte <= 0x7a
    || byte >= 0x30 && byte <= 0x39
    || byte === 0x2d
    || byte === 0x2e
    || byte === 0x5f
    || byte === 0x7e;
}
