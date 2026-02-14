# Flintman's Tech Tool - Android App

A mobile control and monitoring application for refrigeration units running the Flintman refrigeration system.

## Overview

This Android app provides:
- **Real-time monitoring** of refrigeration unit status (temperature, alarms, settings)
- **Remote control** of units via the backend API
- **Unit management** - add, edit, and remove monitored units
- **Alarm notifications** - receive alerts when units trigger alarms
- **Configuration** - adjust unit settings remotely
- **Log viewing** - access system logs for diagnostics

The app connects to the C++ refrigeration backend API over HTTPS and maintains a local database of configured units.

## Prerequisites

- **Android Studio** (latest version recommended)
- **Android SDK** 29 or higher (API level 29+)
- **Android SDK Build Tools** (26 recommended)
- **Java Development Kit (JDK)** 11 or higher
- **Gradle** 8.0+ (included via gradle wrapper)
- **Backend server** running the refrigeration API (available at `https://<host>:<port>`)

## Build Instructions

### 1. Clone and Navigate

```bash
cd tools/android-app
```

### 2. Configure Local SDK Path

Create or update `local.properties` with your Android SDK location:

```properties
sdk.dir=/path/to/Android/Sdk
```

**Note:** `local.properties` is in `.gitignore` and should never be committed.

### 3. Build the App

Using gradle wrapper (recommended):

```bash
./gradlew clean build
```

Or for a release build (requires signing configuration):

```bash
./gradlew clean bundleRelease
```

### 4. Install on Device/Emulator

Connect an Android device or start an emulator, then:

```bash
./gradlew installDebug
```

Or open in Android Studio and click "Run" (green play button).

## Running the App

1. **Launch** the app from your device/emulator home screen (icon: refrigeration unit)
2. **Add a unit**:
   - Click the "+" button
   - Enter unit name, hostname, port, and optional API key
   - Example: `192.168.1.100`, port `8443`, key `your-api-key`
3. **View status** - tap a unit to see real-time temperature and settings
4. **Configure** - tap the settings icon to adjust polling intervals and notification preferences
5. **View logs** - access the Logs screen for backend diagnostics

## Architecture

```
app/src/main/java/com/flintmancomputers/tech_tool/
├── MainActivity.kt              # Main UI and navigation
├── StatusActivity.kt            # Unit status screen
├── ConfigActivity.kt            # AppSettings screen
├── LogActivity.kt               # System logs viewer
├── network/
│   ├── NetworkClient.kt         # Core API communication (GET/POST)
│   ├── SslUtil.kt               # SSL certificate handling
│   ├── StatusFetcher.kt         # Status API calls
│   ├── UnitPoller.kt            # Background polling logic
│   └── SystemStatus.kt          # Data models
├── units/
│   ├── UnitEntity.kt            # Database schema
│   ├── UnitsDatabase.kt         # Room database
│   ├── UnitDao.kt               # Database access
│   ├── UnitsRepository.kt       # Data access layer
│   └── UnitsViewModel.kt        # ViewModel for UI state
├── notifications/
│   ├── NotificationHelper.kt    # Notification creation
│   └── UnitAlarmWorker.kt       # Background alarm polling
├── ui/
│   ├── UnitsScreen.kt           # Jetpack Compose UI
│   ├── UnitEditDialog.kt        # Add/edit dialog
│   └── theme/                   # Material Design 3 theming
└── res/                         # Resources (strings, colors, icons)
```

## API Integration

The app communicates with the refrigeration backend using these endpoints:

- `GET /api/status` - Fetch unit status
- `POST /api/control` - Send control commands
- `GET /api/config` - Read configuration
- `POST /api/config` - Update configuration
- `GET /api/logs` - Retrieve system logs

All requests include the `X-API-Key` header if configured.

## Development Notes

### SSL/TLS Handling

⚠️ **Development Only:** The app currently uses `SslUtil.createInsecureSslSocketFactory()` which:
- Accepts all SSL certificates (including self-signed)
- Disables hostname verification

This is suitable for **local development and testing only**. For production:
1. Use proper signed certificates on the server
2. Implement certificate pinning or remove the insecure SSL factory
3. Use the standard `HttpsURLConnection` without custom SSL configuration

### Database

The app uses **Room** (Android's SQLite abstraction) to store:
- Unit name, hostname, port, and API key
- Last known status
- Polling preferences

Data is stored locally and not synced to cloud.

### Background Tasks

Alarm polling runs via **WorkManager**, which:
- Respects system battery saver and doze modes
- Persists across app restarts
- Configurable polling interval (default: 15 minutes)

## Permissions Required

- `android.permission.INTERNET` - Network requests to backend
- `android.permission.POST_NOTIFICATIONS` - Alarm notifications (Android 13+)


## Testing

Run instrumented tests on a device/emulator:

```bash
./gradlew connectedAndroidTest
```

Run local unit tests:

```bash
./gradlew test
```

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Build fails: "SDK not found" | Verify `sdk.dir` in `local.properties` |
| App crashes on startup | Check Android version (requires API 29+) |
| Can't connect to backend | Verify hostname, port, and API key; check firewall rules |
| Notifications not appearing | Grant POST_NOTIFICATIONS permission on Android 13+ |
| SSL certificate errors | Ensure backend uses valid certificate or check SSL util settings |

## Contributing

When contributing:
1. Follow Kotlin style guide (automated by `ktlint`)
2. Add tests for new features
3. Update this README with any significant changes
4. Never commit `local.properties` or sensitive data

## Version

- **App Version:** 1.0
- **Min SDK:** 29 (Android 10)
- **Target SDK:** 36 (Android 15)
- **Kotlin:** 2.x with Jetpack Compose

## License

See [LICENSE](../../docs/LICENSE) in the project root.
