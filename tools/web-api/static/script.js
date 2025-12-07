// Global variables
let currentUnitId = null;
let currentUnitData = null;
let controlPanelRefreshInterval = null;
let dashboardRefreshInterval = null;

function updateLastRefreshTime() {
    const now = new Date();
    const timeStr = now.toLocaleTimeString();
    document.getElementById('lastRefreshTime').textContent = timeStr;
}

function refreshControlPanel() {
    if (currentUnitId) {
        console.log('Refreshing control panel for unit ' + currentUnitId);
        // Fetch fresh data and update display without full page reload
        fetch('/api/unit/' + currentUnitId + '/system-info')
        .then(r => r.json())
        .then(unitData => {
            console.log('Refresh data received, updating display...');
            // Merge new data with existing data to preserve sensor readings
            currentUnitData = Object.assign({}, currentUnitData, unitData);

            // Parse data - API returns flat structure with dotted keys
            const setpoint = currentUnitData['unit.setpoint'] ? parseFloat(currentUnitData['unit.setpoint']).toFixed(2) : 'N/A';
            const systemStatus = currentUnitData.system_status || 'Run';

            // Get sensor data if available (from initial load or previous data)
            const sensors = currentUnitData.sensors || {};
            const returnTemp = sensors.return_temp ? parseFloat(sensors.return_temp).toFixed(2) : 'N/A';
            const supplyTemp = sensors.supply_temp ? parseFloat(sensors.supply_temp).toFixed(2) : 'N/A';
            const coilTemp = sensors.coil_temp ? parseFloat(sensors.coil_temp).toFixed(2) : 'N/A';

            // Update status section with all values
            let statusHTML = '<p><strong>Status:</strong> ' + systemStatus + '</p>';
            statusHTML += '<p><strong>Setpoint:</strong> ' + setpoint + '°F</p>';
            if (returnTemp !== 'N/A') statusHTML += '<p><strong>Return Temp:</strong> ' + returnTemp + '°F</p>';
            if (supplyTemp !== 'N/A') statusHTML += '<p><strong>Supply Temp:</strong> ' + supplyTemp + '°F</p>';
            if (coilTemp !== 'N/A') statusHTML += '<p><strong>Coil Temp:</strong> ' + coilTemp + '°F</p>';
            const demoMode = unitData.demo_mode ? 'Enabled' : 'Disabled';
            const demoStyle = unitData.demo_mode ? 'color: #FF9800;' : '';
            statusHTML += '<p style="' + demoStyle + '"><strong>Demo Mode:</strong> ' + demoMode + '</p>';
            document.getElementById('statusInfo').innerHTML = statusHTML;
            console.log('Updated statusInfo with setpoint: ' + setpoint);

            // Update setpoint input field
            const spInput = document.getElementById('setpointInput');
            if (spInput) {
                spInput.value = setpoint !== 'N/A' ? setpoint : '';
                console.log('Updated setpointInput to: ' + spInput.value);
            }

            // Update config fields from flat data structure
            document.getElementById('defrostCoilTempInput').value = unitData['defrost.coil_temperature'] || '45';
            document.getElementById('defrostIntervalInput').value = unitData['defrost.interval_hours'] || '8';
            document.getElementById('defrostTimeoutInput').value = unitData['defrost.timeout_mins'] || '45';
            document.getElementById('loggingIntervalInput').value = unitData['logging.interval_mins'] || '5';
            document.getElementById('loggingRetentionInput').value = unitData['logging.retention_period'] || '30';
            document.getElementById('compressorTimerInput').value = unitData['compressor.off_timer'] || '5';
            document.getElementById('setpointLowInput').value = unitData['setpoint.low_limit'] || '-20';
            document.getElementById('setpointHighInput').value = unitData['setpoint.high_limit'] || '80';
            document.getElementById('setpointOffsetInput').value = unitData['setpoint.offset'] || '2';
            document.getElementById('electricHeatInput').value = unitData['unit.electric_heat'] || '0';
            document.getElementById('fanContinuousInput').value = unitData['unit.fan_continuous'] || '0';
            document.getElementById('relayActiveLowInput').value = unitData['unit.relay_active_low'] || '0';
            document.getElementById('unitNumberInput').value = unitData['unit.number'] || '0';

            // Update button visibility based on unit state
            const hasAlarm = systemStatus === 'Alarm';
            const coilTempVal = sensors.coil_temp ? parseFloat(sensors.coil_temp) : null;
            const defrostThreshold = parseFloat(unitData['defrost.coil_temperature']) || 45;
            const canDefrost = coilTempVal && coilTempVal < defrostThreshold;

            const resetBtn = document.querySelector('button[onclick="resetAlarm()"]');
            const defrostBtn = document.querySelector('button[onclick="sendDefrost()"]');
            if (resetBtn) resetBtn.style.display = hasAlarm ? 'block' : 'none';
            if (defrostBtn) defrostBtn.style.display = canDefrost ? 'block' : 'none';

            console.log('Updated button visibility - hasAlarm:', hasAlarm, 'canDefrost:', canDefrost);

            updateLastRefreshTime();
        })
        .catch(e => console.error('Refresh failed:', e));
    }
}

