import type {
  RoutingId,
  Type,
  ZLinkMessageSerializer,
  ZLinkProviderResolver,
  ZLinkSpot,
  ZLinkSpotSubscriptionHandler
} from '../../contracts';
import { zlinkMessageMetadata } from '../../contracts';
import {
  ZLinkRuntimeMessageFlowOutcome as ZLinkMessageFlowOutcome,
  ZLinkRuntimeDispatchErrorAction as ZLinkDispatchErrorAction,
  ZLinkRuntimeDispatchErrorReason as ZLinkDispatchErrorReason,
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind
} from '../../contracts/Dispatch/ZLinkDispatchOptions';
import {
  TopicMessage as BindingTopicMessage,
  type Message,
  type RoutingId as BindingRoutingId
} from '@zlink-systems/zlink';
import type { ZLinkBackendSpot } from '../backend/contracts';
import type { ZLinkDispatchErrorReporter } from '../channels';
import {
  decodeChannelEnvelope,
  decodeChannelPayload,
  ZLinkChannelMessageKind,
  type ZLinkChannelEnvelopeCodecRegistry
} from '../channels/channel-envelope';
import { flowIfEnabled } from '../diagnostics';
import { createInboundFlow, runWithFlow } from '../diagnostics/flow-context';
import { createProviderInstance } from './spot-provider';
import type { ZLinkSpotHandlerRegistration } from './spot-handler-registry';
import { ZLINK_RECV_DONT_WAIT } from './spot-native-flags';
import type { ZLinkSpotSerialExecutor } from './spot-serial-executor';

interface ZLinkSpotSubscriptionDispatchOptions {
  readonly nativeSpot: ZLinkBackendSpot;
  readonly serial: ZLinkSpotSerialExecutor;
  readonly getTarget: () => ZLinkSpot;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
  readonly waitIdle: () => Promise<void>;
}

export class ZLinkSpotSubscriptionDispatch {
  private draining = false;
  private redrainRequested = false;
  private readonly handlers = new Map<string, ZLinkSpotHandlerRegistration[]>();

  constructor(private readonly options: ZLinkSpotSubscriptionDispatchOptions) {}

  private channelCodecs(): ZLinkChannelEnvelopeCodecRegistry | undefined {
    return this.options.messageSerializers === undefined
      ? undefined
      : { serializers: this.options.messageSerializers };
  }

  configure(registrations: readonly ZLinkSpotHandlerRegistration[]): void {
    for (const registration of registrations) {
      if (registration.kind !== 'subscribe'
        || registration.channelName === undefined
        || registration.topic === undefined) {
        continue;
      }
      const key = subscriptionKey(registration.channelName, registration.topic);
      const existing = this.handlers.get(key) ?? [];
      existing.push(registration);
      this.handlers.set(key, existing);
      this.options.nativeSpot.setSubscription(registration.channelName, registration.topic);
    }
  }

  async drain(): Promise<void> {
    if (this.draining) {
      this.redrainRequested = true;
      return;
    }
    this.draining = true;
    try {
      do {
        this.redrainRequested = false;
        await this.drainAvailable();
        // eslint-disable-next-line @typescript-eslint/no-unnecessary-condition
      } while (this.redrainRequested);
    } finally {
      this.draining = false;
    }
  }

  async dispatchRecord(
    topic: string,
    parts: readonly Message[],
    sourceRid: RoutingId | BindingRoutingId | null
  ): Promise<void> {
    await this.dispatch({
      topic,
      parts,
      routingId: sourceRid
    });
  }

  private async drainAvailable(): Promise<void> {
    let message = new BindingTopicMessage();
    try {
      for (;;) {
        if (!this.options.nativeSpot.subscribe(message, ZLINK_RECV_DONT_WAIT)) {
          message.close();
          await this.options.waitIdle();
          return;
        }
        try {
          await this.dispatch(message);
        } finally {
          message.close();
        }
        message = new BindingTopicMessage();
      }
    } finally {
      message.close();
    }
  }

