package com.flintmancomputers.tech_tool.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.flintmancomputers.tech_tool.units.UnitEntity

@Composable
fun UnitEditDialog(
    existing: UnitEntity?,
    onDismiss: () -> Unit,
    onSave: (id: Long?, unitId: String, apiAddress: String, apiPort: Int, apiKey: String) -> Unit
) {
    // Create fresh state every time - don't cache with remember
    var unitId by remember { mutableStateOf(existing?.unitId ?: "") }
    var addr by remember { mutableStateOf(existing?.apiAddress ?: "") }
    var portText by remember { mutableStateOf((existing?.apiPort ?: 8095).toString()) }
    var key by remember { mutableStateOf(existing?.apiKey ?: "") }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(if (existing == null) "Add Unit" else "Edit Unit") },
        text = {
            Column {
                // Make these single-line so the dialog doesn't grow vertically for long tokens
                OutlinedTextField(
                    value = unitId,
                    onValueChange = { unitId = it },
                    label = { Text("unit_id") },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true
                )
                OutlinedTextField(
                    value = addr,
                    onValueChange = { addr = it },
                    label = { Text("api_address") },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true
                )
                OutlinedTextField(
                    value = portText,
                    onValueChange = { portText = it.filter { ch -> ch.isDigit() } },
                    label = { Text("api_port") },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true
                )
                OutlinedTextField(
                    value = key,
                    onValueChange = { key = it },
                    label = { Text("api_key") },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true
                )
            }
        },
        confirmButton = {
            Button(onClick = {
                val p = portText.toIntOrNull() ?: 8095
                onSave(existing?.id, unitId, addr, p, key)
            }) { Text("Save") }
        },
        dismissButton = { Button(onClick = onDismiss) { Text("Cancel") } },
        modifier = Modifier.padding(8.dp)
    )
}
