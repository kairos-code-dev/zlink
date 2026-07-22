import type {
  RoutingId,
  ZLinkHandlerContext,
  ZLinkHandlerFilter,
  ZLinkPublishContext,
  ZLinkRouteRequestContext,
  ZLinkRouteSendContext,
  ZLinkSendContext,
  ZLinkUnhandledDispatchOptions
} from '../../contracts';
import { zlinkMessageMetadata } from '../../contracts';
import {
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind
} from '../../contracts/Dispatch/ZLinkDispatchOptions';
import {
  SubmitResult,
  type Message
} from '@zlink-systems/zlink';
import type { ReceiveRecord } from '../foundation/service-runtime-contracts';
import {
  ZLinkConfigurationException,
  type ZLinkRouteChannelOptions
} from '../configuration';
import type { ZLinkBackendSpotRouteBridge } from '../backend/contracts';
import type { ZLinkAsyncSubmitter } from '../messaging';
import type { ZLinkRuntimeMetrics } from '../diagnostics';
import {
  decodeChannelEnvelope,
  encodeChannelErrorReplyParts,
  encodeChannelReplyParts,
  JSON_CONTENT_TYPE,
  type ZLinkChannelEnvelopeCodecRegistry,
  ZLinkChannelMessageKind
} from './channel-envelope';
import { ZLinkDispatchErrorReporter } from './dispatch-error-reporter';
import { isChannelEnvelope } from './channel-envelope-inspection';
import {
  appendParts,
  type ZLinkMultipartOperation,
  type ZLinkMultipartReplyOperation,
  type ZLinkMultipartSubmitOperation
} from './channel-multipart';
import { codecsForFrameworkPacket } from './channel-framework-packets';
import {
  ZLinkChannelDispatchPipeline,
  type ZLinkChannelDispatchFields
} from './channel-dispatch-pipeline';

export interface ZLinkChannelRequestDispatcherOptions {
  readonly channelName: string;
  readonly codecs?: ZLinkChannelEnvelopeCodecRegistry;
  readonly dispatchErrors: ZLinkDispatchErrorReporter;
  readonly handlers: ReadonlyMap<string, ZLinkChannelRequestHandler>;
  readonly sendHandlers?: ReadonlyMap<string, ZLinkChannelSendHandler>;
  readonly filters?: readonly ZLinkHandlerFilter[];
  readonly unhandled?: ZLinkUnhandledDispatchOptions;
  readonly replySubmitter?: ZLinkAsyncSubmitter;
}

export interface ZLinkChannelRequestHandler {
  handle(payload: unknown, context: ZLinkHandlerContext): Promise<unknown> | unknown;
}

export interface ZLinkChannelSendHandler {
  handle(payload: unknown, context: ZLinkSendContext): Promise<void> | void;
}

export interface ZLinkRouteHandlerRegistration {
  readonly kind: 'send' | 'request';
  readonly packetName: string;
  readonly handler: ZLinkRouteRuntimeSendHandler | ZLinkRouteRuntimeRequestHandler;
}

export interface ZLinkRouteRuntimeSendHandler {
  handle(payload: unknown, context: ZLinkRouteSendContext): Promise<void> | void;
}

export interface ZLinkRouteRuntimeRequestHandler {
  handle(payload: unknown, context: ZLinkRouteRequestContext): Promise<unknown> | unknown;
}

export class ZLinkChannelRequestDispatcher {
  private readonly pipeline: ZLinkChannelDispatchPipeline;

  constructor(private readonly options: ZLinkChannelRequestDispatcherOptions) {
    this.pipeline = new ZLinkChannelDispatchPipeline({
      dispatchErrors: options.dispatchErrors,
      surface: ZLinkDispatchErrorSurface.Channel,
      channelName: options.channelName,
      filters: options.filters,
      unhandled: options.unhandled
    });
  }

