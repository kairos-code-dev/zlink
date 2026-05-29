// SPDX-License-Identifier: MPL-2.0

export { requireNative } from '../native/native';
export {
  configCall,
  handlerCall,
  recvNativeError,
  submitNativeError,
} from '../errors/native_errors';
export {
  executeNativeRequest,
} from '../messaging/request_executor';
export { startRequestProgress } from '../messaging/request_progress';
export {
  adoptTopicMessage,
  materializeReceived,
  materializeReceivedInto,
  materializeTopicMessage,
} from '../messaging/message_materializer';
export { validateCString } from '../options/validation';
export { normalizeBufferLike } from '../../contracts/core/buffer_like';
export { wrapRoutingId } from '../../contracts/service/spot/spot_models';
export type { RuntimeContext as Context } from '../core/context';
export type { BufferLike } from '../../contracts/core/buffer_like';
export type { MessageSnapshot } from '../../contracts';
export {
  Message,
  Received,
  RoutingId,
  SubscriptionEvent,
  TopicMessage,
  type MessageLike,
} from '../../contracts';
export {
  BindError,
  BindResult,
  CloseError,
  CloseResult,
  ConfigError,
  ConfigResult,
  ConnectError,
  ConnectResult,
  HandlerError,
  HandlerResult,
  RecvError,
  RecvResult,
  RequestError,
  RequestResult,
  SubmitError,
  SubmitResult,
  ZlinkError,
} from '../../contracts/errors/errors';
export {
  PollEventFlag,
  RecvFlags,
  RidDuplicatePolicy,
  SendFlags,
  SocketType as NativeSocketType,
} from '../../contracts/sockets/socket_constants';
export { SocketOption } from '../options/option_mapping';
export type {
  ActorBindOperation,
  ActorRef,
  ActorUnbindOperation,
  ReplyOperation,
  RequestCallback,
  RequestOperation,
  SendOperation,
  SocketSendReadyHandler,
  StreamPacketHandler,
} from '../../contracts/service';
