import { randomUUID } from 'node:crypto';
import type {
  ZLinkRawHostPort,
  ZLinkRawMonitorRecord,
  ZLinkRawMonitorPort,
  ZLinkRawRouterPort
} from '../backend/node/node-raw-binding-port';
import { ZLinkNodeRawBindingPort } from '../backend/node/node-raw-binding-port';
import { OperationRegistry, type PendingOperation } from './operation-registry';
import { ServiceLivenessRegistry, type ServiceLivenessTick } from './service-liveness-registry';
import { ServiceMailbox, type ServiceMailboxLimits, type ServiceMailboxRecord } from './service-mailbox';
import {
  ServiceTopologyRegistry,
  type AdmittedServicePeer,
  type PeerAdmissionResult,
  type ServiceNodeDescriptor
} from './service-topology-registry';
import {
  decodeApplicationPayload,
  decodeChannelRequestHeader,
  decodeChannelSendHeader,
  decodeHeader,
  decodeNodeRequestHeader,
  decodeReject,
  decodeReplyHeader,
  decodeRouteMeshAdmission,
  encodeApplicationPayload,
  encodeChannelRequestHeader,
  encodeChannelSendHeader,
  encodeNodeRequestHeader,
  encodeNodeSendHeader,
  encodeReject,
  encodeReplyHeader,
  encodeRouteMeshAdmission,
  M6aServiceWireCommand,
  type ServiceApplicationPayload,
  ServiceWireProtocolError
} from './service-wire-m6a-codec';
import { createServiceWireCodec } from './service-wire-codec';

export type RawServicePumpResult =
  | 'noData'
  | 'infrastructure'
  | 'application'
  | 'protocolError';

export interface RawServiceRequestResult {
  readonly terminalResult: number;
  readonly failureCode: number;
  readonly payload?: ServiceApplicationPayload;
}

export interface RawServiceIngressRecord {
  readonly command: number;
  readonly flags: number;
  readonly sourceRoutingId: string;
  readonly requestSequence?: bigint;
  readonly parts: readonly Buffer[];
}

export type RawServiceIngressHandler = (
  record: RawServiceIngressRecord
) => RawServicePumpResult | undefined;

export interface RawServiceMeshRuntimeOptions {
  readonly descriptor: ServiceNodeDescriptor;
  readonly mailbox?: Partial<ServiceMailboxLimits>;
  readonly probeIntervalMs?: number;
  readonly peerTimeoutMs?: number;
  readonly bindingPort?: { createHost(): ZLinkRawHostPort };
}

const DEFAULT_MAILBOX_LIMITS: ServiceMailboxLimits = {
  applicationMessages: 4_096,
  applicationBytes: 64 * 1024 * 1024,
  infrastructureMessages: 1_024,
  infrastructureBytes: 8 * 1024 * 1024
};
const MONITOR_DISCONNECTED = 0x0200;
const MONITOR_CONNECTION_READY = 0x1000;

const livenessCodec = createServiceWireCodec({
  magic: [0x5a, 0x4d],
  major: 1,
  commands: M6aServiceWireCommand
});

/**
 * RouteMesh M6A runtime built only on the public raw binding package.
 * It owns protocol, admission, mailbox, completion and liveness state.
 */
export class RawServiceMeshRuntime {
  readonly topology: ServiceTopologyRegistry;
  readonly mailbox: ServiceMailbox;
  readonly liveness: ServiceLivenessRegistry;

  private readonly operations = new OperationRegistry<RawServiceRequestResult>();
  private readonly expectedPeers = new Map<string, {
    readonly meshName: string;
    readonly nodeRoutingId: string;
  }>();
  private readonly connectionIds = new Map<string, string>();
  private readonly monitorEvents: ZLinkRawMonitorRecord[] = [];
  private readonly bindingPort: { createHost(): ZLinkRawHostPort };
  private descriptor: ServiceNodeDescriptor;
  private host?: ZLinkRawHostPort;
  private router?: ZLinkRawRouterPort;
  private monitor?: ZLinkRawMonitorPort;
  private nextCorrelation = 1n;
  private serviceIngress?: RawServiceIngressHandler;
  private closed = false;

