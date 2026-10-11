#pragma once
// Simulator: Bluetooth on/off is a flag.
class MultiSerialInterface {
  bool _bt = true;
public:
  bool isBluetoothEnabled() const { return _bt; }
  void enableBluetooth() { _bt = true; }
  void disableBluetooth() { _bt = false; }
};
