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

interface QueuedMessage {
  readonly message: ZlinkStreamMessage<ZlinkStreamEncodedPayload>;
  readonly signal?: AbortSignal;
}

export class ZlinkStreamReceivedMessages {
  private readonly handlers = new Map<string, Set<EncodedMessageHandler>>();
  private readonly queue: QueuedMessage[] = [];
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
    if (this.queue.some((queued) => queued.message.name === name)) {
      queueMicrotask(() => this.scheduleDrain());
    }
    return subscription(() => set.delete(handler));
  }

  enqueue(message: ZlinkStreamMessage<ZlinkStreamEncodedPayload>, signal?: AbortSignal): void {
    if (this.queue.length >= this.capacity) {
      this.reportDropped(signal);
      return;
    }
    this.queue.push({ message, signal });
    this.scheduleDrain();
  }

  private scheduleDrain(): void {
    if (this.drainTask !== undefined) {
      return;
    }
    this.drainTask = this.drain().finally(() => {
      this.drainTask = undefined;
      if (this.findDeliverableIndex() >= 0) {
        this.scheduleDrain();
      }
    });
  }

  private async drain(): Promise<void> {
    for (let index = this.findDeliverableIndex(); index >= 0; index = this.findDeliverableIndex()) {
      const [{ message, signal }] = this.queue.splice(index, 1);
      const handlers = this.handlers.get(message.name)!;
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

  private findDeliverableIndex(): number {
    return this.queue.findIndex(({ message }) => (this.handlers.get(message.name)?.size ?? 0) > 0);
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
