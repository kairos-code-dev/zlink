import {
  Disposable,
  ZlinkStreamEncodedPayload,
  ZlinkStreamErrorCode,
  ZlinkStreamMessage
} from '../Contracts';
import { validateName } from './Protocol/ZlinkStreamPacketNameValidator';
import { subscription } from './ZlinkStreamSupport';
import type { ZlinkStreamConnectorEvents } from './ZlinkStreamConnectorEvents';

type EncodedMessageHandler = (
  message: ZlinkStreamMessage<ZlinkStreamEncodedPayload>,
  signal?: AbortSignal
) => Promise<void> | void;

export class ZlinkStreamReceivedMessages {
  private readonly handlers = new Map<string, Set<EncodedMessageHandler>>();
  private readonly queue: Array<ZlinkStreamMessage<ZlinkStreamEncodedPayload>> = [];
  private drainTask: Promise<void> | undefined;
  private dropReportPending = false;

  constructor(
    private readonly capacity: number,
    private readonly events: ZlinkStreamConnectorEvents
  ) {}

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

  enqueue(message: ZlinkStreamMessage<ZlinkStreamEncodedPayload>, signal?: AbortSignal): void {
    if (this.queue.length >= this.capacity) {
      this.reportDropped(signal);
      return;
    }
    this.queue.push(message);
    this.scheduleDrain(signal);
  }

  private scheduleDrain(signal?: AbortSignal): void {
    if (this.drainTask !== undefined) {
      return;
    }
    this.drainTask = this.drain(signal).finally(() => {
      this.drainTask = undefined;
      if (this.queue.length > 0) {
        this.scheduleDrain(signal);
      }
    });
  }

  private async drain(signal?: AbortSignal): Promise<void> {
    while (this.queue.length > 0) {
      const message = this.queue.shift()!;
      const handlers = this.handlers.get(message.name);
      if (handlers === undefined) {
        continue;
      }
      for (const handler of handlers) {
        try {
          await handler(message, signal);
        } catch (cause) {
          await this.events.publishError({
            code: ZlinkStreamErrorCode.UserCallbackFailed,
            message: 'Typed message handler failed.',
            cause
          }, signal);
        }
      }
    }
  }

  private reportDropped(signal?: AbortSignal): void {
    if (this.dropReportPending) {
      return;
    }
    this.dropReportPending = true;
    queueMicrotask(() => {
      void this.events.publishError({
        code: ZlinkStreamErrorCode.ReceivedMessageDropped,
        message: 'Received stream message was dropped because the received-message queue is full.'
      }, signal).finally(() => {
        this.dropReportPending = false;
      }).catch(() => {});
    });
  }
}