function toggleDarkMode() {
    document.body.classList.toggle('dark-mode');
    localStorage.setItem('darkMode', document.body.classList.contains('dark-mode'));
}

function initDarkMode() {
    if (localStorage.getItem('darkMode') === 'true') {
        document.body.classList.add('dark-mode');
    }
}

function startDashboardRefresh() {
    // Auto-refresh dashboard every 5 minutes to check for alarms
    if (dashboardRefreshInterval) clearInterval(dashboardRefreshInterval);
    dashboardRefreshInterval = setInterval(function() {
        if (!currentUnitId) {  // Only refresh dashboard when not in control panel
            console.log('Background dashboard refresh (checking for alarms)');
            updateUnits();
        }
    }, 5 * 60 * 1000);  // 5 minutes
}

function stopDashboardRefresh() {
    if (dashboardRefreshInterval) {
        clearInterval(dashboardRefreshInterval);
        dashboardRefreshInterval = null;
    }
}

function updateUnits() {
    // Show login status bar if logged in
    const savedPassword = localStorage.getItem('dashboardPassword');
    if (savedPassword) {
        // Try to check if still authenticated
        fetch('/api/login', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({password: savedPassword})
        })
        .then(response => response.json())
        .then(data => {
            if (data.authenticated) {
                document.getElementById('loginStatusBar').style.display = 'block';
            } else {
                document.getElementById('loginStatusBar').style.display = 'none';
            }
        })
        .catch(() => {
            document.getElementById('loginStatusBar').style.display = 'none';
        });
    } else {
        document.getElementById('loginStatusBar').style.display = 'none';
    }

    fetch('/api/units')
        .then(response => response.json())
        .then(data => {
            console.log('Received data:', data);
            // Store unit configs globally for API calls
            window.units_config = data.unit_configs || {};
            const unitsDiv = document.getElementById('units');
            unitsDiv.innerHTML = '';
            if (!data.unit_data || Object.keys(data.unit_data).length === 0) {
                unitsDiv.innerHTML = '<p>No units configured or no data yet. Unit count: ' + (data.unit_count || 0) + '</p>';
                return;
            }
            for (const [unitId, unitData] of Object.entries(data.unit_data)) {
                let status = 'Offline';
                if (unitData.system_status) {
                    status = String(unitData.system_status);
                }
                const statusClass = status === 'Alarm' ? 'status-alarm' : (status === 'Run' ? 'status-ok' : (status === 'Offline' ? 'status-offline' : 'status-unknown'));
                const sensors = unitData.sensors || {};
                const setpoint = unitData.setpoint || 0;
                const returnTemp = sensors.return_temp || 0;
                const supplyTemp = sensors.supply_temp || 0;
                const coilTemp = sensors.coil_temp || 0;
                const isOffline = status === 'Offline';
                const formatValue = (val) => isOffline ? 'N/A' : (val || 0).toFixed(1);
                const card = document.createElement('div');
                card.className = 'unit-card';
                if (!isOffline) {
                    card.onclick = () => promptLogin(unitId, unitData);
                } else {
                    card.style.pointerEvents = 'none';
                    card.style.cursor = 'not-allowed';
                }
                card.innerHTML = `
                    <div class="unit-id">${unitId}</div>
                    <div class="status ${statusClass}">${status}</div>
                    <div class="reading"><span class="reading-label">Setpoint:</span> ${formatValue(setpoint)}&deg;F</div>
                    <div class="reading"><span class="reading-label">Return Temp:</span> ${formatValue(returnTemp)}&deg;F</div>
                    <div class="reading"><span class="reading-label">Supply Temp:</span> ${formatValue(supplyTemp)}&deg;F</div>
                    <div class="reading"><span class="reading-label">Coil Temp:</span> ${formatValue(coilTemp)}&deg;F</div>
                    <div class="last-update">${isOffline ? 'Waiting for connection...' : 'Last update: ' + new Date(unitData.timestamp * 1000).toLocaleString()}</div>
                `;
                unitsDiv.appendChild(card);
            }
        })
        .catch(error => {
            console.error('Error loading units:', error);
            document.getElementById('units').innerHTML = '<p>Error: ' + error + '</p>';
        });
}

