import type { ZLinkDispatchFailure, ZLinkProviderResolver } from '../../contracts';
import {
  ZLinkDispatchErrorReason,
  ZLinkDispatchMessageKind,
  ZLinkMessageFlowLogMode,
  ZLinkMessageFlowOutcome
} from '../../contracts';
import {
  ZLinkMessageFlowTracer,
  DEFAULT_ZLINK_DIAGNOSTICS,
  type ZLinkDiagnosticsContext
} from '../diagnostics';

type ZLinkRuntimeDispatchFailure = ZLinkDispatchFailure & {
  readonly error?: unknown;
};

export interface ZLinkDispatchErrorSink {
  reportRuntimeTaskException(taskName: string, error: unknown): void;
}

export class ZLinkDispatchErrorReporter {
  private reportedEvents = 0;
  /**
   * Success-path tracer companion: every surface already receives a reporter, so
   * exposing the flow tracer here wires all dispatch sites without threading a new
   * parameter. Shares the same diagnostics context (live mode) and error sink.
   */
  readonly flow: ZLinkMessageFlowTracer;

  constructor(
    _observerType: undefined,
    providerResolver: ZLinkProviderResolver | undefined,
    errorSink: ZLinkDispatchErrorSink,
    ctx?: ZLinkDiagnosticsContext
  ) {
    const flowCtx: ZLinkDiagnosticsContext = ctx ?? {
      diagnostics: DEFAULT_ZLINK_DIAGNOSTICS,
      liveMode: { mode: ZLinkMessageFlowLogMode.ErrorsOnly },
      providerResolver
    };
    this.flow = new ZLinkMessageFlowTracer(flowCtx, errorSink);
  }

  report(event: ZLinkRuntimeDispatchFailure): void {
    const errorInfo = dispatchErrorInfo(event);
    this.reportedEvents += 1;
    this.flow.trace({
      outcome: ZLinkMessageFlowOutcome.Error,
      surface: event.surface,
      messageKind: event.messageKind,
      packetName: event.packetName,
      channelName: event.channelName,
      topic: event.topic,
      correlationId: event.correlationId,
      flowId: event.flowId,
      flowOrigin: event.flowOrigin,
      sourceRid: event.sourceRid,
      spotRid: event.spotRid,
      actorId: event.actorId,
      errorReason: event.reason,
      errorAction: event.action,
      errorType: errorInfo.errorType,
      errorMessage: errorInfo.errorMessage
    }, dispatchFailureLogLevel(event));
  }

  get reportedCount(): number {
    return this.reportedEvents;
  }

  get observerFailureCount(): number {
    return this.flow.observerFailureCount;
  }
}

function dispatchFailureLogLevel(
  event: ZLinkRuntimeDispatchFailure
): 'error' | 'warn' | 'debug' {
  if (event.reason === ZLinkDispatchErrorReason.HandlerException) {
    return 'error';
  }
  if (
    event.messageKind === ZLinkDispatchMessageKind.Publish
    && (event.reason === ZLinkDispatchErrorReason.HandlerMissing
      || event.reason === ZLinkDispatchErrorReason.PayloadDecodeFailed
      || event.reason === ZLinkDispatchErrorReason.InvalidFrame)
  ) {
    return 'debug';
  }
  if (
    event.messageKind === ZLinkDispatchMessageKind.Send
    && (event.reason === ZLinkDispatchErrorReason.HandlerMissing
      || event.reason === ZLinkDispatchErrorReason.PayloadDecodeFailed
      || event.reason === ZLinkDispatchErrorReason.InvalidFrame)
  ) {
    return 'warn';
  }
  return 'error';
}

function dispatchErrorInfo(event: ZLinkRuntimeDispatchFailure): { readonly errorType?: string; readonly errorMessage?: string } {
  if (event.errorType !== undefined || event.errorMessage !== undefined) {
    return { errorType: event.errorType, errorMessage: event.errorMessage };
  }
  if (event.error === undefined) {
    return {};
  }
  if (event.error instanceof Error) {
    return { errorType: event.error.name, errorMessage: event.error.message };
  }
  return { errorType: typeof event.error, errorMessage: String(event.error) };
}
