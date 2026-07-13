import { appendFileSync, mkdirSync } from 'node:fs';
import { dirname } from 'node:path';
import type { ZLinkProviderResolver } from '../../contracts';
import {
  MESSAGE_FLOW_MODE_RANK,
  ZLinkMessageFlowLogMode,
  ZLinkMessageFlowOutcome
} from '../../contracts';
import type {
  ZLinkDiagnosticsOptions,
  ZLinkDispatchFailure,
  ZLinkMessageFlowEvent,
  ZLinkMessageFlowObserver,
  Type
} from '../../contracts';
import type { ZLinkDispatchErrorSink } from '../channels';
import { currentOrCreateFlow } from './flow-context';
import { getDispatchObserverType } from '../../contracts/Configuration/DispatchObserverRegistration';

/** Shared, runtime-mutable message-flow mode cell (the C++ live_mode). */
export interface ZLinkMessageFlowModeCell {
  mode: ZLinkMessageFlowLogMode;
}

/**
 * Diagnostics state shared by the flow tracer and the dispatch error reporter: the
 * configured diagnostics, the live-mode cell (so set_message_flow_mode flips every
 * surface), the optional flow observer type, and the provider resolver.
 */
export interface ZLinkDiagnosticsContext {
  readonly diagnostics: ZLinkDiagnosticsOptions;
  readonly liveMode: ZLinkMessageFlowModeCell;
  readonly messageFlowObserverType?: Type<ZLinkMessageFlowObserver>;
  readonly providerResolver?: ZLinkProviderResolver;
}

export const DEFAULT_ZLINK_DIAGNOSTICS: ZLinkDiagnosticsOptions = {
  messageFlow: ZLinkMessageFlowLogMode.ErrorsOnly,
  sampleRate: 1,
  includeMessageSizes: false,
  includeNativeDiagnostics: false
};

export function effectiveMessageFlow(ctx: ZLinkDiagnosticsContext): ZLinkMessageFlowLogMode {
  return ctx.liveMode.mode;
}

/** Create the shared live-mode cell, seeded from the configured mode (default errorsOnly). */
export function createMessageFlowModeCell(
  dispatch: { diagnostics?: ZLinkDiagnosticsOptions } | undefined
): ZLinkMessageFlowModeCell {
  return { mode: dispatch?.diagnostics?.messageFlow ?? ZLinkMessageFlowLogMode.ErrorsOnly };
}

/** Assemble the diagnostics context the reporter + tracer share. */
export function createDiagnosticsContext(
  dispatch:
    | {
        diagnostics?: ZLinkDiagnosticsOptions;
      }
    | undefined,
  providerResolver: ZLinkProviderResolver | undefined,
  liveMode: ZLinkMessageFlowModeCell
): ZLinkDiagnosticsContext {
  return {
    diagnostics: dispatch?.diagnostics ?? DEFAULT_ZLINK_DIAGNOSTICS,
    liveMode,
    messageFlowObserverType: getDispatchObserverType(dispatch as import('../../contracts').ZLinkDispatchOptions | undefined),
    providerResolver
  };
}

function requiredMode(outcome: ZLinkMessageFlowOutcome): ZLinkMessageFlowLogMode {
  return outcome === ZLinkMessageFlowOutcome.Dropped
    || outcome === ZLinkMessageFlowOutcome.Error
    ? ZLinkMessageFlowLogMode.ErrorsOnly
    : ZLinkMessageFlowLogMode.KeyTransitions;
}

function observerFailureTaskName(outcome: ZLinkMessageFlowOutcome): string {
  return outcome === ZLinkMessageFlowOutcome.Dropped
    || outcome === ZLinkMessageFlowOutcome.Error
    ? 'dispatch-error-observer'
    : 'message-flow-observer';
}

/**
 * Returns the tracer only when this outcome is enabled, so call sites read as
 * `flowIfEnabled(reporter?.flow, outcome)?.trace({ ...event })`. Optional chaining
 * short-circuits, so the event object literal is never built when tracing is off —
 * keeping the disabled path allocation-free.
 */
export function flowIfEnabled(
  flow: ZLinkMessageFlowTracer | undefined,
  outcome: ZLinkMessageFlowOutcome
): ZLinkMessageFlowTracer | undefined {
  return flow !== undefined && flow.enabled(outcome) ? flow : undefined;
}

/**
 * Success-path message-flow tracer — the twin of ZLinkDispatchErrorReporter for
 * received/dispatched/replied/sent/replyReceived transitions, keyed by correlation id.
 * Mirrors the C++/.NET/Java tracer. Build the event only after enabled(outcome) so an
 * "off" dispatch pays nothing but a mode read.
 */
export class ZLinkMessageFlowTracer {
  private tracedEvents = 0;
  private observerFailures = 0;
  private sampleCounter = 0;

  constructor(
    private readonly ctx: ZLinkDiagnosticsContext,
    private readonly errorSink: ZLinkDispatchErrorSink
  ) {}

  enabled(outcome: ZLinkMessageFlowOutcome): boolean {
    return MESSAGE_FLOW_MODE_RANK[effectiveMessageFlow(this.ctx)] >= MESSAGE_FLOW_MODE_RANK[requiredMode(outcome)];
  }

