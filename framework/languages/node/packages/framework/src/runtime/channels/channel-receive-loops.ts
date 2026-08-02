import type { RoutingId } from '../../contracts';
import type {
  Message,
  MessageLike
} from '@zlink-systems/zlink';
import type {
  ZLinkBackendRouterSocket,
  ZLinkBackendSpotRouteBridge,
  ZLinkBackendSubscriberSocket,
  ZLinkBackendReadablePoller,
  ZLinkChannelBackendAdapter
} from '../backend/contracts';
import {
  closeMessages
} from './channel-envelope';
import { tryDecodeChannelHeader } from './channel-envelope-inspection';
import type { ZLinkChannelEnvelopeHeader } from './channel-envelope';
import type { ZLinkInboundDispatchBudget } from '../dispatch/inbound-dispatch-budget';
import {
  ZLINK_MAX_CONCURRENT_CHANNEL_DISPATCHES,
  ZLinkReceiveTaskTracker,
  type ZLinkReceiveTaskErrorReporter
} from './channel-receive-task-tracker';

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
    signal?: AbortSignal,
    decodedHeader?: ZLinkChannelEnvelopeHeader
  ): Promise<boolean | void>;
}

interface ZLinkChannelPublishDispatchLoop {
  dispatch(
    topicMessage: { readonly topic: string; readonly parts: readonly Message[] },
    signal?: AbortSignal,
    decodedHeader?: ZLinkChannelEnvelopeHeader
  ): Promise<void>;
}

interface ZLinkSubscriberInfrastructureResult {
  readonly consumed: boolean;
  readonly decodedHeader?: ZLinkChannelEnvelopeHeader;
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
    signal?: AbortSignal,
    decodedHeader?: ZLinkChannelEnvelopeHeader
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
  private readonly inFlight: ZLinkReceiveTaskTracker;

