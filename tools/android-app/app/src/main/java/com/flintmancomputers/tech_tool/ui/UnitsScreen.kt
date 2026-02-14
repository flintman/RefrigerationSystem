package com.flintmancomputers.tech_tool.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.IntOffset
import kotlin.math.roundToInt
import kotlinx.coroutines.launch
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.flintmancomputers.tech_tool.R
import com.flintmancomputers.tech_tool.units.UnitEntity
import com.google.accompanist.swiperefresh.SwipeRefresh
import com.google.accompanist.swiperefresh.rememberSwipeRefreshState
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.draw.alpha
import com.flintmancomputers.tech_tool.ui.theme.OnlineGreen
import kotlin.math.abs

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun UnitsScreen(
    units: List<UnitEntity>,
    unitStatuses: Map<Long, Boolean>,
    unitSystemStatus: Map<Long, String?>,
    unitReturnTemp: Map<Long, Double?>,
    unitSetpoint: Map<Long, Double?>,
    lastRefreshMillis: Long?,
    onRefreshUnit: (UnitEntity) -> Unit,
    onEdit: (UnitEntity) -> Unit,
    onDelete: (UnitEntity) -> Unit,
    onSend: (UnitEntity, String) -> Unit,
    onTest: (UnitEntity) -> Unit,
    isRefreshing: Boolean = false,
    onRefresh: () -> Unit = {},
    onReorder: (List<UnitEntity>) -> Unit = {}, // called with the new ordered list (top->bottom)
    modifier: Modifier = Modifier
) {
    fun formatTimestampLabel(millis: Long?): String {
        return if (millis == null) "Never" else java.time.format.DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss").withZone(java.time.ZoneId.systemDefault()).format(java.time.Instant.ofEpochMilli(millis))
    }

    // remember which unit (if any) is pending deletion confirmation
    var confirmDeleteUnit by remember { mutableStateOf<UnitEntity?>(null) }

    // Search query state: filters units by Unit ID, system_status, or online/offline
    var searchQuery by remember { mutableStateOf("") }

    Column(modifier = modifier.fillMaxSize().padding(8.dp)) {
        // Search input above the list (outside SwipeRefresh so it is always visible)
        OutlinedTextField(
            value = searchQuery,
            onValueChange = { searchQuery = it },
            modifier = Modifier
                .fillMaxWidth()
                .padding(bottom = 8.dp),
            placeholder = { Text(text = "Search by Unit ID, status or online/offline") },
            singleLine = true,
            leadingIcon = { Icon(Icons.Default.Search, contentDescription = "Search") }
        )

        val swipeState = rememberSwipeRefreshState(isRefreshing)
        SwipeRefresh(state = swipeState, onRefresh = onRefresh) {

            // compute filtered list based on the query
            val q = searchQuery.trim().lowercase()
            val filtered = if (q.isEmpty()) units else units.filter { u ->
                val idMatch = u.unitId.lowercase().contains(q)
                val sys = unitSystemStatus[u.id]?.lowercase() ?: ""
                val sysMatch = sys.contains(q)
                val statusStr = when (unitStatuses[u.id]) { true -> "online"; false -> "offline"; null -> "unknown" }
                val statusMatch = statusStr.contains(q)
                idMatch || sysMatch || statusMatch
            }

            // LazyColumn uses contentPadding so items won't be hidden under app bar
            LazyColumn(modifier = Modifier.weight(1f), contentPadding = PaddingValues(top = 12.dp, bottom = 12.dp)) {
                items(filtered) { u ->
                    // Implement swipe-to-delete using pointer input and Animatable for smooth movement
                    val density = LocalDensity.current
                    val thresholdPx = with(density) { 120.dp.toPx() }
                    val offsetX = remember { Animatable(0f) }
                    val scope = rememberCoroutineScope()

                    // State for showing reorder menu for this item
                    var showReorderMenu by remember { mutableStateOf(false) }

                    Box(modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp)) {
                        // Compute alpha for delete (right swipe) and edit (left swipe)
                        val rightAlpha = (offsetX.value / thresholdPx).coerceIn(0f, 1f)
                        val leftAlpha = ((-offsetX.value) / thresholdPx).coerceIn(0f, 1f)

                        // Right-swipe delete background (appears as you swipe right)
                        Surface(
                            color = MaterialTheme.colorScheme.error,
                            shape = MaterialTheme.shapes.small,
                            modifier = Modifier
                                .matchParentSize()
                                .alpha(rightAlpha)
                        ) {
                            Row(modifier = Modifier.fillMaxSize().padding(start = 20.dp), verticalAlignment = Alignment.CenterVertically) {
                                Icon(Icons.Default.Delete, contentDescription = "Delete", tint = MaterialTheme.colorScheme.onError)
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(text = stringResource(R.string.delete), color = MaterialTheme.colorScheme.onError)
                            }
                        }

                        // Left-swipe edit background (appears as you swipe left)
                        Surface(
                            color = MaterialTheme.colorScheme.primaryContainer,
                            shape = MaterialTheme.shapes.small,
                            modifier = Modifier
                                .matchParentSize()
                                .alpha(leftAlpha)
                        ) {
                            Row(modifier = Modifier.fillMaxSize().padding(end = 20.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.End) {
                                Text(text = stringResource(R.string.edit_button), color = MaterialTheme.colorScheme.onPrimaryContainer)
                                Spacer(modifier = Modifier.width(8.dp))
                                Icon(Icons.Default.Edit, contentDescription = "Edit", tint = MaterialTheme.colorScheme.onPrimaryContainer)
                            }
                        }

                        // Foreground: the card that moves horizontally
                        // define card shape & elevation locally so they are in scope for Card
                        val cardShape = MaterialTheme.shapes.small
                        val cardElevation = if (offsetX.value != 0f) CardDefaults.cardElevation(defaultElevation = 0.dp) else CardDefaults.cardElevation(defaultElevation = 2.dp)

                        Card(
                            shape = cardShape,
                            elevation = cardElevation,
                            modifier = Modifier
                                .offset { IntOffset(offsetX.value.roundToInt(), 0) }
                                .fillMaxWidth()
                                .pointerInput(u.id) {
                                    // Use detectDragGestures: only consume horizontal drag deltas so vertical scrolling still works.
                                    detectDragGestures(
                                        onDragStart = { /* no-op */ },
                                        onDragEnd = {
                                            scope.launch {
                                                when {
                                                    offsetX.value > thresholdPx -> {
                                                        // trigger delete confirmation
                                                        confirmDeleteUnit = u
                                                    }
                                                    offsetX.value < -thresholdPx -> {
                                                        // trigger edit action directly
                                                        onEdit(u)
                                                    }
                                                }
                                                offsetX.animateTo(0f, animationSpec = tween(200))
                                            }
                                        },
                                        onDragCancel = {
                                            scope.launch { offsetX.animateTo(0f, animationSpec = tween(200)) }
                                        },
                                        onDrag = { change, dragAmount ->
                                            // if horizontal delta dominates, treat as horizontal swipe and consume it
                                            if (abs(dragAmount.x) > abs(dragAmount.y)) {
                                                // consume this change so the LazyColumn doesn't intercept
                                                change.consume()
                                                val max = thresholdPx * 2f
                                                val new = (offsetX.value + dragAmount.x).coerceIn(-max, max)
                                                scope.launch { offsetX.snapTo(new) }
                                            } else {
                                                // vertical movement: don't consume, let parent LazyColumn handle scrolling
                                            }
                                        }
                                    )
                                }
                                .combinedClickable(
                                    onClick = { if (unitStatuses[u.id] == true && offsetX.value == 0f) onSend(u, "/api/v1/status") },
                                    onLongClick = { showReorderMenu = true }
                                )
                        ) {
                            // inner padding makes the card taller than the delete background
                            Column(modifier = Modifier.padding(horizontal = 12.dp, vertical = 16.dp)) {
                                Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                                    Column(modifier = Modifier.weight(1f)) {
                                        // First row: Unit ID and (optional) return temperature badge
                                        Row(verticalAlignment = Alignment.CenterVertically) {
                                            Text(text = stringResource(R.string.unit_label, u.unitId), style = MaterialTheme.typography.titleMedium)
                                            Spacer(modifier = Modifier.width(8.dp))
                                            val rt = unitReturnTemp[u.id]
                                            if (rt != null) {
                                                Surface(shape = MaterialTheme.shapes.small, color = MaterialTheme.colorScheme.secondaryContainer) {
                                                    Text(text = "Return: ${"%.1f".format(rt)}°F", modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSecondaryContainer)
                                                }
                                            }
                                        }

                                        // Second row: status and (optional) setpoint
                                        val sys = unitSystemStatus[u.id]?.takeIf { !it.isNullOrBlank() }
                                        val statusText: String
                                        val statusColor: androidx.compose.ui.graphics.Color
                                        if (sys != null) {
                                            statusText = sys
                                            statusColor = when (sys.lowercase()) {
                                                "running", "ok", "online" -> OnlineGreen
                                                "warning" -> MaterialTheme.colorScheme.secondary
                                                "shutdown", "offline", "error" -> MaterialTheme.colorScheme.error
                                                else -> MaterialTheme.colorScheme.onSurfaceVariant
                                            }
                                        } else {
                                            val onlineState = unitStatuses[u.id]
                                            statusText = when (onlineState) {
                                                true -> stringResource(R.string.online_label)
                                                false -> "Offline"
                                                else -> "Unknown"
                                            }
                                            statusColor = when (unitStatuses[u.id]) {
                                                true -> OnlineGreen
                                                false -> MaterialTheme.colorScheme.error
                                                else -> MaterialTheme.colorScheme.onSurfaceVariant
                                            }
                                        }
                                        Row(verticalAlignment = Alignment.CenterVertically) {
                                            Text(text = stringResource(R.string.status_label, statusText), color = statusColor, style = MaterialTheme.typography.bodyMedium)
                                            Spacer(modifier = Modifier.width(8.dp))
                                            val sp = unitSetpoint[u.id]
                                            if (sp != null) {
                                                Text(text = "Setpoint: ${"%.1f".format(sp)}°F", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.secondary)
                                            }
                                        }
                                        // when no system status, show IP as secondary small text to the right
                                        if (unitSystemStatus[u.id].isNullOrBlank()) {
                                            Text(text = "${u.apiAddress}:${u.apiPort}", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                                        }
                                    }

                                    Spacer(modifier = Modifier.width(12.dp))

                                    // Right-side column holds the refresh control and the online dot stacked vertically
                                    Column(horizontalAlignment = Alignment.End, verticalArrangement = Arrangement.Center) {
                                        IconButton(onClick = { onRefreshUnit(u) }, modifier = Modifier.size(36.dp)) {
                                            Icon(Icons.Default.Refresh, contentDescription = "Refresh")
                                        }
                                        Spacer(modifier = Modifier.height(4.dp))
                                        val status = unitStatuses[u.id]
                                        Box(modifier = Modifier
                                            .size(14.dp)
                                            .background(color = when (status) {
                                                true -> OnlineGreen
                                                false -> MaterialTheme.colorScheme.error
                                                null -> MaterialTheme.colorScheme.onSurfaceVariant
                                            }, shape = androidx.compose.foundation.shape.CircleShape))
                                    }
                                }

                                Spacer(modifier = Modifier.height(8.dp))
                                // (Removed visible Edit button; swipe left to edit)
                                // Small hint to the user (both directions)
                                Spacer(modifier = Modifier.height(6.dp))
                                Text(text = stringResource(R.string.swipe_hint_both), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)

                             }
                         } // end Card

                        // Dropdown menu for reorder actions
                        DropdownMenu(expanded = showReorderMenu, onDismissRequest = { showReorderMenu = false }) {
                            DropdownMenuItem(text = { Text(stringResource(R.string.move_up)) }, onClick = {
                                showReorderMenu = false
                                val idx = units.indexOfFirst { it.id == u.id }
                                if (idx > 0) {
                                    val mutable = units.toMutableList()
                                    val item = mutable.removeAt(idx)
                                    mutable.add(idx - 1, item)
                                    onReorder(mutable)
                                }
                            })
                            DropdownMenuItem(text = { Text(stringResource(R.string.move_down)) }, onClick = {
                                showReorderMenu = false
                                val idx = units.indexOfFirst { it.id == u.id }
                                if (idx >= 0 && idx < units.size - 1) {
                                    val mutable = units.toMutableList()
                                    val item = mutable.removeAt(idx)
                                    mutable.add(idx + 1, item)
                                    onReorder(mutable)
                                }
                            })
                            DropdownMenuItem(text = { Text(stringResource(R.string.send_to_top)) }, onClick = {
                                showReorderMenu = false
                                val idx = units.indexOfFirst { it.id == u.id }
                                if (idx >= 0) {
                                    val mutable = units.toMutableList()
                                    val item = mutable.removeAt(idx)
                                    mutable.add(0, item)
                                    onReorder(mutable)
                                }
                            })
                            DropdownMenuItem(text = { Text(stringResource(R.string.send_to_bottom)) }, onClick = {
                                showReorderMenu = false
                                val idx = units.indexOfFirst { it.id == u.id }
                                if (idx >= 0) {
                                    val mutable = units.toMutableList()
                                    val item = mutable.removeAt(idx)
                                    mutable.add(item)
                                    onReorder(mutable)
                                }
                            })
                        }

                    } // end Box wrapper
                }
            }
        }
    }

    // Confirmation dialog shown when user taps Delete (or triggers via swipe)
    confirmDeleteUnit?.let { unitToDelete ->
        AlertDialog(
            onDismissRequest = { confirmDeleteUnit = null },
            title = { Text(stringResource(R.string.confirm_delete_title)) },
            text = { Text(stringResource(R.string.confirm_delete_message, unitToDelete.unitId)) },
            confirmButton = {
                TextButton(onClick = {
                    onDelete(unitToDelete)
                    confirmDeleteUnit = null
                }) { Text(stringResource(R.string.delete)) }
            },
            dismissButton = {
                TextButton(onClick = { confirmDeleteUnit = null }) { Text(stringResource(R.string.cancel)) }
            }
        )
    }
}
