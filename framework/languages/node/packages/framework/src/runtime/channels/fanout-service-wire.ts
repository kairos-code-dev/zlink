import type { Message } from '@zlink-systems/zlink';
import { isChannelEnvelope } from './channel-envelope-inspection';

export const FANOUT_LIVENESS_TOPIC = '\x01ZLF1';
export const FANOUT_LIVENESS_PAYLOAD =
  Uint8Array.from([0x5a, 0x46, 0x01, 0x01]);

export type ZLinkFanoutInboundKind =
  | 'application'
  | 'beacon'
  | 'protocolError';

export function classifyFanoutInbound(
  topic: string,
  parts: readonly { data(): Uint8Array }[]
): ZLinkFanoutInboundKind {
  if (topic !== FANOUT_LIVENESS_TOPIC) {
    return isChannelEnvelope(parts as readonly Message[])
      ? 'application'
      : 'protocolError';
  }
  if (parts.length !== 1) return 'protocolError';
  const payload = parts[0]!.data();
  return payload.length === FANOUT_LIVENESS_PAYLOAD.length
    && payload.every((value, index) =>
      value === FANOUT_LIVENESS_PAYLOAD[index])
    ? 'beacon'
    : 'protocolError';
}

export function requirePublicFanoutTopic(topic: string): void {
  if (topic === FANOUT_LIVENESS_TOPIC) {
    throw new TypeError('Fanout topic is reserved for framework liveness.');
  }
}
