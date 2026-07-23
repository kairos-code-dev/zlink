export function encodeAuthorityKey(
  kind: 'actor' | 'user_spot' | 'instance_spot',
  globalId: string
): string {
  const bytes = Buffer.from(globalId);
  if (bytes.byteLength < 1 || bytes.byteLength > 255) {
    throw new RangeError('Authority identity must contain 1..255 UTF-8 bytes.');
  }
  const discriminator = kind === 'actor' ? 'a' : 's';
  let encoded = '';
  for (const byte of bytes) {
    encoded += isUnreserved(byte)
      ? String.fromCharCode(byte)
      : `%${byte.toString(16).toUpperCase().padStart(2, '0')}`;
  }
  return `zla1:${discriminator}:${bytes.byteLength}:${encoded}`;
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
