import type { InjectionToken } from '@nestjs/common';
import { ContextIdFactory, DiscoveryService, ModuleRef } from '@nestjs/core';
import type { Type } from '@zlink-systems/framework';
import type {
  ZLinkChannelOptions,
  ZLinkPublishContext,
  ZLinkRequestContext,
  ZLinkRouteRequestContext,
  ZLinkRouteSendContext,
  ZLinkSendContext
} from '@zlink-systems/framework';
import type {
  ZLinkNestManualHandlerOptions,
  ZLinkNestModuleRegistrationOptions
} from './contracts';
import { loadFramework } from './framework-loader';
import {
  nestHandlerMetadataEntries,
  nestSpotActorHandlerMetadataEntries,
  nestSpotHandlerMetadataEntries,
  nestSpotTimerHandlerMetadataEntries,
  readNestHandlerMetadata,
  readNestSpotActorHandlerMetadata,
  readNestSpotHandlerMetadata,
  readNestSpotTimerHandlerMetadata,
  type ZLinkNestHandlerMetadata,
  type ZLinkNestSpotActorHandlerMetadata,
  type ZLinkNestSpotHandlerMetadata,
  type ZLinkNestSpotTimerHandlerMetadata
} from './handler-metadata';

const framework = loadFramework();

export interface DiscoveredNestProvider {
  readonly handlerKey: InjectionToken;
  readonly handlerName: string;
  readonly token: InjectionToken;
  readonly instance?: Record<string, unknown>;
  readonly metadata: ZLinkNestHandlerMetadata;
}

export interface DiscoveredNestSpotActorProvider {
  readonly handlerKey: Type;
  readonly handlerName: string;
  readonly token: InjectionToken;
  readonly metadata: ZLinkNestSpotActorHandlerMetadata;
}

export interface DiscoveredNestSpotProvider {
  readonly handlerKey: Type;
  readonly handlerName: string;
  readonly token: InjectionToken;
  readonly metadata: ZLinkNestSpotHandlerMetadata;
}

export interface DiscoveredNestSpotTimerProvider {
  readonly handlerKey: Type;
  readonly handlerName: string;
  readonly token: InjectionToken;
  readonly metadata: ZLinkNestSpotTimerHandlerMetadata;
}

export function createDiscoveredRequestHandlers(
  providerRefs: readonly DiscoveredNestProvider[],
  handlerGroups: readonly string[] | undefined,
  moduleRef: ModuleRef
): NonNullable<ZLinkChannelOptions['requestHandlers']> {
  return createDiscoveredHandlerRegistrations(providerRefs, handlerGroups, 'request', (ref, metadata) => ({
    async handle(payload: Buffer, context: ZLinkRequestContext) {
      const result = await invokeDiscoveredHandler(moduleRef, ref, metadata, payload, context);
      return encodeHandlerResult(metadata, result, context);
    }
  }));
}

export function createDiscoveredSendHandlers(
  providerRefs: readonly DiscoveredNestProvider[],
  handlerGroups: readonly string[] | undefined,
  moduleRef: ModuleRef
): NonNullable<NonNullable<ZLinkChannelOptions['routeMesh']>['sendHandlers']> {
  return createDiscoveredHandlerRegistrations(providerRefs, handlerGroups, 'send', (ref, metadata) => ({
    async handle(payload: Buffer, context: ZLinkRouteSendContext) {
      await invokeDiscoveredHandler(moduleRef, ref, metadata, payload, context);
    }
  }));
}

export function createDiscoveredPublishHandlers(
  providerRefs: readonly DiscoveredNestProvider[],
  handlerGroups: readonly string[] | undefined,
  moduleRef: ModuleRef
): NonNullable<ZLinkChannelOptions['publishHandlers']> {
  return createDiscoveredHandlerRegistrations(providerRefs, handlerGroups, 'publish', (ref, metadata) => ({
    async handle(payload: Buffer, context: ZLinkPublishContext) {
      await invokeDiscoveredHandler(moduleRef, ref, metadata, payload, context);
    }
  }));
}

export function createManualRequestHandlers(
  handlerTypes: readonly ZLinkNestManualHandlerOptions[] | undefined,
  moduleRef: ModuleRef
): NonNullable<ZLinkChannelOptions['requestHandlers']> {
  return createManualHandlerRegistrations<ZLinkRequestContext, unknown>(handlerTypes, moduleRef, (result) => result);
}