  private async dispatch(message: {
    readonly topic: string;
    readonly parts: readonly Message[];
    readonly routingId: RoutingId | BindingRoutingId | null;
  }): Promise<void> {
    if (message.parts.length === 0) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotSubscription,
        messageKind: ZLinkDispatchMessageKind.Publish,
        reason: ZLinkDispatchErrorReason.InvalidFrame,
        action: ZLinkDispatchErrorAction.Drop,
        topic: message.topic,
        sourceRid: message.routingId === null ? undefined : String(message.routingId)
      });
      return;
    }
    const envelope = decodeChannelEnvelope(message.parts);
    if (envelope.header.kind !== ZLinkChannelMessageKind.Publish) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotSubscription,
        messageKind: ZLinkDispatchMessageKind.Publish,
        reason: ZLinkDispatchErrorReason.InvalidFrame,
        action: ZLinkDispatchErrorAction.Drop,
        packetName: envelope.packetName,
        channelName: envelope.header.channelName,
        topic: message.topic,
        sourceRid: message.routingId === null ? undefined : String(message.routingId),
        correlationId: envelope.header.correlationId ?? undefined
      });
      return;
    }
    const registrations = this.handlers.get(subscriptionKey(envelope.header.channelName, message.topic));
    if (registrations === undefined || registrations.length === 0) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotSubscription,
        messageKind: ZLinkDispatchMessageKind.Publish,
        reason: ZLinkDispatchErrorReason.HandlerMissing,
        action: ZLinkDispatchErrorAction.Drop,
        packetName: envelope.packetName,
        channelName: envelope.header.channelName,
        topic: message.topic,
        sourceRid: message.routingId === null ? undefined : String(message.routingId),
        correlationId: envelope.header.correlationId ?? undefined
      });
      return;
    }
    const event = decodeChannelPayload(envelope, this.channelCodecs());
    const spot = this.options.getTarget();
    const subSource = message.routingId === null ? undefined : String(message.routingId);
    const subCorr = envelope.header.correlationId ?? undefined;
    flowIfEnabled(this.options.dispatchErrors?.flow, ZLinkMessageFlowOutcome.Received)?.trace({
      outcome: ZLinkMessageFlowOutcome.Received,
      surface: ZLinkDispatchErrorSurface.SpotSubscription,
      messageKind: ZLinkDispatchMessageKind.Publish,
      packetName: envelope.packetName,
      channelName: envelope.header.channelName,
      topic: message.topic,
      sourceRid: subSource,
      correlationId: subCorr,
      flowId: envelope.header.flowId,
      flowOrigin: envelope.header.flowOrigin
    });
    const inboundFlow = createInboundFlow(
      envelope.header.flowId,
      envelope.header.flowOrigin,
      this.options.dispatchErrors?.flow.flowCreationEnabled() ?? true
    );
    await this.options.serial.execute(() => runWithFlow(inboundFlow, async () => {
      for (const registration of registrations) {
        const handler = await createProviderInstance(
          registration.handlerType as Type<ZLinkSpotSubscriptionHandler<ZLinkSpot, unknown>>,
          this.options.providerResolver
        );
        try {
          await handler.handle(spot, event, {
            channelName: envelope.header.channelName,
            contentType: envelope.header.contentType,
            packetName: envelope.packetName,
            topic: message.topic,
            source: subSource,
            metadata: zlinkMessageMetadata(envelope.header.metadata)
          });
          flowIfEnabled(this.options.dispatchErrors?.flow, ZLinkMessageFlowOutcome.Dispatched)?.trace({
            outcome: ZLinkMessageFlowOutcome.Dispatched,
            surface: ZLinkDispatchErrorSurface.SpotSubscription,
            messageKind: ZLinkDispatchMessageKind.Publish,
            packetName: envelope.packetName,
            channelName: envelope.header.channelName,
            topic: message.topic,
            sourceRid: subSource,
            correlationId: subCorr,
            flowId: envelope.header.flowId,
            flowOrigin: envelope.header.flowOrigin
          });
        } catch (error) {
          this.options.dispatchErrors?.report({
            surface: ZLinkDispatchErrorSurface.SpotSubscription,
            messageKind: ZLinkDispatchMessageKind.Publish,
            reason: ZLinkDispatchErrorReason.HandlerException,
            action: ZLinkDispatchErrorAction.Drop,
            packetName: envelope.packetName,
            channelName: envelope.header.channelName,
            topic: message.topic,
            sourceRid: message.routingId === null ? undefined : String(message.routingId),
            correlationId: envelope.header.correlationId ?? undefined,
            error
          });
          throw error;
        }
      }
    }));
  }
}

function subscriptionKey(channelName: string, topic: string): string {
  return `${channelName}\u0000${topic}`;
}
