package com.flintmancomputers.tech_tool

import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Info
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.flintmancomputers.tech_tool.network.NetworkClient
import com.flintmancomputers.tech_tool.ui.theme.FlintmansTechToolTheme
import kotlinx.coroutines.launch

class ConfigActivity : ComponentActivity() {
    companion object {
        const val EXTRA_API_ADDRESS = "apiAddress"
        const val EXTRA_API_PORT = "apiPort"
        const val EXTRA_API_KEY = "apiKey"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val apiAddress = intent.getStringExtra(EXTRA_API_ADDRESS)
        val apiPort = intent.getIntExtra(EXTRA_API_PORT, 80)
        val apiKey = intent.getStringExtra(EXTRA_API_KEY)

        if (apiAddress == null) {
            finish()
            return
        }

        setContent {
            FlintmansTechToolTheme {
                ConfigScreen(apiAddress = apiAddress, apiPort = apiPort, apiKey = apiKey)
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConfigScreen(apiAddress: String, apiPort: Int, apiKey: String?) {
    val context = LocalContext.current
    val coroutineScope = rememberCoroutineScope()

    var systemInfo by remember { mutableStateOf<Map<String, String>>(emptyMap()) }
    var loading by remember { mutableStateOf(false) }
    // persistent editable values keyed by configuration key
    val edits = remember { mutableStateMapOf<String, String>() }
    var demoMode by remember { mutableStateOf<Boolean?>(false) }
    var demoLoading by remember { mutableStateOf(false) }
    // per-field help dialog visibility
    val showHelp = remember { mutableStateMapOf<String, Boolean>() }

    // short descriptions for each updatable key
    val descriptions = mapOf(
        "compressor.off_timer" to "Compressor off timer in seconds.",
        "debug.code" to "Debug mode flag (0 = off, 1 = verbose).",
        "defrost.coil_temperature" to "Target coil temperature for defrost (°F).",
        "defrost.interval_hours" to "Hours between automatic defrost cycles.",
        "defrost.timeout_mins" to "Maximum defrost duration in minutes.",
        "logging.interval_mins" to "How often to log conditions (minutes).",
        "logging.retention_period" to "Number of days to retain logs.",
        "setpoint.high_limit" to "Maximum allowed temperature setpoint (°F).",
        "setpoint.low_limit" to "Minimum allowed temperature setpoint (°F).",
        "setpoint.offset" to "Temperature offset applied to sensor readings (°F).",
        "unit.electric_heat" to "Electric heating enabled (1 = yes, 0 = no).",
        "unit.fan_continuous" to "Run fan continuously (1 = yes, 0 = no).",
        "unit.number" to "Unit identification number.",
        "unit.relay_active_low" to "Relay active state (1 = active low, 0 = active high).",
        "unit.setpoint" to "Current setpoint (°F) for the unit.",
        "wifi.enable_hotspot" to "Enable WiFi hotspot (1 = yes, 0 = no).",
        "wifi.hotspot_password" to "WiFi hotspot password.",
        "demo_mode" to "When enabled, the unit returns simulated sensor data for testing (true/false)."
    )

    fun load() {
        coroutineScope.launch {
            loading = true
            val info = NetworkClient.getSystemInfo(apiAddress, apiPort, apiKey)
            systemInfo = info?.let { json ->
                val keys = json.keys()
                val map = mutableMapOf<String, String>()
                while (keys.hasNext()) {
                    val k = keys.next()
                    map[k] = json.optString(k)
                }
                map
            } ?: emptyMap()
            // demo mode separate
            val dm = NetworkClient.getDemoMode(apiAddress, apiPort, apiKey)
            demoMode = dm?.optBoolean("demo_mode")
            loading = false
        }
    }

    LaunchedEffect(apiAddress, apiPort, apiKey) { load() }

    // Re-initialize edits whenever systemInfo changes (after a successful update)
    LaunchedEffect(systemInfo) {
        if (systemInfo.isNotEmpty()) {
            val updatable = listOf(
                "compressor.off_timer",
                "debug.code",
                "defrost.coil_temperature",
                "defrost.interval_hours",
                "defrost.timeout_mins",
                "logging.interval_mins",
                "logging.retention_period",
                "setpoint.high_limit",
                "setpoint.low_limit",
                "setpoint.offset",
                "unit.electric_heat",
                "unit.fan_continuous",
                "unit.number",
                "unit.relay_active_low",
                "wifi.enable_hotspot",
                "wifi.hotspot_password"
            )
            edits.clear()
            updatable.forEach { k -> edits[k] = systemInfo[k] ?: "" }
        }
    }

    Scaffold(topBar = { TopAppBar(title = { Text("Edit Configuration") }) }) { padding ->
        Column(modifier = Modifier
            .padding(padding)
            .padding(12.dp)
            .verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {

            if (loading) {
                LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
            }

            // Demo mode toggle (moved to top). Perform immediate API call when toggled and also record in edits.
            Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                Text("Demo Mode")
                Spacer(modifier = Modifier.width(12.dp))
                Switch(checked = demoMode == true, onCheckedChange = { enabled ->
                    // guard concurrent
                    if (demoLoading) return@Switch
                    // optimistic UI update
                    demoMode = enabled
                    edits["demo_mode"] = enabled.toString()
                    // perform immediate API call to enable/disable demo mode
                    coroutineScope.launch {
                        demoLoading = true
                        try {
                            val res = NetworkClient.postDemoMode(apiAddress, apiPort, apiKey, enabled)
                            if (res != null) {
                                Toast.makeText(context, if (enabled) "Demo mode enabled" else "Demo mode disabled", Toast.LENGTH_SHORT).show()
                                // reload to reflect any changes
                                load()
                            } else {
                                Toast.makeText(context, "Failed to change demo mode", Toast.LENGTH_SHORT).show()
                                // revert UI state on failure
                                val dm = NetworkClient.getDemoMode(apiAddress, apiPort, apiKey)
                                demoMode = dm?.optBoolean("demo_mode") ?: false
                                edits["demo_mode"] = demoMode.toString()
                            }
                        } catch (ex: Exception) {
                            Toast.makeText(context, "Error: ${ex.message}", Toast.LENGTH_LONG).show()
                            val dm = NetworkClient.getDemoMode(apiAddress, apiPort, apiKey)
                            demoMode = dm?.optBoolean("demo_mode") ?: false
                            edits["demo_mode"] = demoMode.toString()
                        } finally {
                            demoLoading = false
                        }
                    }
                }, enabled = !demoLoading)
                if (demoLoading) {
                    Spacer(modifier = Modifier.width(8.dp))
                    CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp)
                }
            }
            // Demo info dialog
            if (showHelp["demo_mode"] == true) {
                AlertDialog(onDismissRequest = { showHelp["demo_mode"] = false }, title = { Text("Demo Mode") }, text = { Text("When demo mode is enabled the system returns simulated sensor data for testing.") }, confirmButton = {
                    TextButton(onClick = { showHelp["demo_mode"] = false }) { Text("OK") }
                })
            }

            // Show editable fields for the known updatable keys
            val updatable = listOf(
                "compressor.off_timer",
                "debug.code",
                "defrost.coil_temperature",
                "defrost.interval_hours",
                "defrost.timeout_mins",
                "logging.interval_mins",
                "logging.retention_period",
                "setpoint.high_limit",
                "setpoint.low_limit",
                "setpoint.offset",
                "unit.electric_heat",
                "unit.fan_continuous",
                "unit.number",
                "unit.relay_active_low",
                "wifi.enable_hotspot",
                "wifi.hotspot_password"
            )

            updatable.forEach { key ->
                val value = edits[key] ?: ""
                // label with info icon
                OutlinedTextField(value = value, onValueChange = {
                    edits[key] = it
                }, label = {
                    Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                        Text(key, style = MaterialTheme.typography.bodyMedium)
                        Spacer(modifier = Modifier.width(6.dp))
                        IconButton(onClick = { showHelp[key] = true }, modifier = Modifier.size(20.dp)) {
                            Icon(imageVector = Icons.Default.Info, contentDescription = "Help")
                        }
                    }
                }, modifier = Modifier.fillMaxWidth())

                // per-field help dialog
                if (showHelp[key] == true) {
                    AlertDialog(onDismissRequest = { showHelp[key] = false }, title = { Text(key) }, text = { Text(descriptions[key] ?: "No description available.") }, confirmButton = {
                        TextButton(onClick = { showHelp[key] = false }) { Text("OK") }
                    })
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = { load() }) { Text("Reload") }
                Button(onClick = {
                    coroutineScope.launch {
                        // Filter out empty edits
                        val originalFiltered = edits.filterValues { it.isNotBlank() }
                        // remove demo_mode from config POST (we handle demo via its own endpoint)
                        val filtered = originalFiltered.filterKeys { it != "demo_mode" }

                        if (filtered.isEmpty()) {
                            if (originalFiltered.containsKey("demo_mode")) {
                                // only demo_mode changed; already applied on toggle — ensure state refreshed
                                Toast.makeText(context, "Demo mode change applied", Toast.LENGTH_SHORT).show()
                                edits.remove("demo_mode")
                                load()
                                return@launch
                            }
                            Toast.makeText(context, "No changes to save", Toast.LENGTH_SHORT).show()
                            return@launch
                        }

                        val res = NetworkClient.postConfig(apiAddress, apiPort, apiKey, filtered)
                        if (res != null) {
                            Toast.makeText(context, "Config updated", Toast.LENGTH_SHORT).show()
                            // refresh
                            // remove demo_mode if present (it was handled separately)
                            edits.remove("demo_mode")
                            load()
                        } else {
                            Toast.makeText(context, "Failed to update config", Toast.LENGTH_SHORT).show()
                        }
                    }
                }) { Text("Save") }
            }
        }
    }
}