function promptLogin(unitId, unitData) {
    currentUnitId = unitId;
    currentUnitData = unitData;
    var savedPassword = localStorage.getItem('dashboardPassword');
    if (savedPassword) {
        // Try login automatically
        fetch('/api/login', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({password: savedPassword})
        })
        .then(response => response.json())
        .then(data => {
            if (data.authenticated) {
                closeLoginModal();
                showControlPage();
            } else {
                localStorage.removeItem('dashboardPassword');
                document.getElementById('loginModal').style.display = 'block';
                document.getElementById('passwordInput').value = '';
                document.getElementById('loginError').style.display = 'none';
            }
        })
        .catch(() => {
            document.getElementById('loginModal').style.display = 'block';
            document.getElementById('passwordInput').value = '';
            document.getElementById('loginError').style.display = 'none';
        });
    } else {
        document.getElementById('loginModal').style.display = 'block';
        document.getElementById('passwordInput').value = '';
        document.getElementById('loginError').style.display = 'none';
    }
}

function closeLoginModal() {
    document.getElementById('loginModal').style.display = 'none';
    document.getElementById('loginError').style.display = 'none';
}

function handleLogin(event) {
    event.preventDefault();
    const password = document.getElementById('passwordInput').value;
    fetch('/api/login', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({password: password})
    })
    .then(response => response.json())
    .then(data => {
        if (data.authenticated) {
            localStorage.setItem('dashboardPassword', password);
            closeLoginModal();
            showControlPage();
        } else {
            document.getElementById('loginError').style.display = 'block';
            document.getElementById('loginError').textContent = 'Invalid password';
        }
    })
    .catch(error => {
        document.getElementById('loginError').style.display = 'block';
        document.getElementById('loginError').textContent = 'Error: ' + error;
    });
}