  constructor(
    private readonly channelName: string,
    private readonly router: ZLinkBackendRouterSocket & {
      reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
    },
    private readonly dispatcher: ZLinkChannelRequestDispatchLoop,
    private readonly spotRouteBridge: ZLinkBackendSpotRouteBridge | undefined,
    private readonly infrastructureHandler: ZLinkChannelInfrastructureHandler | undefined,
    private readonly inboundDispatchBudget: ZLinkInboundDispatchBudget | undefined,
    private readonly poller: ZLinkBackendReadablePoller,
    reportError?: ZLinkReceiveTaskErrorReporter
  ) {
    this.inFlight = new ZLinkReceiveTaskTracker(
      ZLINK_MAX_CONCURRENT_CHANNEL_DISPATCHES,
      reportError
    );
  }

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
      if (this.inboundDispatchBudget?.receivePaused === true) {
        await this.inboundDispatchBudget.waitUntilResumed(signal);
      }
      if (this.isStopped() || signalAborted(signal)) break;
      const capacityWait = this.inFlight.waitForCapacity(signal, () => this.isStopped());
      if (capacityWait !== undefined) {
        await capacityWait;
      }
      if (this.isStopped() || signalAborted(signal)) break;
      if (!this.poller.wait(0)) {
        await waitReceiveLoopIdle();
        continue;
      }
      const received = this.router.recv(1);
      if (received == null) {
        await waitReceiveLoopIdle();
        continue;
      }
      const task = this.dispatchAndClose(received, signal);
      this.inFlight.track(task);
    }
  }

  async stop(): Promise<void> {
    this.stoppedFlag = true;
    this.inFlight.wakeCapacityWaiter();
    try {
      await this.running;
      await this.inFlight.waitForAll();
    } finally {
      this.poller.dispose();
    }
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
      const decodedHeader = tryDecodeChannelHeader(received.parts);
      if (
        decodedHeader === undefined &&
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
        const consumed = await this.dispatcher.dispatch(
          received,
          this.router,
          signal,
          decodedHeader
        );
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
  private readonly inFlight = new ZLinkReceiveTaskTracker();

  constructor(
    private readonly adapter: ZLinkChannelBackendAdapter,
    private readonly subscriber: ZLinkBackendSubscriberSocket,
    private readonly dispatcher: ZLinkChannelPublishDispatchLoop,
    private readonly infrastructureHandler?: (
      topicMessage: ReturnType<ZLinkChannelBackendAdapter['createTopicMessage']>
    ) => boolean | ZLinkSubscriberInfrastructureResult,
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
      if (this.inboundDispatchBudget?.receivePaused === true) {
        await this.inboundDispatchBudget.waitUntilResumed(signal);
      }
      if (this.isStopped() || signalAborted(signal)) break;
      if (!this.poller.wait(0)) {
        await waitReceiveLoopIdle();
        continue;
      }
      const topicMessage = this.adapter.createTopicMessage();
      if (!this.subscriber.subscribe(topicMessage)) {
        topicMessage.close();
        await waitReceiveLoopIdle();
        continue;
      }
      const task = this.dispatchAndClose(topicMessage, signal);
      this.inFlight.track(task, false);
      try {
        await task;
      } finally {
        this.inFlight.delete(task);
      }
    }
  }

  async stop(): Promise<void> {
    this.stoppedFlag = true;
    this.inFlight.wakeCapacityWaiter();
    try {
      await this.running;
      await this.inFlight.waitForAll();
    } finally {
      this.poller.dispose();
    }
  }

  private async dispatchAndClose(
    topicMessage: ReturnType<ZLinkChannelBackendAdapter['createTopicMessage']>,
    signal?: AbortSignal
  ): Promise<void> {
    try {
      const infrastructureResult = this.infrastructureHandler?.(topicMessage);
      const decodedHeader = typeof infrastructureResult === 'object'
        ? infrastructureResult.decodedHeader
        : undefined;
      if (
        infrastructureResult === true
        || (typeof infrastructureResult === 'object' && infrastructureResult.consumed)
      ) {
        return;
      }
      const payloadBytes = channelPayloadBytes(topicMessage.parts);
      this.inboundDispatchBudget?.enqueue(payloadBytes);
      let started = false;
      try {
        this.inboundDispatchBudget?.start(payloadBytes);
        started = true;
        await this.dispatcher.dispatch(topicMessage, signal, decodedHeader);
      } finally {
        if (started) {
          this.inboundDispatchBudget?.complete(payloadBytes);
        } else {
          this.inboundDispatchBudget?.cancelQueued(payloadBytes);
        }
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
  private readonly inFlight = new ZLinkReceiveTaskTracker();

  constructor(
    private readonly router: ZLinkBackendRouterSocket & {
      reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
    },
    private readonly dispatcher: ZLinkRoutePacketDispatchLoop,
    private readonly inboundDispatchBudget: ZLinkInboundDispatchBudget | undefined,
    private readonly poller: ZLinkBackendReadablePoller
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
      if (this.inboundDispatchBudget?.receivePaused === true) {
        await this.inboundDispatchBudget.waitUntilResumed(signal);
      }
      if (this.isStopped() || signalAborted(signal)) break;
      if (!this.poller.wait(0)) {
        await waitReceiveLoopIdle();
        continue;
      }
      const received = this.router.recv(1);
      if (received == null) {
        await waitReceiveLoopIdle();
        continue;
      }
      const task = this.dispatchAndClose(received, signal);
      this.inFlight.track(task, false);
      try {
        await task;
      } finally {
        this.inFlight.delete(task);
      }
    }
  }

  async stop(): Promise<void> {
    this.stoppedFlag = true;
    this.inFlight.wakeCapacityWaiter();
    try {
      await this.running;
      await this.inFlight.waitForAll();
    } finally {
      this.poller.dispose();
    }
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
      const consumed = await this.dispatcher.dispatch(
        received,
        this.router,
        signal,
        tryDecodeChannelHeader(received.parts)
      );
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

class ZLinkSharedIdleWaiter {
  private pending?: Promise<void>;
  private resolvePending?: () => void;
  private readonly wake = (): void => {
    const resolve = this.resolvePending;
    this.pending = undefined;
    this.resolvePending = undefined;
    resolve?.();
  };

  constructor(private readonly delayMs: number) {}

  wait(): Promise<void> {
    if (this.pending !== undefined) return this.pending;
    this.pending = new Promise<void>((resolve) => {
      this.resolvePending = resolve;
    });
    setTimeout(this.wake, this.delayMs);
    return this.pending;
  }
}

const receiveLoopIdleWaiter = new ZLinkSharedIdleWaiter(5);

function waitReceiveLoopIdle(): Promise<void> {
  return receiveLoopIdleWaiter.wait();
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
