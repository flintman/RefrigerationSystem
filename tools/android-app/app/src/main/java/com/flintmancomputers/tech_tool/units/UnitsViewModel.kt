package com.flintmancomputers.tech_tool.units

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

class UnitsViewModel(private val repo: UnitsRepository) : ViewModel() {
    val units: StateFlow<List<UnitEntity>> = repo.observeAll()
        .map { it }
        .stateIn(viewModelScope, SharingStarted.Eagerly, emptyList())

    fun addUnit(unitId: String, apiAddress: String, apiPort: Int, apiKey: String) {
        viewModelScope.launch {
            val maxPos = repo.getMaxPosition()
            repo.insert(UnitEntity(unitId = unitId, apiAddress = apiAddress, apiPort = apiPort, apiKey = apiKey, position = maxPos + 1))
        }
    }

    fun updateUnit(unit: UnitEntity) {
        viewModelScope.launch { repo.update(unit) }
    }

    fun deleteUnit(unit: UnitEntity) {
        viewModelScope.launch { repo.delete(unit) }
    }

    suspend fun getById(id: Long): UnitEntity? = repo.getById(id)

    fun sendCommand(unit: UnitEntity, endpoint: String = "/api/v1/status", method: String = "GET", bodyJson: String? = null, onResult: (Result<String>) -> Unit) {
        viewModelScope.launch {
            val res = repo.sendRequest(unit, endpoint, method, bodyJson)
            onResult(res)
        }
    }

    /**
     * Reorder units list locally then persist positions. Expects the list passed to be the current ordering (top->bottom).
     */
    fun reorderAndPersist(newOrder: List<UnitEntity>) {
        viewModelScope.launch {
            // assign positions based on index
            val updated = newOrder.mapIndexed { idx, u -> u.copy(position = idx) }
            repo.updatePositions(updated)
        }
    }

    class Factory(private val repo: UnitsRepository) : ViewModelProvider.Factory {
        override fun <T : ViewModel> create(modelClass: Class<T>): T {
            @Suppress("UNCHECKED_CAST")
            return UnitsViewModel(repo) as T
        }
    }
}