  async dispatch(received: {
    parts: readonly Message[];
    routingId: unknown;
    spotRid?: unknown;
    requestSeq: bigint | null;
    send?: () => ZLinkMultipartOperation<ZLinkMultipartSubmitOperation>;
  }, router: {
    reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
  }, signal?: AbortSignal): Promise<boolean | void> {
    if (received.spotRid !== null && received.spotRid !== undefined) {
      if (received.send === undefined) {
        throw new ZLinkConfigurationException('Routed SPOT packet is missing a local SPOT delivery context.');
      }
      appendParts(received.send(), received.parts).submit();
      return true;
    }
    if (received.parts.length === 0 || received.parts[0].data().length === 0) {
      return;
    }
    const envelope = decodeChannelEnvelope(received.parts);
    const packetName = envelope.packetName;
    if (packetName === undefined) {
      throw new ZLinkConfigurationException('Channel packet is missing packetName.');
    }
    const correlationId = envelope.header.correlationId ?? undefined;
    if (envelope.header.kind === ZLinkChannelMessageKind.Command) {
      const fields: ZLinkChannelDispatchFields = {
        messageKind: ZLinkDispatchMessageKind.Send,
        packetName,
        correlationId,
        flowId: envelope.header.flowId,
        flowOrigin: envelope.header.flowOrigin
      };
      const handler = this.options.sendHandlers?.get(packetName);
      const context: ZLinkSendContext = {
        channelName: this.options.channelName,
        contentType: envelope.header.contentType,
        packetName,
        metadata: zlinkMessageMetadata(envelope.header.metadata)
      };
      await this.pipeline.dispatchOneWay({
        fields,
        envelope,
        codecs: this.options.codecs,
        handler,
        context
      });
      return;
    }
    if (envelope.header.kind !== ZLinkChannelMessageKind.Request) {
      return;
    }
    const requestFields: ZLinkChannelDispatchFields = {
      messageKind: ZLinkDispatchMessageKind.Request,
      packetName,
      correlationId,
      flowId: envelope.header.flowId,
      flowOrigin: envelope.header.flowOrigin
    };
    if (received.requestSeq === null) {
      this.pipeline.dropMissingReplyPath(requestFields);
      return;
    }

    const requestSeq = received.requestSeq;
    const handler = this.options.handlers.get(packetName);
    const context: ZLinkHandlerContext = {
      channelName: this.options.channelName,
      contentType: envelope.header.contentType,
      packetName,
      metadata: zlinkMessageMetadata(envelope.header.metadata)
    };
    await this.pipeline.dispatchRequest({
      fields: requestFields,
      envelope,
      codecs: this.options.codecs,
      handler,
      context,
      missingHandlerMessage: `No channel request handler is registered for '${this.options.channelName}:${packetName}'.`,
      writeReply: reply => this.submitReply(
        appendParts(
          router.reply(received.routingId, requestSeq),
          encodeChannelReplyParts(envelope.header, reply, this.options.codecs)
        ),
        signal
      ),
      writeError: error => this.submitReply(
        appendParts(
          router.reply(received.routingId, requestSeq),
          encodeChannelErrorReplyParts(envelope.header, error)
        ),
        signal
      )
    });
  }

