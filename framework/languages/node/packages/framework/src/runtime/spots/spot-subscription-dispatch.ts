import type {
  Type,
  ZLinkMessageSerializer,
  ZLinkProviderResolver,
  ZLinkSpot,
  ZLinkSpotSubscriptionHandler
} from '../../contracts';
import {
  ZLinkDispatchErrorAction,
  ZLinkDispatchErrorReason,
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind,
  ZLinkMessageFlowOutcome
} from '../../contracts';
import { TopicMessage as BindingTopicMessage } from '@zlink-systems/zlink';
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
      if (registration.kind !== 'subscribe' || registration.topic === undefined) {
        continue;
      }
      const existing = this.handlers.get(registration.topic) ?? [];
      existing.push(registration);
      this.handlers.set(registration.topic, existing);
      this.options.nativeSpot.setSubscription(registration.topic);
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

  private async dispatch(message: BindingTopicMessage): Promise<void> {
    const registrations = this.handlers.get(message.topic);
    if (registrations === undefined || registrations.length === 0 || message.parts.length === 0) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotSubscription,
        messageKind: ZLinkDispatchMessageKind.Publish,
        reason: message.parts.length === 0
          ? ZLinkDispatchErrorReason.InvalidFrame
          : ZLinkDispatchErrorReason.HandlerMissing,
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
    const inboundFlow = createInboundFlow(envelope.header.flowId, envelope.header.flowOrigin);
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
            source: subSource
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
