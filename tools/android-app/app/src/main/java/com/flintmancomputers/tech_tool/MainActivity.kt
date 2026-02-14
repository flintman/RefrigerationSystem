package com.flintmancomputers.tech_tool

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.Network
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.flintmancomputers.tech_tool.network.PollResult
import com.flintmancomputers.tech_tool.network.UnitPoller
import com.flintmancomputers.tech_tool.network.UnitStatusPoller
import com.flintmancomputers.tech_tool.notifications.NotificationHelper
import com.flintmancomputers.tech_tool.notifications.UnitAlarmWorker
import com.flintmancomputers.tech_tool.ui.UnitEditDialog
import com.flintmancomputers.tech_tool.ui.UnitsScreen
import com.flintmancomputers.tech_tool.ui.theme.FlintmansTechToolTheme
import com.flintmancomputers.tech_tool.units.UnitEntity
import com.flintmancomputers.tech_tool.units.UnitsDatabase
import com.flintmancomputers.tech_tool.units.UnitsRepository
import com.flintmancomputers.tech_tool.units.UnitsViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import org.json.JSONObject
import androidx.work.*

@OptIn(ExperimentalMaterial3Api::class)
class MainActivity : ComponentActivity() {
    private lateinit var notifPermissionLauncher: ActivityResultLauncher<String>
    private val poller = UnitStatusPoller()
    private val unitPoller = UnitPoller()
    private val pollJobs = mutableMapOf<Long, Job>()
    // simple debounce: count consecutive failed polls per unit; only show offline toast after threshold
    private val consecutiveFailures = mutableMapOf<Long, Int>()
    private val OFFLINE_TOAST_THRESHOLD = 2
    private var networkCallback: ConnectivityManager.NetworkCallback? = null
    private var connectivityManager: ConnectivityManager? = null

    private companion object {
        const val POLL_INTERVAL_KEY = "alarm_poll_interval_minutes"
        const val DEFAULT_POLL_INTERVAL = 15L
    }

    private fun truncate(s: String?, max: Int = 300): String? {
        return if (s == null || s.length <= max) s else s.take(max) + "..."
    }

    // Convert raw network exception text into a concise, user-friendly message
    private fun friendlyNetworkMessage(raw: String?, maxLen: Int = 200): String? {
        if (raw.isNullOrBlank()) return null
        val lower = raw.lowercase()
        val msg = when {
            "unable to resolve host" in lower || "unknownhostexception" in lower || "cannot resolve" in lower || "nodename nor servname provided" in lower -> "DNS resolution failed"
            "connect timed out" in lower || "connection timed out" in lower -> "Connection timed out"
            "failed to connect" in lower || "connection refused" in lower -> "Connection refused"
            else -> raw
        }
        return if (msg.length > maxLen) msg.take(maxLen) + "..." else msg
    }

    private fun getPollIntervalMinutes(): Long {
        val prefs = getSharedPreferences("settings", Context.MODE_PRIVATE)
        return prefs.getLong(POLL_INTERVAL_KEY, DEFAULT_POLL_INTERVAL)
    }

    private fun setPollIntervalMinutes(minutes: Long) {
        val prefs = getSharedPreferences("settings", Context.MODE_PRIVATE)
        prefs.edit().putLong(POLL_INTERVAL_KEY, minutes).apply()
    }

    private fun scheduleAlarmWorker() {
        val interval = getPollIntervalMinutes()
        val workManager = WorkManager.getInstance(this)
        workManager.cancelUniqueWork("unit_alarm_worker")
        // Only run the worker when network is available to avoid spurious timeouts when offline
        val constraints = Constraints.Builder()
            .setRequiredNetworkType(NetworkType.CONNECTED)
            .build()
        val req = PeriodicWorkRequestBuilder<UnitAlarmWorker>(interval, java.util.concurrent.TimeUnit.MINUTES)
            .setConstraints(constraints)
            .setInitialDelay(interval, java.util.concurrent.TimeUnit.MINUTES)
            .build()
        workManager.enqueueUniquePeriodicWork("unit_alarm_worker", ExistingPeriodicWorkPolicy.REPLACE, req)
    }

