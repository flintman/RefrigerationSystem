package com.flintmancomputers.tech_tool.units

import androidx.room.*
import kotlinx.coroutines.flow.Flow

@Dao
interface UnitDao {
    @Query("SELECT * FROM units ORDER BY position ASC")
    fun getAll(): Flow<List<UnitEntity>>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insert(unit: UnitEntity): Long

    @Update
    suspend fun update(unit: UnitEntity)

    @Delete
    suspend fun delete(unit: UnitEntity)

    @Query("SELECT * FROM units ORDER BY position ASC")
    suspend fun getAllList(): List<UnitEntity>

    @Query("SELECT COALESCE(MAX(position), 0) FROM units")
    suspend fun getMaxPosition(): Int

    @Query("SELECT * FROM units WHERE id = :id")
    suspend fun getById(id: Long): UnitEntity?
}
