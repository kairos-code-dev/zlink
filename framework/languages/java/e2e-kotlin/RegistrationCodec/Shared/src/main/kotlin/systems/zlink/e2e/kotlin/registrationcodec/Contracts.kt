package systems.zlink.e2e.kotlin.registrationcodec

import systems.zlink.framework.handlers.ZLinkPacket

object Contracts {
    const val CHANNEL = "registration.codec.kotlin.api"
    const val AUTO_GROUP = "registration-codec-kotlin-auto"
    const val ATTR_GROUP = "registration-codec-kotlin-attr"
}

@ZLinkPacket("EchoAuto")
class EchoAutoRequest() {
    var value: String = ""
    constructor(value: String) : this() { this.value = value }
}

@ZLinkPacket("EchoAuto")
class EchoAutoCommand() {
    var value: String = ""
    constructor(value: String) : this() { this.value = value }
}

class EchoAttrRequest() {
    var value: String = ""
    constructor(value: String) : this() { this.value = value }
}

class EchoAttrCommand() {
    var value: String = ""
    constructor(value: String) : this() { this.value = value }
}

class EchoManualRequest() {
    var value: String = ""
    constructor(value: String) : this() { this.value = value }
}

class EchoManualCommand() {
    var value: String = ""
    constructor(value: String) : this() { this.value = value }
}

class DiLifecycleRequest() {
    var value: String = ""
    constructor(value: String) : this() { this.value = value }
}

class DiLifecycleReply() {
    var value: String = ""
    var scopedId: Int = 0
    var singletonId: Int = 0
    var disposedCount: Int = 0

    constructor(value: String, scopedId: Int, singletonId: Int, disposedCount: Int) : this() {
        this.value = value
        this.scopedId = scopedId
        this.singletonId = singletonId
        this.disposedCount = disposedCount
    }
}

class JsonEchoRequest() {
    var value: String = ""
    constructor(value: String) : this() { this.value = value }
}

class JsonEchoCommand() {
    var value: String = ""
    constructor(value: String) : this() { this.value = value }
}

class PackedEchoRequest() {
    var value: String = ""
    constructor(value: String) : this() { this.value = value }
}

class PackedEchoReply() {
    var value: String = ""
    constructor(value: String) : this() { this.value = value }
}

class PackedEchoCommand() {
    var value: String = ""
    constructor(value: String) : this() { this.value = value }
}

class EchoReply() {
    var value: String = ""
    var handler: String = ""
    constructor(value: String, handler: String) : this() {
        this.value = value
        this.handler = handler
    }
}

class EvidenceEntry() {
    var marker: String = ""
    var packetName: String = ""
    var value: String = ""

    constructor(marker: String, packetName: String, value: String) : this() {
        this.marker = marker
        this.packetName = packetName
        this.value = value
    }
}

class EvidenceSnapshot() {
    var entries: List<EvidenceEntry> = emptyList()
    constructor(entries: List<EvidenceEntry>) : this() { this.entries = entries }
}

fun isPackedType(type: Class<*>): Boolean =
    type == PackedEchoRequest::class.java ||
        type == PackedEchoReply::class.java ||
        type == PackedEchoCommand::class.java