  async dispatchMesh(record: ReceiveRecord, signal?: AbortSignal): Promise<void> {
    if (record.parts.length === 0 || record.parts[0].data().length === 0) {
      return;
    }
    const envelope = decodeChannelEnvelope(record.parts);
    const packetName = envelope.packetName;
    if (packetName === undefined) {
      throw new ZLinkConfigurationException('Channel packet is missing packetName.');
    }
    const correlationId = envelope.header.correlationId ?? undefined;
    if (envelope.header.kind === ZLinkChannelMessageKind.Command) {
      await this.pipeline.dispatchOneWay({
        fields: {
          messageKind: ZLinkDispatchMessageKind.Send,
          packetName,
          correlationId,
          flowId: envelope.header.flowId,
          flowOrigin: envelope.header.flowOrigin
        },
        envelope,
        codecs: this.options.codecs,
        handler: this.options.sendHandlers?.get(packetName),
        context: {
          channelName: this.options.channelName,
          contentType: envelope.header.contentType,
          packetName,
          connectionAborted: signal,
          metadata: zlinkMessageMetadata(envelope.header.metadata)
        }
      });
      return;
    }
    if (envelope.header.kind !== ZLinkChannelMessageKind.Request) {
      return;
    }
    const fields: ZLinkChannelDispatchFields = {
      messageKind: ZLinkDispatchMessageKind.Request,
      packetName,
      correlationId,
      flowId: envelope.header.flowId,
      flowOrigin: envelope.header.flowOrigin
    };
    await this.pipeline.dispatchRequest({
      fields,
      envelope,
      codecs: this.options.codecs,
      handler: this.options.handlers.get(packetName),
      context: {
        channelName: this.options.channelName,
        contentType: envelope.header.contentType,
        packetName,
        connectionAborted: signal,
        metadata: zlinkMessageMetadata(envelope.header.metadata)
      },
      missingHandlerMessage: `No channel request handler is registered for '${this.options.channelName}:${packetName}'.`,
      writeReply: async (reply) => {
        requireMeshReplyAccepted(record.reply(
          encodeChannelReplyParts(envelope.header, reply, this.options.codecs)
        ));
      },
      writeError: async (error) => {
        requireMeshReplyAccepted(record.reply(
          encodeChannelErrorReplyParts(envelope.header, error)
        ));
      }
    });
  }

  private submitReply(operation: ZLinkMultipartReplyOperation, signal?: AbortSignal): Promise<void> {
    const submit = () => operation.submit() !== false;
    if (this.options.replySubmitter !== undefined) {
      return this.options.replySubmitter.submitCommand(submit, signal);
    }
    if (!submit()) {
      throw new ZLinkConfigurationException('Channel reply submit was backpressured but no submitter is available.');
    }
    return Promise.resolve();
  }
}

function requireMeshReplyAccepted(result: number): void {
  if (result !== SubmitResult.Ok) {
    throw new ZLinkConfigurationException(`MeshNode channel reply was not accepted (submit result ${result}).`);
  }
}

export interface ZLinkChannelPublishDispatcherOptions {
  readonly channelName: string;
  readonly codecs?: ZLinkChannelEnvelopeCodecRegistry;
  readonly dispatchErrors: ZLinkDispatchErrorReporter;
  readonly handlers: ReadonlyMap<string, ZLinkRuntimePublishHandler>;
  readonly filters?: readonly ZLinkHandlerFilter[];
  readonly unhandled?: ZLinkUnhandledDispatchOptions;
  readonly metrics?: ZLinkRuntimeMetrics;
}

export interface ZLinkRuntimePublishHandler {
  handle(payload: unknown, context: ZLinkPublishContext): Promise<void> | void;
}

export class ZLinkChannelPublishDispatcher {
  private readonly pipeline: ZLinkChannelDispatchPipeline;

  constructor(private readonly options: ZLinkChannelPublishDispatcherOptions) {
    this.pipeline = new ZLinkChannelDispatchPipeline({
      dispatchErrors: options.dispatchErrors,
      surface: ZLinkDispatchErrorSurface.Channel,
      channelName: options.channelName,
      filters: options.filters,
      unhandled: options.unhandled
    });
  }

  async dispatch(topicMessage: { readonly topic: string; readonly parts: readonly Message[] }): Promise<void> {
    if (topicMessage.parts.length === 0 || topicMessage.parts[0].data().length === 0) {
      return;
    }
    const envelope = decodeChannelEnvelope(topicMessage.parts);
    if (envelope.header.kind !== ZLinkChannelMessageKind.Publish) {
      return;
    }
    const packetName = envelope.packetName;
    if (packetName === undefined) {
      throw new ZLinkConfigurationException('Fanout publish message is missing packetName.');
    }
    const publishTopic = envelope.header.topic ?? topicMessage.topic;
    const publishSource = envelope.header.source ?? undefined;
    const publishCorr = envelope.header.correlationId ?? undefined;
    this.options.metrics?.count('zlink.fanout.received');
    const fields: ZLinkChannelDispatchFields = {
      messageKind: ZLinkDispatchMessageKind.Publish,
      packetName,
      topic: publishTopic,
      sourceRid: publishSource,
      correlationId: publishCorr,
      flowId: envelope.header.flowId,
      flowOrigin: envelope.header.flowOrigin
    };
    const handler = this.options.handlers.get(packetName);
    const context: ZLinkPublishContext = {
      channelName: this.options.channelName,
      packetName,
      contentType: envelope.header.contentType,
      topic: publishTopic,
      source: publishSource,
      metadata: zlinkMessageMetadata(envelope.header.metadata)
    };
    await this.pipeline.dispatchOneWay({
      fields,
      envelope,
      codecs: this.options.codecs,
      handler,
      context
    });
  }
}

