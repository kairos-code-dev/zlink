package systems.zlink.samples.kotlin.bingo.server.api.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.samples.kotlin.bingo.server.api.BingoPlayerRecordStore
import systems.zlink.samples.kotlin.bingo.shared.contracts.ReportBingoResultReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.ReportBingoResultRes

@ZLinkHandlerGroup("api")
class ReportBingoResultHandler(private val records: BingoPlayerRecordStore) :
    ZLinkSuspendingRequestHandler<ReportBingoResultReq, ReportBingoResultRes> {
    override suspend fun handle(
        request: ReportBingoResultReq,
        context: ZLinkRequestContext,
    ): ReportBingoResultRes {
        val record = records.report(request.actorId, request.won)
        return ReportBingoResultRes(record.actorId, record.wins, record.losses)
    }
}
