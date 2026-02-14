package com.flintmancomputers.tech_tool.network

import com.flintmancomputers.tech_tool.units.UnitEntity
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

class UnitStatusPoller(private val poller: UnitPoller = UnitPoller()) {
    fun startPolling(
        unit: UnitEntity,
        scope: CoroutineScope,
        intervalMs: Long = 30_000L,
        onStatus: (PollResult) -> Unit
    ): Job {
        return scope.launch {
            while (isActive) {
                val result = try { poller.poll(unit) } catch (_: Throwable) { PollResult(false, "error", null, null) }
                onStatus(result)
                delay(intervalMs)
            }
        }
    }
}
