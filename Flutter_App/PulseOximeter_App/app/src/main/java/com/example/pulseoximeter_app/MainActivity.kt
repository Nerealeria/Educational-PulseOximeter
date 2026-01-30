package com.example.pulseoximeter_app

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanSettings
import android.os.Bundle
import android.os.Build
import android.widget.Toast
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import android.widget.Button
import androidx.activity.result.ActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import java.util.UUID
import android.bluetooth.le.*
import android.bluetooth.*
import android.content.*
import android.content.pm.*
import android.widget.TextView
import androidx.annotation.RequiresPermission
import androidx.core.app.ActivityCompat


class MainActivity : AppCompatActivity() {

    private val DEVICE_NAME = "Pulse Oximeter ESP32"
    private val SERVICE_UUID = UUID.fromString("13f7b509-a5e3-4469-9308-4b219e12c53b")
    private val BPM_CHAR_UUID = UUID.fromString("17c0efa6-c395-4294-b463-5c6cd560bf91")
    private val SPO2_CHAR_UUID = UUID.fromString("63438932-e944-4ba5-a56a-2deb646a5cd2")
    private val CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    private var scanning = false
    private var BLE_adapter : BluetoothAdapter? = null
    private var scanner : BluetoothLeScanner? = null
    private var gatt : BluetoothGatt? = null
    private lateinit var BPM_value: TextView
    private lateinit var SpO2_value: TextView

    private val BLE_permissionsLauncher = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()){
            results ->
        val allGranted = results.values.all{it}
        if(!allGranted){
            Toast.makeText(this, "BLE permissions denied", Toast.LENGTH_SHORT).show()
            return@registerForActivityResult
            // Return@... stops only this block of code and not all the functions
        }
        ensureBLE_On_Scan()
    }

    private val enableBLE_Launcher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()){
        result ->
        if(result.resultCode == RESULT_OK){
            startScan()
        }else{
            Toast.makeText(this, "BLE not active", Toast.LENGTH_SHORT).show()
        }
    }


    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContentView(R.layout.activity_main)
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.main)) { v, insets ->
            val systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
            v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom)
            insets
        }

        val BLE_manager = getSystemService(BluetoothManager::class.java)
        BLE_adapter = BLE_manager.adapter
        scanner = BLE_adapter?.bluetoothLeScanner

        val BLE_button = findViewById<Button>(R.id.BLE_Connect)
        BLE_button.setOnClickListener{
            Toast.makeText(this, "BLE button pressed", Toast.LENGTH_SHORT).show()
            BLE_permissionsRequest()
        }

        BPM_value = findViewById(R.id.BPM_value)
        SpO2_value = findViewById(R.id.SpO2_value)

    }
    private fun BLE_permissionsRequest(){
        if(BLE_adapter == null){
            Toast.makeText(this, "Device doesn't support BLE", Toast.LENGTH_LONG).show()
            return
        }
        if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.S){
            BLE_permissionsLauncher.launch(arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT ))
        }else{
            BLE_permissionsLauncher.launch(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION))
        }
    }

    private fun ensureBLE_On_Scan(){

        val adapter = BLE_adapter
        if (adapter == null){
            return
        }
        if(!adapter.isEnabled){
            val intent = Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE)
            enableBLE_Launcher.launch(intent)
        }else{
            startScan()
        }
    }

    private fun startScan(){
        if(scanning) return // Prevents starting a new scan if one is already running.

        val leScanner = scanner
        if(leScanner == null){
            Toast.makeText(this, "Not possible to start the scanner", Toast.LENGTH_SHORT).show()
            return
        }

        BPM_value.text = "--"
        SpO2_value.text = "--"
        Toast.makeText(this, "Scanning...", Toast.LENGTH_SHORT).show()
        scanning = true;
        leScanner.startScan(scanCallback)

    }

    private fun stopScan(){
        if(!scanning) return
        scanner?.stopScan(scanCallback)
        scanning = false
    }

    private val scanCallback = object : ScanCallback(){ // Creates an object that listens to scan events
        override fun onScanResult(callbackType: Int, result: ScanResult){

            val device = result.device // Get the BLE device found
            if(device == null) return
            val deviceName = device.name
            if(deviceName == null) return

            if(deviceName == DEVICE_NAME){
                stopScan()
                connect(device)
            }
        }
        override fun onScanFailed(errorCode: Int){
            scanning = false
            toastText( "Scan failed")

        }
    }

    private fun connect(device : BluetoothDevice){
        toastText("Connecting to ${device.name}")
        if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.S){
            val ok = ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) ==
                    PackageManager.PERMISSION_GRANTED
            if(!ok){
                toastText("Missing bluetooth permission")
                return
            }
        }
        gatt?.close()
        gatt = device.connectGatt(this, false, gattCallback)
    }

    private fun toastText(msg: String){
        runOnUiThread {
            Toast.makeText(this,msg, Toast.LENGTH_SHORT).show()
        }
    }

    private val gattCallback = object : BluetoothGattCallback(){

        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int){
            if(status != BluetoothGatt.GATT_SUCCESS){
                toastText("GATT error")
                gatt.close()
                return
            }
            when(newState){
                BluetoothProfile.STATE_CONNECTED -> {
                    toastText("Connected")
                    gatt.discoverServices()
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    toastText("Disconnected")
                    gatt.close()
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int){
            if(status != BluetoothGatt.GATT_SUCCESS){
                toastText(" DiscoverServices failed: $status")
                return
            }
            val service = gatt.getService(SERVICE_UUID)
            if (service == null){
                toastText("Service UUID not found")
                return
            }
            val BPM_Char = service.getCharacteristic(BPM_CHAR_UUID)
            if (BPM_Char == null){
                toastText("BPM characteristic not found")
                return
            }
            val SpO2_Char = service.getCharacteristic(SPO2_CHAR_UUID)
            if (SpO2_Char == null){
                toastText("SpO2 characteristic not found")
                return
            }

            enableNotify(gatt, BPM_Char)
            enableNotify(gatt, SpO2_Char)

        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            val data = characteristic.value
            if(data == null || data.isEmpty()) return
            val value = data[0].toInt() and 0xFF

            runOnUiThread {
                when(characteristic.uuid){
                    BPM_CHAR_UUID -> BPM_value.text = value.toString()
                    SPO2_CHAR_UUID -> SpO2_value.text = value.toString()

                }
            }


        }
    }

    private fun enableNotify(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic){
        val char_ok = gatt.setCharacteristicNotification(characteristic, true)
        if(!char_ok){
            toastText("Set characteristic notification failed")
            return
        }

        val CCCD = characteristic.getDescriptor(CCCD_UUID)
        if(CCCD == null){
            toastText("CCCD (0X2902) not found")
            return
        }
        CCCD.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        gatt.writeDescriptor(CCCD)
    }

    override fun onDestroy(){
        super.onDestroy()
        stopScan()
        gatt?.close()
        gatt = null
    }

    private fun hasScanPermission():Boolean{
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
                ActivityCompat.checkSelfPermission(this,Manifest.permission.BLUETOOTH_SCAN) ==
                PackageManager.PERMISSION_GRANTED
    }

    private fun hasConnectPermission():Boolean{
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
                ActivityCompat.checkSelfPermission(this,Manifest.permission.BLUETOOTH_CONNECT) ==
                PackageManager.PERMISSION_GRANTED
    }







}