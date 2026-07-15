import type {
  ZLinkMessage,
  ZLinkSession,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext,
  ZLinkSessionFactory
} from '@zlink-systems/framework';
import {
  AnnounceWorldHandler,
  NodeDiagnosticsHandler,
  SetMaintenanceHandler,
  WatchNodesHandler
} from './ops-handlers';

class OpsSession implements ZLinkSession {
  constructor(readonly context: ZLinkSessionContext) {
    context.handlers.addHandler(WatchNodesHandler);
    context.handlers.addHandler(AnnounceWorldHandler);
    context.handlers.addHandler(SetMaintenanceHandler);
    context.handlers.addHandler(NodeDiagnosticsHandler);
  }

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void> {
    if (!await this.context.handlers.tryHandle(dispatch, payload)) {
      throw new Error(`Unsupported ops packet '${dispatch.packetName}'.`);
    }
  }
}

class OpsSessionFactory implements ZLinkSessionFactory<OpsSession> {
  async create(context: ZLinkSessionContext): Promise<OpsSession> {
    return new OpsSession(context);
  }
}

export { OpsSession, OpsSessionFactory };
