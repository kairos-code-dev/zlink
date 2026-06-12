const { Inject } = require('@nestjs/common');
const { zlinkRequestHandler } = require('../../../../../../packages/nestjs/dist');
const { BingoServiceRegistry } = require('../bingo-registry-module');
const { PacketNames } = require('../../../Shared/Contracts/messages');
import type {
  RegisterServiceReq,
  RegisterServiceRes,
  ResolveServiceReq,
  ResolveServiceRes
} from '../../../Shared/Contracts/messages';
import type { BingoServiceRegistry as BingoServiceRegistryType } from '../bingo-registry-module';

@zlinkRequestHandler('registry', PacketNames.registerServiceReq)
class RegisterServiceHandler {
  constructor(@Inject(BingoServiceRegistry) private readonly registry: BingoServiceRegistryType) {}

  handle(request: RegisterServiceReq): RegisterServiceRes {
    return this.registry.register(request);
  }
}

@zlinkRequestHandler('registry', PacketNames.resolveServiceReq)
class ResolveServiceHandler {
  constructor(@Inject(BingoServiceRegistry) private readonly registry: BingoServiceRegistryType) {}

  handle(request: ResolveServiceReq): ResolveServiceRes {
    return this.registry.resolve(request);
  }
}

export { RegisterServiceHandler, ResolveServiceHandler };