  constructor(options: RawServiceMeshRuntimeOptions) {
    this.descriptor = options.descriptor;
    this.topology = new ServiceTopologyRegistry(options.descriptor);
    this.mailbox = new ServiceMailbox({ ...DEFAULT_MAILBOX_LIMITS, ...options.mailbox });
    this.liveness = new ServiceLivenessRegistry(options.probeIntervalMs, options.peerTimeoutMs);
    this.bindingPort = options.bindingPort ?? new ZLinkNodeRawBindingPort();
  }

  start(): void {
    if (this.router !== undefined) return;
    if (this.closed) throw new Error('Raw service runtime cannot restart after close.');
    const host = this.bindingPort.createHost();
    try {
      const router = host.createRouter();
      router.setRoutingId(this.descriptor.nodeRoutingId);
      router.bind(this.descriptor.advertisedEndpoint);
      const next = {
        ...this.descriptor,
        descriptorRevision: this.descriptor.descriptorRevision + 1n,
        state: 'serving' as const
      };
      this.topology.publishLocal(next);
      this.descriptor = next;
      this.monitor = router.monitor(event => this.monitorEvents.push(event));
      this.host = host;
      this.router = router;
    } catch (error) {
      host.close();
      throw error;
    }
  }

  connectPeer(endpoint: string, expected: ServiceNodeDescriptor): void {
    this.requireStarted().connectToRoutingId(expected.nodeRoutingId, endpoint);
    this.expectedPeers.set(expected.nodeRoutingId, expected);
  }

  connectPeerByRoutingId(endpoint: string, nodeRoutingId: string): void {
    this.requireStarted().connectToRoutingId(nodeRoutingId, endpoint);
    this.expectedPeers.set(nodeRoutingId, {
      meshName: this.topology.localDescriptor().meshName,
      nodeRoutingId
    });
  }

  disconnectPeer(endpoint: string, nodeRoutingId: string): void {
    this.requireStarted().disconnect(endpoint);
    const current = this.topology.peer(nodeRoutingId);
    if (current !== undefined) this.removePeer(current);
  }

  announcePeer(nodeRoutingId: string): boolean {
    if (!this.expectedPeers.has(nodeRoutingId)) return false;
    return this.trySend(
      nodeRoutingId,
      [encodeRouteMeshAdmission(M6aServiceWireCommand.hello, this.topology.localDescriptor())]
    );
  }

  announceExpectedPeers(): number {
    let accepted = 0;
    for (const nodeRoutingId of this.expectedPeers.keys()) {
      if (this.announcePeer(nodeRoutingId)) accepted++;
    }
    return accepted;
  }

  updateLocalWeights(options: {
    readonly placementWeight?: number;
    readonly channelName?: string;
    readonly channelWeight?: number;
  }): void {
    const current = this.topology.localDescriptor();
    const channels = options.channelName === undefined
      ? current.channels
      : current.channels.map(channel =>
          channel.name === options.channelName
            ? { ...channel, weight: options.channelWeight! }
            : channel);
    const next = {
      ...current,
      descriptorRevision: current.descriptorRevision + 1n,
      placementWeight: options.placementWeight ?? current.placementWeight,
      channels
    };
    this.topology.publishLocal(next);
    this.descriptor = next;
    this.announceExpectedPeers();
  }

  sendToNode(targetNodeRoutingId: string, payload: ServiceApplicationPayload): boolean {
    return this.trySend(
      targetNodeRoutingId,
      [encodeNodeSendHeader(), encodeApplicationPayload(payload)]
    );
  }

  sendToChannel(channelName: string, payload: ServiceApplicationPayload): boolean {
    const selected = this.topology.selectChannel(channelName);
    return selected !== undefined && this.trySend(
      selected.descriptor.nodeRoutingId,
      [encodeChannelSendHeader(channelName), encodeApplicationPayload(payload)]
    );
  }

