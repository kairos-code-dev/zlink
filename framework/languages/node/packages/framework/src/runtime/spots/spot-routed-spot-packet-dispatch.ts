import type {
  RoutingId,
  Type,
  ZLinkProviderResolver,
  ZLinkSpot,
  ZLinkSpotPacketHandler,
  ZLinkSpotRequestHandler
} from '../../contracts';
import { zlinkMessageMetadata } from '../../contracts';
import {
  ZLinkRuntimeDispatchErrorAction as ZLinkDispatchErrorAction,
  ZLinkRuntimeDispatchErrorReason as ZLinkDispatchErrorReason,
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind
} from '../../contracts/Dispatch/ZLinkDispatchOptions';
import type { ZLinkDispatchErrorReporter } from '../channels';
import { ZLinkConfigurationException } from '../configuration';
import { createProviderInstance } from './spot-provider';
import type { ZLinkSpotHandlerRegistration } from './spot-handler-registry';
import type { ZLinkSpotSerialExecutor } from './spot-serial-executor';

interface ZLinkRoutedSpotPacketActivation {
  readonly spotRid: RoutingId;
  readonly spot: ZLinkSpot;
  readonly serial: ZLinkSpotSerialExecutor;
  readonly handlers: {
    snapshot(): readonly ZLinkSpotHandlerRegistration[];
  };
}

interface ZLinkRoutedSpotPacketDispatchOptions {
  readonly resolveActivation: (spotRid: RoutingId) => ZLinkRoutedSpotPacketActivation | undefined;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
}

export class ZLinkRoutedSpotPacketDispatch {
  constructor(private readonly options: ZLinkRoutedSpotPacketDispatchOptions) {}

  async send(
    spotRid: RoutingId,
    packetName: string | undefined,
    message: unknown,
    context: { readonly channelName: string; readonly contentType?: string }
  ): Promise<void> {
    await this.dispatch(spotRid, packetName, message, context, false);
  }

  async request<TReply>(
    spotRid: RoutingId,
    packetName: string | undefined,
    request: unknown,
    context: { readonly channelName: string; readonly contentType?: string }
  ): Promise<TReply> {
    return await this.dispatch(spotRid, packetName, request, context, true) as TReply;
  }

  private async dispatch(
    spotRid: RoutingId,
    packetName: string | undefined,
    payload: unknown,
    context: { readonly channelName: string; readonly contentType?: string },
    returnResponse: boolean
  ): Promise<unknown> {
    const activation = this.options.resolveActivation(spotRid);
    if (activation === undefined) {
      this.reportMissing(spotRid, packetName, context, returnResponse);
      if (!returnResponse) {
        return undefined;
      }
      throw new ZLinkConfigurationException(`Spot '${spotRid}' is not active.`);
    }

    const registrations = activation.handlers.snapshot().filter((registration) =>
      registration.kind === 'packet' &&
      (registration.packetName ?? registration.handlerType.name) === (packetName ?? '')
    );
    if (registrations.length === 0) {
      this.reportMissing(spotRid, packetName, context, returnResponse);
      if (!returnResponse) {
        return undefined;
      }
      throw new ZLinkConfigurationException(`SPOT route handler not found: ${packetName}`);
    }

    let response: unknown;
    try {
      await activation.serial.execute(async () => {
        for (const registration of registrations) {
          const handler = await createProviderInstance(
            registration.handlerType as Type<
              ZLinkSpotPacketHandler<ZLinkSpot, unknown> |
              ZLinkSpotRequestHandler<ZLinkSpot, unknown, unknown>
            >,
            this.options.providerResolver
          );
          response = await handler.handle(activation.spot, payload, {
            channelName: context.channelName,
            contentType: context.contentType,
            packetName,
            metadata: zlinkMessageMetadata({})
          });
        }
      });
    } catch (error) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotRoute,
        messageKind: returnResponse ? ZLinkDispatchMessageKind.Request : ZLinkDispatchMessageKind.Send,
        reason: ZLinkDispatchErrorReason.HandlerException,
        action: returnResponse ? ZLinkDispatchErrorAction.FailCaller : ZLinkDispatchErrorAction.Drop,
        packetName,
        channelName: context.channelName,
        spotRid: String(spotRid),
        error
      });
      throw error;
    }
    return returnResponse ? response : undefined;
  }

  private reportMissing(
    spotRid: RoutingId,
    packetName: string | undefined,
    context: { readonly channelName: string },
    returnResponse: boolean
  ): void {
    this.options.dispatchErrors?.report({
      surface: ZLinkDispatchErrorSurface.SpotRoute,
      messageKind: returnResponse ? ZLinkDispatchMessageKind.Request : ZLinkDispatchMessageKind.Send,
      reason: ZLinkDispatchErrorReason.HandlerMissing,
      action: returnResponse ? ZLinkDispatchErrorAction.FailCaller : ZLinkDispatchErrorAction.Drop,
      packetName,
      channelName: context.channelName,
      spotRid: String(spotRid)
    });
  }
}