function showControlPage() {
    document.getElementById('units').parentElement.style.display = 'none';
    document.getElementById('controlPage').style.display = 'block';
    document.getElementById('controlUnitId').textContent = currentUnitId;

    // Show loading state
    document.getElementById('statusInfo').innerHTML = '<p>Loading...</p>';

    const unit = window.units_config?.[currentUnitId];
    console.log('Control page for unit:', currentUnitId, 'config:', unit);
    if (!unit) { alert('Unit config not found'); return; }

    // Fetch full system info via proxy endpoint
    const proxyUrl = '/api/unit/' + currentUnitId + '/system-info';
    console.log('Fetching system info from proxy:', proxyUrl);
    fetch(proxyUrl)
    .then(r => {
        console.log('System info response:', r.status, r.ok);
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.json();
    })
    .then(config => {
        console.log('Config received:', config);
        // Update currentUnitData with fresh config data
        currentUnitData = Object.assign({}, currentUnitData, config);
        console.log('Updated currentUnitData, setpoint now:', currentUnitData['unit.setpoint'] || currentUnitData.setpoint);

        // Display current sensor readings with proper formatting
        let statusHTML = '<div style="font-size: 14px;">';
        statusHTML += '<p><strong>Status:</strong> ' + (currentUnitData.system_status || 'N/A') + '</p>';
        // Use unit.setpoint from config if available, otherwise use setpoint from dashboard
        const sp = currentUnitData['unit.setpoint'] ? parseFloat(currentUnitData['unit.setpoint']).toFixed(2) : (currentUnitData.setpoint ? parseFloat(currentUnitData.setpoint).toFixed(2) : 'N/A');
        statusHTML += '<p><strong>Setpoint:</strong> ' + sp + '°F</p>';
        const ret = currentUnitData.sensors?.return_temp ? parseFloat(currentUnitData.sensors.return_temp).toFixed(2) : 'N/A';
        statusHTML += '<p><strong>Return Temp:</strong> ' + ret + '°F</p>';
        const sup = currentUnitData.sensors?.supply_temp ? parseFloat(currentUnitData.sensors.supply_temp).toFixed(2) : 'N/A';
        statusHTML += '<p><strong>Supply Temp:</strong> ' + sup + '°F</p>';
        const coil = currentUnitData.sensors?.coil_temp ? parseFloat(currentUnitData.sensors.coil_temp).toFixed(2) : 'N/A';
        statusHTML += '<p><strong>Coil Temp:</strong> ' + coil + '°F</p>';
        const demoMode = currentUnitData.demo_mode ? 'Enabled' : 'Disabled';
        const demoStyle = currentUnitData.demo_mode ? 'color: #FF9800;' : '';
        statusHTML += '<p style="' + demoStyle + '"><strong>Demo Mode:</strong> ' + demoMode + '</p>';
        statusHTML += '</div>';
        document.getElementById('statusInfo').innerHTML = statusHTML;

        // Update setpoint input - use unit.setpoint from config
        const setpointVal = currentUnitData['unit.setpoint'] || currentUnitData.setpoint || 0;
        document.getElementById('setpointInput').value = setpointVal;

        // Populate configuration fields with values from response
        document.getElementById('defrostCoilTempInput').value = config['defrost.coil_temperature'] || '45';
        document.getElementById('defrostIntervalInput').value = config['defrost.interval_hours'] || '8';
        document.getElementById('defrostTimeoutInput').value = config['defrost.timeout_mins'] || '45';
        document.getElementById('loggingIntervalInput').value = config['logging.interval_mins'] || '5';
        document.getElementById('loggingRetentionInput').value = config['logging.retention_period'] || '30';
        document.getElementById('compressorTimerInput').value = config['compressor.off_timer'] || '5';
        document.getElementById('setpointLowInput').value = config['setpoint.low_limit'] || '-20';
        document.getElementById('setpointHighInput').value = config['setpoint.high_limit'] || '80';
        document.getElementById('setpointOffsetInput').value = config['setpoint.offset'] || '2';
        document.getElementById('electricHeatInput').value = config['unit.electric_heat'] || '0';
        document.getElementById('fanContinuousInput').value = config['unit.fan_continuous'] || '0';
        document.getElementById('relayActiveLowInput').value = config['unit.relay_active_low'] || '0';
        document.getElementById('unitNumberInput').value = config['unit.number'] || '0';

        // Show/hide action buttons based on unit state
        const hasAlarm = currentUnitData.system_status === 'Alarm';
        const coilTemp = currentUnitData.sensors?.coil_temp;
        const defrostThreshold = parseFloat(config['defrost.coil_temperature']) || 45;
        const canDefrost = coilTemp && coilTemp < defrostThreshold;

        const resetBtn = document.querySelector('button[onclick="resetAlarm()"]');
        const defrostBtn = document.querySelector('button[onclick="sendDefrost()"]');
        if (resetBtn) resetBtn.style.display = hasAlarm ? 'block' : 'none';
        if (defrostBtn) defrostBtn.style.display = canDefrost ? 'block' : 'none';

        // Fetch demo mode status separately
        fetch('/api/unit/' + currentUnitId + '/demo-mode')
        .then(r => r.json())
        .then(d => {
            console.log('Demo mode status:', d.demo_mode);
            currentUnitData.demo_mode = d.demo_mode;
            const demoMode = d.demo_mode ? 'Enabled' : 'Disabled';
            const demoStyle = d.demo_mode ? 'color: #FF9800;' : '';
            const demoModeEl = document.getElementById('demoModeStatus');
            if (demoModeEl) {
                demoModeEl.innerHTML = '<strong>Demo Mode:</strong> ' + demoMode;
                demoModeEl.style.cssText = demoStyle;
            }
        })
        .catch(e => console.error('Failed to fetch demo mode:', e));
    })
    .catch(e => {
        console.error('Failed to fetch system info:', e);
        // Set defaults if fetch fails
        document.getElementById('defrostCoilTempInput').value = '45';
        document.getElementById('defrostIntervalInput').value = '8';
        document.getElementById('defrostTimeoutInput').value = '45';
        document.getElementById('loggingIntervalInput').value = '5';
        document.getElementById('loggingRetentionInput').value = '30';
        document.getElementById('compressorTimerInput').value = '5';
        document.getElementById('setpointLowInput').value = '-20';
        document.getElementById('setpointHighInput').value = '80';
        document.getElementById('setpointOffsetInput').value = '2';
        document.getElementById('electricHeatInput').value = '0';
        document.getElementById('fanContinuousInput').value = '0';
        document.getElementById('relayActiveLowInput').value = '0';
        document.getElementById('unitNumberInput').value = '0';
        console.log('Using default config values');
    });
}

