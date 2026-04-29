import 'dart:convert';
import 'package:http/http.dart' as http;
import 'package:flutter/material.dart';
import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:fl_chart/fl_chart.dart';
import 'firebase_options.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await Firebase.initializeApp(
    options: DefaultFirebaseOptions.currentPlatform,
  );
  runApp(const MyApp());
}

/// Shared selected device notifier
final ValueNotifier<String> selectedDeviceNotifier =
ValueNotifier<String>('roomA_2gate');

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'Smart Room Monitor',
      theme: ThemeData(
        primarySwatch: Colors.blue,
        scaffoldBackgroundColor: Colors.grey.shade100,
      ),
      home: const MainScreen(),
    );
  }
}

class MainScreen extends StatefulWidget {
  const MainScreen({super.key});

  @override
  State<MainScreen> createState() => _MainScreenState();
}

class _MainScreenState extends State<MainScreen> {
  int selectedIndex = 0;

  final pages = const [
    DashboardScreen(),
    AnalyticsScreen(),
    SettingsScreen(),
  ];

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: pages[selectedIndex],
      bottomNavigationBar: BottomNavigationBar(
        currentIndex: selectedIndex,
        onTap: (index) {
          setState(() {
            selectedIndex = index;
          });
        },
        items: const [
          BottomNavigationBarItem(
            icon: Icon(Icons.dashboard),
            label: "Dashboard",
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.analytics),
            label: "Analytics",
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.settings),
            label: "Settings",
          ),
        ],
      ),
    );
  }
}

