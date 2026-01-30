import 'package:flutter/material.dart';
import 'ble_controller.dart';

class PPGvaluesPage extends StatelessWidget{
  final BleController ble;
  const PPGvaluesPage({super.key, required this.ble});

  @override
  Widget build(BuildContext context){
    return Scaffold(
      appBar: AppBar(title: const Text("Live Measurement"), actions:[
        IconButton(icon: const Icon(Icons.logout), onPressed: () async{
          await ble.disconnect();
          Navigator.pop(context); // Closes current screen and goes back to the previous one
        },
        ),
      ],
      ),
      body: StreamBuilder<Vitals>(stream: ble.vitalsStream, builder: (context, snapshot){
        final BPM = snapshot.data?.BPM;
        final SPO2 = snapshot.data?.SPO2;

        return Padding(padding: const EdgeInsets.all(24),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [ card("BPM", BPM?.toString()?? "--"), const SizedBox(height: 16),
              card("SPO2 (%)", SPO2?.toString()?? "--"), const SizedBox(height:24),
              const Text(
                "Normal ranges (resting adult):\n - BPM: ~60-100 bpm\n - SpO2: ~95-100%\n",
                style:TextStyle(fontSize: 16),
              ),
            ],
          ),
        );
      },
      ),
    );
  }

  Widget card(String title, String value){
    return Card(
      child: Padding(padding: const EdgeInsets.all(18),
        child: Column(
          children: [Text(title, style: const TextStyle(fontSize:18)), const SizedBox(height:8),
            Text(value, style: const TextStyle(fontSize:44, fontWeight: FontWeight.bold),),
          ],
        ),
      ),
    );
  }
}