    // Helper function to parse JSON response and update state maps
    private fun updateStatusFromJson(
        unitId: Long,
        json: JSONObject?,
        unitStatuses: MutableMap<Long, Boolean>,
        unitSystemStatus: MutableMap<Long, String?>,
        unitReturnTemp: MutableMap<Long, Double?>,
        unitSetpoint: MutableMap<Long, Double?>
    ) {
        if (json == null) {
            unitSystemStatus[unitId] = null
            unitReturnTemp[unitId] = null
            unitSetpoint[unitId] = null
            return
        }

        try {
            unitSystemStatus[unitId] = json.optString("system_status").takeIf { it.isNotBlank() }
            val sensors = json.optJSONObject("sensors")
            val returnTemp = sensors?.optDouble("return_temp")
            unitReturnTemp[unitId] = if (returnTemp == null || returnTemp.isNaN()) null else returnTemp
            val sp = json.optDouble("setpoint")
            unitSetpoint[unitId] = if (sp.isNaN()) null else sp
        } catch (_: Throwable) {
            unitSystemStatus[unitId] = null
            unitReturnTemp[unitId] = null
            unitSetpoint[unitId] = null
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // Setup permission launcher and request POST_NOTIFICATIONS on Android 13+
        notifPermissionLauncher = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) NotificationHelper.createChannel(this)
            else {
                // Permission denied: notifications won't appear; optional: inform user
            }
        }

        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                notifPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
            } else {
                NotificationHelper.createChannel(this)
            }
        } else {
            NotificationHelper.createChannel(this)
        }
        // register network callback to detect when connectivity returns
        connectivityManager = getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
        connectivityManager?.let { cm ->
            val cb = object : ConnectivityManager.NetworkCallback() {
                override fun onAvailable(network: Network) {
                    super.onAvailable(network)
                    // clear transient failures and trigger a background refresh for visible UI
                    lifecycleScope.launch(Dispatchers.IO) {
                        consecutiveFailures.clear()
                        // quick refresh of all units
                        val db = UnitsDatabase.getInstance(this@MainActivity)
                        val dao = db.unitDao()
                        val units = dao.getAllList()
                        units.forEach { u: com.flintmancomputers.tech_tool.units.UnitEntity ->
                             try {
                                 val res = unitPoller.poll(u)
                                 // update on main thread
                                 lifecycleScope.launch {
                                     // update state maps if present
                                     // We'll simply update pollJobs-triggered state on next tick; this helps reduce toasts
                                 }
                             } catch (_: Throwable) { }
                         }
                    }
                }
            }
            try {
                cm.registerDefaultNetworkCallback(cb)
                networkCallback = cb
            } catch (_: Throwable) {
                // ignore registration failure on older devices
            }
        }
        scheduleAlarmWorker()
        setContent {
            FlintmansTechToolTheme {
                val db = UnitsDatabase.getInstance(this)
                val repo = UnitsRepository(db.unitDao())
                val vm: UnitsViewModel by viewModels { UnitsViewModel.Factory(repo) }

                val units by vm.units.collectAsState()
                // UI state: editing dialog and test dialog
                var editing by remember { mutableStateOf<com.flintmancomputers.tech_tool.units.UnitEntity?>(null) }
                var showDialog by remember { mutableStateOf(false) }
                var testDialogVisible by remember { mutableStateOf(false) }
                var testResult by remember { mutableStateOf<PollResult?>(null) }
                // per-unit last successful poll timestamp (epoch millis)
                val unitLastSuccessMillis = remember { mutableStateMapOf<Long, Long>() }

                // map of unit.id -> online status
                val unitStatuses = remember { mutableStateMapOf<Long, Boolean>() }
                // map of unit.id -> system_status string (from /api/v1/status -> system_status)
                val unitSystemStatus = remember { mutableStateMapOf<Long, String?>() }
                // map of unit.id -> return temperature (°F)
                val unitReturnTemp = remember { mutableStateMapOf<Long, Double?>() }
                // map of unit.id -> setpoint (°F)
                val unitSetpoint = remember { mutableStateMapOf<Long, Double?>() }
                var isRefreshing by remember { mutableStateOf(false) }
                var lastRefreshMillis by remember { mutableStateOf<Long?>(null) }
                val unitAlarmState = remember { mutableStateMapOf<Long, Boolean>() }

                // Add missing state for showIntervalDialog
                var showIntervalDialog by remember { mutableStateOf(false) }

                Scaffold(
                    topBar = {
                        TopAppBar(title = {
                            Column {
                                Text(stringResource(R.string.units_manager_title))
                                Text(
                                    text = lastRefreshMillis?.let { stringResource(R.string.last_refresh_label) + " " + java.time.format.DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss").withZone(java.time.ZoneId.systemDefault()).format(java.time.Instant.ofEpochMilli(it)) } ?: stringResource(R.string.last_refresh_label) + " Never",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                        }, actions = {
                            // Polling interval / schedule icon (use Material Settings icon)
                            IconButton(onClick = { showIntervalDialog = true }) {
                                Icon(imageVector = Icons.Filled.Settings, contentDescription = "Polling interval")
                            }
                        })
                    },
                    floatingActionButton = {
                        FloatingActionButton(onClick = { editing = null; showDialog = true }) {
                            Icon(Icons.Default.Add, contentDescription = "Add Unit")
                        }
                    }
                ) { padding ->
                    UnitsScreen(units, unitStatuses, unitSystemStatus, unitReturnTemp, unitSetpoint, lastRefreshMillis,
                        onRefreshUnit = { unit ->
                            lifecycleScope.launch {
                                try {
                                    val res = unitPoller.poll(unit)
                                    unitStatuses[unit.id] = res.online
                                    if (res.online) unitLastSuccessMillis[unit.id] = System.currentTimeMillis()
                                    val body = res.body
                                    val json = if (!body.isNullOrBlank()) {
                                        try {
                                            org.json.JSONObject(body)
                                        } catch (_: Throwable) {
                                            null
                                        }
                                    } else null
                                    updateStatusFromJson(unit.id, json, unitStatuses, unitSystemStatus, unitReturnTemp, unitSetpoint)
                                } catch (_: Throwable) {
                                    unitStatuses[unit.id] = false
                                    updateStatusFromJson(unit.id, null, unitStatuses, unitSystemStatus, unitReturnTemp, unitSetpoint)
                                }
                            }
                        },
                        onEdit = { unit ->
                            // Fetch fresh data from database before opening edit dialog
                            lifecycleScope.launch {
                                val fresh = vm.getById(unit.id)
                                if (fresh != null) {
                                    editing = fresh
                                } else {
                                    editing = unit
                                }
                                showDialog = true
                            }
                        },
                        onDelete = { vm.deleteUnit(it) },
                        onSend = { unit, endpoint ->
                            if (endpoint == "/api/v1/status") {
                                // start StatusActivity
                                val intent = Intent(this@MainActivity, StatusActivity::class.java).apply {
                                    putExtra(StatusActivity.EXTRA_API_ADDRESS, unit.apiAddress)
                                    putExtra(StatusActivity.EXTRA_API_PORT, unit.apiPort)
                                    putExtra(StatusActivity.EXTRA_API_KEY, unit.apiKey)
                                    putExtra(StatusActivity.EXTRA_UNIT_ID, unit.unitId)
                                }
                                startActivity(intent)
                            } else {
                                // for other endpoints, call repository
                                vm.sendCommand(unit, endpoint) { result ->
                                    // TODO: show snackbar or toast with result
                                }
                            }
                        }, onTest = { unit ->
                            // run single poll and show dialog with result
                            lifecycleScope.launch {
                                val r = unitPoller.poll(unit)
                                testResult = r
                                testDialogVisible = true
                            }
                        }, isRefreshing = isRefreshing, onRefresh = {
                            lifecycleScope.launch {
                                isRefreshing = true
                                var onlineCount = 0
                                units.forEach { u ->
                                    try {
                                        val res = unitPoller.poll(u)
                                        unitStatuses[u.id] = res.online
                                        if (res.online) unitLastSuccessMillis[u.id] = System.currentTimeMillis()
                                        if (res.online) onlineCount++
                                        val body = res.body
                                        val json = if (!body.isNullOrBlank()) {
                                            try {
                                                org.json.JSONObject(body)
                                            } catch (_: Throwable) {
                                                null
                                            }
                                        } else null
                                        updateStatusFromJson(u.id, json, unitStatuses, unitSystemStatus, unitReturnTemp, unitSetpoint)
                                    } catch (_: Throwable) {
                                        unitStatuses[u.id] = false
                                        updateStatusFromJson(u.id, null, unitStatuses, unitSystemStatus, unitReturnTemp, unitSetpoint)
                                    }
                                }
                                isRefreshing = false
                                lastRefreshMillis = System.currentTimeMillis()
                                Toast.makeText(this@MainActivity, "Refreshed: $onlineCount/${units.size} online", Toast.LENGTH_SHORT).show()
                            }
                        }, onReorder = { newList ->
                            // persist new order
                            vm.reorderAndPersist(newList)
                        }, modifier = Modifier.padding(padding))
                }

                if (showDialog) {
                    UnitEditDialog(
                        existing = editing,
                        onDismiss = {
                            showDialog = false
                            editing = null
                        },
                        onSave = { id, unitId, addr, port, key ->
                            if (id != null && id > 0) {
                                vm.updateUnit(UnitEntity(id = id, unitId = unitId, apiAddress = addr, apiPort = port, apiKey = key))
                            } else {
                                vm.addUnit(unitId, addr, port, key)
                            }
                            showDialog = false
                            editing = null
                        }
                    )
                }

                if (testDialogVisible && testResult != null) {
                    val r = testResult!!
                    val friendlyBody = friendlyNetworkMessage(r.body) ?: (r.body?.takeIf { it.isNotBlank() } ?: "<empty>")
                    AlertDialog(
                        onDismissRequest = { testDialogVisible = false },
                        confirmButton = {
                            Button(onClick = { testDialogVisible = false }) { Text("OK") }
                        },
                        title = { Text("Connection Test") },
                        text = {
                            Column {
                                Text("Endpoint: ${r.endpoint}")
                                Text("Status code: ${r.statusCode}")
                                Text("Online: ${r.online}")
                                Text("Response:\n$friendlyBody")
                            }
                        }
                    )
                }

                // Poll interval dialog (opened by the watch/clock icon)
                if (showIntervalDialog) {
                    // initial selection based on current prefs
                    val current = getPollIntervalMinutes()
                    var selectedOption by remember { mutableStateOf(if (current == 5L) "High" else if (current == 15L) "Medium" else if (current == 30L) "Low" else "Custom") }
                    var customText by remember { mutableStateOf(if (selectedOption == "Custom") current.toString() else "") }

                    AlertDialog(
                        onDismissRequest = { showIntervalDialog = false },
                        title = { Text("Polling interval") },
                        text = {
                            Column {
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    RadioButton(selected = selectedOption == "High", onClick = { selectedOption = "High"; customText = "" })
                                    Spacer(modifier = Modifier.width(8.dp))
                                    Text("High (5 minutes)")
                                }
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    RadioButton(selected = selectedOption == "Medium", onClick = { selectedOption = "Medium"; customText = "" })
                                    Spacer(modifier = Modifier.width(8.dp))
                                    Text("Medium (15 minutes)")
                                }
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    RadioButton(selected = selectedOption == "Low", onClick = { selectedOption = "Low"; customText = "" })
                                    Spacer(modifier = Modifier.width(8.dp))
                                    Text("Low (30 minutes)")
                                }
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    RadioButton(selected = selectedOption == "Custom", onClick = { selectedOption = "Custom" })
                                    Spacer(modifier = Modifier.width(8.dp))
                                    Text("Custom (minutes):")
                                    Spacer(modifier = Modifier.width(8.dp))
                                    OutlinedTextField(value = customText, onValueChange = { customText = it }, modifier = Modifier.width(120.dp), singleLine = true)
                                }
                            }
                        },
                        confirmButton = {
                            Button(onClick = {
                                // determine minutes
                                val minutes = when (selectedOption) {
                                    "High" -> 5L
                                    "Medium" -> 15L
                                    "Low" -> 30L
                                    else -> customText.toLongOrNull() ?: -1L
                                }
                                if (minutes <= 0) {
                                    Toast.makeText(this@MainActivity, "Invalid interval", Toast.LENGTH_SHORT).show()
                                } else {
                                    setPollIntervalMinutes(minutes)
                                    scheduleAlarmWorker()
                                    Toast.makeText(this@MainActivity, "Polling interval set to ${minutes} minutes", Toast.LENGTH_SHORT).show()
                                    showIntervalDialog = false
                                }
                            }) { Text("Save") }
                        },
                        dismissButton = {
                            Button(onClick = { showIntervalDialog = false }) { Text("Cancel") }
                        }
                    )
                }

                 // Start/stop polling for each unit when units list changes
                 LaunchedEffect(units) {
                     // cancel removed
                     val currentIds = units.map { it.id }.toSet()
                     val toCancel = pollJobs.keys.filter { it !in currentIds }
                     toCancel.forEach { id -> pollJobs[id]?.cancel(); pollJobs.remove(id); unitStatuses.remove(id) ; unitSystemStatus.remove(id) }

                     // start polling for new units
                     units.forEach { u ->
                        if (!pollJobs.containsKey(u.id)) {
                             // Immediate synchronous one-shot poll (suspend) to prime UI before starting periodic poll
                             val initialRes = try {
                                 unitPoller.poll(u)
                             } catch (t: Throwable) {
                                 PollResult(false, friendlyNetworkMessage(t.message ?: "network error"), null, null)
                             }
                             unitStatuses[u.id] = initialRes.online
                             if (initialRes.online) unitLastSuccessMillis[u.id] = System.currentTimeMillis()

                             val job = poller.startPolling(u, lifecycleScope, intervalMs = 15_000L) { result: PollResult ->
                                val online = result.online
                                val body = result.body
                                val code = result.statusCode
                                val endpoint = result.endpoint

                                // Update consecutive failure counts and show a toast only after threshold to avoid spurious messages
                                val previous = unitStatuses[u.id]
                                if (!online) {
                                    val prevCount = consecutiveFailures[u.id] ?: 0
                                    val newCount = prevCount + 1
                                    consecutiveFailures[u.id] = newCount
                                    // show toast only if previously online AND last success was recent (<= 60s)
                                    val lastSuccess = unitLastSuccessMillis[u.id] ?: 0L
                                    val recentlySeen = System.currentTimeMillis() - lastSuccess <= 60_000L
                                    if (previous == true && newCount >= OFFLINE_TOAST_THRESHOLD && recentlySeen) {
                                        // Only show toasts when the Activity is in a visible/started state
                                        val isActive = !this@MainActivity.isDestroyed && !this@MainActivity.isFinishing && lifecycle.currentState.isAtLeast(androidx.lifecycle.Lifecycle.State.STARTED)
                                        if (isActive) {
                                            val bodyMsg = friendlyNetworkMessage(body) ?: truncate(body)
                                            val bodyStr = bodyMsg?.let { if (it.isNotBlank()) it else null }
                                            val msg = if (!bodyStr.isNullOrBlank()) "Unit ${u.unitId} is offline (endpoint=$endpoint code=$code): $bodyStr" else "Unit ${u.unitId} is offline (endpoint=$endpoint code=$code)"
                                            Toast.makeText(this@MainActivity, msg, Toast.LENGTH_LONG).show()
                                        }
                                    }
                                } else {
                                    // reset failures on success
                                    consecutiveFailures.remove(u.id)
                                    unitLastSuccessMillis[u.id] = System.currentTimeMillis()
                                 }

                                // Update online status
                                unitStatuses[u.id] = online
                                lastRefreshMillis = System.currentTimeMillis()

                                // Parse JSON and update state
                                val json = if (!body.isNullOrBlank()) {
                                    try {
                                        org.json.JSONObject(body)
                                    } catch (_: Throwable) {
                                        null
                                    }
                                } else null
                                updateStatusFromJson(u.id, json, unitStatuses, unitSystemStatus, unitReturnTemp, unitSetpoint)

                                // Detect alarm conditions
                                var inAlarm = false
                                var alarmText: String? = null
                                if (json != null) {
                                    val activeAlarms = json.optJSONArray("active_alarms")
                                    if (activeAlarms != null && activeAlarms.length() > 0) {
                                        inAlarm = true
                                        val codes = (0 until activeAlarms.length()).mapNotNull { idx -> activeAlarms.optString(idx) }
                                        alarmText = "Active alarms: ${codes.joinToString(", ") }"
                                    } else {
                                        val shutdown = json.optBoolean("alarm_shutdown", false)
                                        val warning = json.optBoolean("alarm_warning", false)
                                        if (shutdown) { inAlarm = true; alarmText = "Shutdown alarm" }
                                        else if (warning) { inAlarm = true; alarmText = "Warning alarm" }
                                    }
                                }

                                // Handle alarm notifications
                                val prevAlarm = unitAlarmState[u.id] == true
                                if (inAlarm && !prevAlarm) {
                                    val text = alarmText ?: ("Alarm on unit ${u.unitId}")
                                    NotificationHelper.notifyAlarm(this@MainActivity, u, text)
                                    unitAlarmState[u.id] = true
                                } else if (!inAlarm && prevAlarm) {
                                    NotificationHelper.clearAlarm(this@MainActivity, u.id)
                                    unitAlarmState.remove(u.id)
                                }
                             }
                             pollJobs[u.id] = job
                         }
                     }
                 }
            }
        }
    }

    override fun onDestroy() {
        // unregister network callback
        try {
            connectivityManager?.let { cm -> networkCallback?.let { cm.unregisterNetworkCallback(it) } }
        } catch (_: Throwable) {}
        pollJobs.values.forEach { it.cancel() }
        super.onDestroy()
    }
}

@Composable
fun Greeting(name: String, modifier: Modifier = Modifier) {
    Text(
        text = "Hello $name!",
        modifier = modifier
    )
}
