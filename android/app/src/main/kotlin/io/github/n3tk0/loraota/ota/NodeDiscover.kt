package io.github.n3tk0.loraota.ota

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Zero-hop nearby repeater discovery (`CMD_SEND_CONTROL_DATA` / `PUSH_CODE_CONTROL_DATA` in
 * `examples/companion_radio/MyMesh.cpp` and `CTL_TYPE_NODE_DISCOVER_REQ/RESP` in `docs/payloads.md`).
 *
 * The companion broadcasts a zero-hop `DISCOVER_REQ` control packet with a random 4-byte tag.
 * Repeaters in direct radio range reply after a random backoff with `DISCOVER_RESP` containing
 * their node type, the SNR they heard the request at (`outSnr`), the echoed tag, and their public
 * key (full 32 bytes when `prefixOnly` is false, or an 8-byte prefix). The companion forwards each
 * reply as `PUSH_CODE_CONTROL_DATA` prefixed with the SNR (`inSnr`) and RSSI it received it with.
 */
object NodeDiscover {
    const val CMD_SEND_CONTROL_DATA = 55
    const val CMD_ADD_UPDATE_CONTACT = 9
    const val CMD_SEND_ANON_REQ = 57
    /** Anonymous request a repeater answers without a login: its name and owner info (direct only). */
    const val ANON_REQ_TYPE_OWNER = 0x02
    const val PUSH_CONTROL_DATA = 0x8E
    const val CTL_TYPE_NODE_DISCOVER_REQ = 0x80
    const val CTL_TYPE_NODE_DISCOVER_RESP = 0x90
    const val ADV_TYPE_REPEATER = 2
    const val REPEATER_FILTER = 1 shl ADV_TYPE_REPEATER

    /** Same 10-second listening window as the companion UI (`NEARBY_LISTEN_MILLIS`): repeaters apply random backoff. */
    const val LISTEN_MS = 10_000L

    /** Minimum BLE push frame size: 4-byte push header + 6-byte discover header + 8-byte key prefix. */
    private const val MIN_PUSH_LEN = 4 + 6 + 8
    const val FULL_KEY_LEN = 32
    const val MIN_KEY_LEN = 8
    /**
     * `CMD_ADD_UPDATE_CONTACT` without the optional gps_lat/gps_lon/lastmod tail (1+32+3+64+32+4).
     * Leaving `lastmod` out makes the companion stamp the contact with its own clock; a zero
     * `lastmod` would make it the first contact evicted when the table is full and hide it from
     * apps that sync contacts incrementally (`CMD_GET_CONTACTS` with `since`).
     */
    const val CONTACT_FRAME_LEN = 136
    /** A contact as `CMD_GET_CONTACTS` lists it (the frame with gps and lastmod). */
    const val LISTED_CONTACT_LEN = 148

    private fun le(v: Int): ByteArray = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(v).array()

    /**
     * Builds `[CMD_SEND_CONTROL_DATA][flags][type_filter][tag 4B LE]`.
     * With `prefixOnly = false` (default), repeaters answer with their full 32-byte public key so
     * newly discovered repeaters can be added directly to the companion's contacts.
     */
    fun command(tag: Int, prefixOnly: Boolean = false, typeFilter: Int = REPEATER_FILTER): ByteArray {
        val flags = CTL_TYPE_NODE_DISCOVER_REQ or (if (prefixOnly) 1 else 0)
        return byteArrayOf(CMD_SEND_CONTROL_DATA.toByte(), flags.toByte(), typeFilter.toByte()) + le(tag)
    }

    /**
     * The data of a `CMD_SEND_ANON_REQ` asking a repeater for its name: `[ANON_REQ_TYPE_OWNER][reply_path_len 0]`.
     * The repeater answers only a direct request; reply path length 0 makes it answer zero-hop direct too.
     */
    fun ownerRequestData(): ByteArray = byteArrayOf(ANON_REQ_TYPE_OWNER.toByte(), 0)

