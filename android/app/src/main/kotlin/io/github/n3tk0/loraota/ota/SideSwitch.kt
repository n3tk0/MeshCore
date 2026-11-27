package io.github.n3tk0.loraota.ota

import android.content.SharedPreferences
import kotlinx.coroutines.CancellationException

/** How the companion goes to the side channel for an update and back. */
interface SideSwitch {
    /** Onto [radio] (again before every window); [leaseMins]: where the companion can, it goes back by itself after that. */
    suspend fun enter(radio: SideRadio, leaseMins: Int)

    /** Back to the normal channel. */
    suspend fun leave()
}

class SideSwitchFailed(val detail: String) : Exception(detail)

/** Our companion firmware: `tempradio`, which goes back by itself when the lease runs out (the app is gone). */
class TempRadioSwitch(private val cli: suspend (String) -> String) : SideSwitch {
    override suspend fun enter(radio: SideRadio, leaseMins: Int) {
        val r = cli("tempradio ${radio.cliArgs()},$leaseMins")
        if (!r.contains("OK")) throw SideSwitchFailed(r.trim())
    }

    override suspend fun leave() {
        cli("tempradio off")
    }
}

/** The settings a stock companion ran on before the app moved it to the side channel (with its client repeat). */
data class SavedRadio(val radio: SideRadio, val repeat: Int) {
    fun format(): String = "${radio.freqKhz},${radio.bwHz},${radio.sf},${radio.cr},$repeat"

    companion object {
        fun parse(s: String?): SavedRadio? {
            val n = s?.split(',')?.map { it.trim().toIntOrNull() ?: return null } ?: return null
            if (n.size != 5) return null
            val r = SideRadio(n[0], n[1], n[2], n[3])
            // the same limits as the companion's own check of CMD_SET_RADIO_PARAMS
            if (r.freqKhz !in 150_000..2_500_000 || r.bwHz !in 7_000..500_000 || r.sf !in 5..12 || r.cr !in 5..8) return null
            return SavedRadio(r, if (n[4] != 0) 1 else 0)
        }
    }
}

/**
 * Per companion (Bluetooth address): its normal settings while the app has it on the side channel. Kept on the phone,
 * so a connection lost half way is put right the next time the app connects to that companion.
 */
class SavedRadios(private val prefs: SharedPreferences) {
    fun get(address: String): SavedRadio? = if (address.isEmpty()) null else SavedRadio.parse(prefs.getString(address, null))

    fun put(address: String, saved: SavedRadio) {
        if (address.isNotEmpty()) prefs.edit().putString(address, saved.format()).commit()
    }

    fun clear(address: String) {
        if (address.isNotEmpty() && prefs.contains(address)) prefs.edit().remove(address).commit()
    }
}

/**
 * Stock companion firmware has no `tempradio`: the app sets the side channel with the standard radio command and
 * puts the normal settings back. Nothing brings it back by itself, so the normal settings are saved first ([store]),
 * and [leave] or the next connection ([restoreSaved]) restores them. Client repeat is off while on the side channel
 * (the companion refuses it on frequencies that do not allow it).
 */
class StockRadioSwitch(
    private val address: String,
    private val normalNow: suspend () -> SavedRadio,   // read at the first enter(), before the switch
    private val store: SavedRadios,
    private val setRadio: suspend (SideRadio, repeat: Int) -> Unit,
) : SideSwitch {
    private var onSide = false
    private var normal: SavedRadio? = null

    override suspend fun enter(radio: SideRadio, leaseMins: Int) {
        if (onSide) return   // no lease to renew: it stays there until leave()
        val n = normal ?: try {
            normalNow()
        } catch (e: Exception) {
            if (e is CancellationException) throw e
            throw SideSwitchFailed(e.message ?: "")
        }.also { normal = it }
        store.put(address, n)
        try {
            setRadio(radio, 0)
        } catch (e: Exception) {
            if (e is CancellationException) throw e
            // kept saved: a lost reply may mean it did switch; leave() or the next connection sets them again
            throw SideSwitchFailed(e.message ?: "")
        }
        onSide = true
    }

    override suspend fun leave() {
        val n = normal ?: return   // never left its normal settings
        setRadio(n.radio, n.repeat)   // an error keeps the saved settings for the next connection
        onSide = false
        store.clear(address)
    }

    companion object {
        /**
         * After a connection: a companion the app left on the side channel goes back to its saved settings (set again
         * even when they look the same: its client repeat is not in the self info). True when settings were saved.
         */
        suspend fun restoreSaved(address: String, store: SavedRadios, setRadio: suspend (SideRadio, repeat: Int) -> Unit): Boolean {
            val saved = store.get(address) ?: return false
            setRadio(saved.radio, saved.repeat)
            store.clear(address)
            return true
        }
    }
}
