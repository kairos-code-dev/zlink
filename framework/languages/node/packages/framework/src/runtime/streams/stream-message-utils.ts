import type { Message } from '../../contracts/Common/Message';
import { utf8Decode, utf8Encode } from './protocol';

export function copyMessage(message: Message): Message {
  const value = message as unknown as {
    copy?: () => Message;
    toBytes?: () => Uint8Array;
    data?: () => Uint8Array;
    bytes?: Uint8Array;
    getString?: () => string;
  };
  if (value.copy !== undefined) {
    return value.copy();
  }
  if (value.toBytes !== undefined) {
    return simpleMessage(value.toBytes()) as Message;
  }
  if (value.data !== undefined) {
    return simpleMessage(value.data()) as Message;
  }
  if (value.bytes !== undefined) {
    return simpleMessage(value.bytes) as Message;
  }
  if (value.getString !== undefined) {
    return simpleMessage(utf8Encode(value.getString())) as Message;
  }
  throw new Error('Stream response payload cannot be copied.');
}

export function simpleMessage(bytes: Uint8Array): unknown {
  const copy = new Uint8Array(bytes);
  return {
    bytes: copy,
    toBytes() {
      return new Uint8Array(copy);
    },
    data() {
      return copy;
    },
    getString() {
      return utf8Decode(copy);
    },
    value() {
      return JSON.parse(utf8Decode(copy));
    },
    close() {}
  };
}