  requestToNode(
    targetNodeRoutingId: string,
    payload: ServiceApplicationPayload,
    timeoutMs: number
  ): PendingOperation<RawServiceRequestResult> {
    return this.requestToTarget(targetNodeRoutingId, payload, timeoutMs);
  }

  requestToChannel(
    channelName: string,
    payload: ServiceApplicationPayload,
    timeoutMs: number
  ): PendingOperation<RawServiceRequestResult> | undefined {
    const selected = this.topology.selectChannel(channelName);
    return selected === undefined
      ? undefined
      : this.requestToTarget(selected.descriptor.nodeRoutingId, payload, timeoutMs, channelName);
  }

  setServiceIngress(handler: RawServiceIngressHandler): void {
    if (this.serviceIngress !== undefined && this.serviceIngress !== handler) {
      throw new Error('Raw service ingress is already registered.');
    }
    this.serviceIngress = handler;
  }

  sendService(targetNodeRoutingId: string, parts: readonly Uint8Array[]): boolean {
    return this.trySend(targetNodeRoutingId, parts);
  }

  requestService(
    targetNodeRoutingId: string,
    parts: readonly Uint8Array[],
    timeoutMs: number
  ): Promise<readonly Buffer[]> {
    return this.requireStarted().request(targetNodeRoutingId, parts, timeoutMs);
  }

  replyService(
    record: Pick<RawServiceIngressRecord, 'sourceRoutingId' | 'requestSequence'>,
    parts: readonly Uint8Array[]
  ): void {
    if (record.requestSequence === undefined) {
      throw new TypeError('Service reply requires a request sequence.');
    }
    this.requireStarted().reply(record.sourceRoutingId, record.requestSequence, parts);
  }

  reply(
    request: ServiceMailboxRecord,
    payload: ServiceApplicationPayload,
    terminalResult = 0,
    failureCode = 0
  ): void {
    if (
      request.sourceRoutingId === undefined
      || request.requestSequence === undefined
      || request.correlation === undefined
    ) {
      throw new TypeError('Reply requires a request mailbox record.');
    }
    this.requireStarted().reply(
      request.sourceRoutingId,
      request.requestSequence,
      [
        encodeReplyHeader(request.correlation, terminalResult, failureCode),
        ...(terminalResult === 0 ? [encodeApplicationPayload(payload)] : [])
      ]
    );
  }

