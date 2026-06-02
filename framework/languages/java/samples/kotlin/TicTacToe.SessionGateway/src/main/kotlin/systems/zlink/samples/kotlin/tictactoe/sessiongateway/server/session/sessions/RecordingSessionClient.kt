package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.streams.ZLinkSessionClient
import systems.zlink.framework.streams.ZLinkSessionReplyCall
import systems.zlink.framework.streams.ZLinkSessionSendCall

class RecordingSessionClient : ZLinkSessionClient {
    override fun <TMessage> send(message: TMessage): ZLinkSessionSendCall = RecordingSessionSendCall()
    override fun <TMessage> reply(message: TMessage): ZLinkSessionReplyCall = RecordingSessionReplyCall()
}

private class RecordingSessionSendCall : ZLinkSessionSendCall {
    override fun metadata(key: String, value: String): ZLinkSessionSendCall = this
    override fun packetName(messageName: String): ZLinkSessionSendCall = this
    override fun compress(): ZLinkSessionSendCall = this
    override fun submitAsync(): CompletionStage<Void> = CompletableFuture.completedFuture(null)
}

private class RecordingSessionReplyCall : ZLinkSessionReplyCall {
    override fun metadata(key: String, value: String): ZLinkSessionReplyCall = this
    override fun compress(): ZLinkSessionReplyCall = this
    override fun submitAsync(): CompletionStage<Void> = CompletableFuture.completedFuture(null)
}
