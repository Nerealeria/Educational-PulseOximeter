import 'package:flutter/material.dart';
import 'ble_controller.dart';
import 'ppgvalues_screen.dart';

class WelcomePage extends StatelessWidget{
  final BleController ble;
  const WelcomePage({super.key, required this.ble});

  @override
  Widget build(BuildContext context){
    return Scaffold(
      body: Padding(
        padding: const EdgeInsets.all(24),
        child: Center(
          child: ValueListenableBuilder<BleConnectionState>(valueListenable: ble.state,
            builder: (context, state,_){
              if (state == BleConnectionState.connected){
                WidgetsBinding.instance.addPostFrameCallback((_){
                  Navigator.pushReplacement(context, MaterialPageRoute(builder: (_) => PPGvaluesPage(ble:ble)),
                  );
                });
              }
              return Column(
                mainAxisSize: MainAxisSize.min,
                children:[ const Text( "WELCOME TO\n PULSE OXIMETER APP", textAlign: TextAlign.center,
                  style: TextStyle(fontSize: 28, fontWeight: FontWeight.bold),),
                  const SizedBox(height:30),
                  ElevatedButton(onPressed: (state == BleConnectionState.scanning ||
                      state == BleConnectionState.connecting)? null:() => ble.connectBLE(),
                    child: const Text("CONNECT TO PULSE OXIMETER VIA BLE"),),
                  const SizedBox(height: 16),

                  ValueListenableBuilder<String>(valueListenable: ble.statusText,
                    builder: (context, msg, _) => Text(msg),),

                ],
              );
            },
          ),
        ),
      ),
    );
  }
}