  pumpOne(nowMs = performance.now()): RawServicePumpResult {
    const router = this.requireStarted();
    const received = router.receive(true);
    if (received === undefined) return 'noData';
    if (received.parts.length === 0) return 'protocolError';
    try {
      const header = decodeHeader(received.parts[0]!);
      if (
        header.command === M6aServiceWireCommand.hello
        || header.command === M6aServiceWireCommand.admit
        || header.command === M6aServiceWireCommand.update
      ) {
        if (received.parts.length !== 1) return 'protocolError';
        const descriptor = decodeRouteMeshAdmission(
          received.parts[0]!,
          header.command,
          received.sourceRid
        );
        const expected = this.expectedPeers.get(received.sourceRid);
        if (
          expected !== undefined
          && (
            expected.meshName !== descriptor.meshName
            || expected.nodeRoutingId !== descriptor.nodeRoutingId
          )
        ) {
          router.send(received.sourceRid, [encodeReject(3)], true);
          return 'infrastructure';
        }
        const connectionId = this.currentConnectionId(received.sourceRid);
        const result = this.admitPeer(descriptor, connectionId, nowMs);
        if (result !== 'admitted') {
          router.send(received.sourceRid, [encodeReject(admissionReason(result))], true);
          return 'infrastructure';
        }
        if (header.command === M6aServiceWireCommand.hello) {
          router.send(
            received.sourceRid,
            [encodeRouteMeshAdmission(M6aServiceWireCommand.admit, this.topology.localDescriptor())],
            true
          );
        }
        return 'infrastructure';
      }
      if (header.command === M6aServiceWireCommand.reject) {
        if (received.parts.length !== 1) return 'protocolError';
        decodeReject(received.parts[0]!);
        const current = this.topology.peer(received.sourceRid);
        if (current !== undefined) this.removePeer(current);
        return 'infrastructure';
      }
      const peer = this.topology.peer(received.sourceRid);
      if (peer === undefined) return 'protocolError';
      if (
        header.command === M6aServiceWireCommand.livenessProbe
        || header.command === M6aServiceWireCommand.livenessAck
      ) {
        if (received.parts.length !== 1) return 'protocolError';
        const record = livenessCodec.decodeLivenessRecord(received.parts[0]!);
        if (record.command === M6aServiceWireCommand.livenessProbe) {
          const ack = this.liveness.acknowledgeProbe(
            received.sourceRid,
            peer.connectionId,
            record.probeId
          );
          if (
            ack === undefined
            || !router.send(received.sourceRid, [livenessCodec.encodeLivenessRecord({
              command: M6aServiceWireCommand.livenessAck,
              probeId: record.probeId
            })], true)
          ) {
            return 'protocolError';
          }
        } else {
          this.liveness.acknowledge(
            received.sourceRid,
            peer.connectionId,
            record.probeId,
            nowMs
          );
        }
        return 'infrastructure';
      }
      const stateful = this.serviceIngress?.({
        command: header.command,
        flags: header.flags,
        sourceRoutingId: received.sourceRid,
        ...(received.requestSeq === undefined ? {} : { requestSequence: received.requestSeq }),
        parts: received.parts
      });
      if (stateful !== undefined) return stateful;
      if (
        header.flags !== 0
        || received.parts.length !== 2
        || ![
          M6aServiceWireCommand.nodeSend,
          M6aServiceWireCommand.nodeRequest,
          M6aServiceWireCommand.channelSend,
          M6aServiceWireCommand.channelRequest
        ].includes(header.command as never)
      ) {
        return 'protocolError';
      }
      decodeApplicationPayload(received.parts[1]!);
      let owner: string;
      let correlation: bigint | undefined;
      if (header.command === M6aServiceWireCommand.nodeSend) {
        owner = `node:${this.descriptor.nodeRoutingId}`;
      } else if (header.command === M6aServiceWireCommand.nodeRequest) {
        owner = `node:${this.descriptor.nodeRoutingId}`;
        correlation = decodeNodeRequestHeader(received.parts[0]!);
      } else if (header.command === M6aServiceWireCommand.channelSend) {
        owner = `channel:${decodeChannelSendHeader(received.parts[0]!)}`;
      } else {
        const channel = decodeChannelRequestHeader(received.parts[0]!);
        owner = `channel:${channel.channelName}`;
        correlation = channel.correlation;
      }
      return this.mailbox.tryEnqueue({
        owner,
        domain: 'application',
        parts: received.parts,
        sourceRoutingId: received.sourceRid,
        requestSequence: received.requestSeq,
        ...(correlation === undefined ? {} : { correlation })
      })
        ? 'application'
        : 'protocolError';
    } catch (error) {
      if (error instanceof ServiceWireProtocolError) return 'protocolError';
      throw error;
    }
  }

  tickLiveness(nowMs = performance.now()): ServiceLivenessTick {
    const result = this.liveness.tick(nowMs);
    this.requireStarted();
    for (const probe of result.probes) {
      this.trySend(probe.nodeRoutingId, [livenessCodec.encodeLivenessRecord({
        command: M6aServiceWireCommand.livenessProbe,
        probeId: probe.probeId
      })]);
    }
    for (const nodeRoutingId of result.timedOutNodes) {
      const peer = this.topology.peer(nodeRoutingId);
      if (peer !== undefined) this.topology.disconnect(nodeRoutingId, peer.connectionId);
    }
    return result;
  }