export function createManualPublishHandlers(
  handlerTypes: readonly ZLinkNestManualHandlerOptions[] | undefined,
  moduleRef: ModuleRef
): NonNullable<ZLinkChannelOptions['publishHandlers']> {
  return createManualHandlerRegistrations<ZLinkPublishContext, void>(handlerTypes, moduleRef, () => undefined);
}

export function createManualSendHandlers(
  handlerTypes: readonly ZLinkNestManualHandlerOptions[] | undefined,
  moduleRef: ModuleRef
): NonNullable<ZLinkChannelOptions['sendHandlers']> {
  return createManualHandlerRegistrations<ZLinkSendContext, void>(handlerTypes, moduleRef, () => undefined);
}

export function createManualRouteSendHandlers(
  handlerTypes: readonly ZLinkNestManualHandlerOptions[] | undefined,
  moduleRef: ModuleRef
): NonNullable<NonNullable<ZLinkChannelOptions['routeMesh']>['sendHandlers']> {
  return createManualHandlerRegistrations<ZLinkRouteSendContext, void>(handlerTypes, moduleRef, () => undefined);
}

export function createManualRouteRequestHandlers(
  handlerTypes: readonly ZLinkNestManualHandlerOptions[] | undefined,
  moduleRef: ModuleRef
): NonNullable<NonNullable<ZLinkChannelOptions['routeMesh']>['requestHandlers']> {
  return createManualHandlerRegistrations<ZLinkRouteRequestContext, unknown>(handlerTypes, moduleRef, (result) => result);
}

type ManualHandlerContext =
  | ZLinkRequestContext
  | ZLinkSendContext
  | ZLinkRouteRequestContext
  | ZLinkRouteSendContext
  | ZLinkPublishContext;

export function createManualHandlerRegistrations<TContext extends ManualHandlerContext, TResult>(
  handlerTypes: readonly ZLinkNestManualHandlerOptions[] | undefined,
  moduleRef: ModuleRef,
  result: (value: unknown) => TResult
): Array<{
  readonly packetName: string;
  readonly handler: {
    handle(payload: Buffer, context: TContext): Promise<TResult>;
  };
}> {
  return (handlerTypes ?? []).map((registration) => ({
    packetName: registration.packetName,
    handler: {
      async handle(payload: Buffer, context: TContext): Promise<TResult> {
        return result(await invokeManualHandler(moduleRef, registration.handlerType, payload, context));
      }
    }
  }));
}

export function createDiscoveredHandlerRegistrations<THandler>(
  providerRefs: readonly DiscoveredNestProvider[],
  handlerGroups: readonly string[] | undefined,
  kind: string,
  createHandler: (ref: DiscoveredNestProvider, metadata: ZLinkNestHandlerMetadata) => THandler
): Array<{ readonly packetName: string; readonly handler: THandler }> {
  const descriptors = createDiscoveredHandlerDescriptors(providerRefs, handlerGroups, kind);

  return descriptors.map(({ ref, metadata }) => ({
    packetName: metadata.packetName,
    handler: createHandler(ref, metadata)
  }));
}

export function createDiscoveredHandlerDescriptors(
  providerRefs: readonly DiscoveredNestProvider[],
  handlerGroups: readonly string[] | undefined,
  kind: string
): Array<{ readonly ref: DiscoveredNestProvider; readonly metadata: ZLinkNestHandlerMetadata }> {
  if ((handlerGroups ?? []).length === 0) {
    return [];
  }

  const groups = new Set(handlerGroups);
  const seen = new Map<string, InjectionToken>();
  const selected: Array<{ readonly ref: DiscoveredNestProvider; readonly metadata: ZLinkNestHandlerMetadata }> = [];
  for (const ref of providerRefs) {
    const metadata = ref.metadata;
    if (metadata.kind !== kind || !groups.has(metadata.groupName)) {
      continue;
    }
    const key = `${metadata.kind}:${metadata.packetName}`;
    const previousType = seen.get(key);
    if (previousType === ref.handlerKey) {
      continue;
    }
    if (previousType !== undefined) {
      throw new framework.ZLinkConfigurationException(
        `Duplicate handler '${metadata.groupName}:${metadata.kind}:${metadata.packetName}'.`
      );
    }
    seen.set(key, ref.handlerKey);
    selected.push({ ref, metadata });
  }
  return selected;
}

