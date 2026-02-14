package com.flintmancomputers.tech_tool

import android.app.DatePickerDialog
import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.flintmancomputers.tech_tool.network.NetworkClient
import com.flintmancomputers.tech_tool.ui.theme.FlintmansTechToolTheme
import java.io.File
import java.io.FileOutputStream
import java.time.LocalDate
import java.time.format.DateTimeFormatter
import java.util.Calendar
import kotlinx.coroutines.launch

class LogActivity : ComponentActivity() {
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
                LogScreen(apiAddress = apiAddress, apiPort = apiPort, apiKey = apiKey)
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LogScreen(apiAddress: String, apiPort: Int, apiKey: String?) {
    val context = LocalContext.current
    val coroutineScope = rememberCoroutineScope()

    var retention by remember { mutableStateOf(7) }
    var selectedDate by remember { mutableStateOf<String?>(null) }
    var logText by remember { mutableStateOf<String?>(null) }
    var loading by remember { mutableStateOf(false) }

    fun buildDates(n: Int) {
        val fmt = DateTimeFormatter.ofPattern("yyyy-MM-dd")
        val list = mutableListOf<String>()
        var d = LocalDate.now()
        repeat(n) {
            list.add(d.format(fmt))
            d = d.minusDays(1)
        }
        if (selectedDate == null && list.isNotEmpty()) selectedDate = list.first()
    }

    fun loadRetention() {
        coroutineScope.launch {
            val info = NetworkClient.getSystemInfo(apiAddress, apiPort, apiKey)
            val r = info?.optString("logging.retention_period")?.toIntOrNull() ?: 7
            retention = r
            buildDates(retention)
        }
    }

    fun fetchLog(date: String) {
        coroutineScope.launch {
            loading = true
            val (content, err) = NetworkClient.getEventsLog(apiAddress, apiPort, apiKey, date)
            if (content != null) {
                logText = content
            } else {
                logText = null
                Toast.makeText(context, "Failed to fetch log: $err", Toast.LENGTH_LONG).show()
            }
            loading = false
        }
    }

    LaunchedEffect(apiAddress, apiPort, apiKey) { loadRetention() }

    Scaffold(topBar = { TopAppBar(title = { Text("Events Log") }) }) { padding ->
        Column(modifier = Modifier.padding(padding).padding(12.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = { buildDates(retention); if (selectedDate != null) fetchLog(selectedDate!!) }) { Text("Refresh") }
                // date selector using platform DatePickerDialog limited to retention period
                Button(onClick = {
                     val fmt = DateTimeFormatter.ofPattern("yyyy-MM-dd")
                     val initial = selectedDate?.let { LocalDate.parse(it, fmt) } ?: LocalDate.now()
                     val cal = Calendar.getInstance().apply { set(initial.year, initial.monthValue - 1, initial.dayOfMonth) }
                     val today = Calendar.getInstance()
                     val min = Calendar.getInstance().apply { timeInMillis = today.timeInMillis; add(Calendar.DAY_OF_YEAR, -(retention - 1)) }

                    val dp = DatePickerDialog(context, { _, year, month, dayOfMonth ->
                         val picked = LocalDate.of(year, month + 1, dayOfMonth)
                         val dstr = picked.format(fmt)
                         selectedDate = dstr
                         fetchLog(dstr)
                     }, cal.get(Calendar.YEAR), cal.get(Calendar.MONTH), cal.get(Calendar.DAY_OF_MONTH))
                     dp.datePicker.minDate = min.timeInMillis
                     dp.datePicker.maxDate = today.timeInMillis
                     dp.show()
                 }) { Text(selectedDate ?: "Select date") }
                // Save and Share buttons (enabled when logText is loaded)
                Button(onClick = {
                    // save to app external files dir under logs/
                    val d = selectedDate ?: return@Button
                    val content = logText ?: return@Button
                    coroutineScope.launch {
                        val path = saveLogToFile(context, d, content)
                        if (path != null) Toast.makeText(context, "Saved to: $path", Toast.LENGTH_LONG).show() else Toast.makeText(context, "Failed to save log", Toast.LENGTH_SHORT).show()
                    }
                }, enabled = logText != null) { Text("Save") }

                Button(onClick = {
                    val content = logText
                    if (content == null) {
                        Toast.makeText(context, "No log to share", Toast.LENGTH_SHORT).show()
                        return@Button
                    }
                    val share = Intent(Intent.ACTION_SEND).apply {
                        type = "text/plain"
                        putExtra(Intent.EXTRA_SUBJECT, "Events log ${selectedDate ?: ""}")
                        putExtra(Intent.EXTRA_TEXT, content)
                    }
                    context.startActivity(Intent.createChooser(share, "Share log"))
                }, enabled = logText != null) { Text("Share") }
             }

            if (loading) {
                LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
            }

            Box(modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
                Text(logText ?: "No log loaded. Select a date and press Refresh.")
            }
        }
    }
}

// Save the log text to app external files directory under /Android/data/<package>/files/logs/
fun saveLogToFile(context: android.content.Context, date: String, content: String): String? {
    return try {
        val dir = File(context.getExternalFilesDir(null), "logs")
        if (!dir.exists()) dir.mkdirs()
        val file = File(dir, "events-$date.log")
        FileOutputStream(file).use { it.write(content.toByteArray(Charsets.UTF_8)) }
        file.absolutePath
    } catch (ex: Exception) {
        null
    }
}
