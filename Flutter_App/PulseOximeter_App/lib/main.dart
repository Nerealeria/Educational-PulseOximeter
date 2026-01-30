import 'package:flutter/material.dart';
import 'ble_controller.dart';
import 'welcome_screen.dart';


void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  // This widget is the root of your application.
  @override
  Widget build(BuildContext context) {
    final ble = BleController();
    return MaterialApp(debugShowCheckedModeBanner: false, home: WelcomePage(ble:ble),
    );
  }
}