    /**
     * Parses the owner answer after its tag (`[clock 4B][name\nowner info]`, maybe zero-padded):
     * (name, owner info or null); null when there is no name.
     */
    fun parseOwnerReply(data: ByteArray): Pair<String, String?>? {
        if (data.size <= 4) return null
        val end = (4 until data.size).firstOrNull { data[it].toInt() == 0 } ?: data.size
        val text = String(data, 4, end - 4, Charsets.UTF_8)
        val nl = text.indexOf('\n')
        val name = (if (nl >= 0) text.substring(0, nl) else text).trim()
        val owner = if (nl >= 0) text.substring(nl + 1).trim().replace('\n', ' ').takeIf { it.isNotEmpty() } else null
        if (name.isEmpty()) return null
        return name to owner
    }

    /** One parsed `DISCOVER_RESP` received via `PUSH_CODE_CONTROL_DATA`. */
    class Reply(
        val nodeType: Int,
        val tag: Int,
        val pubKey: ByteArray,
        val outSnr: Double,
        val inSnr: Double,
        val rssi: Int,
    ) {
        val pubKeyHex: String = hex(pubKey)
        val hasFullKey: Boolean get() = pubKey.size == FULL_KEY_LEN
        val shortId: String get() = pubKeyHex.take(8).uppercase()

        /** True when [fullKey] starts with [pubKey] (exact match for 32B, prefix match for 8B..31B). */
        fun matchesKey(fullKey: ByteArray): Boolean {
            if (pubKey.size < MIN_KEY_LEN || fullKey.size < pubKey.size) return false
            for (i in pubKey.indices) {
                if (fullKey[i] != pubKey[i]) return false
            }
            return true
        }

        override fun equals(other: Any?): Boolean {
            if (this === other) return true
            if (other !is Reply) return false
            return nodeType == other.nodeType && tag == other.tag && pubKey.contentEquals(other.pubKey) &&
                outSnr == other.outSnr && inSnr == other.inSnr && rssi == other.rssi
        }

        override fun hashCode(): Int {
            var r = nodeType
            r = 31 * r + tag
            r = 31 * r + pubKey.contentHashCode()
            r = 31 * r + outSnr.hashCode()
            r = 31 * r + inSnr.hashCode()
            r = 31 * r + rssi
            return r
        }
    }

    /**
     * Parses `PUSH_CODE_CONTROL_DATA`:
     * `[0x8E][in_snr*4][rssi][path_len][flags][out_snr*4][tag 4B LE][pubkey 8..32B]`.
     * Returns null for non-discover control frames or malformed/short frames.
     */
    fun parsePush(frame: ByteArray): Reply? {
        if (frame.size < MIN_PUSH_LEN) return null
        if ((frame[0].toInt() and 0xff) != PUSH_CONTROL_DATA) return null
        val flags = frame[4].toInt() and 0xff
        if ((flags and 0xF0) != CTL_TYPE_NODE_DISCOVER_RESP) return null
        val nodeType = flags and 0x0F
        val inSnr = frame[1].toInt() / 4.0
        val rssi = frame[2].toInt()
        val outSnr = frame[5].toInt() / 4.0
        val tag = ByteBuffer.wrap(frame, 6, 4).order(ByteOrder.LITTLE_ENDIAN).int
        val keyLen = minOf(frame.size - 10, FULL_KEY_LEN)
        if (keyLen < MIN_KEY_LEN) return null
        val pubKey = frame.copyOfRange(10, 10 + keyLen)
        return Reply(nodeType, tag, pubKey, outSnr, inSnr, rssi)
    }

