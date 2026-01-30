import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';
import 'dart:async';
import 'dart:convert';

const String DEVICE_NAME = "Pulse Oximeter ESP32";
const String SERVICE_UUID = "13f7b509-a5e3-4469-9308-4b219e12c53b";
const String BPM_CHAR_UUID = "17c0efa6-c395-4294-b463-5c6cd560bf91";
const String SPO2_CHAR_UUID = "63438932-e944-4ba5-a56a-2deb646a5cd2";

// Connection states for UI
enum BleConnectionState{
  idle, scanning, connecting, connected, error,
}

class Vitals{
  int? BPM;
  int? SPO2;
  Vitals({this.BPM, this.SPO2});
}


class BleController {

  final ValueNotifier<BleConnectionState> state = ValueNotifier<BleConnectionState>(BleConnectionState.idle);
  final ValueNotifier<String> statusText = ValueNotifier<String>("");

  final StreamController<Vitals> vitalsController = StreamController<Vitals>.broadcast();
  Stream<Vitals> get vitalsStream => vitalsController.stream;
  Vitals latest_vitals = Vitals();

  BluetoothDevice? device;
  BluetoothCharacteristic? BPM_char;
  BluetoothCharacteristic? SPO2_char;

  StreamSubscription<List<int>>? BPM_sub;
  StreamSubscription<List<int>>? SPO2_sub;

  Future<void> connectBLE() async{
    try{
      state.value = BleConnectionState.scanning;
      statusText.value = "Scanning...";
      await ensurePermission();
      await ensureBluetoothON();

      final found = await scanAndFindDevice(targetName: DEVICE_NAME, serviceUUID: Guid(SERVICE_UUID), scanSeconds:8,);
      if(found == null)throw Exception("Device not found");

      device = found;
      state.value = BleConnectionState.connecting;
      statusText.value = "Connecting to ${found.platformName}...";
      await device!.connect(timeout: const Duration(seconds:12));
      final services = await device!.discoverServices();
      findCharacteristics(services);
      if(BPM_char  == null || SPO2_char == null){
        throw Exception("Required characteristics not found");
      }
      await BPM_char!.setNotifyValue(true);
      await SPO2_char!.setNotifyValue(true);
      listenNotifications();
      state.value = BleConnectionState.connected;
    } catch(e){
      state.value = BleConnectionState.error;
      statusText.value = e.toString();
      debugPrint("BLE error: $e");
    }
  }

  Future<void> disconnect() async{
    await BPM_sub?.cancel();
    BPM_sub = null;
    await SPO2_sub?.cancel();
    SPO2_sub = null;

    try{
      if(device != null) await device!.disconnect();
    }catch(e){}
    device = null;
    BPM_sub = null;
    SPO2_sub = null;
    state.value = BleConnectionState.idle;
    statusText.value = "";
  }

  void dispose(){
    disconnect();
    state.dispose();
    statusText.dispose();
    vitalsController.close();
  }

  Future<void> ensurePermission() async {
    if(defaultTargetPlatform == TargetPlatform.android){
      final scan = await Permission.bluetoothScan.request();
      if(!scan.isGranted) throw Exception("Scan denied");
      final connect = await Permission.bluetoothConnect.request();
      if(!connect.isGranted) throw Exception("Connection denied");
    }

    if(defaultTargetPlatform == TargetPlatform.iOS){
      return;
    }
  }

  Future<void> ensureBluetoothON() async {
    final adapter = await FlutterBluePlus.adapterState.first;
    if(adapter!= BluetoothAdapterState.on) throw Exception("BLE is OFF");
  }

  Future<BluetoothDevice?> scanAndFindDevice({ required String targetName, required Guid serviceUUID, required int scanSeconds,}) async {
    BluetoothDevice? found;
    final completer = Completer<BluetoothDevice?>();

    bool matchName(BluetoothDevice device){
      final name = device.platformName.trim().toLowerCase();
      final target = targetName.trim().toLowerCase();
      if(name.isEmpty) return false;
      return name == target || name.contains(target) || name.contains("pulse");
    }
    try{ await FlutterBluePlus.stopScan();} catch(_){}

    late final StreamSubscription<List<ScanResult>> subscription;
    subscription = FlutterBluePlus.scanResults.listen((results) async{
      for (final sub in results){
        if(matchName(sub.device)){
          found = sub.device;
          try{ await FlutterBluePlus.stopScan();} catch(_){}
          await subscription.cancel();
          if (!completer.isCompleted) completer.complete(found);
          return;
        }
      }
    });
    await FlutterBluePlus.startScan(timeout: Duration(seconds: scanSeconds));

    Future.delayed(Duration(seconds: scanSeconds), () async{
      try{ await FlutterBluePlus.stopScan();} catch(_){}
      await subscription.cancel();
      if (!completer.isCompleted) completer.complete(null);
    });

    return completer.future;
  }

  void findCharacteristics(List<BluetoothService> services){
    final serviceId = Guid(SERVICE_UUID);
    final bpmId = Guid(BPM_CHAR_UUID);
    final spo2Id = Guid(SPO2_CHAR_UUID);
    for(final service in services){
      if(service.uuid != serviceId) continue;
      for(final characteristic in service.characteristics){
        if(characteristic.uuid == bpmId) BPM_char = characteristic;
        if(characteristic.uuid == spo2Id) SPO2_char = characteristic;
      }
    }
  }

  void listenNotifications(){
    int? parseInt(List<int> bytes){
      final valueVitals = bytes.where((b) => b != 0).toList();
      if(valueVitals.isEmpty) return null;
      final vitalsInt =  utf8.decode(valueVitals, allowMalformed: true).trim();
      return int.tryParse(vitalsInt);
    }

    BPM_sub= BPM_char!.lastValueStream.listen((bytes){
      final value = parseInt(bytes);
      if(value!=null){
        latest_vitals.BPM = value;
        vitalsController.add(latest_vitals);
      }
    });
    SPO2_sub= SPO2_char!.lastValueStream.listen((bytes){
      final value = parseInt(bytes);
      if(value!=null){
        latest_vitals.SPO2 = value;
        vitalsController.add(latest_vitals);
      }
    });

  }
}