  drainMonitorEvents(nowMs = performance.now()): number {
    let handled = 0;
    for (const event of this.monitorEvents.splice(0)) {
      handled++;
      const nodeRoutingId = event.routingId;
      if (event.event === MONITOR_CONNECTION_READY && nodeRoutingId !== undefined) {
        this.connectionIds.set(nodeRoutingId, randomUUID());
        this.announcePeer(nodeRoutingId);
      } else if (event.event === MONITOR_DISCONNECTED && nodeRoutingId !== undefined) {
        const peer = this.topology.peer(nodeRoutingId);
        if (peer !== undefined) this.removePeer(peer);
        this.connectionIds.delete(nodeRoutingId);
      }
    }
    void nowMs;
    return handled;
  }

  close(): void {
    if (this.closed) return;
    this.closed = true;
    this.mailbox.close();
    this.operations.close('Raw service runtime closed.');
    this.serviceIngress = undefined;
    this.monitor?.close();
    this.monitor = undefined;
    const host = this.host;
    this.router = undefined;
    this.host = undefined;
    if (host !== undefined) host.close();
  }

  private requestToTarget(
    targetNodeRoutingId: string,
    payload: ServiceApplicationPayload,
    timeoutMs: number,
    channelName?: string
  ): PendingOperation<RawServiceRequestResult> {
    const pending = this.operations.reserve(timeoutMs);
    const correlation = this.nextCorrelation++;
    const header = channelName === undefined
      ? encodeNodeRequestHeader(correlation)
      : encodeChannelRequestHeader(correlation, channelName);
    void this.requireStarted().request(
      targetNodeRoutingId,
      [header, encodeApplicationPayload(payload)],
      timeoutMs
    ).then(
      parts => {
        try {
          if (parts.length < 1 || parts.length > 2) throw new ServiceWireProtocolError('Invalid reply parts.');
          const reply = decodeReplyHeader(parts[0]!);
          if (reply.correlation !== correlation) throw new ServiceWireProtocolError('Reply correlation mismatch.');
          if (reply.terminalResult === 0 && parts.length !== 2) {
            throw new ServiceWireProtocolError('Successful reply omits its payload.');
          }
          if (reply.terminalResult !== 0 && parts.length !== 1) {
            throw new ServiceWireProtocolError('Failed reply carries a payload.');
          }
          const result: RawServiceRequestResult = {
            terminalResult: reply.terminalResult,
            failureCode: reply.failureCode
          };
          this.operations.complete(
            pending.id,
            reply.terminalResult === 0
              ? { ...result, payload: decodeApplicationPayload(parts[1]!) }
              : result
          );
        } catch (error) {
          this.operations.fail(pending.id, error);
        }
      },
      error => this.operations.fail(pending.id, error)
    );
    return pending;
  }

  private admitPeer(
    descriptor: ServiceNodeDescriptor,
    connectionId: string,
    nowMs: number
  ): PeerAdmissionResult {
    const result = this.topology.admit(descriptor, connectionId);
    if (result === 'admitted') {
      this.liveness.admit(descriptor.nodeRoutingId, connectionId, nowMs);
    }
    return result;
  }

  private currentConnectionId(nodeRoutingId: string): string {
    let connectionId = this.connectionIds.get(nodeRoutingId);
    if (connectionId === undefined) {
      connectionId = randomUUID();
      this.connectionIds.set(nodeRoutingId, connectionId);
    }
    return connectionId;
  }

  private removePeer(peer: AdmittedServicePeer): void {
    this.topology.disconnect(peer.descriptor.nodeRoutingId, peer.connectionId);
    this.liveness.disconnect(peer.descriptor.nodeRoutingId, peer.connectionId);
  }

  private requireStarted(): ZLinkRawRouterPort {
    if (this.router === undefined) throw new Error('Raw service runtime is not started.');
    return this.router;
  }

  private trySend(targetNodeRoutingId: string, parts: readonly Uint8Array[]): boolean {
    try {
      return this.requireStarted().send(targetNodeRoutingId, parts, true);
    } catch {
      return false;
    }
  }
}

function admissionReason(result: PeerAdmissionResult): number {
  switch (result) {
    case 'meshMismatch':
      return 2;
    case 'staleDescriptor':
      return 7;
    case 'invalidDescriptor':
      return 11;
    case 'admitted':
      return 1;
  }
}
