import type {
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind,
  ZLinkHandlerFilter,
  ZLinkMessageFlowOutcome,
  ZLinkProviderResolver
} from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
import type { ZLinkFrameworkRegistration } from '../configuration';
import {
  ZLinkMessageFlowTracer,
  ZLinkRuntimeMetrics,
  createDiagnosticsContext,
  createMessageFlowModeCell,
  flowIfEnabled,
  type ZLinkDiagnosticsContext,
  type ZLinkMessageFlowModeCell
} from '../diagnostics';
import type { ZLinkRuntimeErrorSink } from '../execution';
import { ZLinkDispatchErrorReporter } from './dispatch-error-reporter';

export interface ZLinkChannelOutboundTrace {
  readonly surface: ZLinkDispatchErrorSurface;
  readonly messageKind: ZLinkDispatchMessageKind;
  readonly channelName: string;
  readonly packetName: string | undefined;
  readonly correlationId: string | undefined;
  readonly topic?: string;
  readonly sourceRid?: string;
}

export class ZLinkChannelDispatchServices {
  private readonly dispatchErrorReporters = new WeakMap<ZLinkRuntimeErrorSink, ZLinkDispatchErrorReporter>();
  private messageFlowModeCellValue?: ZLinkMessageFlowModeCell;
  private diagnosticsContextValue?: ZLinkDiagnosticsContext;
  private handlerFiltersValue?: readonly ZLinkHandlerFilter[];
  private outboundFlowValue?: ZLinkMessageFlowTracer;
  private readonly metricsValue: ZLinkRuntimeMetrics;

  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly providerResolver?: ZLinkProviderResolver,
    private readonly configuredMessageFlowModeCell?: ZLinkMessageFlowModeCell
  ) {
    this.metricsValue = new ZLinkRuntimeMetrics(registration.metrics?.meterProvider);
  }

  metrics(): ZLinkRuntimeMetrics {
    return this.metricsValue;
  }

  flowCreationEnabled(): boolean {
    return this.outboundFlow().flowCreationEnabled();
  }

  dispatchErrorReporter(errorSink: ZLinkRuntimeErrorSink): ZLinkDispatchErrorReporter {
    const existing = this.dispatchErrorReporters.get(errorSink);
    if (existing !== undefined) {
      return existing;
    }
    const reporter = new ZLinkDispatchErrorReporter(
      undefined,
      undefined,
      errorSink,
      this.diagnosticsContext()
    );
    this.dispatchErrorReporters.set(errorSink, reporter);
    return reporter;
  }

  handlerFilters(): readonly ZLinkHandlerFilter[] {
    if (this.handlerFiltersValue !== undefined) {
      return this.handlerFiltersValue;
    }
    this.handlerFiltersValue = this.registration.filterTypes.map((filterType) => {
      const filter = this.providerResolver?.get?.(filterType);
      if (filter === undefined) {
        throw new ZLinkConfigurationException(
          `Handler filter '${filterType.name}' is not registered in the provider resolver.`
        );
      }
      return filter;
    });
    return this.handlerFiltersValue;
  }

  traceOutbound(
    outcome: ZLinkMessageFlowOutcome,
    createTrace: () => ZLinkChannelOutboundTrace
  ): void {
    const flow = flowIfEnabled(this.outboundFlow(), outcome);
    if (flow === undefined) {
      return;
    }
    const trace = createTrace();
    flow.trace({
      outcome,
      surface: trace.surface,
      messageKind: trace.messageKind,
      channelName: trace.channelName,
      packetName: trace.packetName,
      correlationId: trace.correlationId,
      topic: trace.topic,
      sourceRid: trace.sourceRid
    });
  }

  private messageFlowModeCell(): ZLinkMessageFlowModeCell {
    this.messageFlowModeCellValue ??=
      this.configuredMessageFlowModeCell ?? createMessageFlowModeCell(this.registration.dispatch);
    return this.messageFlowModeCellValue;
  }

  private diagnosticsContext(): ZLinkDiagnosticsContext {
    this.diagnosticsContextValue ??= createDiagnosticsContext(
      this.registration.dispatch,
      this.providerResolver,
      this.messageFlowModeCell()
    );
    return this.diagnosticsContextValue;
  }

  private outboundFlow(): ZLinkMessageFlowTracer {
    this.outboundFlowValue ??= new ZLinkMessageFlowTracer(
      this.diagnosticsContext(),
      { reportRuntimeTaskException() {} }
    );
    return this.outboundFlowValue;
  }
}
