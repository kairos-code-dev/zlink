import type { ActorRef, ZLinkSessionActor } from '../../contracts';
import type { ZLinkBackendSpotNode } from '../backend';
import type { DefaultZLinkSessionActor } from './session-context';
import type { ZLinkBoundSessionResponseTarget } from './bound-session-response-target';

export interface ZLinkStreamActorLookupPort {
  find(actorId: string): DefaultZLinkSessionActor | undefined;
}

export interface ZLinkStreamActorLifecyclePort {
  rebindActor(actorRef: ActorRef, signal?: AbortSignal): Promise<void>;
  refreshActor(actorRef: ActorRef, signal?: AbortSignal): Promise<void>;
  unbindActor(actorId: string): void;
}

export interface ZLinkBoundSessionResponsePort {
  captureBoundSessionResponseTarget(actor: ZLinkSessionActor): ZLinkBoundSessionResponseTarget | undefined;
  sendLocalBoundSessionResponse(
    actorId: string, packetName: string, requestSeq: bigint, message: unknown,
    metadata: ReadonlyMap<string, string>, compressPayload: boolean
  ): boolean;
  sendLocalBoundSessionError(
    actorId: string, packetName: string, requestSeq: bigint, error: unknown,
    metadata: ReadonlyMap<string, string>
  ): boolean;
}

export interface ZLinkRemoteBoundSessionPort extends ZLinkStreamActorLifecyclePort {
  sendLocalBoundSession(
    actorId: string, message: unknown, packetName: string | undefined,
    metadata: ReadonlyMap<string, string>
  ): boolean;
  sendLocalBoundSessionResponse(
    actorId: string, packetName: string, requestSeq: bigint, message: unknown,
    metadata: ReadonlyMap<string, string>, compressPayload: boolean
  ): boolean;
  sendLocalBoundSessionError(
    actorId: string, packetName: string, requestSeq: bigint, error: unknown,
    metadata: ReadonlyMap<string, string>
  ): boolean;
  sendNativeBoundSessionResponse(
    node: ZLinkBackendSpotNode, actorRef: ActorRef, packetName: string, requestSeq: bigint,
    message: unknown, metadata: ReadonlyMap<string, string>, compressPayload: boolean,
    signal?: AbortSignal
  ): Promise<void>;
  sendNativeBoundSessionError(
    node: ZLinkBackendSpotNode, actorRef: ActorRef, packetName: string, requestSeq: bigint,
    error: unknown, metadata: ReadonlyMap<string, string>, signal?: AbortSignal
  ): Promise<void>;
}

export interface ZLinkNativeFallbackBoundSessionPort {
  disconnectNativeBoundSession(node: ZLinkBackendSpotNode, actorRef: ActorRef, signal?: AbortSignal): Promise<void>;
  disconnectBoundSession(actorId: string, signal?: AbortSignal): Promise<void>;
  sendLocalBoundSession(
    actorId: string, message: unknown, packetName: string | undefined,
    metadata: ReadonlyMap<string, string>
  ): boolean;
  sendNativeBoundSession(
    node: ZLinkBackendSpotNode, actorRef: ActorRef, message: unknown, packetName: string | undefined,
    metadata: ReadonlyMap<string, string>, signal?: AbortSignal
  ): Promise<void>;
  sendBoundSession(
    actorId: string, message: unknown, packetName: string | undefined,
    metadata: ReadonlyMap<string, string>, signal?: AbortSignal
  ): Promise<void>;
}