export interface ZLinkRoutePacketDispatcherOptions {
  readonly routerChannelId: string;
  readonly codecs?: ZLinkChannelEnvelopeCodecRegistry;
  readonly dispatchErrors: ZLinkDispatchErrorReporter;
  readonly handlers: readonly ZLinkRouteHandlerRegistration[];
  readonly filters?: readonly ZLinkHandlerFilter[];
  readonly unhandled?: ZLinkUnhandledDispatchOptions;
  readonly replySubmitter?: ZLinkAsyncSubmitter;
  readonly spotRouteBridge?: ZLinkBackendSpotRouteBridge;
  readonly rawBridgeReplyHandler?: (received: {
    readonly parts: readonly Message[];
    readonly routingId: unknown;
    readonly spotRid?: unknown;
    readonly requestSeq: bigint | null;
  }) => boolean;
}

export function collectRouteChannelHandlers(routeChannel: ZLinkRouteChannelOptions): ZLinkRouteHandlerRegistration[] {
  return [
    ...(routeChannel.handlers ?? []),
    ...(routeChannel.sendHandlers ?? []).map((handler): ZLinkRouteHandlerRegistration => ({
      kind: 'send',
      packetName: handler.packetName,
      handler: handler.handler
    })),
    ...(routeChannel.requestHandlers ?? []).map((handler): ZLinkRouteHandlerRegistration => ({
      kind: 'request',
      packetName: handler.packetName,
      handler: handler.handler
    }))
  ];
}

export class ZLinkRoutePacketDispatcher {
  private readonly sendHandlers = new Map<string, ZLinkRouteRuntimeSendHandler>();
  private readonly requestHandlers = new Map<string, ZLinkRouteRuntimeRequestHandler>();
  private readonly codecs?: ZLinkChannelEnvelopeCodecRegistry;
  private readonly pipeline: ZLinkChannelDispatchPipeline;
  private readonly replySubmitter?: ZLinkAsyncSubmitter;

  constructor(options: ZLinkRoutePacketDispatcherOptions) {
    this.routerChannelId = options.routerChannelId;
    this.codecs = options.codecs;
    this.pipeline = new ZLinkChannelDispatchPipeline({
      dispatchErrors: options.dispatchErrors,
      surface: ZLinkDispatchErrorSurface.RouteMeshChannel,
      channelName: options.routerChannelId,
      filters: options.filters,
      unhandled: options.unhandled
    });
    this.replySubmitter = options.replySubmitter;
    this.spotRouteBridge = options.spotRouteBridge;
    this.rawBridgeReplyHandler = options.rawBridgeReplyHandler;
    for (const handler of options.handlers) {
      const target = handler.kind === 'send' ? this.sendHandlers : this.requestHandlers;
      if (target.has(handler.packetName)) {
        throw new ZLinkConfigurationException(`Duplicate routed handler '${options.routerChannelId}:${handler.kind}:${handler.packetName}'.`);
      }
      target.set(handler.packetName, handler.handler as never);
    }
  }

  private readonly routerChannelId: string;
  private readonly spotRouteBridge?: ZLinkBackendSpotRouteBridge;
  private readonly rawBridgeReplyHandler?: ZLinkRoutePacketDispatcherOptions['rawBridgeReplyHandler'];

