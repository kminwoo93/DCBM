using System;
using System.Collections.Generic;
using System.Text;
using Plugin.BLE.Abstractions.Contracts;

namespace SmartOrganApp.Models;

public class BleDeviceInfo
{
    public string Name { get; set; } = string.Empty;
    public string Id { get; set; } = string.Empty;
    public int Rssi { get; set; }

    // 실제 BLE 디바이스 참조
    public IDevice? Device { get; set; }
}
