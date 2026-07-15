import type {
  ZLinkDispatchErrorSurface,
  ZLinkHandlerContext,
  ZLinkHandlerFilter,
  ZLinkUnhandledDispatchOptions,
  ZLinkFlowOrigin
} from '../../contracts';
import {
  ZLinkDispatchErrorAction,
  ZLinkDispatchErrorReason,
  ZLinkDispatchMessageKind,
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkMessageFlowOutcome,
  ZLinkUnhandledDispatchAction
} from '../../contracts';
import { invokeZLinkHandlerFilters } from '../handlers';
import {
  decodeChannelPayload,
  type ZLinkChannelEnvelope,
  type ZLinkChannelEnvelopeCodecRegistry
} from './channel-envelope';
import type { ZLinkDispatchErrorReporter } from './dispatch-error-reporter';
import { createInboundFlow, runWithFlow } from '../diagnostics/flow-context';

export interface ZLinkChannelDispatchPipelineOptions {
  readonly dispatchErrors: ZLinkDispatchErrorReporter;
  readonly surface: ZLinkDispatchErrorSurface;
  readonly channelName: string;
  readonly filters?: readonly ZLinkHandlerFilter[];
  readonly unhandled?: ZLinkUnhandledDispatchOptions;
}

export interface ZLinkChannelDispatchFields {
  readonly messageKind: ZLinkDispatchMessageKind;
  readonly packetName: string;
  readonly correlationId?: string;
  readonly topic?: string;
  readonly sourceRid?: string;
  readonly flowId?: string;
  readonly flowOrigin?: ZLinkFlowOrigin;
}

interface ZLinkChannelDispatchHandler<TContext extends ZLinkHandlerContext, TResult> {
  handle(payload: unknown, context: TContext): Promise<TResult> | TResult;
}

interface ZLinkOneWayDispatch<TContext extends ZLinkHandlerContext> {
  readonly fields: ZLinkChannelDispatchFields;
  readonly envelope: ZLinkChannelEnvelope;
  readonly codecs?: ZLinkChannelEnvelopeCodecRegistry;
  readonly handler?: ZLinkChannelDispatchHandler<TContext, void>;
  readonly context: TContext;
}

interface ZLinkRequestDispatch<TContext extends ZLinkHandlerContext> {
  readonly fields: ZLinkChannelDispatchFields;
  readonly envelope: ZLinkChannelEnvelope;
  readonly codecs?: ZLinkChannelEnvelopeCodecRegistry;
  readonly handler?: ZLinkChannelDispatchHandler<TContext, unknown>;
  readonly context: TContext;
  readonly missingHandlerMessage: string;
  readonly writeReply: (reply: unknown) => Promise<void>;
  readonly writeError: (error: unknown) => Promise<void>;
}

export class ZLinkChannelDispatchPipeline {
  private readonly filters: readonly ZLinkHandlerFilter[];
  private readonly unhandled: ZLinkUnhandledDispatchOptions;

  constructor(private readonly options: ZLinkChannelDispatchPipelineOptions) {
    this.filters = options.filters ?? [];
    this.unhandled = options.unhandled ?? DEFAULT_UNHANDLED_DISPATCH;
  }

  async dispatchOneWay<TContext extends ZLinkHandlerContext>(dispatch: ZLinkOneWayDispatch<TContext>): Promise<void> {
    this.trace(ZLinkMessageFlowOutcome.Received, dispatch.fields);
    if (dispatch.handler === undefined) {
      const action = dispatch.fields.messageKind === ZLinkDispatchMessageKind.Publish
        ? this.unhandled.publish
        : this.unhandled.send;
      this.report(
        dispatch.fields,
        ZLinkDispatchErrorReason.HandlerMissing,
        ZLinkDispatchErrorAction.Drop
      );
      this.applyOneWayUnhandled(action, dispatch.fields);
      return;
    }

    try {
      const flow = createInboundFlow(
        dispatch.fields.flowId,
        dispatch.fields.flowOrigin,
        this.options.dispatchErrors.flow.flowCreationEnabled()
      );
      await runWithFlow(flow, () => this.invoke(
          dispatch.envelope,
          dispatch.codecs,
          dispatch.handler!,
          dispatch.context
        ));
      this.trace(ZLinkMessageFlowOutcome.Dispatched, dispatch.fields);
    } catch (error) {
      this.report(
        dispatch.fields,
        this.failureReason(error),
        ZLinkDispatchErrorAction.Drop,
        error
      );
    }
  }