class DashboardScreen extends StatelessWidget {
  const DashboardScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder<String>(
      valueListenable: selectedDeviceNotifier,
      builder: (context, selectedDeviceId, _) {
        final DatabaseReference deviceRef =
        FirebaseDatabase.instance.ref("devices/$selectedDeviceId");



        return StreamBuilder<DatabaseEvent>(
          stream: deviceRef.onValue,
          builder: (context, snapshot) {
            Map<String, dynamic>? deviceData;

            if (snapshot.hasData &&
                snapshot.data!.snapshot.value != null &&
                snapshot.data!.snapshot.value is Map) {
              deviceData = Map<String, dynamic>.from(
                snapshot.data!.snapshot.value as Map,
              );
            }

            final roomName = deviceData?['roomName']?.toString() ?? 'Loading...';
            final deviceId = deviceData?['deviceId']?.toString() ?? selectedDeviceId;
            final gateType = (deviceData?['gateType'] as num?)?.toInt() ?? 1;
            final roomCapacity = (deviceData?['roomCapacity'] as num?)?.toInt() ?? 0;
            final currentCount = (deviceData?['currentCount'] as num?)?.toInt() ?? 0;
            final inCount = (deviceData?['inCount'] as num?)?.toInt() ?? 0;
            final outCount = (deviceData?['outCount'] as num?)?.toInt() ?? 0;
            final gate1Status = deviceData?['gate1Status']?.toString() ?? 'inactive';
            final gate2Status = deviceData?['gate2Status']?.toString() ?? 'inactive';
            final status = deviceData?['status']?.toString() ?? 'offline';
            final lastUpdateMs = (deviceData?['lastUpdateMs'] as num?)?.toInt() ?? 0;

            final bool deviceConnected = status.toLowerCase() == "online";

            return Scaffold(
              appBar: AppBar(
                title: const Text("GateKeeper"),
                actions: [
                  Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 12),
                    child: Row(
                      children: [
                        Icon(
                          Icons.circle,
                          color: deviceConnected ? Colors.green : Colors.red,
                          size: 12,
                        ),
                        const SizedBox(width: 6),
                        Text(deviceConnected ? "Online" : "Offline"),
                      ],
                    ),
                  ),
                ],
              ),
              body: SingleChildScrollView(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      "Select Device / Room",
                      style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const SizedBox(height: 12),

                    /// Device dropdown
                    /// Device dropdown (DYNAMIC)
                    StreamBuilder<DatabaseEvent>(
                      stream: FirebaseDatabase.instance.ref("devices").onValue,
                      builder: (context, snapshot) {
                        if (!snapshot.hasData || snapshot.data!.snapshot.value == null) {
                          return const Center(child: CircularProgressIndicator());
                        }

                        final data = Map<String, dynamic>.from(
                          snapshot.data!.snapshot.value as Map,
                        );

                        final List<DropdownMenuItem<String>> items = [];

                        data.forEach((key, value) {
                          final device = Map<String, dynamic>.from(value);

                          final roomName = device['roomName'] ?? key;
                          final gateType = device['gateType'] ?? 1;

                          items.add(
                            DropdownMenuItem(
                              value: key,
                              child: Text(
                                "$roomName (${gateType == 2 ? "2 Gate" : "1 Gate"})",
                              ),
                            ),
                          );
                        });

                        /// auto select first device if empty
                        if (selectedDeviceNotifier.value.isEmpty && items.isNotEmpty) {
                          selectedDeviceNotifier.value = items.first.value!;
                        }

                        return ValueListenableBuilder<String>(
                          valueListenable: selectedDeviceNotifier,
                          builder: (context, selectedValue, _) {
                            return Container(
                              padding: const EdgeInsets.symmetric(horizontal: 12),
                              decoration: BoxDecoration(
                                color: Colors.white,
                                borderRadius: BorderRadius.circular(12),
                                border: Border.all(color: Colors.blue.shade100),
                              ),
                              child: DropdownButton<String>(
                                value: data.containsKey(selectedValue)
                                    ? selectedValue
                                    : items.first.value,
                                isExpanded: true,
                                underline: const SizedBox(),
                                items: items,
                                onChanged: (value) {
                                  if (value != null) {
                                    selectedDeviceNotifier.value = value;
                                  }
                                },
                              ),
                            );
                          },
                        );
                      },
                    ),

                    const SizedBox(height: 20),

                    buildRoomHeader(roomName, deviceId, gateType),

                    const SizedBox(height: 20),

                    const Text(
                      "Overview",
                      style: TextStyle(
                        fontSize: 22,
                        fontWeight: FontWeight.bold,
                      ),
                    ),

                    const SizedBox(height: 16),

                    GridView(
                      shrinkWrap: true,
                      physics: const NeverScrollableScrollPhysics(),
                      gridDelegate:
                      const SliverGridDelegateWithFixedCrossAxisCount(
                        crossAxisCount: 2,
                        crossAxisSpacing: 12,
                        mainAxisSpacing: 12,
                        childAspectRatio: 1.1,
                      ),
                      children: [
                        buildGradientCard(
                          "People Count",
                          currentCount.toString(),
                          Icons.people,
                          Colors.blue,
                          Colors.blueAccent,
                        ),
                        buildGradientCard(
                          "Room Capacity",
                          roomCapacity.toString(),
                          Icons.meeting_room,
                          Colors.deepPurple,
                          Colors.purpleAccent,
                        ),
                        buildGradientCard(
                          "IN Count",
                          inCount.toString(),
                          Icons.login,
                          Colors.green,
                          Colors.lightGreen,
                        ),
                        buildGradientCard(
                          "OUT Count",
                          outCount.toString(),
                          Icons.logout,
                          Colors.orange,
                          Colors.deepOrangeAccent,
                        ),
                      ],
                    ),

                    const SizedBox(height: 20),

                    const Text(
                      "Gate Status",
                      style: TextStyle(
                        fontSize: 20,
                        fontWeight: FontWeight.bold,
                      ),
                    ),

                    const SizedBox(height: 12),

                    GridView(
                      shrinkWrap: true,
                      physics: const NeverScrollableScrollPhysics(),
                      gridDelegate:
                      const SliverGridDelegateWithFixedCrossAxisCount(
                        crossAxisCount: 2,
                        crossAxisSpacing: 12,
                        mainAxisSpacing: 12,
                        childAspectRatio: 1.2,
                      ),
                      children: [
                        buildStatusCard(
                          gateType == 1 ? "Single Gate" : "Gate 1",
                          gate1Status,
                        ),
                        if (gateType == 2)
                          buildStatusCard(
                            "Gate 2",
                            gate2Status,
                          ),
                      ],
                    ),

                    const SizedBox(height: 24),

                    buildRoomStatusRow(currentCount, roomCapacity),

                    const SizedBox(height: 20),

                    buildLastUpdateCard(lastUpdateMs),
                  ],
                ),
              ),
            );
          },
        );
      },
    );
  }

  Widget buildRoomHeader(String roomName, String deviceId, int gateType) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(16),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withOpacity(0.04),
            blurRadius: 6,
            offset: const Offset(0, 3),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            roomName,
            style: const TextStyle(
              fontSize: 22,
              fontWeight: FontWeight.bold,
            ),
          ),
          const SizedBox(height: 6),
          Text(
            "Device ID: $deviceId",
            style: TextStyle(color: Colors.grey.shade700),
          ),
          const SizedBox(height: 4),
          Text(
            gateType == 2 ? "2 Gate Monitoring System" : "1 Gate Monitoring System",
            style: TextStyle(
              color: gateType == 2 ? Colors.blue : Colors.deepPurple,
              fontWeight: FontWeight.w600,
            ),
          ),
        ],
      ),
    );
  }

  Widget buildGradientCard(
      String title,
      String value,
      IconData icon,
      Color startColor,
      Color endColor,
      ) {
    return Container(
      decoration: BoxDecoration(
        gradient: LinearGradient(
          colors: [startColor, endColor],
        ),
        borderRadius: BorderRadius.circular(16),
      ),
      padding: const EdgeInsets.all(16),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(icon, color: Colors.white, size: 34),
          const SizedBox(height: 10),
          Text(
            title,
            style: const TextStyle(
              color: Colors.white70,
              fontSize: 14,
            ),
            textAlign: TextAlign.center,
          ),
          const SizedBox(height: 10),
          Text(
            value,
            style: const TextStyle(
              color: Colors.white,
              fontSize: 24,
              fontWeight: FontWeight.bold,
            ),
          ),
        ],
      ),
    );
  }

  Widget buildStatusCard(String title, String status) {
    final bool active = status.toLowerCase() == "active";

    return Container(
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(16),
        border: Border.all(
          color: active ? Colors.green.shade200 : Colors.red.shade200,
        ),
      ),
      padding: const EdgeInsets.all(16),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(
            Icons.sensors,
            color: active ? Colors.green : Colors.red,
            size: 34,
          ),
          const SizedBox(height: 10),
          Text(
            title,
            style: const TextStyle(fontWeight: FontWeight.w600),
          ),
          const SizedBox(height: 8),
          Text(
            status.toUpperCase(),
            style: TextStyle(
              color: active ? Colors.green : Colors.red,
              fontWeight: FontWeight.bold,
            ),
          ),
        ],
      ),
    );
  }

  Widget buildRoomStatusRow(int peopleCount, int roomCapacity) {
    final bool full = roomCapacity > 0 && peopleCount >= roomCapacity;

    return Row(
      children: [
        Icon(
          full ? Icons.warning : Icons.check_circle,
          color: full ? Colors.red : Colors.green,
        ),
        const SizedBox(width: 8),
        Expanded(
          child: Text(
            full ? "Room Capacity Reached" : "Space Available",
            style: TextStyle(
              fontSize: 16,
              color: full ? Colors.red : Colors.green,
              fontWeight: FontWeight.w600,
            ),
          ),
        ),
      ],
    );
  }

  Widget buildLastUpdateCard(int lastUpdateMs) {
    final String text = lastUpdateMs == 0
        ? "No updates yet"
        : "Timestamp: $lastUpdateMs";

    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(14),
      ),
      child: Row(
        children: [
          const Icon(Icons.access_time, color: Colors.blue),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              text,
              style: const TextStyle(fontWeight: FontWeight.w500),
            ),
          ),
        ],
      ),
    );
  }
}