export function discoverProviderRefs(discovery: DiscoveryService, moduleRef: ModuleRef): DiscoveredNestProvider[] {
  return discoverDecoratedProviderRefs({
    discovery,
    moduleRef,
    metadataEntries: nestHandlerMetadataEntries(),
    readMetadata: readNestHandlerMetadata,
    metadataKey: (metadata) => `${metadata.groupName}:${metadata.kind}:${metadata.packetName}`,
    createRef: ({ handlerKey, handlerName, token, instance, metadata }) => ({
      handlerKey,
      handlerName,
      token,
      instance,
      metadata
    })
  });
}

export function discoverSpotProviderRefs(discovery: DiscoveryService, moduleRef: ModuleRef): DiscoveredNestSpotProvider[] {
  return discoverDecoratedClassProviderRefs({
    discovery,
    moduleRef,
    metadataEntries: nestSpotHandlerMetadataEntries(),
    readMetadata: readNestSpotHandlerMetadata,
    metadataKey: (metadata) => `${metadata.kind}:${metadata.packetName ?? metadata.topic ?? ''}`,
    invalidClassMessage: 'ZLink SPOT handler decorators must be applied to class providers.',
    createRef: ({ handlerKey, handlerName, token, metadata }) => ({
      handlerKey,
      handlerName,
      token,
      metadata
    })
  });
}

export function discoverSpotActorProviderRefs(discovery: DiscoveryService, moduleRef: ModuleRef): DiscoveredNestSpotActorProvider[] {
  return discoverDecoratedClassProviderRefs({
    discovery,
    moduleRef,
    metadataEntries: nestSpotActorHandlerMetadataEntries(),
    readMetadata: readNestSpotActorHandlerMetadata,
    metadataKey: (metadata) => `${metadata.kind}:${metadata.packetName}`,
    invalidClassMessage: 'ZLink SPOT actor handler decorators must be applied to class providers.',
    createRef: ({ handlerKey, handlerName, token, metadata }) => ({
      handlerKey,
      handlerName,
      token,
      metadata
    })
  });
}

export function discoverSpotTimerProviderRefs(discovery: DiscoveryService, moduleRef: ModuleRef): DiscoveredNestSpotTimerProvider[] {
  return discoverDecoratedClassProviderRefs({
    discovery,
    moduleRef,
    metadataEntries: nestSpotTimerHandlerMetadataEntries(),
    readMetadata: readNestSpotTimerHandlerMetadata,
    metadataKey: (metadata) => metadata.name,
    invalidClassMessage: 'ZLink SPOT timer handler decorators must be applied to class providers.',
    createRef: ({ handlerKey, handlerName, token, metadata }) => ({
      handlerKey,
      handlerName,
      token,
      metadata
    })
  });
}

export interface DecoratedProviderRefInput<TMetadata, THandlerKey extends InjectionToken = InjectionToken> {
  readonly handlerKey: THandlerKey;
  readonly handlerName: string;
  readonly token: InjectionToken;
  readonly instance?: Record<string, unknown>;
  readonly metadata: TMetadata;
}

export interface DecoratedProviderDiscoveryOptions<TMetadata, TRef> {
  readonly discovery: DiscoveryService;
  readonly moduleRef: ModuleRef;
  readonly metadataEntries: readonly (readonly [InjectionToken, readonly TMetadata[]])[];
  readonly readMetadata: (handlerToken: InjectionToken | undefined) => readonly TMetadata[];
  readonly metadataKey: (metadata: TMetadata) => string;
  readonly createRef: (input: DecoratedProviderRefInput<TMetadata>) => TRef;
}

export interface DecoratedClassProviderDiscoveryOptions<TMetadata, TRef> extends Omit<
  DecoratedProviderDiscoveryOptions<TMetadata, TRef>,
  'createRef'
> {
  readonly invalidClassMessage: string;
  readonly createRef: (input: DecoratedProviderRefInput<TMetadata, Type>) => TRef;
}