  async dispatch(received: {
    parts: readonly Message[];
    routingId: unknown;
    spotRid?: unknown;
    requestSeq: bigint | null;
    send?: () => ZLinkMultipartOperation<ZLinkMultipartSubmitOperation>;
  }, router: {
    reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
  }): Promise<boolean | void> {
    if (
      this.spotRouteBridge !== undefined &&
      !isChannelEnvelope(received.parts)
    ) {
      const processed = this.spotRouteBridge.handleRouterReceived(
        this.routerChannelId,
        received.routingId as RoutingId,
        received.requestSeq ?? 0n,
        received.parts
      );
      if (processed) {
        return true;
      }
    }
    if (received.spotRid !== null && received.spotRid !== undefined) {
      if (received.send === undefined) {
        throw new ZLinkConfigurationException('Routed SPOT packet is missing a local SPOT delivery context.');
      }
      appendParts(received.send(), received.parts).submit();
      return true;
    }
    if (this.rawBridgeReplyHandler?.(received) === true) {
      return true;
    }
    if (received.parts.length === 0 || received.parts[0].data().length === 0) {
      return;
    }
    const envelope = decodeChannelEnvelope(received.parts);
    const packetName = envelope.packetName;
    if (packetName === undefined) {
      throw new ZLinkConfigurationException('Route packet is missing packetName.');
    }

    const routeCorr = envelope.header.correlationId ?? undefined;
    const routeSource = String(received.routingId);
    if (envelope.header.kind === ZLinkChannelMessageKind.Command) {
      const fields: ZLinkChannelDispatchFields = {
        messageKind: ZLinkDispatchMessageKind.Send,
        packetName,
        sourceRid: routeSource,
        correlationId: routeCorr,
        flowId: envelope.header.flowId,
        flowOrigin: envelope.header.flowOrigin
      };
      const handler = this.sendHandlers.get(packetName);
      await this.pipeline.dispatchOneWay({
        fields,
        envelope,
        codecs: codecsForFrameworkPacket(packetName, this.codecs),
        handler,
        context: this.createRouteContext(packetName, received.routingId)
      });
      return;
    }

    if (envelope.header.kind !== ZLinkChannelMessageKind.Request) {
      return;
    }

    if (received.requestSeq === null) {
      this.pipeline.dropMissingReplyPath({
        messageKind: ZLinkDispatchMessageKind.Request,
        packetName,
        sourceRid: routeSource,
        correlationId: routeCorr,
        flowId: envelope.header.flowId,
        flowOrigin: envelope.header.flowOrigin
      });
      return;
    }

    const requestFields: ZLinkChannelDispatchFields = {
      messageKind: ZLinkDispatchMessageKind.Request,
      packetName,
      sourceRid: routeSource,
      correlationId: routeCorr,
      flowId: envelope.header.flowId,
      flowOrigin: envelope.header.flowOrigin
    };
    const handler = this.requestHandlers.get(packetName);
    const requestSeq = received.requestSeq;
    const codecs = codecsForFrameworkPacket(packetName, this.codecs);
    await this.pipeline.dispatchRequest({
      fields: requestFields,
      envelope,
      codecs,
      handler,
      context: this.createRouteContext(packetName, received.routingId, requestSeq),
      missingHandlerMessage: `No routed request handler is registered for '${this.routerChannelId}:${packetName}'.`,
      writeReply: reply => this.submitReply(
        appendParts(
          router.reply(received.routingId, requestSeq),
          encodeChannelReplyParts(envelope.header, reply, codecs)
        )
      ),
      writeError: error => this.submitReply(
        appendParts(
          router.reply(received.routingId, requestSeq),
          encodeChannelErrorReplyParts(envelope.header, error)
        )
      )
    });
  }

