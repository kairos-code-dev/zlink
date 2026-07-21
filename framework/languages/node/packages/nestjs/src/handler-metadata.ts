import type { InjectionToken } from '@nestjs/common';
import type {
  Type,
  ZLinkActor,
  ZLinkEntrySpot,
  ZLinkPublishContext,
  ZLinkRequestContext,
  ZLinkRouteRequestContext,
  ZLinkRouteSendContext,
  ZLinkRuntimeEventPublisher,
  ZLinkSendContext,
  ZLinkSpot,
  ZLinkTimerOptions
} from '@zlink-systems/framework';
import { ZLINK_NEST_HANDLER_GROUP } from './tokens';

export type ZLinkNestHandlerKind = 'request' | 'send' | 'publish';
type ZLinkNestTypeResolver<T> = Type<T> | (() => Type<T>);

export interface ZLinkNestHandlerMetadata {
  readonly groupName: string;
  readonly kind: ZLinkNestHandlerKind;
  readonly packetName: string;
  readonly methodName: string;
  readonly decodePayload?: (
    payload: Buffer,
    context: ZLinkRequestContext | ZLinkSendContext | ZLinkRouteRequestContext | ZLinkRouteSendContext | ZLinkPublishContext
  ) => unknown;
  readonly encodeResult?: (
    result: unknown,
    context: ZLinkRequestContext | ZLinkRouteRequestContext
  ) => unknown;
}

export type ZLinkNestSpotActorHandlerKind =
  | 'spotActorSend'
  | 'spotActorRequest'
  | 'entrySpotActorSend'
  | 'entrySpotActorRequest';

export interface ZLinkNestSpotActorHandlerMetadata {
  readonly kind: ZLinkNestSpotActorHandlerKind;
  readonly packetName: string;
  readonly methodName: string;
  readonly handlerType: Type;
  readonly actor: ZLinkNestTypeResolver<ZLinkActor>;
  readonly spot?: ZLinkNestTypeResolver<ZLinkSpot>;
  readonly entrySpot?: ZLinkNestTypeResolver<ZLinkEntrySpot>;
}

export type ZLinkNestSpotHandlerKind =
  | 'spotPacket'
  | 'spotSubscription'
  | 'entrySpotPacket'
  | 'entrySpotSubscription';

export interface ZLinkNestSpotHandlerMetadata {
  readonly kind: ZLinkNestSpotHandlerKind;
  readonly handlerType: Type;
  readonly packetName?: string;
  readonly channelName?: string;
  readonly topic?: string;
  readonly spot?: ZLinkNestTypeResolver<ZLinkSpot>;
  readonly entrySpot?: ZLinkNestTypeResolver<ZLinkEntrySpot>;
}

export interface ZLinkNestSpotTimerHandlerMetadata {
  readonly handlerType: Type;
  readonly name: string;
  readonly periodMs: number;
  readonly options?: ZLinkTimerOptions;
  readonly spot?: ZLinkNestTypeResolver<ZLinkSpot>;
  readonly entrySpot?: ZLinkNestTypeResolver<ZLinkEntrySpot>;
}

const handlerMetadata = new Map<InjectionToken, readonly ZLinkNestHandlerMetadata[]>();
const spotActorHandlerMetadata = new Map<InjectionToken, readonly ZLinkNestSpotActorHandlerMetadata[]>();
const spotHandlerMetadata = new Map<InjectionToken, readonly ZLinkNestSpotHandlerMetadata[]>();
const spotTimerHandlerMetadata = new Map<InjectionToken, readonly ZLinkNestSpotTimerHandlerMetadata[]>();
const spotTimerHandlerTokens = new Set<unknown>();
const runtimeEventHandlerTokens = new Set<unknown>();
const registeredRuntimeEventHandlers = new WeakMap<ZLinkRuntimeEventPublisher, WeakSet<object>>();

export function appendNestHandlerMetadata(
  handlerToken: InjectionToken,
  metadata: ZLinkNestHandlerMetadata
): void {
  const current = readNestHandlerMetadata(handlerToken);
  handlerMetadata.set(handlerToken, [...current, metadata]);
  if (typeof handlerToken === 'function') {
    Object.defineProperty(handlerToken, ZLINK_NEST_HANDLER_GROUP, {
      configurable: true,
      enumerable: false,
      value: [...current, metadata],
      writable: false
    });
  }
}