export function discoverDecoratedProviderRefs<TMetadata, TRef>(
  options: DecoratedProviderDiscoveryOptions<TMetadata, TRef>
): TRef[] {
  const refs: TRef[] = [];
  const seen = new Set<string>();
  for (const wrapper of options.discovery.getProviders()) {
    const token = wrapper.token as InjectionToken | undefined;
    if (token === undefined) {
      continue;
    }
    const instance = wrapper.instance === undefined ? undefined : wrapper.instance as Record<string, unknown>;
    for (const handlerKey of providerMetadataCandidateTokens(wrapper.metatype, instance, token)) {
      appendDiscoveredProviderRefs(refs, seen, {
        handlerKey,
        token,
        instance,
        metadataList: options.readMetadata(handlerKey),
        metadataKey: options.metadataKey,
        createRef: options.createRef
      });
    }
  }

  for (const [handlerKey, metadataList] of options.metadataEntries) {
    const instance = tryGetProviderInstance(options.moduleRef, handlerKey);
    if (instance === undefined) {
      continue;
    }
    appendDiscoveredProviderRefs(refs, seen, {
      handlerKey,
      token: handlerKey,
      instance,
      metadataList,
      metadataKey: options.metadataKey,
      createRef: options.createRef
    });
  }
  return refs;
}

export function discoverDecoratedClassProviderRefs<TMetadata, TRef>(
  options: DecoratedClassProviderDiscoveryOptions<TMetadata, TRef>
): TRef[] {
  return discoverDecoratedProviderRefs({
    ...options,
    createRef: (input) => {
      if (typeof input.handlerKey !== 'function') {
        throw new framework.ZLinkConfigurationException(options.invalidClassMessage);
      }
      return options.createRef({
        ...input,
        handlerKey: input.handlerKey as Type
      });
    }
  });
}

export interface AppendDiscoveredProviderRefsOptions<TMetadata, TRef> {
  readonly handlerKey: InjectionToken;
  readonly token: InjectionToken;
  readonly instance?: Record<string, unknown>;
  readonly metadataList: readonly TMetadata[];
  readonly metadataKey: (metadata: TMetadata) => string;
  readonly createRef: (input: DecoratedProviderRefInput<TMetadata>) => TRef;
}

function appendDiscoveredProviderRefs<TMetadata, TRef>(
  refs: TRef[],
  seen: Set<string>,
  options: AppendDiscoveredProviderRefsOptions<TMetadata, TRef>
): void {
  const handlerName = handlerKeyName(options.handlerKey);
  for (const metadata of options.metadataList) {
    const key = `${String(options.token)}:${handlerName}:${options.metadataKey(metadata)}`;
    if (seen.has(key)) {
      continue;
    }
    seen.add(key);
    refs.push(options.createRef({
      handlerKey: options.handlerKey,
      handlerName,
      token: options.token,
      instance: options.instance,
      metadata
    }));
  }
}

export function providerMetadataCandidateTokens(
  metatype: unknown,
  instance: Record<string, unknown> | undefined,
  token: InjectionToken
): readonly InjectionToken[] {
  return [...new Set([metatype, instance?.constructor, token].filter(isInjectionToken))];
}

export function isInjectionToken(value: unknown): value is InjectionToken {
  return typeof value === 'function' || typeof value === 'string' || typeof value === 'symbol';
}

export function tryGetProviderInstance(moduleRef: ModuleRef, token: InjectionToken): Record<string, unknown> | undefined {
  try {
    return moduleRef.get(token, { strict: false }) as Record<string, unknown>;
  } catch {
    return undefined;
  }
}

async function invokeDiscoveredHandler(
  moduleRef: ModuleRef,
  ref: DiscoveredNestProvider,
  metadata: ZLinkNestHandlerMetadata,
  payload: Buffer,
  context: ZLinkRequestContext | ZLinkSendContext | ZLinkRouteRequestContext | ZLinkRouteSendContext | ZLinkPublishContext
): Promise<unknown> {
  const instance = await resolveHandlerInstance(moduleRef, ref.token, context);
  const methodName = metadata.methodName;
  const method = instance[methodName];
  if (typeof method !== 'function') {
    throw new framework.ZLinkConfigurationException(
      `Discovered handler ${ref.handlerName}.${methodName} is not callable.`
    );
  }
  return await method.call(instance, decodePayload(metadata, payload, context), context);
}

