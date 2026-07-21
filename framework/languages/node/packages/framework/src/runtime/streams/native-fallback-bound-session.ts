import type { ActorRef, ZLinkBoundSession, ZLinkBoundSessionSendCall, ZLinkSubmitResult } from '../../contracts';
import { ZLinkSpotKind, ZLinkSubmitStatus } from '../../contracts';
import {
  ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
  ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET,
  type ZLinkActorRoutedJoinTransport,
  type ZLinkRemoteActorPacketTarget,
  type ZLinkRemoteBoundSessionTarget
} from '../actors';
import {
  encodeRemoteActorPacketRelayPayload,
  encodeRemoteActorPacketTarget
} from '../actors/actor-packet-relay-wire';
import { encodeRemoteBoundSessionSendPayload } from '../actors/bound-session-wire';
import { tryRequestRoutedJson } from '../actors/actor-routed-json-request';
import {
  ZLinkStreamCodec,
  ZLinkStreamHeaderFlags,
  ZLinkStreamMessageKind,
  encodeStreamHeader,
  type ZLinkStreamFrameHeader
} from './protocol';
import type { ZLinkNativeFallbackBoundSessionPort } from './stream-binding-runtime-ports';
import { throwIfAborted } from '../abort';
import type { ZLinkBackendActorSessionNode } from '../backend';
import { resolveFrameworkPacketName } from '../messaging/packet-name';
import { currentOrCreateFlow } from '../diagnostics/flow-context';

export interface ZLinkNativeFallbackBoundSessionOptions {
  readonly runtime: ZLinkNativeFallbackBoundSessionPort;
  readonly routedTransport: ZLinkActorRoutedJoinTransport;
  readonly actorRefProvider: () => ActorRef | undefined;
  readonly nativeActorNodeProvider: () => ZLinkBackendActorSessionNode | undefined;
  readonly localActorProvider?: () => boolean;
  readonly remoteBoundSessionTargetProvider: () => ZLinkRemoteBoundSessionTarget | undefined;
  readonly remoteActorPacketTargetProvider: () => ZLinkRemoteActorPacketTarget | undefined;
  readonly requestTimeoutMs?: number;
  readonly actorId: string;
  readonly onSend?: (actorId: string, packetName: string) => void;
  readonly reportError: (error: unknown) => void;
  readonly flowCreationEnabled?: () => boolean;
}

export class ZLinkNativeFallbackBoundSession implements ZLinkBoundSession {
  constructor(private readonly options: ZLinkNativeFallbackBoundSessionOptions) {}

  send(message: unknown): ZLinkBoundSessionSendCall {
    return new ZLinkNativeFallbackBoundSessionSendCall(this.options, message);
  }

  async disconnect(signal?: AbortSignal): Promise<void> {
    const remoteTarget = this.options.remoteBoundSessionTargetProvider() ?? this.options.remoteActorPacketTargetProvider();
    if (remoteTarget !== undefined) {
      const spotKind: ZLinkSpotKind | undefined =
        'spotKind' in remoteTarget ? remoteTarget.spotKind as ZLinkSpotKind | undefined : undefined;
      const payload = encodeRemoteActorPacketRelayPayload({
        actorId: this.options.actorId,
        routerChannelId: remoteTarget.routerChannelId,
        header: encodeStreamHeader(disconnectedFrameHeader()),
        payload: Buffer.alloc(0)
      });
      const target = {
        routerChannelId: remoteTarget.routerChannelId,
        targetNodeRid: remoteTarget.targetNodeRid,
        spotRid: remoteTarget.spotRid,
        spotKind: spotKind ?? ZLinkSpotKind.Entry
      };
      if (await tryRequestRoutedJson(
        this.options.routedTransport,
        target,
        payload,
        { timeoutMs: this.options.requestTimeoutMs, signal }
      )) {
        return;
      }
      await this.options.routedTransport.sendToSpot(
        target,
        payload,
        { packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET, signal }
      );
      return;
    }
    const actorRef = this.options.actorRefProvider();
    const node = this.options.nativeActorNodeProvider();
    if (actorRef !== undefined && node !== undefined) {
      await this.options.runtime.disconnectNativeBoundSession(node, actorRef, signal);
      return;
    }
    await this.options.runtime.disconnectBoundSession(this.options.actorId, signal);
  }
}

class ZLinkNativeFallbackBoundSessionSendCall implements ZLinkBoundSessionSendCall {
  private selectedPacketName: string | undefined;
  private readonly selectedMetadata = new Map<string, string>();
  private executed = false;

