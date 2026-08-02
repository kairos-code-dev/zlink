import type { RoutingId } from '../../contracts';
import type {
  Message,
  MessageLike
} from '@zlink-systems/zlink';
import type {
  ZLinkBackendRouterSocket,
  ZLinkBackendSpotRouteBridge,
  ZLinkBackendSubscriberSocket,
  ZLinkChannelBackendAdapter
} from '../backend/contracts';
import {
  closeMessages
} from './channel-envelope';
import { isChannelEnvelope } from './channel-envelope-inspection';
import type { ZLinkInboundDispatchBudget } from '../dispatch/inbound-dispatch-budget';

//  The loop awaits between reads and the signal can abort in that gap. Reading
//  through a function keeps the later check honest; an inline read stays
//  narrowed by the loop condition and the comparison is rejected as dead.
function signalAborted(signal?: AbortSignal): boolean {
  return signal?.aborted === true;
}


interface ZLinkMultipartOperation<TNext> {
  message(message: MessageLike): TNext;
}

interface ZLinkMultipartSubmitOperation extends ZLinkMultipartOperation<ZLinkMultipartSubmitOperation> {
  submit(): unknown;
}

type ZLinkMultipartReplyOperation = ZLinkMultipartSubmitOperation;

interface ZLinkChannelRequestDispatchLoop {
  dispatch(
    received: {
      readonly parts: readonly Message[];
      readonly routingId: unknown;
      readonly spotId?: unknown;
      readonly requestSeq: bigint | null;
      readonly send?: () => ZLinkMultipartOperation<ZLinkMultipartSubmitOperation>;
    },
    router: ZLinkBackendRouterSocket & {
      reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
    },
    signal?: AbortSignal
  ): Promise<boolean | void>;
}

interface ZLinkChannelPublishDispatchLoop {
  dispatch(
    topicMessage: { readonly topic: string; readonly parts: readonly Message[] },
    signal?: AbortSignal
  ): Promise<void>;
}

interface ZLinkRoutePacketDispatchLoop {
  dispatch(
    received: {
      readonly parts: readonly Message[];
      readonly routingId: unknown;
      readonly spotId?: unknown;
      readonly requestSeq: bigint | null;
    },
    router: {
      reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
    },
    signal?: AbortSignal
  ): Promise<boolean | void>;
}

interface ZLinkChannelInfrastructureHandler {
  (
    received: {
      readonly parts: readonly Message[];
      readonly routingId: unknown;
      readonly requestSeq: bigint | null;
    },
    router: ZLinkBackendRouterSocket
  ): boolean;
}

export class ZLinkChannelReceiveLoop {
  //  The loop awaits between the two reads and stop() can land in that gap.
  //  Reading through a method keeps the second check honest; a field read stays
  //  narrowed to false across the await and is treated as dead code.
  private stoppedFlag = false;

  private isStopped(): boolean {
    return this.stoppedFlag;
  }
  private running?: Promise<void>;
  private readonly inFlight = new Set<Promise<void>>();

  constructor(
    private readonly channelName: string,
    private readonly router: ZLinkBackendRouterSocket & {
      reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
    },
    private readonly dispatcher: ZLinkChannelRequestDispatchLoop,
    private readonly spotRouteBridge?: ZLinkBackendSpotRouteBridge,
    private readonly infrastructureHandler?: ZLinkChannelInfrastructureHandler,
    private readonly inboundDispatchBudget?: ZLinkInboundDispatchBudget
  ) {}

  async run(signal?: AbortSignal): Promise<void> {
    if (this.running !== undefined) {
      return this.running;
    }
    const running = this.runLoop(signal);
    this.running = running;
    try {
      await running;
    } finally {
      if (this.running === running) {
        this.running = undefined;
      }
    }
  }

  private async runLoop(signal?: AbortSignal): Promise<void> {
    while (!this.isStopped() && signal?.aborted !== true) {
      await this.inboundDispatchBudget?.waitUntilResumed(signal);
      if (this.isStopped() || signalAborted(signal)) break;
      const received = this.router.recv(1);
      if (received == null) {
        await waitReceiveLoopIdle();
        continue;
      }
      const task = this.dispatchAndClose(received, signal);
      this.inFlight.add(task);
      void task.finally(() => this.inFlight.delete(task));
    }
  }

