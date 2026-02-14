package com.flintmancomputers.tech_tool.units

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "units")
data class UnitEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val unitId: String,
    val apiAddress: String,
    val apiPort: Int = 8095,
    val apiKey: String,
    val position: Int = 0 // persisted ordering index; lower numbers show first
)
