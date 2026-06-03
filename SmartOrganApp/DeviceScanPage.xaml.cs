using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using Plugin.BLE;
using Plugin.BLE.Abstractions;
using Plugin.BLE.Abstractions.Contracts;
using Plugin.BLE.Abstractions.EventArgs;
using SmartOrganApp.Models;

namespace SmartOrganApp;

public sealed partial class DeviceScanPage : Page
{
    private readonly IBluetoothLE _ble;
    private readonly IAdapter _adapter;

    private readonly ObservableCollection<BleDeviceInfo> _devices =
        new ObservableCollection<BleDeviceInfo>();

    private bool _isScanning = false;

    public DeviceScanPage()
    {
        this.InitializeComponent();

        _ble = CrossBluetoothLE.Current;
        _adapter = _ble.Adapter;

        DevicesListView.ItemsSource = _devices;

        // 디바이스 발견 이벤트 등록
        _adapter.DeviceDiscovered += Adapter_DeviceDiscovered;
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e)
    {
        base.OnNavigatedFrom(e);

        _adapter.DeviceDiscovered -= Adapter_DeviceDiscovered;
    }

    private void BackButton_Click(object sender, RoutedEventArgs e)
    {
        if (Frame.CanGoBack)
            Frame.GoBack();
    }

    // Scan 버튼 클릭
    private async void ScanButton_Click(object sender, RoutedEventArgs e)
    {
        if (_isScanning)
            return;

        System.Diagnostics.Debug.WriteLine($"[BLE] State = {_ble.State}");

        // 블루투스 상태 체크
        if (_ble.State != BluetoothState.On)
        {
            System.Diagnostics.Debug.WriteLine($"[BLE] Bluetooth state: {_ble.State}");
            // TODO: 필요하면 Dialog로 "블루투스를 켜세요" 안내
            return;
        }

        _isScanning = true;
        ScanButton.Content = "Scanning...";
        ScanButton.IsEnabled = false;
        ScanProgress.IsActive = true;
        ScanProgress.Visibility = Visibility.Visible;

        _devices.Clear();

        _adapter.ScanTimeout = 10000;       // 10초
        _adapter.ScanMode = ScanMode.Balanced;

        try
        {
            // 실제 스캔 시작
            System.Diagnostics.Debug.WriteLine("[BLE] StartScanningForDevicesAsync()");
            await _adapter.StartScanningForDevicesAsync();

            // 🔥 스캔 끝난 후, DiscoveredDevices 내용 확인
            var found = _adapter.DiscoveredDevices?.ToList() ?? new List<IDevice>();
            System.Diagnostics.Debug.WriteLine($"[BLE] Scan finished. DiscoveredDevices.Count = {found.Count}");

            // 1) 이름 있는 디바이스만
            // 2) RSSI 내림차순(신호 강한 순)으로 정렬
            var ordered = found
                .Where(d => !string.IsNullOrEmpty(d.Name))
                .OrderByDescending(d => d.Rssi)
                .ToList();

            foreach (var dev in ordered)
            {
                var name = dev.Name; // 이미 null/empty 필터했음
                var id = dev.Id.ToString();

                System.Diagnostics.Debug.WriteLine($"[BLE] DEV: {name} ({id}), RSSI={dev.Rssi}");

                // 중복 방지
                if (_devices.Any(d => d.Id == id))
                    continue;

                _devices.Add(new BleDeviceInfo
                {
                    Name = name,
                    Id = id,
                    Rssi = dev.Rssi,
                    Device = dev,
                });
            }
            System.Diagnostics.Debug.WriteLine($"[BLE] _devices.Count after copy = {_devices.Count}");

        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[BLE] Scan error: {ex}");
        }
        finally
        {
            ScanButton.Content = "Scan";
            ScanButton.IsEnabled = true;
            ScanProgress.IsActive = false;
            ScanProgress.Visibility = Visibility.Collapsed;
            _isScanning = false;
        }
    }
    private void Adapter_DeviceDiscovered(object? sender, DeviceEventArgs e)
    {
        var dev = e.Device;

         //❌ 이거 있으면 이름 없는 디바이스는 다 버림
         if (string.IsNullOrEmpty(dev.Name))
            return;

        System.Diagnostics.Debug.WriteLine($"[BLE] DeviceDiscovered: {dev.Name} ({dev.Id}), RSSI={dev.Rssi}");
    }

   
    private async void ConnectButton_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button btn)
            return;

        if (btn.DataContext is not BleDeviceInfo info || info.Device == null)
            return;

        try
        {
            ScanButton.IsEnabled = false;
            btn.IsEnabled = false;
            btn.Content = "Connecting...";

            await _adapter.ConnectToDeviceAsync(info.Device);

            System.Diagnostics.Debug.WriteLine($"[BLE] Connected to {info.Name} ({info.Id})");

            btn.Content = "Connected";

            await _adapter.ConnectToDeviceAsync(info.Device);



            //// 연결성공 → 다음 페이지로 device 넘기기
            //Frame.Navigate(typeof(ResearcherDashboard), info.Device);

            // TODO: 여기서 전역 상태에 저장해서 다른 페이지에서 사용해도 됨
            // 예: App 또는 BleService에 ConnectedDevice 저장
            ((App)Application.Current).ConnectedDevice = info.Device;
            // 연결 후 ResearcherDashboard로 돌아가고 싶으면:
            // if (Frame.CanGoBack) Frame.GoBack();
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"[BLE] Connect error: {ex}");
            btn.Content = "Connect";
            btn.IsEnabled = true;
        }
        finally
        {
            ScanButton.IsEnabled = true;
        }
    }

}
