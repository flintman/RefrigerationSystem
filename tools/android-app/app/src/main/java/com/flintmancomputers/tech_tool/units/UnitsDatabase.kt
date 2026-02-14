package com.flintmancomputers.tech_tool.units

import android.content.Context
import androidx.room.Database
import androidx.room.Room
import androidx.room.RoomDatabase

@Database(entities = [UnitEntity::class], version = 2, exportSchema = false)
abstract class UnitsDatabase : RoomDatabase() {
    abstract fun unitDao(): UnitDao

    companion object {
        @Volatile private var INSTANCE: UnitsDatabase? = null
        fun getInstance(context: Context): UnitsDatabase =
            INSTANCE ?: synchronized(this) {
                INSTANCE ?: Room.databaseBuilder(
                    context.applicationContext,
                    UnitsDatabase::class.java,
                    "units-db"
                ).fallbackToDestructiveMigration().build().also { INSTANCE = it }
            }
    }
}
