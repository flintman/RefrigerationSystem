package com.flintmancomputers.tech_tool

import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.flintmancomputers.tech_tool.network.NetworkClient
import com.flintmancomputers.tech_tool.network.SystemStatus
import com.flintmancomputers.tech_tool.network.fetchSystemStatusWithError
import com.flintmancomputers.tech_tool.ui.theme.FlintmansTechToolTheme
import com.flintmancomputers.tech_tool.ui.theme.OnlineGreen
import com.google.accompanist.swiperefresh.SwipeRefresh
import com.google.accompanist.swiperefresh.rememberSwipeRefreshState
import kotlinx.coroutines.launch
import org.json.JSONObject

class StatusActivity : ComponentActivity() {
    companion object {
        const val EXTRA_API_ADDRESS = "apiAddress"
        const val EXTRA_API_PORT = "apiPort"
        const val EXTRA_API_KEY = "apiKey"
        const val EXTRA_UNIT_ID = "unitId"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val apiAddress = intent.getStringExtra(EXTRA_API_ADDRESS)
        val apiPort = intent.getIntExtra(EXTRA_API_PORT, 80)
        val apiKey = intent.getStringExtra(EXTRA_API_KEY)
        val unitId = intent.getStringExtra(EXTRA_UNIT_ID)

        if (apiAddress == null) {
            finish()
            return
        }

        setContent {
            // Use the app theme so this screen follows system dark/light mode
            FlintmansTechToolTheme {
                StatusScreen(apiAddress = apiAddress, apiPort = apiPort, apiKey = apiKey, unitId = unitId)
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun StatusScreen(apiAddress: String, apiPort: Int, apiKey: String?, unitId: String?) {
    val context = LocalContext.current
    var statusJson by remember { mutableStateOf<JSONObject?>(null) }
    var systemInfoJson by remember { mutableStateOf<JSONObject?>(null) }
    var online by remember { mutableStateOf(false) }
    var loading by remember { mutableStateOf(false) }

    val fetchFailedFmt = stringResource(R.string.fetch_failed)
    val coroutineScope = rememberCoroutineScope()

    var lastDiagnostic by remember { mutableStateOf<String?>(null) }
    var lastRefreshMillis by remember { mutableStateOf<Long?>(null) }

    fun formatTime(millis: Long?): String {
        return millis?.let {
            java.time.format.DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss")
                .withZone(java.time.ZoneId.systemDefault())
                .format(java.time.Instant.ofEpochMilli(it))
        } ?: "--"
    }

    fun refresh() {
        coroutineScope.launch {
            loading = true
            val pair = fetchSystemStatusWithError(apiAddress, apiPort, apiKey)
            val s = pair.first
            val diag = pair.second
            online = s != null
            statusJson = s
            lastDiagnostic = diag
            if (s != null) lastRefreshMillis = System.currentTimeMillis()
            if (!online && diag != null) {
                Toast.makeText(context, String.format(fetchFailedFmt, diag), Toast.LENGTH_LONG).show()
            }
            systemInfoJson = NetworkClient.getSystemInfo(apiAddress, apiPort, apiKey)
            loading = false
        }
    }

    LaunchedEffect(apiAddress, apiPort, apiKey) { refresh() }

    @Composable
    fun mapStatus(s: String?): String {
        return when (s?.lowercase()?.trim()) {
            null, "", "null" -> stringResource(R.string.status_null)
            "cooling" -> stringResource(R.string.status_cooling)
            "heating", "heading" -> stringResource(R.string.status_heating)
            "defrost" -> stringResource(R.string.status_defrost)
            "alarm" -> stringResource(R.string.status_alarm)
            else -> s.replaceFirstChar { if (it.isLowerCase()) it.titlecase() else it.toString() }
        }
    }

    @Composable
    fun statusColorFor(s: String?): Color {
        val colors = MaterialTheme.colorScheme
        return when (s?.lowercase()) {
            "cooling" -> colors.primary
            "heating" -> colors.secondary
            "defrost" -> colors.secondary
            "alarm", "shutdown" -> colors.error
            else -> colors.onSurfaceVariant
        }
    }

    MaterialTheme {
        Scaffold(topBar = { TopAppBar(title = { Text(stringResource(R.string.unit_status_title)) }) }) { padding ->
            val swipeState = rememberSwipeRefreshState(isRefreshing = loading)

            SwipeRefresh(
                state = swipeState,
                onRefresh = { refresh() },
                modifier = Modifier
                    .padding(padding)
                    .padding(12.dp)
                    .fillMaxSize()
            ) {
                // Make content vertically scrollable so pull-to-refresh works on short content.
                Column(modifier = Modifier
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    // Top row: Online + system status and setpoint + last refresh
                    val setpointVal: Double? = statusJson?.optDouble("setpoint")?.takeIf { !it.isNaN() }
                        ?: systemInfoJson?.optString("unit.setpoint")?.toDoubleOrNull()

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column {
                            // show unitId + online dot + online label + system_status (this is the line above the setpoint)
                            val rawStatus = statusJson?.optString("system_status")
                            val friendly = mapStatus(rawStatus)
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Text(text = unitId ?: apiAddress, style = MaterialTheme.typography.titleMedium)
                                Spacer(modifier = Modifier.width(10.dp))
                                val dotColor = if (online) OnlineGreen else MaterialTheme.colorScheme.error
                                Box(modifier = Modifier
                                    .size(10.dp)
                                    .background(color = dotColor, shape = CircleShape))
                                Spacer(modifier = Modifier.width(6.dp))
                                Text(text = if (online) stringResource(R.string.online_label) else stringResource(R.string.offline_label), color = if (online) OnlineGreen else MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodyMedium)
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(text = "-", color = MaterialTheme.colorScheme.onSurfaceVariant)
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(text = friendly, color = statusColorFor(friendly), style = MaterialTheme.typography.bodyMedium)
                            }

                            // determine if an alarm is active later (we check status/info where needed)

                            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                Text(text = stringResource(R.string.setpoint_label) + " ${setpointVal?.let { String.format(java.util.Locale.US, "%.1f°F", it) } ?: "—"}", style = MaterialTheme.typography.bodyMedium)
                            }

                            if (!online && lastDiagnostic != null) {
                                Text(text = String.format(stringResource(R.string.last_response_label), lastDiagnostic), style = MaterialTheme.typography.bodySmall)
                            }
                            Text(text = stringResource(R.string.last_refresh_label) + " " + formatTime(lastRefreshMillis), style = MaterialTheme.typography.bodySmall)
                        }

                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                            Button(onClick = {
                                val intent = android.content.Intent(context, LogActivity::class.java).apply {
                                    putExtra(LogActivity.EXTRA_API_ADDRESS, apiAddress)
                                    putExtra(LogActivity.EXTRA_API_PORT, apiPort)
                                    putExtra(LogActivity.EXTRA_API_KEY, apiKey)
                                }
                                context.startActivity(intent)
                            }) { Text(stringResource(R.string.log_button)) }
                        }
                    }

                    // System info + defrost (moved to appear before Sensors and Relays)
                    Card(modifier = Modifier.fillMaxWidth()) {
                        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text(
                                text = stringResource(R.string.system_info_label),
                                style = MaterialTheme.typography.titleSmall,
                                modifier = Modifier.fillMaxWidth(),
                                textAlign = TextAlign.Center,
                                fontWeight = FontWeight.Bold
                            )
                            val info = systemInfoJson
                            if (info != null) {
                                val defrostTemp = info.optString("defrost.coil_temperature", "").toIntOrNull()
                                val coil = statusJson?.optJSONObject("sensors")?.optDouble("coil_temp", Double.NaN)

                                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                                    Text(stringResource(R.string.defrost_setpoint_label))
                                    Text(defrostTemp?.toString() ?: "—")
                                }
                                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                                    Text(stringResource(R.string.coil_temp_label))
                                    Text(coil?.let { String.format(java.util.Locale.US, "%.1f°F", it) } ?: "—")
                                }
                                if (defrostTemp != null && coil != null && coil < defrostTemp) {
                                    Text(stringResource(R.string.coil_below_defrost), color = MaterialTheme.colorScheme.error)
                                }

                                val alarmWarning = /* prefer status response */ statusJson?.optBoolean("alarm_warning") ?: info.optBoolean("alarm_warning", false)
                                val alarmShutdown = /* prefer status response */ statusJson?.optBoolean("alarm_shutdown") ?: info.optBoolean("alarm_shutdown", false)

                                // collect active alarm codes (prefer statusJson, fall back to system-info)
                                val activeAlarmsArray = statusJson?.optJSONArray("active_alarms") ?: info.optJSONArray("active_alarms")
                                val activeAlarmsList = mutableListOf<String>()
                                if (activeAlarmsArray != null) {
                                    for (i in 0 until activeAlarmsArray.length()) {
                                        val code = activeAlarmsArray.optString(i)
                                        if (!code.isNullOrBlank()) activeAlarmsList.add(code)
                                    }
                                }

                                Row(horizontalArrangement = Arrangement.SpaceBetween, modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                                    Column {
                                        if (alarmShutdown) {
                                            Text(stringResource(R.string.alarm_shutdown), color = MaterialTheme.colorScheme.error)
                                        } else if (alarmWarning) {
                                            Text(stringResource(R.string.alarm_warning), color = MaterialTheme.colorScheme.error)
                                        }

                                        if (activeAlarmsList.isNotEmpty()) {
                                            Text(text = String.format(stringResource(R.string.codes_label), activeAlarmsList.joinToString(", ")), color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
                                        }
                                    }

                                    // Reset button appears next to the alarm info when an alarm is active
                                    if (alarmShutdown || alarmWarning) {
                                        Button(onClick = {
                                            coroutineScope.launch {
                                                val res = NetworkClient.postResetAlarms(apiAddress, apiPort, apiKey)
                                                Toast.makeText(context, if (res != null) context.getString(R.string.reset_alarms) else context.getString(R.string.fetch_failed, ""), Toast.LENGTH_SHORT).show()
                                                refresh()
                                            }
                                        }) { Text(stringResource(R.string.reset_button)) }
                                    }
                                }
                            } else {
                                Text(stringResource(R.string.no_data))
                            }
                        }
                    }

                    // Sensors card (moved to be after System Info)
                    Card(modifier = Modifier.fillMaxWidth()) {
                        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text(
                                text = stringResource(R.string.sensors_label),
                                style = MaterialTheme.typography.titleSmall,
                                modifier = Modifier.fillMaxWidth(),
                                textAlign = TextAlign.Center,
                                fontWeight = FontWeight.Bold
                            )
                            val parsed = statusJson?.let { SystemStatus.fromJson(it) }
                            if (parsed != null) {
                                parsed.sensors.forEach { (k, v) ->
                                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                                        Text(k.replace('_', ' ').replaceFirstChar { it.uppercaseChar() })
                                        Text(String.format(java.util.Locale.US, "%.1f°F", v))
                                    }
                                }
                            } else {
                                Text(stringResource(R.string.no_data))
                            }
                        }
                    }