  async dispatchRequest<TContext extends ZLinkHandlerContext>(dispatch: ZLinkRequestDispatch<TContext>): Promise<void> {
    this.trace(ZLinkMessageFlowOutcome.Received, dispatch.fields);
    if (dispatch.handler === undefined) {
      const missing = new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.HandlerNotFound,
        dispatch.missingHandlerMessage
      );
      if (this.unhandled.request === ZLinkUnhandledDispatchAction.ReplyError) {
        await dispatch.writeError(missing);
      }
      this.report(
        dispatch.fields,
        ZLinkDispatchErrorReason.HandlerMissing,
        this.unhandled.request === ZLinkUnhandledDispatchAction.ReplyError
          ? ZLinkDispatchErrorAction.ReplyError
          : ZLinkDispatchErrorAction.Drop
      );
      if (this.unhandled.request === ZLinkUnhandledDispatchAction.Throw) {
        throw missing;
      }
      return;
    }

    let reply: unknown;
    try {
      const flow = createInboundFlow(
        dispatch.fields.flowId,
        dispatch.fields.flowOrigin,
        this.options.dispatchErrors.flow.flowCreationEnabled()
      );
      reply = await runWithFlow(flow, () => this.invoke(
          dispatch.envelope,
          dispatch.codecs,
          dispatch.handler!,
          dispatch.context
        ));
    } catch (error) {
      try {
        await dispatch.writeError(error);
      } catch (replyError) {
        this.report(
          dispatch.fields,
          ZLinkDispatchErrorReason.UnexpectedReply,
          ZLinkDispatchErrorAction.Drop,
          replyError
        );
        return;
      }
      this.report(
        dispatch.fields,
        this.failureReason(error),
        ZLinkDispatchErrorAction.ReplyError,
        error
      );
      return;
    }

    try {
      await dispatch.writeReply(reply);
      this.trace(ZLinkMessageFlowOutcome.Replied, dispatch.fields);
    } catch (error) {
      this.report(
        dispatch.fields,
        ZLinkDispatchErrorReason.UnexpectedReply,
        ZLinkDispatchErrorAction.Drop,
        error
      );
    }
  }

  dropMissingReplyPath(fields: ZLinkChannelDispatchFields): void {
    this.report(
      fields,
      ZLinkDispatchErrorReason.ReplyPathMissing,
      ZLinkDispatchErrorAction.Drop
    );
  }

  private invoke<TContext extends ZLinkHandlerContext, TResult>(
    envelope: ZLinkChannelEnvelope,
    codecs: ZLinkChannelEnvelopeCodecRegistry | undefined,
    handler: ZLinkChannelDispatchHandler<TContext, TResult>,
    context: TContext
  ): Promise<TResult> {
    const payload = decodeChannelPayload(envelope, codecs);
    return invokeZLinkHandlerFilters(
      this.filters,
      { message: payload, context },
      () => Promise.resolve(handler.handle(payload, context))
    ) as Promise<TResult>;
  }

  private report(
    fields: ZLinkChannelDispatchFields,
    reason: ZLinkDispatchErrorReason,
    action: ZLinkDispatchErrorAction,
    error?: unknown
  ): void {
    this.options.dispatchErrors.report({
      surface: this.options.surface,
      messageKind: fields.messageKind,
      reason,
      action,
      packetName: fields.packetName,
      channelName: this.options.channelName,
      topic: fields.topic,
      sourceRid: fields.sourceRid,
      correlationId: fields.correlationId,
      flowId: fields.flowId,
      flowOrigin: fields.flowOrigin,
      error
    });
  }

  private trace(outcome: ZLinkMessageFlowOutcome, fields: ZLinkChannelDispatchFields): void {
    const flow = this.options.dispatchErrors.flow;
    if (!flow.enabled(outcome)) {
      return;
    }
    flow.trace({
      outcome,
      surface: this.options.surface,
      messageKind: fields.messageKind,
      packetName: fields.packetName,
      channelName: this.options.channelName,
      topic: fields.topic,
      sourceRid: fields.sourceRid,
      correlationId: fields.correlationId,
      flowId: fields.flowId,
      flowOrigin: fields.flowOrigin
    });
  }

  private failureReason(error: unknown): ZLinkDispatchErrorReason {
    return error instanceof ZLinkFrameworkException && error.kind === ZLinkFrameworkErrorKind.PayloadDecodeFailed
      ? ZLinkDispatchErrorReason.PayloadDecodeFailed
      : ZLinkDispatchErrorReason.HandlerException;
  }

  private applyOneWayUnhandled(
    action: ZLinkUnhandledDispatchAction,
    fields: ZLinkChannelDispatchFields
  ): void {
    if (action === ZLinkUnhandledDispatchAction.Throw) {
      throw new Error(`No ${fields.messageKind} handler is registered for '${this.options.channelName}:${fields.packetName}'.`);
    }
  }
}

const DEFAULT_UNHANDLED_DISPATCH: ZLinkUnhandledDispatchOptions = {
  request: ZLinkUnhandledDispatchAction.ReplyError,
  send: ZLinkUnhandledDispatchAction.LogAndDrop,
  publish: ZLinkUnhandledDispatchAction.LogAndDrop
};
