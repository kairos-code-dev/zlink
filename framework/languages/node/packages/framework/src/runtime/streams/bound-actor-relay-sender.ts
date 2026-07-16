import type {
  ZLinkMessage,
  ZLinkMessageSerializer,
  ZLinkSessionActor
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { encodeFrameworkPayloadMessage } from '../messaging/payload-codec';
import {
  encodeStreamHeader,
  messageToBytes,
  type ZLinkStreamFrameHeader
} from './protocol';
import {
  ZLinkActorSessionBindingRegistry
} from './actor-session-binding-registry';
import { ZLinkActorSessionLifecycleCoordinator } from './actor-session-lifecycle-coordinator';
import { ZLinkManagedStream } from './managed-stream';
import {
  DefaultZLinkSessionActor,
  DefaultZLinkSessionContext
} from './session-context';
import {
  ZLinkStreamFrameMessageFactory
} from './stream-frame-factory';

export interface ZLinkBoundActorRelaySenderOptions {
  readonly actorBindTimeoutMs?: number;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly relay?: (actor: ZLinkSessionActor, header: ZLinkStreamFrameHeader, payload: Message, signal?: AbortSignal) => Promise<boolean>;
  readonly notifyDisconnected?: (actor: ZLinkSessionActor, signal?: AbortSignal) => Promise<void>;
}

export class ZLinkBoundActorRelaySender {
  constructor(
    private readonly routes: ZLinkActorSessionBindingRegistry<DefaultZLinkSessionContext, DefaultZLinkSessionActor>,
    private readonly frameMessages: ZLinkStreamFrameMessageFactory,
    private readonly options: ZLinkBoundActorRelaySenderOptions = {},
    private readonly lifecycle = new ZLinkActorSessionLifecycleCoordinator()
  ) {}

  async relay(
    actor: DefaultZLinkSessionActor,
    payload: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<void> {
    await this.lifecycle.run(actor.actorId, () => this.relayInsideLifecycle(actor, payload, signal));
  }

  private async relayInsideLifecycle(
    actor: DefaultZLinkSessionActor,
    payload: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<void> {
    this.routes.requireCurrentToken(actor.actorId, actor.bindingToken);
    const currentHeader = this.routes.requireRoute(actor.actorId).context.dispatchHeader;
    if (currentHeader === undefined) {
      throw new Error('Session actor relay requires an active stream dispatch.');
    }
    const payloadMessage = encodeFrameworkPayloadMessage(payload, this.options.messageSerializers);
    try {
      if (this.options.relay !== undefined) {
        const handled = await this.options.relay(actor, currentHeader, payloadMessage, signal);
        if (handled) {
          return;
        }
      }
      const route = this.routes.requireRoute(actor.actorId);
      if (!(route.context.stream instanceof ZLinkManagedStream)) {
        return;
      }
      const headerMessage = this.frameMessages.createBinaryMessage(encodeStreamHeader(currentHeader));
      const framePayloadMessage = this.frameMessages.createBinaryMessage(messageToBytes(payloadMessage));
      try {
        if (!route.context.stream.sendBoundActor(actor.actorId, [headerMessage, framePayloadMessage], 0)) {
          throw new Error('Actor session relay failed because the session relay route was not ready before timeout.');
        }
      } finally {
        headerMessage.close();
        framePayloadMessage.close();
      }
    } finally {
      payloadMessage.close();
    }
  }

  async notifyDisconnected(actor: DefaultZLinkSessionActor, signal?: AbortSignal): Promise<void> {
    await this.lifecycle.run(actor.actorId, async () => {
      this.routes.requireCurrentToken(actor.actorId, actor.bindingToken);
      const route = this.routes.requireRoute(actor.actorId);
      try {
        await this.options.notifyDisconnected?.(actor, signal);
        if (
          route.bindingToken === actor.bindingToken
          && route.context.stream instanceof ZLinkManagedStream
        ) {
          await route.context.stream.unbindActor(
            actor.actorId,
            this.options.actorBindTimeoutMs ?? 2000,
            signal
          );
        }
      } finally {
        this.routes.unbind(actor.actorId, route.context, actor.bindingToken);
      }
    });
  }
}
