import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkMessage,
  ZLinkMessageSerializer,
  ZLinkProviderResolver,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse
} from '../../contracts';
import { ZLinkBackendSpotDispatchEvent } from '../backend/contracts';
import type {
  ZLinkBackendActorJoinInfo,
  ZLinkBackendActorRef,
  ZLinkBackendActorRecvInfo,
  ZLinkBackendSpot
} from '../backend/contracts';
import type { Message } from '../../contracts/Common/Message';
import type { RequestResult } from '@zlink-systems/zlink';
import type {
  ZLinkRemoteActorPacketTarget,
  ZLinkRemoteBoundSessionTarget
} from '../actors';
import type { ZLinkDispatchErrorReporter } from '../channels';
import { ZLinkSpotActorLifecycleDrain } from './spot-actor-lifecycle-drain';
import type { ZLinkActorResponseOptions } from './spot-actor-packet-dispatch';
import {
  ZLinkSpotActorPacketDrain,
  type ZLinkActorDispatchPart
} from './spot-actor-packet-drain';
import { ZLINK_RECV_DONT_WAIT } from './spot-native-flags';
import { ZLinkSpotNativeActorJoinAdmission } from './spot-native-actor-join-admission';
import type { ZLinkRemoteActorJoinActor } from './spot-remote-codec';
import { ZLinkSpotRoutedFrameDispatch } from './spot-routed-frame-dispatch';
import {
  ZLinkSpotSubscriptionDispatch
} from './spot-subscription-dispatch';
import type { ZLinkSpotHandlerRegistration } from './spot-handler-registry';
import type { ZLinkSpotSerialExecutor } from './spot-serial-executor';

const ZLINK_SPOT_DISPATCH_SUBJECT_SPOT = 1;
const ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER = 3;

/**
 * Minimal lifecycle callback surface required by the native join and actor
 * lifecycle drains. User and Entry Spots differ in ownership policy, but both
 * drains only need the callbacks declared here.
 */
