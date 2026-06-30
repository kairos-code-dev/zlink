import { zlinkStreamJsonCodec } from '@zlink-systems/stream-connector';
import type { ZlinkStreamEncodedPayload } from '@zlink-systems/stream-connector';

export function decodeStreamReply<TReply>(reply: TReply | ZlinkStreamEncodedPayload): TReply {
  if (
    typeof reply === 'object'
    && reply !== null
    && 'codec' in reply
    && 'payload' in reply
  ) {
    return zlinkStreamJsonCodec.decode<TReply>(reply as ZlinkStreamEncodedPayload);
  }
  return reply as TReply;
}