export function readNestHandlerMetadata(
  handlerToken: InjectionToken | undefined
): readonly ZLinkNestHandlerMetadata[] {
  if (handlerToken === undefined) {
    return [];
  }
  let reflected: readonly ZLinkNestHandlerMetadata[] | undefined;
  if (typeof handlerToken === 'function') {
    const metadataBySymbol = handlerToken as unknown as Record<symbol, readonly ZLinkNestHandlerMetadata[]>;
    reflected = metadataBySymbol[ZLINK_NEST_HANDLER_GROUP];
  }
  return handlerMetadata.get(handlerToken) ?? reflected ?? [];
}

export function appendNestSpotActorHandlerMetadata(
  handlerToken: InjectionToken,
  metadata: ZLinkNestSpotActorHandlerMetadata
): void {
  const current = readNestSpotActorHandlerMetadata(handlerToken);
  spotActorHandlerMetadata.set(handlerToken, [...current, metadata]);
}

export function readNestSpotActorHandlerMetadata(
  handlerToken: InjectionToken | undefined
): readonly ZLinkNestSpotActorHandlerMetadata[] {
  return handlerToken === undefined ? [] : spotActorHandlerMetadata.get(handlerToken) ?? [];
}

export function appendNestSpotHandlerMetadata(
  handlerToken: InjectionToken,
  metadata: ZLinkNestSpotHandlerMetadata
): void {
  const current = readNestSpotHandlerMetadata(handlerToken);
  spotHandlerMetadata.set(handlerToken, [...current, metadata]);
}

export function readNestSpotHandlerMetadata(
  handlerToken: InjectionToken | undefined
): readonly ZLinkNestSpotHandlerMetadata[] {
  return handlerToken === undefined ? [] : spotHandlerMetadata.get(handlerToken) ?? [];
}

export function markNestSpotTimerHandler(handlerToken: InjectionToken): void {
  spotTimerHandlerTokens.add(handlerToken);
}

export function appendNestSpotTimerHandlerMetadata(
  handlerToken: InjectionToken,
  metadata: ZLinkNestSpotTimerHandlerMetadata
): void {
  const current = readNestSpotTimerHandlerMetadata(handlerToken);
  spotTimerHandlerMetadata.set(handlerToken, [...current, metadata]);
}

export function readNestSpotTimerHandlerMetadata(
  handlerToken: InjectionToken | undefined
): readonly ZLinkNestSpotTimerHandlerMetadata[] {
  return handlerToken === undefined ? [] : spotTimerHandlerMetadata.get(handlerToken) ?? [];
}

export function hasNestSpotTimerHandlerMetadata(handlerToken: InjectionToken | undefined): boolean {
  return handlerToken !== undefined && spotTimerHandlerTokens.has(handlerToken);
}

export function nestHandlerMetadataEntries() {
  return [...handlerMetadata.entries()];
}

export function nestSpotActorHandlerMetadataEntries() {
  return [...spotActorHandlerMetadata.entries()];
}

export function nestSpotHandlerMetadataEntries() {
  return [...spotHandlerMetadata.entries()];
}

export function nestSpotTimerHandlerMetadataEntries() {
  return [...spotTimerHandlerMetadata.entries()];
}

export function markNestRuntimeEventHandler(handlerToken: InjectionToken): void {
  runtimeEventHandlerTokens.add(handlerToken);
}

export function isNestRuntimeEventHandler(handlerToken: InjectionToken | undefined): boolean {
  return handlerToken !== undefined && runtimeEventHandlerTokens.has(handlerToken);
}

export function claimRuntimeEventHandler(
  publisher: ZLinkRuntimeEventPublisher,
  handler: object
): boolean {
  let registered = registeredRuntimeEventHandlers.get(publisher);
  if (registered === undefined) {
    registered = new WeakSet<object>();
    registeredRuntimeEventHandlers.set(publisher, registered);
  }
  if (registered.has(handler)) {
    return false;
  }
  registered.add(handler);
  return true;
}
