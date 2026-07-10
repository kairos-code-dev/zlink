import { Message as BindingMessage } from '@zlink-systems/zlink';
import type { ActorRef, ZLinkBoundSession, ZLinkBoundSessionSendCall } from '../../contracts';
import { ZLinkSpotKind } from '../../contracts';
import type { ZLinkBackendSpotNode } from '../backend';
import {
  ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
  ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET,
  type ZLinkActorRoutedJoinTransport,
  type ZLinkRemoteActorPacketTarget,
  type ZLinkRemoteBoundSessionTarget
} from '../actors';
import {
  ZLinkStreamCodec,
  ZLinkStreamHeaderFlags,
  ZLinkStreamMessageKind,
  encodeStreamHeader,
  type ZLinkStreamFrameHeader
} from './protocol';
import type { ZLinkStreamBindingRuntime } from './index';

export interface ZLinkNativeFallbackBoundSessionOptions {
  readonly runtime: ZLinkStreamBindingRuntime;
  readonly routedTransport: ZLinkActorRoutedJoinTransport;
  readonly nodeProvider: () => ZLinkBackendSpotNode;
  readonly actorRefProvider: () => ActorRef | undefined;
  readonly remoteBoundSessionTargetProvider: () => ZLinkRemoteBoundSessionTarget | undefined;
  readonly remoteActorPacketTargetProvider: () => ZLinkRemoteActorPacketTarget | undefined;
  readonly requestTimeoutMs?: number;
  readonly actorId: string;
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
      const payload = {
        packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
        actorId: this.options.actorId,
        routerChannelId: remoteTarget.routerChannelId,
        header: Buffer.from(encodeStreamHeader(disconnectedFrameHeader())).toString('base64'),
        payload: Buffer.alloc(0).toString('base64')
      };
      if (this.options.routedTransport.requestRawToSpot !== undefined) {
        const rawPayload = BindingMessage.from(Buffer.from(JSON.stringify(payload)));
        try {
          const replyParts = await this.options.routedTransport.requestRawToSpot(
            {
              routerChannelId: remoteTarget.routerChannelId,
              targetNodeRid: remoteTarget.targetNodeRid,
              spotRid: remoteTarget.spotRid,
              spotKind: spotKind ?? ZLinkSpotKind.Entry
            },
            rawPayload,
            {
              timeoutMs: this.options.requestTimeoutMs,
              signal
            }
          );
          for (const part of replyParts) {
            part.close();
          }
        } finally {
          rawPayload.close();
        }
        return;
      }
      await this.options.routedTransport.sendToSpot(
        {
          routerChannelId: remoteTarget.routerChannelId,
          targetNodeRid: remoteTarget.targetNodeRid,
          spotRid: remoteTarget.spotRid,
          spotKind: spotKind ?? ZLinkSpotKind.Entry
        },
        payload,
        { packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET, signal }
      );
      return;
    }
    const actorRef = this.options.actorRefProvider();
    if (actorRef !== undefined) {
      await nextNativeGatewayTurn();
      await this.options.runtime.disconnectNativeBoundSession(this.options.nodeProvider(), actorRef, signal);
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

  async submit(signal?: AbortSignal): Promise<void> {
    if (this.executed) {
      throw new Error('Bound session send already submitted.');
    }
    this.executed = true;
    const remoteTarget = this.options.remoteBoundSessionTargetProvider();
    if (remoteTarget !== undefined) {
      const actorRef = this.options.actorRefProvider();
      const ownershipGeneration = (actorRef as (ActorRef & { ownershipGeneration?: bigint }) | undefined)
        ?.ownershipGeneration;
      const payload = {
        packetName: ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET,
        actorId: this.options.actorId,
        actorNodeRid: actorRef === undefined ? undefined : String(actorRef.nodeRid),
        actorNodeRidHex: (actorRef?.nodeRid as { toHex?: () => string } | undefined)?.toHex?.(),
        actorGeneration: actorRef?.generation.toString(),
        actorOwnershipGeneration: ownershipGeneration?.toString(),
        message: this.message,
        boundPacketName: this.selectedPacketName,
        metadata: Object.fromEntries(this.selectedMetadata)
      };
      if (this.options.routedTransport.requestRawToSpot !== undefined) {
        const rawPayload = BindingMessage.from(Buffer.from(JSON.stringify(payload)));
        try {
          const replyParts = await this.options.routedTransport.requestRawToSpot(
            {
              routerChannelId: remoteTarget.routerChannelId,
              targetNodeRid: remoteTarget.targetNodeRid,
              spotRid: remoteTarget.spotRid,
              spotKind: ZLinkSpotKind.Entry
            },
            rawPayload,
            {
              timeoutMs: this.options.requestTimeoutMs,
              signal
            }
          );
          for (const part of replyParts) {
            part.close();
          }
        } finally {
          rawPayload.close();
        }
        return;
      }
      await this.options.routedTransport.sendToSpot(
        {
          routerChannelId: remoteTarget.routerChannelId,
          targetNodeRid: remoteTarget.targetNodeRid,
          spotRid: remoteTarget.spotRid,
          spotKind: ZLinkSpotKind.Entry
        },
        payload,
        { packetName: ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET, signal }
      );
      return;
    }
    if (this.options.runtime.sendLocalBoundSession(
      this.options.actorId,
      this.message,
      this.selectedPacketName,
      this.selectedMetadata
    )) {
      return;
    }
    const actorRef = this.options.actorRefProvider();
    if (actorRef !== undefined) {
      await nextNativeGatewayTurn();
      await this.options.runtime.sendNativeBoundSession(
        this.options.nodeProvider(),
        actorRef,
        this.message,
        this.selectedPacketName,
        this.selectedMetadata,
        signal
      );
      return;
    }
    await this.options.runtime.sendBoundSession(
      this.options.actorId,
      this.message,
      this.selectedPacketName,
      this.selectedMetadata,
      signal
    );
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

function nextNativeGatewayTurn(): Promise<void> {
  return new Promise((resolve) => setImmediate(resolve));
}