  trace(flowInput: Omit<ZLinkMessageFlowEvent, 'effectiveMode' | 'flowId' | 'flowOrigin'> & {
    readonly effectiveMode?: ZLinkMessageFlowLogMode;
    readonly flowId?: string;
    readonly flowOrigin?: import('../../contracts').ZLinkFlowOrigin;
  }, defaultLogLevel: 'error' | 'warn' | 'debug' = 'error'): void {
    const root = flowInput.flowId !== undefined && flowInput.flowOrigin !== undefined
      ? { flowId: flowInput.flowId, flowOrigin: flowInput.flowOrigin }
      : currentOrCreateFlow();
    const flow: ZLinkMessageFlowEvent = {
      ...flowInput,
      ...root,
      effectiveMode: flowInput.effectiveMode ?? effectiveMessageFlow(this.ctx)
    };
    if (!this.enabled(flow.outcome)) {
      return;
    }
    if (
      flow.outcome !== ZLinkMessageFlowOutcome.Dropped &&
      flow.outcome !== ZLinkMessageFlowOutcome.Error &&
      !this.sample()
    ) {
      return;
    }
    this.tracedEvents += 1;
    try {
      this.logDefault(flow, defaultLogLevel);
    } catch (error) {
      this.errorSink.reportRuntimeTaskException('message-flow', error);
    }

    const observerType = this.ctx.messageFlowObserverType;
    if (observerType === undefined) {
      return;
    }
    queueMicrotask(() => {
      void this.resolveObserver(observerType)
        .then((observer) => observer.onMessageFlow(flow))
        .catch((error) => {
          this.observerFailures += 1;
          this.errorSink.reportRuntimeTaskException(observerFailureTaskName(flow.outcome), error);
        });
    });
  }

  get tracedCount(): number {
    return this.tracedEvents;
  }

  get observerFailureCount(): number {
    return this.observerFailures;
  }

  private sample(): boolean {
    const rate = this.ctx.diagnostics.sampleRate;
    if (rate >= 1.0) {
      return true;
    }
    if (rate <= 0.0) {
      return false;
    }
    const stride = Math.max(1, Math.round(1.0 / rate));
    this.sampleCounter += 1;
    return this.sampleCounter % stride === 0;
  }

  private logDefault(flow: ZLinkMessageFlowEvent, level: 'error' | 'warn' | 'debug'): void {
    const d = this.ctx.diagnostics;
    const includeSize =
      flow.messageSize !== undefined &&
      MESSAGE_FLOW_MODE_RANK[effectiveMessageFlow(this.ctx)] >= MESSAGE_FLOW_MODE_RANK[ZLinkMessageFlowLogMode.Verbose] &&
      d.includeMessageSizes !== false;
    const line = flowLine(flow, d.label, includeSize ? flow.messageSize : undefined);
    if (d.logFile !== undefined) {
      writeTraceFile(d.logFile, line);
    } else {
      console[level](line);
    }
  }

  private async resolveObserver(
    observerType: Type<ZLinkMessageFlowObserver>
  ): Promise<ZLinkMessageFlowObserver> {
    const existing = this.ctx.providerResolver?.get?.(observerType);
    if (existing !== undefined) {
      return existing;
    }
    const created = await this.ctx.providerResolver?.create?.(observerType);
    if (created !== undefined) {
      return created;
    }
    return new observerType();
  }
}

function field(name: string, value: string | undefined): string | undefined {
  return value === undefined || value === '' ? undefined : `${name}=${value}`;
}

export function flowLine(flow: ZLinkMessageFlowEvent, label: string | undefined, size: number | undefined): string {
  return [
    'message flow',
    `phase=${flow.outcome}`,
    `surface=${flow.surface}`,
    `kind=${flow.messageKind}`,
    field('label', label),
    field('packet', flow.packetName),
    field('channel', flow.channelName),
    field('topic', flow.topic),
    field('corr', flow.correlationId),
    field('flow', flow.flowId),
    field('origin', flow.flowOrigin),
    field('src', flow.sourceRid),
    field('spot', flow.spotRid),
    field('actor', flow.actorId),
    field('errorReason', flow.errorReason),
    field('errorAction', flow.errorAction),
    field('errorType', flow.errorType),
    field('errorMessage', flow.errorMessage),
    size === undefined ? undefined : `size=${size}`
  ]
    .filter((value): value is string => value !== undefined)
    .join(' ');
}

export function errorLine(event: ZLinkDispatchFailure, label: string | undefined): string {
  return [
    'dispatch error',
    `surface=${event.surface}`,
    `kind=${event.messageKind}`,
    `reason=${event.reason}`,
    `action=${event.action}`,
    field('label', label),
    field('packet', event.packetName),
    field('channel', event.channelName),
    field('topic', event.topic),
    field('corr', event.correlationId),
    field('src', event.sourceRid),
    field('spot', event.spotRid),
    field('actor', event.actorId),
    field('errorType', event.errorType),
    field('errorMessage', event.errorMessage)
  ]
    .filter((value): value is string => value !== undefined)
    .join(' ');
}

export function writeTraceFile(path: string, line: string): void {
  try {
    const dir = dirname(path);
    if (dir.length > 0) {
      mkdirSync(dir, { recursive: true });
    }
    appendFileSync(path, `${line}\n`);
  } catch {
    // best-effort: tracing must never break dispatch.
  }
}