interface ZLinkActorJoinAdmissionTarget {
  onActorJoin?(actor: ZLinkActor, request: ZLinkMessage, signal?: AbortSignal): Promise<ZLinkSpotActorJoinResponse>;
  onJoinedActor?(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
  onLeaveActor?(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
  onDisconnectActor?(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
}

interface ZLinkSpotActorJoinDispatchOptions {
  readonly nativeSpot: ZLinkBackendSpot;
  readonly serial: ZLinkSpotSerialExecutor;
  readonly resolveActor: (actorId: string) => ZLinkActor | undefined;
  readonly getTarget: () => ZLinkActorJoinAdmissionTarget;
  readonly defaultAccept: boolean;
  readonly routedActorProvider?: (
    actorId: string,
    actorType: string,
    actorRef?: ZLinkBackendActorRef,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    actorCreateRequest?: Message,
    signal?: AbortSignal
  ) => Promise<ZLinkRemoteActorJoinActor>;
  readonly nativeJoinBoundSessionTargetResolver?: (
    info: ZLinkBackendActorJoinInfo
  ) => ZLinkRemoteBoundSessionTarget | undefined;
  readonly commitRoutedActor?: (actor: ZLinkActor) => Promise<void> | void;
  readonly actorPacketHandler?: (
    actorId: string,
    parts: readonly Message[],
    returnResponse?: boolean,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ) => Promise<unknown>;
  readonly routedBoundSessionReceiver?: (
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly routedBoundSessionResponseReceiver?: (
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    message: unknown,
    replyOptions: ZLinkActorResponseOptions,
    actorPacketTarget?: unknown,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly routedBoundSessionErrorReceiver?: (
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>,
    actorPacketTarget?: unknown,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly actorPacketTargetProvider?: (actorId: string) => ZLinkRemoteActorPacketTarget | undefined;
  readonly bindRemoteActorSession?: (
    actor: ZLinkBackendActorRef,
    sourceNodeRid: RoutingId,
    sourceSessionRid: RoutingId
  ) => void;
  readonly replyActorNoBind?: (
    info: ZLinkBackendActorRecvInfo,
    parts: readonly Message[],
    result: RequestResult
  ) => void;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
}

export class ZLinkSpotActorJoinDispatch {
  private draining = false;
  private redrainRequested = false;
  private readonly nativeSpotRid: string;
  private readonly subscriptions: ZLinkSpotSubscriptionDispatch;
  private readonly actorLifecycleDrain: ZLinkSpotActorLifecycleDrain;
  private readonly actorPacketDrain: ZLinkSpotActorPacketDrain;
  private readonly nativeActorJoinAdmission: ZLinkSpotNativeActorJoinAdmission;
  private readonly routedFrames: ZLinkSpotRoutedFrameDispatch;
  private readonly nativeSpot: ZLinkBackendSpot;

  constructor(options: ZLinkSpotActorJoinDispatchOptions) {
    this.nativeSpot = options.nativeSpot;
    this.nativeSpotRid = String(options.nativeSpot.routingId);
    this.actorLifecycleDrain = new ZLinkSpotActorLifecycleDrain({
      nativeSpot: options.nativeSpot,
      serial: options.serial,
      resolveActor: options.resolveActor,
      getTarget: options.getTarget,
      waitIdle: waitSpotDispatchIdle
    });
    this.actorPacketDrain = new ZLinkSpotActorPacketDrain({
      messageSerializers: options.messageSerializers,
      actorPacketHandler: options.actorPacketHandler,
      bindRemoteActorSession: options.bindRemoteActorSession,
      replyActorNoBind: options.replyActorNoBind,
      waitIdle: waitSpotDispatchIdle
    });
    this.nativeActorJoinAdmission = new ZLinkSpotNativeActorJoinAdmission({
      nativeSpot: options.nativeSpot,
      serial: options.serial,
      resolveActor: options.resolveActor,
      getTarget: options.getTarget,
      defaultAccept: options.defaultAccept,
      routedActorProvider: options.routedActorProvider,
      nativeJoinBoundSessionTargetResolver: options.nativeJoinBoundSessionTargetResolver,
      commitRoutedActor: options.commitRoutedActor,
      messageSerializers: options.messageSerializers,
      dispatchErrors: options.dispatchErrors
    });
    this.routedFrames = new ZLinkSpotRoutedFrameDispatch({
      nativeSpot: options.nativeSpot,
      nativeSpotRid: this.nativeSpotRid,
      serial: options.serial,
      resolveActor: options.resolveActor,
      getTarget: () => options.getTarget() as ZLinkActorJoinAdmissionTarget & ZLinkSpot,
      defaultAccept: options.defaultAccept,
      routedActorProvider: options.routedActorProvider,
      commitRoutedActor: options.commitRoutedActor,
      actorPacketHandler: options.actorPacketHandler,
      routedBoundSessionReceiver: options.routedBoundSessionReceiver,
      routedBoundSessionResponseReceiver: options.routedBoundSessionResponseReceiver,
      routedBoundSessionErrorReceiver: options.routedBoundSessionErrorReceiver,
      actorPacketTargetProvider: options.actorPacketTargetProvider,
      messageSerializers: options.messageSerializers,
      providerResolver: options.providerResolver,
      dispatchErrors: options.dispatchErrors,
      waitIdle: waitSpotDispatchIdle
    });
    this.subscriptions = new ZLinkSpotSubscriptionDispatch({
      nativeSpot: options.nativeSpot,
      serial: options.serial,
      getTarget: () => options.getTarget() as ZLinkSpot,
      messageSerializers: options.messageSerializers,
      providerResolver: options.providerResolver,
      dispatchErrors: options.dispatchErrors,
      waitIdle: waitSpotDispatchIdle
    });
  }

  configureSubscriptions(registrations: readonly ZLinkSpotHandlerRegistration[]): void {
    this.subscriptions.configure(registrations);
    this.routedFrames.configurePacketHandlers(registrations);
  }

  attach(): void {
    if (typeof this.nativeSpot.setDispatchHandler !== 'function') {
      return;
    }
    this.nativeSpot.setDispatchHandler((info) => {
      if (info.event === ZLinkBackendSpotDispatchEvent.ActorJoinReadable) {
        void this.drain();
        return;
      }
      if (info.event === ZLinkBackendSpotDispatchEvent.SubscribeReadable) {
        void this.subscriptions.drain().catch((error) => console.error(error));
        return;
      }
      if (info.event === ZLinkBackendSpotDispatchEvent.ChannelReplyReadable) {
        if (info.subjectKind === ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER &&
            info.subjectHandle !== undefined) {
          this.nativeSpot.drainChannelReply(info.subjectHandle);
          return;
        }
        if (info.subjectKind === ZLINK_SPOT_DISPATCH_SUBJECT_SPOT ||
            info.subjectKind === undefined) {
          this.nativeSpot.drainReply();
        }
        return;
      }
      if (info.event === ZLinkBackendSpotDispatchEvent.RoutedReadable) {
        if (info.routed !== undefined && info.routed !== null) {
          void this.routedFrames.dispatchFromEvent(info.routed);
        }
        return;
      }
      if (info.event === ZLinkBackendSpotDispatchEvent.ActorReadable) {
        void this.actorPacketDrain.drain(info as unknown as {
          recvActorPart(flags?: number): ZLinkActorDispatchPart | null;
        });
        return;
      }
      if (info.event === ZLinkBackendSpotDispatchEvent.ActorLifecycleReadable) {
        void this.actorLifecycleDrain.drain();
      }
    });
  }

  private async drain(): Promise<void> {
    if (this.draining) {
      this.redrainRequested = true;
      return;
    }
    this.draining = true;
    try {
      do {
        this.redrainRequested = false;
        await this.drainAvailableActorJoins();
        // eslint-disable-next-line @typescript-eslint/no-unnecessary-condition
      } while (this.redrainRequested);
    } finally {
      this.draining = false;
    }
  }

  private async drainAvailableActorJoins(): Promise<void> {
    for (;;) {
      const request = this.nativeSpot.recvActorJoin(ZLINK_RECV_DONT_WAIT);
      if (request === null) {
        await waitSpotDispatchIdle();
        return;
      }
      await this.nativeActorJoinAdmission.admit(request);
    }
  }
}

function waitSpotDispatchIdle(): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, 5));
}