  constructor(
    private readonly options: ZLinkNativeFallbackBoundSessionOptions,
    private readonly message: unknown
  ) {}

  metadata(key: string, value: string): this {
    this.selectedMetadata.set(key, value);
    return this;
  }

  packetName(packetName: string): this {
    this.selectedPacketName = packetName;
    return this;
  }

  async submit(signal?: AbortSignal): Promise<ZLinkSubmitResult> {
    if (this.executed) {
      throw new Error('Bound session send already submitted.');
    }
    const packetName = resolveFrameworkPacketName(this.message, this.selectedPacketName, 'Bound session');
    this.executed = true;
    throwIfAborted(signal);
    return await this.execute(packetName, signal);
  }

  private async execute(packetName: string, signal?: AbortSignal): Promise<ZLinkSubmitResult> {
    const localActor = this.options.localActorProvider?.() === true;
    const remoteTarget = this.options.remoteBoundSessionTargetProvider();
    if (localActor) {
      const result = await this.options.runtime.submitLocalBoundSession(
        this.options.actorId,
        this.message,
        packetName,
        this.selectedMetadata,
        signal
      );
      if (result.status !== ZLinkSubmitStatus.TargetNotFound) {
        this.traceSubmitted(result, packetName);
        return result;
      }
    }
    if (remoteTarget !== undefined) {
      const actorRef = this.options.actorRefProvider();
      const ownershipGeneration = (actorRef as (ActorRef & { ownershipGeneration?: bigint }) | undefined)
        ?.ownershipGeneration;
      const payload = encodeRemoteBoundSessionSendPayload({
        actorId: this.options.actorId,
        actorNodeRid: actorRef === undefined ? undefined : String(actorRef.nodeRid),
        actorNodeRidHex: (actorRef?.nodeRid as { toHex?: () => string } | undefined)?.toHex?.(),
        actorGeneration: actorRef?.generation.toString(),
        actorOwnershipGeneration: ownershipGeneration?.toString(),
        message: this.message,
        boundPacketName: packetName,
        metadata: this.selectedMetadata,
        actorPacketTarget: encodeRemoteActorPacketTarget(
          this.options.remoteActorPacketTargetProvider()
        ),
        ...(currentOrCreateFlow(
          'Application',
          this.options.flowCreationEnabled?.() ?? true
        ) ?? {})
      });
      const target = {
        routerChannelId: remoteTarget.routerChannelId,
        targetNodeRid: remoteTarget.targetNodeRid,
        spotRid: remoteTarget.spotRid,
        spotKind: ZLinkSpotKind.Entry
      };
      const result = await this.options.routedTransport.sendToSpot(
        target,
        payload,
        { packetName: ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET, signal }
      );
      this.traceSubmitted(result, packetName);
      return result;
    }
    const localResult = await this.options.runtime.submitLocalBoundSession(
      this.options.actorId,
      this.message,
      packetName,
      this.selectedMetadata,
      signal
    );
    if (localResult.status !== ZLinkSubmitStatus.TargetNotFound) {
      this.traceSubmitted(localResult, packetName);
      return localResult;
    }
    const actorRef = this.options.actorRefProvider();
    const node = this.options.nativeActorNodeProvider();
    if (actorRef !== undefined && node !== undefined) {
      const result = await this.options.runtime.sendNativeBoundSession(
        node,
        actorRef,
        this.message,
        packetName,
        this.selectedMetadata,
        signal
      );
      this.traceSubmitted(result, packetName);
      return result;
    }
    if (localActor) {
      throw new Error(`No current session binding exists for actor '${this.options.actorId}'.`);
    }
    const result = await this.options.runtime.sendBoundSession(
      this.options.actorId,
      this.message,
      packetName,
      this.selectedMetadata,
      signal
    );
    this.traceSubmitted(result, packetName);
    return result;
  }

  private traceSubmitted(result: ZLinkSubmitResult, packetName: string): void {
    if (result.status === ZLinkSubmitStatus.Submitted) {
      this.options.onSend?.(this.options.actorId, packetName);
    }
  }
}

function disconnectedFrameHeader(): ZLinkStreamFrameHeader {
  return {
    kind: ZLinkStreamMessageKind.Send,
    codec: ZLinkStreamCodec.Raw,
    flags: ZLinkStreamHeaderFlags.None,
    name: ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET,
    metadata: new Map()
  };
}
