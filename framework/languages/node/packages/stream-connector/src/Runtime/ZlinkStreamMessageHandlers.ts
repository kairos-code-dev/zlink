import {
  Disposable,
  ZlinkStreamEncodedPayload,
  ZlinkStreamError,
  ZlinkStreamErrorCode,
  ZlinkStreamMessage
} from '../Contracts';
import { subscription } from './ZlinkStreamSupport';
import { validateName } from './Protocol/ZlinkStreamPacketNameValidator';

type EncodedMessageHandler = (message: ZlinkStreamMessage<ZlinkStreamEncodedPayload>, signal?: AbortSignal) => Promise<void> | void;

export class ZlinkStreamMessageHandlers {
  private readonly handlers = new Map<string, Set<EncodedMessageHandler>>();

  on(name: string, handler: EncodedMessageHandler): Disposable {
    validateName(name);
    let set = this.handlers.get(name);
    if (set === undefined) {
      set = new Set();
      this.handlers.set(name, set);
    }
    set.add(handler);
    return subscription(() => set.delete(handler));
  }

  async dispatch(
    message: ZlinkStreamMessage<ZlinkStreamEncodedPayload>,
    signal: AbortSignal | undefined,
    publishError: (error: ZlinkStreamError, signal?: AbortSignal) => Promise<void>
  ): Promise<void> {
    const handlers = this.handlers.get(message.name);
    if (handlers === undefined) {
      return;
    }
    for (const handler of handlers) {
      try {
        await handler(message, signal);
      } catch (cause) {
        await publishError({
          code: ZlinkStreamErrorCode.UserCallbackFailed,
          message: 'Typed message handler failed.',
          cause
        }, signal);
      }
    }
  }
}