function backToDashboard() {
    document.getElementById('controlPage').style.display = 'none';
    document.getElementById('units').parentElement.style.display = 'block';
    currentUnitId = null;
    currentUnitData = null;
}

function resetAlarm() {
    if (!confirm('Reset alarm for unit ' + currentUnitId + '?')) return;
    const proxyUrl = '/api/unit/' + currentUnitId + '/alarms/reset';
    console.log('Resetting alarm via proxy:', proxyUrl);
    fetch(proxyUrl, {
        method: 'POST'
    })
    .then(r => {
        console.log('Reset alarm response status:', r.status, r.statusText);
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.json();
    })
    .then(d => {
        console.log('Reset alarm full response:', JSON.stringify(d, null, 2));
        const responseStr = 'Status: ' + (d.success ? 'SUCCESS' : 'FAILED') + '\n' + JSON.stringify(d, null, 2);
        alert('RESET ALARM RESPONSE:\n\n' + responseStr);
        if (d.success) {
            setTimeout(function() {
                if (currentUnitId) {
                    refreshControlPanel();
                } else {
                    updateUnits();
                }
            }, 500);
        }
    })
    .catch(e => {
        console.error('Reset alarm error:', e);
        alert('Failed to reset alarm: ' + e.message + '\n\nCheck browser console (F12) for details');
    });
}

function sendDefrost() {
    const coilTemp = currentUnitData.sensors?.coil_temp;
    if (!coilTemp || coilTemp > 45) {
        alert('Cannot send defrost - coil temp is too high (' + (coilTemp || 'N/A') + '°F). Must be below 45°F');
        return;
    }
    if (!confirm('Send defrost command?')) return;
    const proxyUrl = '/api/unit/' + currentUnitId + '/defrost/trigger';
    fetch(proxyUrl, {method: 'POST'})
    .then(r => r.json())
    .then(d => {
        console.log('Defrost response:', JSON.stringify(d, null, 2));
        const responseStr = 'Status: ' + (d.success ? 'SUCCESS' : 'FAILED') + '\n' + JSON.stringify(d, null, 2);
        alert('DEFROST RESPONSE:\n\n' + responseStr);
        if (d.success) {
            setTimeout(function() {
                if (currentUnitId) {
                    refreshControlPanel();
                } else {
                    updateUnits();
                }
            }, 500);
        }
    })
    .catch(e => alert('Failed: ' + e.message + '\n\nCheck console for details'));
}

function toggleDemoMode() {
    if (!confirm('Toggle demo mode?')) return;
    const proxyUrl = '/api/unit/' + currentUnitId + '/demo-mode';
    const enable = !(currentUnitData.demo_mode || false);
    fetch(proxyUrl, {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({enable: enable})
    })
    .then(r => r.json())
    .then(d => {
        console.log('Demo mode response:', JSON.stringify(d, null, 2));
        const responseStr = 'Status: ' + (d.success ? 'SUCCESS' : 'FAILED') + '\n' + JSON.stringify(d, null, 2);
        alert('DEMO MODE RESPONSE:\n\n' + responseStr);
        if (d.success) {
            setTimeout(function() {
                if (currentUnitId) {
                    refreshControlPanel();
                } else {
                    updateUnits();
                }
            }, 500);
        }
    })
    .catch(e => alert('Failed: ' + e.message + '\n\nCheck console for details'));
}

function updateSetpoint() {
    const newSetpoint = document.getElementById('setpointInput').value;
    if (!newSetpoint) {
        alert('Please enter a setpoint value');
        return;
    }
    if (!confirm('Update setpoint to ' + newSetpoint + '°F?')) return;
    const proxyUrl = '/api/unit/' + currentUnitId + '/setpoint';
    fetch(proxyUrl, {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({setpoint: parseFloat(newSetpoint)})
    })
    .then(r => r.json())
    .then(d => {
        console.log('Setpoint response:', JSON.stringify(d, null, 2));
        const responseStr = JSON.stringify(d, null, 2);
        if (d.error) {
            alert('SETPOINT ERROR:\n\n' + responseStr);
        } else {
            alert('SETPOINT UPDATED:\n\n' + responseStr);
            // Refresh control panel immediately to show updated value
            setTimeout(function() {
                if (currentUnitId) {
                    refreshControlPanel();
                } else {
                    updateUnits();
                }
            }, 500);
        }
    })
    .catch(e => alert('Failed: ' + e.message + '\n\nCheck console for details'));
}