                    // Relays card (moved to be after Sensors)
                    Card(modifier = Modifier.fillMaxWidth()) {
                        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text(
                                text = stringResource(R.string.relays_label),
                                style = MaterialTheme.typography.titleSmall,
                                modifier = Modifier.fillMaxWidth(),
                                textAlign = TextAlign.Center,
                                fontWeight = FontWeight.Bold
                            )
                            val parsed = statusJson?.let { SystemStatus.fromJson(it) }
                            if (parsed != null) {
                                parsed.relays.forEach { (k, v) ->
                                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                                        Text(k.replace('_', ' ').replaceFirstChar { it.uppercaseChar() })
                                        Text(if (v) stringResource(R.string.relay_on) else stringResource(R.string.relay_off), color = if (v) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant)
                                    }
                                }
                            } else {
                                Text(stringResource(R.string.no_data))
                            }
                        }
                    }

                    // Actions: defrost, setpoint, edit config (removed duplicate reset)
                    val infoForActions = systemInfoJson
                    val defrostTempForActions = infoForActions?.optString("defrost.coil_temperature", "")?.toIntOrNull()
                    val coilForActions = statusJson?.optJSONObject("sensors")?.optDouble("coil_temp", Double.NaN)
                    val coilVal = coilForActions?.takeIf { !it.isNaN() }
                    val defrostEligible = (defrostTempForActions != null && coilVal != null && coilVal < defrostTempForActions)
                    // note: alarm flags are read from statusJson/info where needed instead of storing here

                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        if (defrostEligible) {
                            Button(onClick = {
                                 coroutineScope.launch {
                                     val res = NetworkClient.postTriggerDefrost(apiAddress, apiPort, apiKey)
                                     Toast.makeText(context, if (res != null) context.getString(R.string.trigger_defrost) else context.getString(R.string.fetch_failed, ""), Toast.LENGTH_SHORT).show()
                                     refresh()
                                 }
                             }) { Text(stringResource(R.string.trigger_defrost)) }
                        }

                        var showSetpointDialog by remember { mutableStateOf(false) }
                        var setpointText by remember { mutableStateOf("") }

                        Button(onClick = {
                            // Pre-fill the dialog with the current setpoint (if available)
                            setpointText = setpointVal?.let { String.format(java.util.Locale.US, "%.1f", it) } ?: ""
                            showSetpointDialog = true
                        }) { Text(stringResource(R.string.set_setpoint)) }

                        Button(onClick = {
                             val intent = android.content.Intent(context, ConfigActivity::class.java).apply {
                                 putExtra(ConfigActivity.EXTRA_API_ADDRESS, apiAddress)
                                 putExtra(ConfigActivity.EXTRA_API_PORT, apiPort)
                                 putExtra(ConfigActivity.EXTRA_API_KEY, apiKey)
                             }
                             context.startActivity(intent)
                        }) { Text(stringResource(R.string.edit_config)) }

                        if (showSetpointDialog) {
                            AlertDialog(
                                 onDismissRequest = { showSetpointDialog = false }, confirmButton = {
                                     TextButton(onClick = {
                                         val v = setpointText.toDoubleOrNull()
                                         if (v != null) {
                                             coroutineScope.launch {
                                                 val res = NetworkClient.postSetpoint(apiAddress, apiPort, apiKey, v)
                                                 Toast.makeText(context, if (res != null) context.getString(R.string.setpoint_updated) else context.getString(R.string.fetch_failed, ""), Toast.LENGTH_SHORT).show()
                                                 showSetpointDialog = false
                                                 refresh()
                                             }
                                         } else {
                                            Toast.makeText(context, context.getString(R.string.invalid_setpoint), Toast.LENGTH_SHORT).show()
                                         }
                                     }) { Text(stringResource(R.string.save)) }
                                 },
                                 dismissButton = {
                                     TextButton(onClick = { showSetpointDialog = false }) { Text(stringResource(R.string.cancel)) }
                                 },
                                 title = { Text(stringResource(R.string.set_setpoint)) },
                                 text = {
                                     Column {
                                        OutlinedTextField(value = setpointText, onValueChange = { setpointText = it }, label = { Text(stringResource(R.string.setpoint_label)) })
                                     }
                                 }
                             )
                        }
                    }

                    Spacer(modifier = Modifier.height(8.dp))
                }
            }
        }
    }
}