//
// class AnalyticsScreen extends StatelessWidget {
//   const AnalyticsScreen({super.key});
//
//   Future<List<FlSpot>> fetchSheetData(String deviceId) async {
//     final url = Uri.parse(
//       "https://script.google.com/macros/s/AKfycbwlNYJGfFJfWfeNBKAZ8la-4kPhY5kb1tcPVb2lqHNKbK1ItuPNFihErJLaPxH_Mmkp/exec?deviceId=$deviceId",
//     );
//
//     final response = await http.get(url);
//     print(response.body);
//     if (response.statusCode == 200) {
//       final List data = jsonDecode(response.body);
//
//       List<FlSpot> spots = [];
//
//       for (int i = 0; i < data.length; i++) {
//         spots.add(
//           FlSpot(
//             i.toDouble(),
//             (data[i]['count'] as num).toDouble(),
//           ),
//         );
//       }
//
//       return spots;
//     } else {
//       throw Exception("Failed to load data");
//     }
//   }
//
//   @override
//   Widget build(BuildContext context) {
//     return ValueListenableBuilder<String>(
//       valueListenable: selectedDeviceNotifier,
//       builder: (context, selectedDeviceId, _) {
//
//         return Scaffold(
//           appBar: AppBar(
//             title: const Text("People Analytics"),
//           ),
//           body: Padding(
//             padding: const EdgeInsets.all(16),
//             child: Column(
//               crossAxisAlignment: CrossAxisAlignment.start,
//               children: [
//
//                 Text(
//                   "Analytics for $selectedDeviceId",
//                   style: const TextStyle(
//                     fontSize: 22,
//                     fontWeight: FontWeight.bold,
//                   ),
//                 ),
//
//                 const SizedBox(height: 20),
//
//                 /// 🔥 Graph from Google Sheets
//                 FutureBuilder<List<FlSpot>>(
//                   future: fetchSheetData(selectedDeviceId),
//                   builder: (context, snapshot) {
//
//                     if (snapshot.connectionState == ConnectionState.waiting) {
//                       return const Center(child: CircularProgressIndicator());
//                     }
//
//                     if (snapshot.hasError) {
//                       return const Text("Error loading data");
//                     }
//
//                     final chartData = snapshot.data!;
//
//                     if (chartData.isEmpty) {
//                       return const Text("No data available");
//                     }
//
//                     return Container(
//                       height: 250,
//                       padding: const EdgeInsets.all(12),
//                       decoration: BoxDecoration(
//                         color: Colors.white,
//                         borderRadius: BorderRadius.circular(16),
//                       ),
//                       child: LineChart(
//                         LineChartData(
//                           gridData: FlGridData(show: true),
//                           titlesData: FlTitlesData(show: false),
//                           borderData: FlBorderData(show: false),
//                           lineBarsData: [
//                             LineChartBarData(
//                               spots: chartData,
//                               isCurved: true,
//                               barWidth: 4,
//                               color: Colors.blue,
//                               dotData: const FlDotData(show: true),
//                             ),
//                           ],
//                         ),
//                       ),
//                     );
//                   },
//                 ),
//               ],
//             ),
//           ),
//         );
//       },
//     );
//   }
// }