async function invokeManualHandler(
  moduleRef: ModuleRef,
  handlerType: Type,
  payload: Buffer,
  context: ZLinkRequestContext | ZLinkSendContext | ZLinkRouteRequestContext | ZLinkRouteSendContext | ZLinkPublishContext
): Promise<unknown> {
  const instance = await resolveHandlerInstance(moduleRef, handlerType, context);
  const method = instance.handle;
  if (typeof method !== 'function') {
    throw new framework.ZLinkConfigurationException(
      `Manual handler ${handlerType.name}.handle is not callable.`
    );
  }
  return await method.call(instance, decodePayload(undefined, payload, context), context);
}

async function resolveHandlerInstance(
  moduleRef: ModuleRef,
  token: InjectionToken,
  context: ZLinkRequestContext | ZLinkSendContext | ZLinkRouteRequestContext | ZLinkRouteSendContext | ZLinkPublishContext
): Promise<Record<string, unknown>> {
  if (!isInjectionToken(token)) {
    throw new framework.ZLinkConfigurationException('Handler token is not a valid Nest injection token.');
  }
  const contextId = ContextIdFactory.create();
  moduleRef.registerRequestByContextId({ zlinkContext: context }, contextId);
  return await moduleRef.resolve(token, contextId, { strict: false }) as Record<string, unknown>;
}

export function handlerKeyName(handlerKey: InjectionToken): string {
  if (typeof handlerKey === 'function') {
    return handlerKey.name;
  }
  if (typeof handlerKey === 'symbol') {
    return handlerKey.description ?? handlerKey.toString();
  }
  return handlerKey;
}

export function encodeHandlerResult(
  metadata: ZLinkNestHandlerMetadata,
  result: unknown,
  context: ZLinkRequestContext | ZLinkRouteRequestContext
): unknown {
  return metadata.encodeResult === undefined
    ? result
    : metadata.encodeResult(result, context);
}

export function decodePayload(
  metadata: ZLinkNestHandlerMetadata | undefined,
  payload: Buffer | Uint8Array | string | unknown,
  context: ZLinkRequestContext | ZLinkSendContext | ZLinkRouteRequestContext | ZLinkRouteSendContext | ZLinkPublishContext
): unknown {
  if (Buffer.isBuffer(payload) || payload instanceof Uint8Array) {
    if (metadata?.decodePayload !== undefined) {
      return metadata.decodePayload(Buffer.from(payload), context);
    }
    if (context.contentType !== undefined && context.contentType !== 'application/json') {
      return Buffer.from(payload);
    }
    return parseWireJson(Buffer.from(payload).toString());
  }
  if (typeof payload === 'string') {
    return parseWireJson(payload);
  }
  return payload;
}

export function parseWireJson(payload: string): unknown {
  return JSON.parse(payload, (key, value) => {
    if (isPrototypeKey(key)) {
      throw new framework.ZLinkConfigurationException(`NestJS handler JSON key '${key}' is not allowed.`);
    }
    return value;
  });
}

export function isPrototypeKey(key: string): boolean {
  return key === '__proto__' || key === 'constructor' || key === 'prototype';
}

export function hasNestHandlerDiscovery(options: ZLinkNestModuleRegistrationOptions): boolean {
  return hasConfiguredSpotNodes(options.spotNodes)
    || Object.values(options.clientServerChannels ?? {}).some(
      (channel) => (channel.handlerGroups ?? []).length > 0
        || (channel.requestHandlerTypes ?? []).length > 0
        || (channel.sendHandlerTypes ?? []).length > 0
    )
    || Object.values(options.fanoutChannels ?? {}).some(
      (channel) => (channel.handlerGroups ?? []).length > 0
        || (channel.publishHandlerTypes ?? []).length > 0
    )
    || Object.values(options.routerMeshes ?? {}).some(
      (channel) => (channel.handlerGroups ?? []).length > 0
        || (channel.requestHandlerTypes ?? []).length > 0
        || (channel.sendHandlerTypes ?? []).length > 0
    );
}

function hasConfiguredSpotNodes(value: ZLinkNestModuleRegistrationOptions['spotNodes']): boolean {
  if (value === undefined) {
    return false;
  }
  return Array.isArray(value) ? value.length > 0 : Object.keys(value).length > 0;
}
