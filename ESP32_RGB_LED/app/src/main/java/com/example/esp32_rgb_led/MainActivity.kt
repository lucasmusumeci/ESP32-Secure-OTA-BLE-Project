package com.example.esp32_rgb_led

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.example.esp32_rgb_led.ui.theme.ESP32_RGB_LEDTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            ESP32_RGB_LEDTheme {
                MainScreen()
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen() {
    val context = LocalContext.current
    val bleManager = remember { BleManager(context) }
    
    val scannedDevices by bleManager.scannedDevices.collectAsState()
    val isConnected by bleManager.isConnected.collectAsState()
    
    var hasPermissions by remember {
        mutableStateOf(
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED &&
                ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
            } else {
                ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
            }
        )
    }

    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        hasPermissions = permissions.values.all { it }
    }

    LaunchedEffect(Unit) {
        if (!hasPermissions) {
            val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
            } else {
                arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
            }
            permissionLauncher.launch(permissions)
        }
    }

    Scaffold(
        topBar = {
            CenterAlignedTopAppBar(title = { Text("ESP32 RGB Controller") })
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .fillMaxSize()
                .padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            if (!hasPermissions) {
                Text("Bluetooth permissions are required.")
                Button(onClick = {
                    val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                        arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
                    } else {
                        arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
                    }
                    permissionLauncher.launch(permissions)
                }) {
                    Text("Grant Permissions")
                }
            } else if (!isConnected) {
                Text("Scan for your ESP32-S3", style = MaterialTheme.typography.titleMedium)
                Spacer(modifier = Modifier.height(8.dp))
                Row {
                    Button(onClick = { bleManager.startScanning() }) {
                        Text("Start Scan")
                    }
                    Spacer(modifier = Modifier.width(8.dp))
                    Button(onClick = { bleManager.stopScanning() }) {
                        Text("Stop Scan")
                    }
                }
                Spacer(modifier = Modifier.height(16.dp))
                LazyColumn(modifier = Modifier.fillMaxSize()) {
                    items(scannedDevices) { device ->
                        DeviceItem(device) {
                            bleManager.stopScanning()
                            bleManager.connect(device)
                        }
                    }
                }
            } else {
                Text("Connected!", color = Color.Green, style = MaterialTheme.typography.titleLarge)
                Spacer(modifier = Modifier.height(16.dp))
                
                ColorPicker { r, g, b ->
                    bleManager.sendColor(r, g, b)
                }
                
                Spacer(modifier = Modifier.height(32.dp))
                Button(onClick = { bleManager.disconnect() }) {
                    Text("Disconnect")
                }
            }
        }
    }
}

@SuppressLint("MissingPermission")
@Composable
fun DeviceItem(device: BluetoothDevice, onClick: () -> Unit) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp)
            .clickable { onClick() }
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text(text = device.name ?: "Unknown Device", style = MaterialTheme.typography.bodyLarge)
            Text(text = device.address, style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Composable
fun ColorPicker(onColorChanged: (Int, Int, Int) -> Unit) {
    var red by remember { mutableFloatStateOf(0f) }
    var green by remember { mutableFloatStateOf(0f) }
    var blue by remember { mutableFloatStateOf(0f) }

    val currentColor = Color(red / 255f, green / 255f, blue / 255f)

    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Box(
            modifier = Modifier
                .size(100.dp)
                .background(currentColor)
        )
        Spacer(modifier = Modifier.height(16.dp))

        Text("Red: ${red.toInt()}")
        Slider(value = red, onValueChange = { 
            red = it
            onColorChanged(red.toInt(), green.toInt(), blue.toInt())
        }, valueRange = 0f..255f)

        Text("Green: ${green.toInt()}")
        Slider(value = green, onValueChange = { 
            green = it
            onColorChanged(red.toInt(), green.toInt(), blue.toInt())
        }, valueRange = 0f..255f)

        Text("Blue: ${blue.toInt()}")
        Slider(value = blue, onValueChange = { 
            blue = it
            onColorChanged(red.toInt(), green.toInt(), blue.toInt())
        }, valueRange = 0f..255f)
    }
}
