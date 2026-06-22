'use strict';

// MFLOW-xxx: message-flow tracing parity (mirrors C++/.NET/Java). Exercises the tracer's
// mode gating (zero-cost off), structured node-stamped output, file routing, observer
// offload, the live-mode toggle, and the stream correlation_id wire round-trip
// (framework <-> connector codecs byte-identical).

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');

const framework = require('../../packages/framework/dist/internal');
const streamProtocol = require('../../packages/framework/dist/runtime/streams/protocol');
const connector = require('../../packages/stream-connector/dist');

const {
  ZLinkMessageFlowTracer,
  createDiagnosticsContext,
  createMessageFlowModeCell,
  ZLinkMessageFlowPhase,
  ZLinkMessageFlowLogMode,
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind
} = framework;

function silentSink() {
  return { reportRuntimeTaskException() {} };
}

function makeTracer(diagnostics, providerResolver) {
  const dispatch = { diagnostics, providerResolver };
  const cell = createMessageFlowModeCell(dispatch);
  const ctx = createDiagnosticsContext(dispatch, providerResolver, cell);
  return { tracer: new ZLinkMessageFlowTracer(ctx, silentSink()), cell };
}

function receivedEvent() {
  return {
    phase: ZLinkMessageFlowPhase.Received,
    surface: ZLinkDispatchErrorSurface.Channel,
    messageKind: ZLinkDispatchMessageKind.Request,
    packetName: 'EchoRequest',
    channelName: 'api',
    correlationId: 'corr-1'
  };
}

test('MFLOW-001 off mode is zero-cost: enabled() false and trace() is a no-op', () => {
  const { tracer } = makeTracer({ messageFlowLogMode: ZLinkMessageFlowLogMode.Off });
  assert.equal(tracer.enabled(ZLinkMessageFlowPhase.Received), false);
  assert.equal(tracer.enabled(ZLinkMessageFlowPhase.Dropped), false);
  tracer.trace(receivedEvent());
  assert.equal(tracer.tracedCount, 0);
});

test('MFLOW-002 mode ladder gates phases by severity', () => {
  const errorsOnly = makeTracer({ messageFlowLogMode: ZLinkMessageFlowLogMode.ErrorsOnly }).tracer;
  assert.equal(errorsOnly.enabled(ZLinkMessageFlowPhase.Dropped), true);
  assert.equal(errorsOnly.enabled(ZLinkMessageFlowPhase.Received), false);

  const keyTransitions = makeTracer({ messageFlowLogMode: ZLinkMessageFlowLogMode.KeyTransitions }).tracer;
  assert.equal(keyTransitions.enabled(ZLinkMessageFlowPhase.Received), true);
  assert.equal(keyTransitions.enabled(ZLinkMessageFlowPhase.Replied), true);
  assert.equal(keyTransitions.enabled(ZLinkMessageFlowPhase.Dropped), true);
});

test('MFLOW-003/005 structured key=value line with node= is written to the separated log file', () => {
  const logDir = fs.mkdtempSync(path.join(os.tmpdir(), 'zlink-mflow-'));
  const logFile = path.join(logDir, 'flow.log');
  const { tracer } = makeTracer({
    messageFlowLogMode: ZLinkMessageFlowLogMode.KeyTransitions,
    logFile,
    nodeId: 'api'
  });
  tracer.trace(receivedEvent());
  assert.equal(tracer.tracedCount, 1);
  const line = fs.readFileSync(logFile, 'utf8').trim();
  assert.match(line, /phase=received/);
  assert.match(line, /surface=channel/);
  assert.match(line, /node=api/);
  assert.match(line, /packet=EchoRequest/);
  assert.match(line, /channel=api/);
  assert.match(line, /corr=corr-1/);
});

