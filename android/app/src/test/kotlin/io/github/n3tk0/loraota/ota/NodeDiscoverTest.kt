package io.github.n3tk0.loraota.ota

import io.github.n3tk0.loraota.ble.Contact
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class NodeDiscoverTest {
    private fun makePush(
        inSnr4: Int,
        rssi: Int,
        outSnr4: Int,
        tag: Int,
        pubKey: ByteArray,
        nodeType: Int = NodeDiscover.ADV_TYPE_REPEATER,
        subType: Int = NodeDiscover.CTL_TYPE_NODE_DISCOVER_RESP,
    ): ByteArray {
        val tagBytes = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(tag).array()
        return byteArrayOf(
            NodeDiscover.PUSH_CONTROL_DATA.toByte(),
            inSnr4.toByte(),
            rssi.toByte(),
            0, // path_len = 0 (zero-hop)
            (subType or (nodeType and 0x0F)).toByte(),
            outSnr4.toByte(),
        ) + tagBytes + pubKey
    }

    @Test
    fun commandEncodesFullKeyAndPrefixRequests() {
        val full = NodeDiscover.command(tag = 0x11223344, prefixOnly = false)
        assertContentEquals(
            byteArrayOf(55, 0x80.toByte(), 0x04, 0x44, 0x33, 0x22, 0x11),
            full,
        )

        val pref = NodeDiscover.command(tag = 0x01020304, prefixOnly = true)
        assertContentEquals(
            byteArrayOf(55, 0x81.toByte(), 0x04, 0x04, 0x03, 0x02, 0x01),
            pref,
        )
    }

    @Test
    fun parsePushFullKeyAndNegativeSnr() {
        val key = ByteArray(32) { (it + 0xA0).toByte() }
        val frame = makePush(
            inSnr4 = 26,      // +6.5 dB
            rssi = -94,       // -94 dBm
            outSnr4 = -18,    // -4.5 dB
            tag = 0xCAFEBABE.toInt(),
            pubKey = key,
        )
        val r = NodeDiscover.parsePush(frame)
        assertNotNull(r)
        assertEquals(NodeDiscover.ADV_TYPE_REPEATER, r.nodeType)
        assertEquals(0xCAFEBABE.toInt(), r.tag)
        assertEquals(6.5, r.inSnr, 0.001)
        assertEquals(-4.5, r.outSnr, 0.001)
        assertEquals(-94, r.rssi)
        assertTrue(r.hasFullKey)
        assertContentEquals(key, r.pubKey)
        assertEquals("A0A1A2A3", r.shortId)
        assertTrue(r.matchesKey(key))
    }

    @Test
    fun parsePushPrefixOnlyKeyMatchesFullKey() {
        val full = ByteArray(32) { (0x10 + it).toByte() }
        val prefix = full.copyOf(8)
        val r = NodeDiscover.parsePush(makePush(inSnr4 = 40, rssi = -60, outSnr4 = 32, tag = 7, pubKey = prefix))
        assertNotNull(r)
        assertFalse(r.hasFullKey)
        assertEquals(8, r.pubKey.size)
        assertTrue(r.matchesKey(full))
        val other = full.copyOf().also { it[7] = (it[7].toInt() xor 1).toByte() }
        assertFalse(r.matchesKey(other))
    }

    @Test
    fun parsePushRejectsInvalidFrames() {
        // wrong push code
        val badCode = makePush(0, -80, 0, 1, ByteArray(8)).also { it[0] = 0x89.toByte() }
        assertNull(NodeDiscover.parsePush(badCode))

        // control frame with another subtype (e.g. DISCOVER_REQ = 0x80)
        val reqEcho = makePush(0, -80, 0, 1, ByteArray(8), subType = NodeDiscover.CTL_TYPE_NODE_DISCOVER_REQ)
        assertNull(NodeDiscover.parsePush(reqEcho))

        // truncated key (< 8 bytes)
        assertNull(NodeDiscover.parsePush(makePush(0, -80, 0, 1, ByteArray(7))))

        // empty
        assertNull(NodeDiscover.parsePush(ByteArray(0)))
    }

    @Test
    fun newRepeaterContactFrameRoundTripsThroughContact() {
        val key = ByteArray(32) { (it * 3 + 1).toByte() }
        val frame = NodeDiscover.newRepeaterContactFrame(key, "  Vitosha-Peak-R1  ")
        assertEquals(1 + 32 + 3 + 64 + 32 + 4 + 8, frame.size)   // gps zero, no lastmod: the companion stamps its own clock
        assertTrue(frame.copyOfRange(136, 144).all { it.toInt() == 0 })
        assertEquals(NodeDiscover.CMD_ADD_UPDATE_CONTACT, frame[0].toInt() and 0xff)

        val contact = Contact(frame)
        assertContentEquals(key, contact.publicKey)
        assertTrue(contact.isRepeater)
        assertEquals(0, contact.hops)
        assertEquals("Vitosha-Peak-R1", contact.name)
    }

    @Test
    fun newRepeaterContactFrameTruncatesUtf8Cleanly() {
        val key = ByteArray(32) { 0x55 }
        // Cyrillic chars are 2 bytes each in UTF-8; 20 chars = 40 bytes -> must truncate <= 31 bytes without broken codepoint
        val longBg = "РетранслаторВитошаВръх"
        val frame = NodeDiscover.newRepeaterContactFrame(key, longBg)
        val nameBytes = frame.copyOfRange(100, 132)
        assertEquals(0, nameBytes[31].toInt())
        val decoded = Contact(frame).name
        assertTrue(decoded.isNotEmpty())
        assertTrue(decoded.toByteArray(Charsets.UTF_8).size <= 31)
        assertFalse(decoded.contains('\uFFFD'))
    }

    @Test
    fun mergeInOrderDeduplicatesAndSortStrongestOrdersByInSnr() {
        val k1 = ByteArray(32) { (0x10 + it).toByte() }
        val k2 = ByteArray(32) { (0x40 + it).toByte() }
        val list = mutableListOf<NodeDiscover.DiscoveredRepeater>()

        NodeDiscover.mergeInOrder(
            list,
            NodeDiscover.DiscoveredRepeater(k1.copyOf(8), hex(k1.copyOf(8)), outSnr = 1.0, inSnr = 2.0, rssi = -100, name = null, inContacts = false),
        )
        NodeDiscover.mergeInOrder(
            list,
            NodeDiscover.DiscoveredRepeater(k2, hex(k2), outSnr = 8.0, inSnr = 9.5, rssi = -72, name = "Peak-2", inContacts = true),
        )
        // Duplicate of k1 arriving with full 32B key and updated SNR keeps its slot (index 0) during listening
        NodeDiscover.mergeInOrder(
            list,
            NodeDiscover.DiscoveredRepeater(k1, hex(k1), outSnr = 3.0, inSnr = 4.5, rssi = -90, name = "Valley-1", inContacts = false),
        )

        assertEquals(2, list.size)
        assertEquals("Valley-1", list[0].name)
        assertTrue(list[0].hasFullKey)
        assertEquals(4.5, list[0].inSnr, 0.001)
        assertEquals("Peak-2", list[1].name)

        val sorted = NodeDiscover.sortStrongest(list)
        assertEquals("Peak-2", sorted[0].name)
        assertEquals("Valley-1", sorted[1].name)
    }

    @Test
    fun ownerRequestIsTypeOwnerWithZeroHopReplyPath() {
        assertContentEquals(byteArrayOf(0x02, 0x00), NodeDiscover.ownerRequestData())
    }

    @Test
    fun parsesOwnerReplyWithPaddingAndMultilineOwner() {
        val clock = byteArrayOf(1, 2, 3, 4)
        val data = clock + "Витоша-R1\nPetko\nSofia".toByteArray(Charsets.UTF_8) + ByteArray(5)
        val (name, owner) = assertNotNull(NodeDiscover.parseOwnerReply(data))
        assertEquals("Витоша-R1", name)
        assertEquals("Petko Sofia", owner)
    }

    @Test
    fun parsesOwnerReplyWithoutOwner() {
        val data = byteArrayOf(0, 0, 0, 0) + "Peak\n".toByteArray()
        val (name, owner) = assertNotNull(NodeDiscover.parseOwnerReply(data))
        assertEquals("Peak", name)
        assertNull(owner)
        assertNull(NodeDiscover.parseOwnerReply(byteArrayOf(0, 0, 0, 0)))
        assertNull(NodeDiscover.parseOwnerReply(byteArrayOf(0, 0, 0, 0, 0, 0)))
    }

    @Test
    fun mergeKeepsOwner() {
        val key = ByteArray(32) { 7 }
        val hexKey = key.joinToString("") { "%02x".format(it) }
        val list = mutableListOf(
            NodeDiscover.DiscoveredRepeater(key, hexKey, 1.0, 1.0, -90, "A", false, owner = "Petko"),
        )
        NodeDiscover.mergeInOrder(list, NodeDiscover.DiscoveredRepeater(key, hexKey, 2.0, 2.0, -80, null, false))
        assertEquals(1, list.size)
        assertEquals("Petko", list[0].owner)
        assertEquals("A", list[0].name)
    }
}
