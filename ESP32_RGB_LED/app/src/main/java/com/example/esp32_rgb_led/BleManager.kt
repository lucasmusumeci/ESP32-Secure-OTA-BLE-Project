package com.example.esp32_rgb_led

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Context
import android.os.Build
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.util.*

@SuppressLint("MissingPermission")
class BleManager(private val context: Context) {

    private val TAG = "BLE_DEBUG"

    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        bluetoothManager.adapter
    }

    private var bluetoothGatt: BluetoothGatt? = null
    private var rgbCharacteristic: BluetoothGattCharacteristic? = null

    private val _scannedDevices = MutableStateFlow<List<BluetoothDevice>>(emptyList())
    val scannedDevices: StateFlow<List<BluetoothDevice>> = _scannedDevices

    private val _isConnected = MutableStateFlow(false)
    val isConnected: StateFlow<Boolean> = _isConnected

    // 128-bit UUIDs matching the updated ESP32 C code
    private val SERVICE_UUID = UUID.fromString("4fafc201-1fb5-459e-8fcc-c5c9c331914b")
    private val CHARACTERISTIC_UUID = UUID.fromString("beb5483e-36e1-4688-b7f5-ea07361b26a8")

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device
            if (device.name != null && !_scannedDevices.value.contains(device)) {
                Log.d(TAG, "Device found: ${device.name} - ${device.address}")
                _scannedDevices.value = _scannedDevices.value + device
            }
        }
    }

    fun startScanning() {
        Log.d(TAG, "Starting Scan...")
        _scannedDevices.value = emptyList()
        bluetoothAdapter?.bluetoothLeScanner?.startScan(scanCallback)
    }

    fun stopScanning() {
        Log.d(TAG, "Stopping Scan")
        bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
    }

    fun connect(device: BluetoothDevice) {
        Log.d(TAG, "Connecting to ${device.address}...")
        bluetoothGatt = device.connectGatt(context, false, gattCallback)
    }

    fun disconnect() {
        Log.d(TAG, "Disconnecting...")
        bluetoothGatt?.disconnect()
        bluetoothGatt?.close()
        bluetoothGatt = null
        _isConnected.value = false
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.i(TAG, "Connected to GATT server.")
                _isConnected.value = true
                gatt.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.i(TAG, "Disconnected from GATT server.")
                _isConnected.value = false
                rgbCharacteristic = null
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val service = gatt.getService(SERVICE_UUID)
                rgbCharacteristic = service?.getCharacteristic(CHARACTERISTIC_UUID)
                if (rgbCharacteristic != null) {
                    Log.i(TAG, "RGB Characteristic found!")
                } else {
                    Log.e(TAG, "RGB Characteristic NOT found. Check UUIDs.")
                    gatt.services.forEach { s ->
                        Log.d(TAG, "Discovered Service: ${s.uuid}")
                        s.characteristics.forEach { c ->
                            Log.d(TAG, "   Char: ${c.uuid}")
                        }
                    }
                }
            }
        }
    }

    fun sendColor(red: Int, green: Int, blue: Int) {
        val gatt = bluetoothGatt ?: return
        val characteristic = rgbCharacteristic ?: return
        val data = byteArrayOf(red.toByte(), green.toByte(), blue.toByte())
        
        Log.d(TAG, "Sending RGB: $red, $green, $blue")
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            gatt.writeCharacteristic(characteristic, data, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
        } else {
            @Suppress("DEPRECATION")
            characteristic.value = data
            @Suppress("DEPRECATION")
            gatt.writeCharacteristic(characteristic)
        }
    }
}