class AnalyticsScreen extends StatefulWidget {
  const AnalyticsScreen({super.key});

  @override
  State<AnalyticsScreen> createState() => _AnalyticsScreenState();
}

class _AnalyticsScreenState extends State<AnalyticsScreen> {
  List<dynamic> sheetData = [];
  bool isLoading = true;

  Future<void> fetchData(String deviceId) async {
    try {
      final url = Uri.parse(
          "https://script.google.com/macros/s/AKfycbwlNYJGfFJfWfeNBKAZ8la-4kPhY5kb1tcPVb2lqHNKbK1ItuPNFihErJLaPxH_Mmkp/exec?deviceId=$deviceId");

      final response = await http.get(url);

      if (response.statusCode == 200) {
        setState(() {
          sheetData = json.decode(response.body);
          isLoading = false;
        });
      } else {
        setState(() => isLoading = false);
      }
    } catch (e) {
      setState(() => isLoading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder<String>(
      valueListenable: selectedDeviceNotifier,
      builder: (context, selectedDeviceId, _) {

        ///  Fetch when device changes
        fetchData(selectedDeviceId);

        return Scaffold(
          appBar: AppBar(
            title: const Text("Analytics"),
          ),
          body: Padding(
            padding: const EdgeInsets.all(16),
            child: isLoading
                ? const Center(child: CircularProgressIndicator())
                : sheetData.isEmpty
                ? const Center(child: Text("No data available"))
                : Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [

                /// Title
                Text(
                  "Device: $selectedDeviceId",
                  style: const TextStyle(
                    fontSize: 18,
                    fontWeight: FontWeight.bold,
                  ),
                ),

                const Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    Text("X-axis → Time", style: TextStyle(fontSize: 12)),
                    Text("Y-axis → People Count", style: TextStyle(fontSize: 12)),
                  ],
                ),
                const SizedBox(height: 20),

                /// 📊 Graph Card
                Container(
                  height: 300,
                  padding: const EdgeInsets.only(left: 12, right: 16),
                  decoration: BoxDecoration(
                    color: Colors.white,
                    borderRadius: BorderRadius.circular(16),
                  ),
                  child: LineChart(
                    LineChartData(
                      gridData: FlGridData(show: true),

                      titlesData: FlTitlesData(
                        leftTitles: AxisTitles(
                          axisNameWidget: const Text(
                            "People Count",
                            style: TextStyle(
                              fontWeight: FontWeight.bold,
                              fontSize: 12,
                            ),
                          ),
                          sideTitles: SideTitles(
                            showTitles: true,
                            reservedSize: 40,
                            interval: 5,
                            getTitlesWidget: (value, meta) {
                              return Text(
                                value.toInt().toString(),
                                style: const TextStyle(fontSize: 10),
                              );
                            },
                          ),
                        ),
                        bottomTitles: AxisTitles(
                          axisNameWidget: const Text(
                            "Time",
                            style: TextStyle(
                              fontWeight: FontWeight.bold,
                              fontSize: 12,
                            ),
                          ),
                          sideTitles: SideTitles(
                            showTitles: true,
                            reservedSize: 35,
                            interval: (sheetData.length / 4).ceilToDouble(),

                            getTitlesWidget: (value, meta) {
                              int index = value.toInt();

                              if (index < 0 || index >= sheetData.length) {
                                return const SizedBox();
                              }

                              /// 🔥 convert timestamp to readable time
                              final rawTime = sheetData[index]['time'];

                              String displayTime = index.toString();

                              try {
                                final dt = DateTime.parse(rawTime);
                                displayTime = "${dt.hour}:${dt.minute}";
                              } catch (_) {}

                              return Text(
                                displayTime,
                                style: const TextStyle(fontSize: 10),
                              );
                            },
                          ),
                        ),
                        rightTitles: AxisTitles(
                          sideTitles: SideTitles(showTitles: false),
                        ),
                        topTitles: AxisTitles(
                          sideTitles: SideTitles(showTitles: false),
                        ),
                      ),

                      lineBarsData: [
                        LineChartBarData(
                          spots: sheetData.asMap().entries.map((entry) {
                            int index = entry.key;
                            var item = entry.value;

                            return FlSpot(
                              index.toDouble(),
                              (item['count'] ?? 0).toDouble(),
                            );
                          }).toList(),
                          isCurved: true,
                          barWidth: 3,
                          color: Colors.blue,
                          dotData: FlDotData(show: true),
                        ),
                      ],
                    ),
                  ),
                ),

                const SizedBox(height: 10),

                /// 📌 Summary Cards
                Row(
                  children: [
                    Expanded(
                      child: buildStatCard(
                        "Latest Count",
                        sheetData.last['count'].toString(),
                        Icons.people,
                        Colors.blue,
                      ),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: buildStatCard(
                        "Records",
                        sheetData.length.toString(),
                        Icons.bar_chart,
                        Colors.green,
                      ),
                    ),
                  ],
                ),
              ],
            ),
          ),
        );
      },
    );
  }

  Widget buildStatCard(
      String title,
      String value,
      IconData icon,
      Color color,
      ) {
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(14),
      ),
      child: Column(
        children: [
          Icon(icon, color: color),
          const SizedBox(height: 8),
          Text(title),
          const SizedBox(height: 6),
          Text(
            value,
            style: const TextStyle(
              fontSize: 18,
              fontWeight: FontWeight.bold,
            ),
          ),
        ],
      ),
    );
  }
}