test('MFLOW-004 observer offload delivers the event asynchronously', async () => {
  const events = [];
  class FlowObserver {
    onMessageFlow(event) {
      events.push(event);
    }
  }
  const { tracer } = makeTracer({
    messageFlowLogMode: ZLinkMessageFlowLogMode.KeyTransitions,
    logFile: path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'zlink-mflow-obs-')), 'flow.log')
  });
  // The observer type lives on the diagnostics context; rebuild with it wired.
  const dispatch = {
    diagnostics: { messageFlowLogMode: ZLinkMessageFlowLogMode.KeyTransitions },
    messageFlowObserverType: FlowObserver
  };
  const cell = createMessageFlowModeCell(dispatch);
  const ctx = createDiagnosticsContext(dispatch, undefined, cell);
  const observed = new ZLinkMessageFlowTracer(ctx, silentSink());
  assert.equal(events.length, 0, 'observer must not fire synchronously');
  observed.trace(receivedEvent());
  assert.equal(events.length, 0, 'observer is offloaded, not synchronous');
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(events.length, 1);
  assert.equal(events[0].phase, ZLinkMessageFlowPhase.Received);
  assert.equal(events[0].correlationId, 'corr-1');
  void tracer;
});

test('MFLOW-009 live-mode cell toggles every reader without rebuilding the tracer', () => {
  const { tracer, cell } = makeTracer({ messageFlowLogMode: ZLinkMessageFlowLogMode.Off });
  assert.equal(tracer.enabled(ZLinkMessageFlowPhase.Received), false);
  cell.mode = ZLinkMessageFlowLogMode.Verbose;
  assert.equal(tracer.enabled(ZLinkMessageFlowPhase.Received), true);
  cell.mode = ZLinkMessageFlowLogMode.Off;
  assert.equal(tracer.enabled(ZLinkMessageFlowPhase.Received), false);
});

test('MFLOW-010 stream correlation_id round-trips byte-identically across framework and connector codecs', () => {
  const correlationId = '1a2b';
  const requestSeq = 7n;
  const name = 'EchoRequest';

  const connectorBytes = connector.ZlinkStreamHeaderCodec.encode({
    kind: connector.ZlinkStreamMessageKind.Request,
    codec: connector.ZlinkStreamCodec.Json,
    flags: connector.ZlinkStreamHeaderFlags.None,
    requestSeq,
    name,
    metadata: connector.ZlinkStreamMetadataMap.empty,
    correlationId
  });

  const frameworkDecoded = streamProtocol.decodeStreamHeader(connectorBytes);
  assert.equal(frameworkDecoded.correlationId, correlationId);
  assert.equal(frameworkDecoded.requestSeq, requestSeq);
  assert.equal(frameworkDecoded.name, name);

  const frameworkBytes = streamProtocol.encodeStreamHeader({
    kind: streamProtocol.ZLinkStreamMessageKind.Request,
    codec: streamProtocol.ZLinkStreamCodec.Json,
    flags: streamProtocol.ZLinkStreamHeaderFlags.None,
    requestSeq,
    name,
    metadata: new Map(),
    correlationId
  });
  assert.deepEqual(Buffer.from(frameworkBytes), Buffer.from(connectorBytes));

  const connectorDecoded = connector.ZlinkStreamHeaderCodec.decode(frameworkBytes);
  assert.equal(connectorDecoded.correlationId, correlationId);

  assert.notEqual((frameworkDecoded.flags & 0x08), 0, 'HasCorrelationId flag must be set');
});

test('MFLOW-011 control packets reject a correlation id', () => {
  assert.throws(() => connector.ZlinkStreamHeaderCodec.encode({
    kind: connector.ZlinkStreamMessageKind.Control,
    codec: connector.ZlinkStreamCodec.Raw,
    flags: connector.ZlinkStreamHeaderFlags.None,
    requestSeq: undefined,
    name: 'ctl',
    metadata: connector.ZlinkStreamMetadataMap.empty,
    correlationId: 'nope'
  }));
});