    /**
     * Builds a 136-byte `CMD_ADD_UPDATE_CONTACT` frame for a newly discovered direct repeater
     * (`type = ADV_TYPE_REPEATER`, `out_path_len = 0` for zero-hop direct).
     */
    fun newRepeaterContactFrame(pubKey: ByteArray, name: String): ByteArray {
        require(pubKey.size == FULL_KEY_LEN) { "full 32-byte public key required" }
        val frame = ByteArray(CONTACT_FRAME_LEN)
        frame[0] = CMD_ADD_UPDATE_CONTACT.toByte()
        pubKey.copyInto(frame, 1)
        frame[33] = ADV_TYPE_REPEATER.toByte()
        frame[34] = 0 // flags
        frame[35] = 0 // out_path_len = 0 (direct, zero-hop)
        // [36..99]: 64-byte out_path (zeros)
        // [100..131]: 32-byte null-terminated UTF-8 name (max 31 content bytes)
        // [132..135]: last_advert_timestamp (0: not heard as an advert yet)
        val trimmed = truncateUtf8(name.trim(), 31)
        trimmed.copyInto(frame, 100)
        return frame
    }

    private fun truncateUtf8(text: String, maxBytes: Int): ByteArray {
        val raw = text.toByteArray(Charsets.UTF_8)
        if (raw.size <= maxBytes) return raw
        var end = maxBytes
        // avoid splitting a multi-byte UTF-8 sequence (continuation bytes are 10xxxxxx)
        while (end > 0 && (raw[end].toInt() and 0xC0) == 0x80) end--
        return raw.copyOf(end)
    }

    /** UI model for a repeater heard during zero-hop discovery. */
    class DiscoveredRepeater(
        val pubKey: ByteArray,
        val pubKeyHex: String,
        val outSnr: Double,
        val inSnr: Double,
        val rssi: Int,
        val name: String?,
        val inContacts: Boolean,
        val contactKeyHex: String? = null,
        val contactHops: Int? = null,
        /** The repeater's owner info, from its answer to the name request. */
        val owner: String? = null,
    ) {
        val hasFullKey: Boolean get() = pubKey.size == FULL_KEY_LEN
        val shortId: String get() = pubKeyHex.take(8).uppercase()
        val displayName: String get() = name?.takeIf { it.isNotBlank() } ?: shortId
    }

    /**
     * Inserts or updates [item] in [list] in arrival order (so rows do not jump while listening).
     * Deduplicates when one key is a prefix of the other (same 8-byte prefix).
     */
    fun mergeInOrder(list: MutableList<DiscoveredRepeater>, item: DiscoveredRepeater) {
        val idx = list.indexOfFirst { existing ->
            sameNode(existing.pubKeyHex, item.pubKeyHex)
        }
        if (idx >= 0) {
            val prev = list[idx]
            // prefer keeping full 32B key / resolved name if either has it
            val keepFull = if (item.hasFullKey || !prev.hasFullKey) item.pubKey else prev.pubKey
            val keepHex = if (item.hasFullKey || !prev.hasFullKey) item.pubKeyHex else prev.pubKeyHex
            list[idx] = DiscoveredRepeater(
                pubKey = keepFull,
                pubKeyHex = keepHex,
                outSnr = item.outSnr,
                inSnr = item.inSnr,
                rssi = item.rssi,
                name = item.name ?: prev.name,
                inContacts = item.inContacts || prev.inContacts,
                contactKeyHex = item.contactKeyHex ?: prev.contactKeyHex,
                contactHops = item.contactHops ?: prev.contactHops,
                owner = item.owner ?: prev.owner,
            )
        } else {
            list.add(item)
        }
    }

    fun sameNode(keyHexA: String, keyHexB: String): Boolean {
        val minHex = MIN_KEY_LEN * 2
        if (keyHexA.length < minHex || keyHexB.length < minHex) return false
        return keyHexA.startsWith(keyHexB, ignoreCase = true) || keyHexB.startsWith(keyHexA, ignoreCase = true)
    }

    /** Strongest incoming signal first, then strongest outgoing signal, then name. */
    fun sortStrongest(items: List<DiscoveredRepeater>): List<DiscoveredRepeater> =
        items.sortedWith(
            compareByDescending<DiscoveredRepeater> { it.inSnr }
                .thenByDescending { it.outSnr }
                .thenBy { it.displayName.lowercase() },
        )
}