class SettingsScreen extends StatelessWidget {
  const SettingsScreen({super.key});

  /// 🔴 DELETE DEVICE
  void showDeleteDeviceDialog(BuildContext context, String deviceId) {
    showDialog(
      context: context,
      builder: (_) {
        return AlertDialog(
          title: const Text("Delete Device"),
          content: const Text(
            "Are you sure you want to delete this device?\n\nThis action cannot be undone.",
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text("Cancel"),
            ),
            ElevatedButton(
              onPressed: () async {
                final devicesRef =
                FirebaseDatabase.instance.ref("devices");

                final snapshot = await devicesRef.get();

                if (!snapshot.exists) {
                  Navigator.pop(context);
                  return;
                }

                final data =
                Map<String, dynamic>.from(snapshot.value as Map);

                final deviceIds = data.keys.toList();

                await devicesRef.child(deviceId).remove();

                if (deviceIds.length > 1) {
                  final newDevice =
                  deviceIds.firstWhere((id) => id != deviceId);
                  selectedDeviceNotifier.value = newDevice;
                }

                Navigator.pop(context);
              },
              child: const Text("Delete"),
            ),
          ],
        );
      },
    );
  }

  void showAddDeviceDialog(BuildContext context) {
    final roomNameController = TextEditingController();
    final capacityController = TextEditingController();
    int gateType = 1;

    showDialog(
      context: context,
      builder: (_) {
        return AlertDialog(
          title: const Text("Add Device"),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            children: [

              /// Room Name
              TextField(
                controller: roomNameController,
                decoration: const InputDecoration(labelText: "Room Name"),
              ),

              /// Capacity
              TextField(
                controller: capacityController,
                keyboardType: TextInputType.number,
                decoration:
                const InputDecoration(labelText: "Room Capacity"),
              ),

              const SizedBox(height: 10),

              /// Gate Type
              DropdownButtonFormField<int>(
                value: gateType,
                items: const [
                  DropdownMenuItem(value: 1, child: Text("1 Gate")),
                  DropdownMenuItem(value: 2, child: Text("2 Gate")),
                ],
                onChanged: (value) => gateType = value!,
                decoration: const InputDecoration(labelText: "Gate Type"),
              ),
            ],
          ),

          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text("Cancel"),
            ),

            ElevatedButton(
              onPressed: () async {
                final roomName = roomNameController.text.trim();
                final capacity =
                    int.tryParse(capacityController.text.trim()) ?? 0;

                if (roomName.isEmpty) return;

                /// 🔥 AUTO-GENERATE UNIQUE ID
                final ref =
                FirebaseDatabase.instance.ref("devices").push();

                final deviceId = ref.key!;

                await ref.set({
                  "deviceId": deviceId,
                  "roomName": roomName,
                  "gateType": gateType,
                  "roomCapacity": capacity,
                  "currentCount": 0,
                  "inCount": 0,
                  "outCount": 0,
                  "gate1Status": "inactive",
                  "gate2Status":
                  gateType == 2 ? "inactive" : "not_applicable",
                  "status": "offline",
                  "lastUpdateMs": DateTime.now().millisecondsSinceEpoch,
                });

                /// ✅ Auto select new device
                selectedDeviceNotifier.value = deviceId;

                Navigator.pop(context);
              },
              child: const Text("Add"),
            ),
          ],
        );
      },
    );
  }

  /// ✏️ EDIT DEVICE
  void showEditDeviceDialog(
      BuildContext context,
      String deviceId,
      Map<String, dynamic>? deviceData,
      ) {
    final roomNameController =
    TextEditingController(text: deviceData?['roomName'] ?? "");

    final capacityController = TextEditingController(
        text: (deviceData?['roomCapacity'] ?? 0).toString());

    int gateType = deviceData?['gateType'] ?? 1;

    showDialog(
      context: context,
      builder: (_) {
        return AlertDialog(
          title: const Text("Edit Device"),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              TextField(
                enabled: false,
                decoration: InputDecoration(
                  labelText: "Device ID",
                  hintText: deviceId,
                ),
              ),
              TextField(
                controller: roomNameController,
                decoration: const InputDecoration(labelText: "Room Name"),
              ),
              TextField(
                controller: capacityController,
                keyboardType: TextInputType.number,
                decoration:
                const InputDecoration(labelText: "Room Capacity"),
              ),
              const SizedBox(height: 10),
              DropdownButtonFormField<int>(
                value: gateType,
                items: const [
                  DropdownMenuItem(value: 1, child: Text("1 Gate")),
                  DropdownMenuItem(value: 2, child: Text("2 Gate")),
                ],
                onChanged: (value) => gateType = value!,
                decoration:
                const InputDecoration(labelText: "Gate Type"),
              ),
            ],
          ),
          actions: [
            TextButton(
                onPressed: () => Navigator.pop(context),
                child: const Text("Cancel")),
            ElevatedButton(
              onPressed: () async {
                final roomName = roomNameController.text.trim();
                final capacity =
                    int.tryParse(capacityController.text.trim()) ?? 0;

                if (roomName.isEmpty) return;

                final ref =
                FirebaseDatabase.instance.ref("devices/$deviceId");

                await ref.update({
                  "roomName": roomName,
                  "roomCapacity": capacity,
                  "gateType": gateType,
                  "gate2Status":
                  gateType == 2 ? "inactive" : "not_applicable",
                });

                Navigator.pop(context);
              },
              child: const Text("Update"),
            ),
          ],
        );
      },
    );
  }

  ///  UI
  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder<String>(
      valueListenable: selectedDeviceNotifier,
      builder: (context, selectedDeviceId, _) {
        final ref =
        FirebaseDatabase.instance.ref("devices/$selectedDeviceId");

        return StreamBuilder<DatabaseEvent>(
          stream: ref.onValue,
          builder: (context, snapshot) {
            Map<String, dynamic>? deviceData;

            if (snapshot.hasData &&
                snapshot.data!.snapshot.value != null &&
                snapshot.data!.snapshot.value is Map) {
              deviceData = Map<String, dynamic>.from(
                  snapshot.data!.snapshot.value as Map);
            }

            final roomName =
                deviceData?['roomName'] ?? "Loading...";
            final gateType = deviceData?['gateType'] ?? 1;
            final capacity = deviceData?['roomCapacity'] ?? 0;
            final status = deviceData?['status'] ?? "offline";

            return Scaffold(
              appBar: AppBar(title: const Text("Settings")),
              body: ListView(
                children: [

                  /// DEVICE SECTION
                  buildSectionHeader("Device"),

                  buildTile(Icons.devices, "Device ID", selectedDeviceId),
                  buildTile(Icons.meeting_room, "Room Name", roomName),
                  buildTile(Icons.sensor_door, "Gate Type",
                      "$gateType Gate System"),
                  buildTile(Icons.people, "Room Capacity",
                      capacity.toString()),
                  buildTile(Icons.circle, "Status", status,
                      trailingColor: status == "online"
                          ? Colors.green
                          : Colors.red),

                  const Divider(),

                  /// ACTIONS
                  buildSectionHeader("Actions"),

                  buildActionTile(
                    icon: Icons.add,
                    title: "Add Device",
                    onTap: () => showAddDeviceDialog(context),
                  ),
                  buildActionTile(
                    icon: Icons.edit,
                    title: "Edit Device",
                    onTap: () => showEditDeviceDialog(
                        context, selectedDeviceId, deviceData),
                  ),
                  buildActionTile(
                    icon: Icons.delete,
                    title: "Delete Device",
                    onTap: () =>
                        showDeleteDeviceDialog(context, selectedDeviceId),
                  ),
                ],
              ),
            );
          },
        );
      },
    );
  }

  /// 🔹 UI Helpers

  Widget buildSectionHeader(String title) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 20, 16, 8),
      child: Text(
        title.toUpperCase(),
        style: TextStyle(
          fontSize: 13,
          color: Colors.grey.shade600,
          fontWeight: FontWeight.w600,
        ),
      ),
    );
  }

  Widget buildTile(IconData icon, String title, String value,
      {Color? trailingColor}) {
    return ListTile(
      leading: Icon(icon, color: Colors.grey.shade700),
      title: Text(title),
      subtitle: Text(value),
      trailing: trailingColor != null
          ? Icon(Icons.circle, color: trailingColor, size: 12)
          : null,
    );
  }

  Widget buildActionTile({
    required IconData icon,
    required String title,
    required VoidCallback onTap,
    Color? textColor,
  }) {
    return ListTile(
      leading: Icon(icon, color: textColor ?? Colors.blue),
      title: Text(
        title,
        style: TextStyle(color: textColor ?? Colors.black),
      ),
      trailing: const Icon(Icons.arrow_forward_ios, size: 16),
      onTap: onTap,
    );
  }
}