function updateConfig() {
    const configData = {};

    // Map input IDs to config field names
    const fields = {
        'defrostCoilTempInput': 'defrost.coil_temperature',
        'defrostIntervalInput': 'defrost.interval_hours',
        'defrostTimeoutInput': 'defrost.timeout_mins',
        'loggingIntervalInput': 'logging.interval_mins',
        'loggingRetentionInput': 'logging.retention_period',
        'compressorTimerInput': 'compressor.off_timer',
        'setpointLowInput': 'setpoint.low_limit',
        'setpointHighInput': 'setpoint.high_limit',
        'setpointOffsetInput': 'setpoint.offset',
        'electricHeatInput': 'unit.electric_heat',
        'fanContinuousInput': 'unit.fan_continuous',
        'relayActiveLowInput': 'unit.relay_active_low',
        'unitNumberInput': 'unit.number'
    };

    // Collect any changed fields
    for (const [inputId, fieldName] of Object.entries(fields)) {
        const input = document.getElementById(inputId);
        if (input && input.value !== '') {
            configData[fieldName] = input.value;
        }
    }

    if (Object.keys(configData).length === 0) {
        alert('Please enter at least one configuration value');
        return;
    }

    if (!confirm('Update ' + Object.keys(configData).length + ' configuration value(s)?')) return;
    const proxyUrl = '/api/unit/' + currentUnitId + '/config';

    fetch(proxyUrl, {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(configData)
    })
    .then(r => r.json())
    .then(d => {
        console.log('Config update response:', JSON.stringify(d, null, 2));
        const responseStr = JSON.stringify(d, null, 2);
        if (d.success) {
            alert('CONFIG UPDATE SUCCESS:\n\n' + responseStr);
            // Refresh control panel immediately to show updated values
            setTimeout(refreshControlPanel, 500);
        } else {
            alert('CONFIG UPDATE RESPONSE:\n\n' + responseStr);
            setTimeout(refreshControlPanel, 500);
        }
    })
    .catch(e => alert('Failed: ' + e.message + '\n\nCheck console for details'));
}

function logoutDashboard() {
    localStorage.removeItem('dashboardPassword');
    document.getElementById('loginStatusBar').style.display = 'none';
    backToDashboard();
    updateUnits();
}

function downloadTodayAndYesterdayLogs() {
    const today = new Date();
    const yesterday = new Date(today);
    yesterday.setDate(yesterday.getDate() - 1);

    const formatDate = (date) => {
        const year = date.getFullYear();
        const month = String(date.getMonth() + 1).padStart(2, '0');
        const day = String(date.getDate()).padStart(2, '0');
        return year + '-' + month + '-' + day;
    };

    const todayStr = formatDate(today);
    const yesterdayStr = formatDate(yesterday);

    // Download both files
    downloadEventLog(todayStr);
    downloadEventLog(yesterdayStr);
}

function downloadEventLog(dateStr) {
    const url = '/api/v1/logs/events?date=' + dateStr;
    console.log('Downloading event log from:', url);

    fetch(url)
    .then(response => {
        if (!response.ok) {
            return response.text().then(text => {
                alert('Failed to download events log for ' + dateStr + ':\n\n' + (text || response.statusText));
                throw new Error(response.statusText);
            });
        }
        return response.blob();
    })
    .then(blob => {
        const downloadUrl = window.URL.createObjectURL(blob);
        const link = document.createElement('a');
        link.href = downloadUrl;
        link.download = 'events_' + dateStr + '.log';
        document.body.appendChild(link);
        link.click();
        link.parentNode.removeChild(link);
        window.URL.revokeObjectURL(downloadUrl);
        alert('Successfully downloaded events log for ' + dateStr);
    })
    .catch(error => {
        console.error('Download error:', error);
    });
}

// Modal close on outside click
window.onclick = function(event) {
    const modal = document.getElementById('loginModal');
    if (event.target === modal) {
        closeLoginModal();
    }
}

// Initialize on page load
document.addEventListener('DOMContentLoaded', function() {
    initDarkMode();
    updateUnits();
    startDashboardRefresh();  // Start background refresh every 5 minutes
});

// Expose logoutDashboard to window for onclick handlers
window.logoutDashboard = logoutDashboard;