  async stop(): Promise<void> {
    this.stoppedFlag = true;
    await this.running;
    await Promise.allSettled([...this.inFlight]);
  }

  private async dispatchAndClose(received: {
    parts: readonly Message[];
    routingId: unknown;
    spotId?: unknown;
    requestSeq: bigint | null;
    send?: () => ZLinkMultipartOperation<ZLinkMultipartSubmitOperation>;
    close(): void;
  }, signal?: AbortSignal): Promise<void> {
    let closeReceived = true;
    try {
      if (this.infrastructureHandler?.(received, this.router) === true) {
        return;
      }
      if (
        !isChannelEnvelope(received.parts) &&
        this.spotRouteBridge?.handleRouterReceived(
          this.channelName,
          received.routingId as RoutingId,
          received.requestSeq ?? 0n,
          received.parts
        ) === true
      ) {
        closeReceived = false;
        return;
      }
      const payloadBytes = channelPayloadBytes(received.parts);
      this.inboundDispatchBudget?.enqueue(payloadBytes);
      let started = false;
      let releaseCompletion: (() => void) | undefined;
      try {
        releaseCompletion = received.requestSeq !== null
          ? await this.inboundDispatchBudget?.acquireCompletionSend(signal)
          : undefined;
        this.inboundDispatchBudget?.start(payloadBytes);
        started = true;
        const consumed = await this.dispatcher.dispatch(received, this.router, signal);
        if (consumed === true) {
          closeReceived = false;
        }
      } finally {
        releaseCompletion?.();
        if (started) {
          this.inboundDispatchBudget?.complete(payloadBytes);
        } else {
          this.inboundDispatchBudget?.cancelQueued(payloadBytes);
        }
      }
    } finally {
      if (closeReceived) {
        received.close();
      }
    }
  }
}

export class ZLinkSubscriberReceiveLoop {
  //  The loop awaits between the two reads and stop() can land in that gap.
  //  Reading through a method keeps the second check honest; a field read stays
  //  narrowed to false across the await and is treated as dead code.
  private stoppedFlag = false;

  private isStopped(): boolean {
    return this.stoppedFlag;
  }
  private running?: Promise<void>;
  private readonly inFlight = new Set<Promise<void>>();

  constructor(
    private readonly adapter: ZLinkChannelBackendAdapter,
    private readonly subscriber: ZLinkBackendSubscriberSocket,
    private readonly dispatcher: ZLinkChannelPublishDispatchLoop,
    private readonly infrastructureHandler?: (
      topicMessage: ReturnType<ZLinkChannelBackendAdapter['createTopicMessage']>
    ) => boolean,
    private readonly inboundDispatchBudget?: ZLinkInboundDispatchBudget
  ) {
    this.poller = adapter.createReadablePoller(subscriber);
  }

  private readonly poller: ReturnType<ZLinkChannelBackendAdapter['createReadablePoller']>;

  async run(signal?: AbortSignal): Promise<void> {
    if (this.running !== undefined) {
      return this.running;
    }
    const running = this.runLoop(signal);
    this.running = running;
    try {
      await running;
    } finally {
      if (this.running === running) {
        this.running = undefined;
      }
    }
  }

  private async runLoop(signal?: AbortSignal): Promise<void> {
    while (!this.isStopped() && signal?.aborted !== true) {
      await this.inboundDispatchBudget?.waitUntilResumed(signal);
      if (this.isStopped() || signalAborted(signal)) break;
      if (!this.poller.wait(0)) {
        await waitReceiveLoopIdle();
        continue;
      }
      const topicMessage = this.adapter.createTopicMessage();
      this.subscriber.subscribe(topicMessage);
      const task = this.dispatchAndClose(topicMessage, signal);
      this.inFlight.add(task);
      void task.finally(() => this.inFlight.delete(task));
      await task;
    }
  }

  async stop(): Promise<void> {
    this.stoppedFlag = true;
    try {
      await this.running;
      await Promise.allSettled([...this.inFlight]);
    } finally {
      this.poller.dispose();
    }
  }

