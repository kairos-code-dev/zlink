import type { Message } from '@zlink-systems/zlink';
import { decodeChannelEnvelope } from './channel-envelope';

export function isChannelEnvelope(parts: readonly Message[]): boolean {
  if (parts.length < 2 || parts[0].data().length === 0) {
    return false;
  }
  try {
    decodeChannelEnvelope(parts);
    return true;
  } catch {
    return false;
  }
}
