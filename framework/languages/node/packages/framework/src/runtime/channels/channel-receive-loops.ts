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
      readonly spotRid?: unknown;
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
  dispatch(topicMessage: { readonly topic: string; readonly parts: readonly Message[] }): Promise<void>;
}

interface ZLinkRoutePacketDispatchLoop {
  dispatch(
    received: {
      readonly parts: readonly Message[];
      readonly routingId: unknown;
      readonly spotRid?: unknown;
      readonly requestSeq: bigint | null;
    },
    router: {
      reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
    }
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
  private stopped = false;
  private running?: Promise<void>;
  private readonly inFlight = new Set<Promise<void>>();

  constructor(
    private readonly channelName: string,
    private readonly router: ZLinkBackendRouterSocket & {
      reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
    },
    private readonly dispatcher: ZLinkChannelRequestDispatchLoop,
    private readonly spotRouteBridge?: ZLinkBackendSpotRouteBridge,
    private readonly infrastructureHandler?: ZLinkChannelInfrastructureHandler
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
    while (!this.stopped && signal?.aborted !== true) {
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
    this.stopped = true;
    await this.running;
    await Promise.allSettled([...this.inFlight]);
  }

  private async dispatchAndClose(received: {
    parts: readonly Message[];
    routingId: unknown;
    spotRid?: unknown;
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
      const consumed = await this.dispatcher.dispatch(received, this.router, signal);
      if (consumed === true) {
        closeReceived = false;
      }
    } finally {
      if (closeReceived) {
        received.close();
      }
    }
  }
}

export class ZLinkSubscriberReceiveLoop {
  private stopped = false;
  private running?: Promise<void>;
  private readonly inFlight = new Set<Promise<void>>();

  constructor(
    private readonly adapter: ZLinkChannelBackendAdapter,
    private readonly subscriber: ZLinkBackendSubscriberSocket,
    private readonly dispatcher: ZLinkChannelPublishDispatchLoop
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
    while (!this.stopped && signal?.aborted !== true) {
      if (!this.poller.wait(0)) {
        await waitReceiveLoopIdle();
        continue;
      }
      const topicMessage = this.adapter.createTopicMessage();
      this.subscriber.subscribe(topicMessage);
      const task = this.dispatchAndClose(topicMessage);
      this.inFlight.add(task);
      void task.finally(() => this.inFlight.delete(task));
      await task;
    }
  }

  async stop(): Promise<void> {
    this.stopped = true;
    try {
      await this.running;
      await Promise.allSettled([...this.inFlight]);
    } finally {
      this.poller.dispose();
    }
  }

  private async dispatchAndClose(topicMessage: ReturnType<ZLinkChannelBackendAdapter['createTopicMessage']>): Promise<void> {
    try {
      await this.dispatcher.dispatch(topicMessage);
    } finally {
      closeMessages(topicMessage.parts as readonly Message[]);
    }
  }
}

export class ZLinkRouteReceiveLoop {
  private stopped = false;
  private running?: Promise<void>;
  private readonly inFlight = new Set<Promise<void>>();

  constructor(
    private readonly router: ZLinkBackendRouterSocket & {
      reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
    },
    private readonly dispatcher: ZLinkRoutePacketDispatchLoop
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
    while (!this.stopped && signal?.aborted !== true) {
      const received = this.router.recv(1);
      if (received == null) {
        await waitReceiveLoopIdle();
        continue;
      }
      const task = this.dispatchAndClose(received);
      this.inFlight.add(task);
      void task.finally(() => this.inFlight.delete(task));
      await task;
    }
  }

  async stop(): Promise<void> {
    this.stopped = true;
    await this.running;
    await Promise.allSettled([...this.inFlight]);
  }

  private async dispatchAndClose(received: {
    parts: readonly Message[];
    routingId: unknown;
    spotRid?: unknown;
    requestSeq: bigint | null;
    close(): void;
  }): Promise<void> {
    let closeReceived = true;
    try {
      const consumed = await this.dispatcher.dispatch(received, this.router);
      if (consumed === true) {
        closeReceived = false;
      }
    } finally {
      if (closeReceived) {
        received.close();
      }
    }
  }
}

function waitReceiveLoopIdle(): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, 5));
}
