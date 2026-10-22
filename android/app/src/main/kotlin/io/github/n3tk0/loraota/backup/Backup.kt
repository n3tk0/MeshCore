package io.github.n3tk0.loraota.backup

import org.json.JSONArray
import org.json.JSONObject

/** What a backup is of: a companion (over Bluetooth) or a repeater (over LoRa, its console). */
enum class DeviceKind { COMPANION, REPEATER }

/** One thing kept: [group] is one of the GROUP_ names, [id] says which (a setting's name, a slot, a key). */
data class BackupItem(val group: String, val id: String, val label: String, val value: String) {
    val ref get() = "$group/$id"

    companion object {
        const val GROUP_KEY = "key"           // the identity: value = the public key (the private one is kept apart)
        const val GROUP_SETTING = "setting"
        const val GROUP_CHANNEL = "channel"   // id = slot, value = hex of name (32) + secret (16)
        const val GROUP_CONTACT = "contact"   // id = public key, value = hex of the contact's frame
        const val KEY_ID = "identity"
    }
}

/**
 * The settings of one device at one time. [privateKey]: the companion's 64-byte identity, only in memory
 * (on the phone it is encrypted, see [BackupStore]).
 */
class Backup(
    val kind: DeviceKind,
    val deviceKey: String,   // the device's public key, hex
    val deviceName: String,
    val time: Long,
    val auto: Boolean,       // made before a flash or an update, not by the user
    val firmware: String,
    val items: List<BackupItem>,
    val privateKey: ByteArray? = null,
    val file: String = "",   // its name on the phone, once kept
) {
    fun count(group: String) = items.count { it.group == group }

    /** Without the private key: what goes in the JSON (the key is added encrypted, one way or another). */
    fun toJson(): JSONObject = JSONObject()
        .put("format", FORMAT)
        .put("kind", kind.name.lowercase())
        .put("device", deviceKey)
        .put("name", deviceName)
        .put("time", time)
        .put("auto", auto)
        .put("firmware", firmware)
        .put("items", JSONArray().also { a ->
            for (i in items) a.put(JSONObject().put("g", i.group).put("id", i.id).put("label", i.label).put("v", i.value))
        })

    fun with(privateKey: ByteArray?, file: String = this.file) =
        Backup(kind, deviceKey, deviceName, time, auto, firmware, items, privateKey, file)

    companion object {
        const val FORMAT = 1

        /** The JSON of [toJson]; the key is the caller's to read. Throws on anything else. */
        fun fromJson(o: JSONObject, file: String = ""): Backup {
            require(o.optInt("format") == FORMAT) { "format" }
            val kind = DeviceKind.valueOf(o.getString("kind").uppercase())
            val a = o.getJSONArray("items")
            val items = (0 until a.length()).map {
                val i = a.getJSONObject(it)
                BackupItem(i.getString("g"), i.getString("id"), i.optString("label"), i.getString("v"))
            }
            return Backup(kind, o.getString("device"), o.optString("name"), o.getLong("time"), o.optBoolean("auto"),
                o.optString("firmware"), items, null, file)
        }
    }
}

/** One difference between a backup and the device now; [current] null: the device does not have it. */
class DiffRow(val item: BackupItem, val current: String?)

object BackupDiff {
    /**
     * What a restore would change. A contact counts only when it is gone (its route and last advert change
     * all the time); a channel or setting when its value differs. Things the device has and the backup
     * does not are left alone.
     */
    fun diff(backup: List<BackupItem>, now: List<BackupItem>): List<DiffRow> {
        val cur = now.associateBy { it.ref }
        return backup.mapNotNull { b ->
            val c = cur[b.ref]
            when {
                b.group == BackupItem.GROUP_CONTACT -> if (c == null) DiffRow(b, null) else null
                c?.value != b.value -> DiffRow(b, c?.value)
                else -> null
            }
        }
    }
}