  async dispatchMesh(record: ReceiveRecord): Promise<void> {
    if (record.parts.length === 0 || record.parts[0].data().length === 0) {
      return;
    }
    const envelope = decodeChannelEnvelope(record.parts);
    const packetName = envelope.packetName;
    if (packetName === undefined) {
      throw new ZLinkConfigurationException('Route packet is missing packetName.');
    }
    const sourceNodeRid = String(record.sourceNodeRid ?? '');
    const correlationId = envelope.header.correlationId ?? undefined;
    if (envelope.header.kind === ZLinkChannelMessageKind.Command) {
      await this.dispatchMeshCommand(envelope, packetName, sourceNodeRid, record.sourceNodeRid);
      return;
    }
    if (envelope.header.kind !== ZLinkChannelMessageKind.Request) {
      return;
    }
    const fields: ZLinkChannelDispatchFields = {
      messageKind: ZLinkDispatchMessageKind.Request,
      packetName,
      sourceRid: sourceNodeRid,
      correlationId,
      flowId: envelope.header.flowId,
      flowOrigin: envelope.header.flowOrigin
    };
    const codecs = codecsForFrameworkPacket(packetName, this.codecs);
    await this.pipeline.dispatchRequest({
      fields,
      envelope,
      codecs,
      handler: this.requestHandlers.get(packetName),
      context: this.createRouteContext(packetName, record.sourceNodeRid, 0n),
      missingHandlerMessage: `No routed request handler is registered for '${this.routerChannelId}:${packetName}'.`,
      writeReply: async (reply) => {
        requireMeshReplyAccepted(record.reply(encodeChannelReplyParts(envelope.header, reply, codecs)));
      },
      writeError: async (error) => {
        requireMeshReplyAccepted(record.reply(encodeChannelErrorReplyParts(envelope.header, error)));
      }
    });
  }

  async dispatchLocalCommand(parts: readonly Message[], sourceNodeRid: RoutingId): Promise<void> {
    if (parts.length === 0 || parts[0].data().length === 0) return;
    const envelope = decodeChannelEnvelope(parts);
    if (envelope.header.kind !== ZLinkChannelMessageKind.Command) {
      throw new ZLinkConfigurationException('Local node-direct dispatch requires a command packet.');
    }
    const packetName = envelope.packetName;
    if (packetName === undefined) {
      throw new ZLinkConfigurationException('Route packet is missing packetName.');
    }
    await this.dispatchMeshCommand(envelope, packetName, String(sourceNodeRid), sourceNodeRid);
  }

  private dispatchMeshCommand(
    envelope: ReturnType<typeof decodeChannelEnvelope>,
    packetName: string,
    sourceRid: string,
    sourceNodeRid: unknown
  ): Promise<void> {
    return this.pipeline.dispatchOneWay({
      fields: {
        messageKind: ZLinkDispatchMessageKind.Send,
        packetName,
        sourceRid,
        correlationId: envelope.header.correlationId ?? undefined,
        flowId: envelope.header.flowId,
        flowOrigin: envelope.header.flowOrigin
      },
      envelope,
      codecs: codecsForFrameworkPacket(packetName, this.codecs),
      handler: this.sendHandlers.get(packetName),
      context: this.createRouteContext(packetName, sourceNodeRid)
    });
  }

  private submitReply(operation: ZLinkMultipartReplyOperation): Promise<void> {
    const submit = () => operation.submit() !== false;
    if (this.replySubmitter !== undefined) {
      return this.replySubmitter.submitCommand(submit);
    }
    if (!submit()) {
      throw new ZLinkConfigurationException('Route reply submit was backpressured but no submitter is available.');
    }
    return Promise.resolve();
  }

  private createRouteContext(packetName: string, sourceRid: unknown): ZLinkRouteSendContext;
  private createRouteContext(packetName: string, sourceRid: unknown, requestSeq: bigint): ZLinkRouteRequestContext;
  private createRouteContext(
    packetName: string,
    sourceRid: unknown,
    requestSeq?: bigint
  ): ZLinkRouteSendContext | ZLinkRouteRequestContext {
    const sourceNodeRid = String(sourceRid ?? '');
    if (requestSeq === undefined) {
      return {
        channelName: this.routerChannelId,
        packetName,
        contentType: JSON_CONTENT_TYPE,
        routerChannelId: this.routerChannelId,
        sourceNodeRid,
        metadata: zlinkMessageMetadata({})
      };
    }
    return {
      channelName: this.routerChannelId,
      packetName,
      contentType: JSON_CONTENT_TYPE,
      routerChannelId: this.routerChannelId,
      sourceNodeRid,
      metadata: zlinkMessageMetadata({})
    };
  }
}