  private async dispatchAndClose(
    topicMessage: ReturnType<ZLinkChannelBackendAdapter['createTopicMessage']>,
    signal?: AbortSignal
  ): Promise<void> {
    try {
      if (this.infrastructureHandler?.(topicMessage) === true) {
        return;
      }
      const payloadBytes = channelPayloadBytes(topicMessage.parts);
      this.inboundDispatchBudget?.enqueue(payloadBytes);
      try {
        this.inboundDispatchBudget?.start(payloadBytes);
        await this.dispatcher.dispatch(topicMessage, signal);
      } finally {
        this.inboundDispatchBudget?.complete(payloadBytes);
      }
    } finally {
      closeMessages(topicMessage.parts as readonly Message[]);
    }
  }
}

export class ZLinkRouteReceiveLoop {
  //  The loop awaits between the two reads and stop() can land in that gap.
  //  Reading through a method keeps the second check honest; a field read stays
  //  narrowed to false across the await and is treated as dead code.
  private stoppedFlag = false;

  private isStopped(): boolean {
    return this.stoppedFlag;
  }
  private running?: Promise<void>;
  private readonly inFlight = new Set<Promise<void>>();

  constructor(
    private readonly router: ZLinkBackendRouterSocket & {
      reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
    },
    private readonly dispatcher: ZLinkRoutePacketDispatchLoop,
    private readonly inboundDispatchBudget?: ZLinkInboundDispatchBudget
  ) {}

  async run(signal?: AbortSignal): Promise<void> {
    if (this.running !== undefined) {
      return this.running;
    }
    const running = this.runLoop(signal);
    this.running = running;
    try {
      await running;
    } finally {
      if (this.running === running) {
        this.running = undefined;
      }
    }
  }

  private async runLoop(signal?: AbortSignal): Promise<void> {
    while (!this.isStopped() && signal?.aborted !== true) {
      await this.inboundDispatchBudget?.waitUntilResumed(signal);
      if (this.isStopped() || signalAborted(signal)) break;
      const received = this.router.recv(1);
      if (received == null) {
        await waitReceiveLoopIdle();
        continue;
      }
      const task = this.dispatchAndClose(received, signal);
      this.inFlight.add(task);
      void task.finally(() => this.inFlight.delete(task));
      await task;
    }
  }

  async stop(): Promise<void> {
    this.stoppedFlag = true;
    await this.running;
    await Promise.allSettled([...this.inFlight]);
  }

  private async dispatchAndClose(received: {
    parts: readonly Message[];
    routingId: unknown;
    spotId?: unknown;
    requestSeq: bigint | null;
    close(): void;
  }, signal?: AbortSignal): Promise<void> {
    let closeReceived = true;
    const payloadBytes = messageBytes(received.parts);
    this.inboundDispatchBudget?.enqueue(payloadBytes);
    let started = false;
    let releaseCompletion: (() => void) | undefined;
    try {
      releaseCompletion = received.requestSeq !== null
        ? await this.inboundDispatchBudget?.acquireCompletionSend(signal)
        : undefined;
      this.inboundDispatchBudget?.start(payloadBytes);
      started = true;
      const consumed = await this.dispatcher.dispatch(received, this.router, signal);
      if (consumed === true) {
        closeReceived = false;
      }
    } finally {
      releaseCompletion?.();
      if (started) {
        this.inboundDispatchBudget?.complete(payloadBytes);
      } else {
        this.inboundDispatchBudget?.cancelQueued(payloadBytes);
      }
      if (closeReceived) {
        received.close();
      }
    }
  }
}

function waitReceiveLoopIdle(): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, 5));
}

function messageBytes(parts: readonly Message[]): bigint {
  return parts.reduce((sum, part) => sum + messagePartBytes(part), 0n);
}

function channelPayloadBytes(parts: readonly Message[]): bigint {
  return messagePartBytes(parts[1]);
}

function messagePartBytes(part: Message | undefined): bigint {
  if (part === undefined) {
    return 0n;
  }
  const size = (part as Message & { size?: () => number }).size;
  if (typeof size === 'function') {
    return BigInt(size.call(part));
  }
  return BigInt(part.data().byteLength);
}
