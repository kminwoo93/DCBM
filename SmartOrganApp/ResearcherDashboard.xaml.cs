using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Text;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Navigation;
using Microsoft.UI.Xaml.Shapes;
using Plugin.BLE.Abstractions.Contracts;
using ScottPlot;
using ScottPlot.WinUI;
using SmartOrganApp.Models;
using SmartOrganApp.Services;
using SmartOrganApp.Services.Drive;
using Windows.Foundation;
using Windows.Foundation.Collections;
using Windows.Storage;

namespace SmartOrganApp;

/// <summary>
/// An empty page that can be used on its own or navigated to within a Frame.
/// </summary>
/// 

public sealed partial class ResearcherDashboard : Page
{
    private List<DataPointModel> _rawData = new();
    private readonly IGoogleDriveService? _googleDriveService;

    // 6 channel concentration×time plots (set in XAML as ChPlot1..ChPlot6)
    // Mapping: ChPlot1=CH1 Glu-P, ChPlot2=CH2 Lac-P, ChPlot3=CH3 pH-P
    //          ChPlot4=CH5 Glu-B, ChPlot5=CH6 Lac-B, ChPlot6=CH7 pH-B
    private static readonly (int Ch, string Analyte, string Label, string HexColor)[] ChDefs =
    {
        (1, "Glu", "CH1 Glucose-P", "#2196F3"),
        (2, "Lac", "CH2 Lactate-P", "#E91E63"),
        (3, "pH",  "CH3 pH-P",      "#4CAF50"),
        (5, "Glu", "CH5 Glucose-B", "#64B5F6"),
        (6, "Lac", "CH6 Lactate-B", "#F48FB1"),
        (7, "pH",  "CH7 pH-B",      "#A5D6A7"),
    };

    // ---------------- BLE fields ----------------
    readonly Guid ServiceUuid = Guid.Parse("000000ff-0000-1000-8000-00805f9b34fb");
    readonly Guid RwCharUuid = Guid.Parse("0000ff01-0000-1000-8000-00805f9b34fb");
    readonly Guid NotifyCharUuid = Guid.Parse("0000ff02-0000-1000-8000-00805f9b34fb");


    private ICharacteristic _rwCharacteristic;
    private ICharacteristic _nCharacteristic;
    private IDevice _connectedDevice;
    // ------------------------------------------------
    // ---------------- BLE data model ----------------
    private readonly List<CaSample> _currentSessionSamples = new();
    private bool _isCollecting = false;
    //-------------------------------------------------

    public ResearcherDashboard()
    {
        this.InitializeComponent();
        this.Loaded += ResearcherDashboard_Loaded;

        try
        {
            _googleDriveService = ((App)Application.Current).Host?.Services.GetService<IGoogleDriveService>();
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[Drive] Failed to resolve IGoogleDriveService: {ex.Message}");
        }
        
    }

    protected override async void OnNavigatedTo(NavigationEventArgs e)
    {
        base.OnNavigatedTo(e);

        _connectedDevice = ((App)Application.Current).ConnectedDevice;
        // Read App.CurrentLiverId that was set in LiverIDPage
        _currentLiverId = ((App)Application.Current).CurrentLiverId ?? "Liver01";
        var currentId = ((App)Application.Current).CurrentLiverId ?? "N/A";
        // Reflect in UI
        LiverID.Text = $"Liver ID: {currentId}";
        //ComboBox update
        await RefreshTimePointDropdownAsync();   // Rebuild list when Liver ID changes

        if (_connectedDevice != null)
        {
            ConnectedBLEName.Text = _connectedDevice.Name ?? "Unknown";
            await InitCharacteristicsAsync(_connectedDevice);
        }
        else
        {
            ConnectedBLEName.Text = "Not connected";
        }
    }
    private async Task InitCharacteristicsAsync(IDevice device)
    {
        var service = await device.GetServiceAsync(ServiceUuid);
        _rwCharacteristic = await service.GetCharacteristicAsync(RwCharUuid);
        _nCharacteristic = await service.GetCharacteristicAsync(NotifyCharUuid);
        
    }


    private void ExitButton_Click(object sender, RoutedEventArgs e)
    {
        if (Frame.CanGoBack)
            Frame.GoBack();
    }

    private void DeviceScanButton_Click(object sender, RoutedEventArgs e)
    {
        Frame.Navigate(typeof(DeviceScanPage));
    }

    private void LiverIDButton_Click(object sender, RoutedEventArgs e)
    {
        Frame.Navigate(typeof(LiverIDPage));
    }

    // Opens the Calibration screen (includes PBS rinse as Step 1)
    private void CalibrationButton_Click(object sender, RoutedEventArgs e)
    {
        Frame.Navigate(typeof(CalibrationPage));
    }

    private bool _isRunningStartStopBtn = false; // false = Start state, true = Stop state
    private bool _isRunningPumpBtn = false; // false = Start pump state, true = Stop pump state

    private async void StartPumpButton_Click(object sender, RoutedEventArgs e)
    {
        if (_rwCharacteristic == null)
            return;

        if (!_isRunningPumpBtn)
        {
            // Start slow pump 20 uL/min (0x24) — used for monitoring
            byte[] payload = new byte[] { 0x24 };
            await _rwCharacteristic.WriteAsync(payload);
            _isRunningPumpBtn = true;
            StartPumpText.Text = "Stop pump";
            System.Diagnostics.Debug.WriteLine("[BLE] Write Success: 0b00020000 sent");
        }
        else
        {
            // Stop pump command: 0b00020001
            byte[] payload = new byte[] { 0x21 };
            await _rwCharacteristic.WriteAsync(payload);
            _isRunningPumpBtn = false;
            StartPumpText.Text = "Start pump";
            System.Diagnostics.Debug.WriteLine("[BLE] Write Success: 0b00020001 sent");
        }
    }

    private async void StartStopButton_Click(object sender, RoutedEventArgs e)
    {
        
        if (!_isRunningStartStopBtn)
        {
            // Clicked while in Start state -> run Start behavior
            UpdateStartStopButtonUI();
            _isRunningStartStopBtn = true;
            await StartMonitoringAsync();
        }
        else
        {
            // Clicked while in Stop state -> run Stop behavior
            UpdateStartStopButtonUI();
            _isRunningStartStopBtn = false;
            await StopMonitoringAsync();
        }
        
    }
    // Class-level member fields
    private DateTime _monitoringStartTime;
    private DateTime _currentSessionTimestamp;
    private string _currentAnalyte = "Unknown";
    private string _currentTLabel = "0.0";
    private int _currentChannel = 0;
    private string _currentLiverId = "Liver01";  // 나중에 UI에서 설정할 예정
    private CancellationTokenSource _monitoringCts;
    private async Task StartMonitoringAsync()
    {
        if (_rwCharacteristic == null)
            return;

        // Cancel a previous loop first, if one is running
        _monitoringCts?.Cancel();
        _monitoringCts = new CancellationTokenSource();
        var token = _monitoringCts.Token;

        // 1) Save monitoring start time
        _monitoringStartTime = DateTime.Now;

        // Register notify event
        if (_nCharacteristic.CanUpdate)
        {
            _nCharacteristic.ValueUpdated -= OnBleValueUpdated;
            _nCharacteristic.ValueUpdated += OnBleValueUpdated;
            await _nCharacteristic.StartUpdatesAsync();
        }
        try
        {
            int[] monitoringChannels = { 1, 2, 3, 5, 6, 7 };

            while (!token.IsCancellationRequested)
            {
                foreach (int channel in monitoringChannels)
                {
                    token.ThrowIfCancellationRequested();
                    await CAMeasureAsync(channel);
                    await Task.Delay(TimeSpan.FromSeconds(70), token); // Wait 70 seconds (without blocking UI)
                }
            }
        }
        catch (TaskCanceledException)
        {
            System.Diagnostics.Debug.WriteLine("[BLE] StartMonitoringAsync canceled");
            // Add extra cleanup here if needed
        }
        ////byte[] payload = new byte[] { 0b00010001 };
        ////await _rwCharacteristic.WriteAsync(payload);
        ////System.Diagnostics.Debug.WriteLine("[BLE] Write Success: 0b00010001 sent");
    }
    private async Task StopMonitoringAsync()
    {
        _monitoringCts?.Cancel();
        _monitoringCts = null;
        if (_rwCharacteristic == null)
            return;
        // Send "00000000" to ESP32 -> stop measurement command
        byte[] payload = new byte[] { 0b00000000 };
        await _rwCharacteristic.WriteAsync(payload);
        System.Diagnostics.Debug.WriteLine("[BLE] Write Success: 0b00000000 sent");

    }

    private async Task CAMeasureAsync(int ch)
    {
        if (_rwCharacteristic == null)
            return;

        if (ch < 1 || ch > 8)
            throw new ArgumentOutOfRangeException(nameof(ch), "ch must be between 1 and 8.");

        // 1) Compute command byte
        byte command = (byte)(0b00010000 + ch);
        byte[] payload = new byte[] { command };

        // 2) Channel -> analyte
        string analyte = ch switch
        {
            1 => "Glu",
            2 => "Lac",
            3 => "pH",
            4 => "Ch4",
            5 => "Glu",
            6 => "Lac",
            7 => "pH",
            8 => "Ch8",
            _ => "Unknown"
        };

        // 3) Compute tLabel (elapsed hours since Start button)
        double hours = (DateTime.Now - _monitoringStartTime).TotalHours;
        double hoursRounded = Math.Round(hours, 1);
        string tLabel = hoursRounded.ToString("0.0", CultureInfo.InvariantCulture); // "1.0", "1.5", ...

        // Update fields first
        _currentAnalyte = analyte;
        _currentTLabel = tLabel;
        _currentChannel = ch;

        // Then send BLE payload
        await _rwCharacteristic.WriteAsync(payload);

        System.Diagnostics.Debug.WriteLine(
            $"[BLE] CAMeasureAsync: Ch={ch}, analyte={_currentAnalyte}, tLabel={_currentTLabel}, cmd=0b{Convert.ToString(command, 2).PadLeft(8, '0')}");

    }

    // LMP91000 설정값(너의 매크로 기준)
    private const double LmpVref_V = 3.3;           // PIC 펌웨어: LMP internal reference(VDD)
    private const double InternalZeroFrac = 0.67;   // PIC 펌웨어: INT_Z_67_PCT
    private const double Rtia_Ohm = 120_000.0;      // PIC 펌웨어: TIA gain 120k

    // PIC ADC 설정값에 맞춰서 수정 필요
    // - ADC가 VDD(3.3V)를 Vref로 쓰면 3.3
    // - FVR 2.048V를 쓰면 2.048
    private const double AdcVref_V = 2.048;          // PIC 펌웨어: ADC_USE_FVR_REF=1

    private const int AdcMaxCode = 1023;             // 10-bit ADC

    private static double CalcCurrentFromAdc_uA(ushort adcCode)
    {
        // 1) ADC code -> VOUT (volts)
        double vOut_V = (adcCode / (double)AdcMaxCode) * AdcVref_V;

        // 2) VZERO (volts) = InternalZeroFrac * LMP VREF
        double vZero_V = InternalZeroFrac * LmpVref_V;

        // 3) I (A) = (VOUT - VZERO) / RTIA
        double i_A = (vOut_V - vZero_V) / Rtia_Ohm;

        // 4) A -> uA
        return i_A * 1e6;
    }
    private void OnBleValueUpdated(object sender, Plugin.BLE.Abstractions.EventArgs.CharacteristicUpdatedEventArgs e)
    {
        var bytes = e.Characteristic.Value;
        string hex = bytes == null ? "" : BitConverter.ToString(bytes);

        if (bytes == null || bytes.Length < 1)
        {
            Debug.WriteLine("[BLE] Empty packet");
            return;
        }

        // ✅ 새 포맷(ADC 포함) 13 bytes, 구 포맷 11 bytes 둘 다 대응
        //int packetSize =
        //    (bytes.Length % PacketSizeNew == 0) ? PacketSizeNew :
        //    (bytes.Length % PacketSizeOld == 0) ? PacketSizeOld :
        //    (bytes.Length >= PacketSizeNew) ? PacketSizeNew :
        //    PacketSizeOld;
        const int packetSize = 7;

        if (bytes.Length < packetSize)
        {
            Debug.WriteLine($"[BLE] Invalid packet length: {bytes.Length}, expect {packetSize}. raw={hex}");
            return;
        }

        int offset = 0;
        while (offset + packetSize <= bytes.Length)
        {
            byte type = bytes[offset + 0];
            uint t_ms = BitConverter.ToUInt32(bytes, offset + 1);
            ushort adc = BitConverter.ToUInt16(bytes, offset + 5);

            if (type == 1)
            {
                // Session START
                _currentSessionSamples.Clear();
                _isCollecting = true;
                _currentSessionTimestamp = DateTime.Now;
                Debug.WriteLine("[BLE] Session START");

                DispatcherQueue.TryEnqueue(() =>
                {
                    _liveXs.Clear();
                    _liveYs.Clear();
                    _plotUpdateSkipCounter = 0;
                    WinUIPlot1.Plot.Clear();
                    WinUIPlot1.Refresh();

                    CurrentValueTextBlock.Text = "Session started";
                });
            }
            else if (type == 0)
            {
                // DATA
                if (_isCollecting)
                {
                    

                    // Compute current from ADC
                    double i_uA_calc = CalcCurrentFromAdc_uA(adc);
                    var sample = new CaSample
                    {
                        Type = type,
                        TimeMs = t_ms,
                        Current_uA = i_uA_calc,
                        Adc = adc
                    };
                    _currentSessionSamples.Add(sample);

                    DispatcherQueue.TryEnqueue(() =>
                    {
                        CurrentValueTextBlock.Text =
                            $"CH={_currentChannel} | t={sample.TimeMs} ms | ADC={sample.Adc} | I={sample.Current_uA:F3} uA";
                        UpdateLivePlot(sample);
                    });
                }
            }
            else if (type == 2)
            {
                // Session END
                if (_isCollecting)
                {
                    _isCollecting = false;
                    Debug.WriteLine("[BLE] Session END, samples=" + _currentSessionSamples.Count);

                    _ = SaveCurrentSessionCsvAsync(_currentAnalyte, _currentTLabel, _currentLiverId, _currentChannel, _currentSessionTimestamp);

                    DispatcherQueue.TryEnqueue(async () =>
                    {
                        await RefreshTimePointDropdownAsync();
                        await RefreshConcentrationGraphsAsync();
                    });

                    WinUIPlot1.Plot.Axes.AutoScale();
                    WinUIPlot1.Refresh();
                }
            }
            else
            {
                Debug.WriteLine($"[BLE] Unknown type={type} at offset={offset}");
            }

            offset += packetSize;
        }
    }


    private async Task SaveCurrentSessionCsvAsync(
        string analyte,      // 예: "Glu"
        string tLabel,       // 예: "1h", "3h"
        string liverId,      // 예: "L1", "L2"
        int channel,
        DateTime sessionTimestamp
        )
    {
        if (_currentSessionSamples.Count == 0)
        {
            System.Diagnostics.Debug.WriteLine("[BLE] No samples to save for this session.");
            return;
        }

        var sb = new StringBuilder();
        // 메타데이터 한 줄 추가 (나중에 파일 안에서도 확인 용도)
        sb.AppendLine($"# {analyte}_{tLabel}_{sessionTimestamp:yyyyMMdd_HHmmss}_{liverId}_ch{channel}");
        sb.AppendLine("t_ms,ADC,I_uA");

        foreach (var s in _currentSessionSamples)
        {
            // Save only type == 0 data (exclude start/end packets)
            if (s.Type == 0)
            {
                sb.AppendLine($"{s.TimeMs},{s.Adc},{s.Current_uA.ToString("F6", CultureInfo.InvariantCulture)}");
            }
        }

        // 파일 이름 규칙: 측정항목_시간_날짜_liverID.csv
        string fileName = $"{analyte}_{tLabel}_{sessionTimestamp:yyyyMMdd_HHmmss}_{liverId}_ch{channel}.csv";

        //1) Save in LocalFolder
        StorageFolder localfolder = ApplicationData.Current.LocalFolder;
        StorageFile localfile = await localfolder.CreateFileAsync(fileName, CreationCollisionOption.ReplaceExisting);
        await FileIO.WriteTextAsync(localfile, sb.ToString());
        System.Diagnostics.Debug.WriteLine($"[BLE] CSV saved (LocalFolder): {localfile.Path}");

        _ = UploadSavedCsvToDriveAsync(localfile);

        //2) Save in Downloads foler
        try
        {
            StorageFolder downloads = await GetDownloadsFolderAsync();
            StorageFile downloadsFile = await downloads.CreateFileAsync(
                fileName, CreationCollisionOption.ReplaceExisting);

            await FileIO.WriteTextAsync(downloadsFile, sb.ToString());
            System.Diagnostics.Debug.WriteLine($"[BLE] CSV saved (Downloads): {downloadsFile.Path}");
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine("[BLE] Failed to save to Downloads: " + ex.Message);
        }


        // Notify UI (optional)
        DispatcherQueue.TryEnqueue(() =>
        {
            CurrentValueTextBlock.Text = $"Saved CSV: {_currentSessionSamples.Count} points";
        });

        if (channel == 1)
        {
            await RefreshConcentrationGraphsAsync();
        }
    }

    private async Task UploadSavedCsvToDriveAsync(StorageFile localFile)
    {
        if (_googleDriveService is null)
        {
            Debug.WriteLine("[Drive] Upload skipped: IGoogleDriveService is not available.");
            return;
        }

        DispatcherQueue.TryEnqueue(() =>
        {
            CurrentValueTextBlock.Text = "Uploading CSV to Drive...";
        });

        try
        {
            // Strategy B: file names include timestamp, so we currently create-only uploads.
            // TODO: Add a retry queue and create/update behavior (search by file name + folder) for robust offline sync.
            var uploaded = await _googleDriveService.UploadCsvAsync(localFile.Path);

            Debug.WriteLine($"[Drive] Upload success: name={uploaded.Name}, id={uploaded.Id}");

            DispatcherQueue.TryEnqueue(() =>
            {
                CurrentValueTextBlock.Text = $"Uploaded to Drive: {uploaded.Name}";
            });
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[Drive] Upload failed: {ex.Message}");

            DispatcherQueue.TryEnqueue(() =>
            {
                CurrentValueTextBlock.Text = "Drive upload failed (saved locally).";
            });
        }
    }

    private async Task<StorageFolder> GetDownloadsFolderAsync()
    {
#if WINDOWS
    return KnownFolders.DownloadsLibrary;

#elif __ANDROID__
    var javaIoFile = Android.OS.Environment.GetExternalStoragePublicDirectory(
        Android.OS.Environment.DirectoryDownloads);

    // Convert to Windows.Storage.StorageFolder
    return await StorageFolder.GetFolderFromPathAsync(javaIoFile.AbsolutePath);

#elif __IOS__
    // iOS has no Downloads folder -> use Documents instead
    var path = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
    return await StorageFolder.GetFolderFromPathAsync(path);

#else
        // Default handling for other platforms
        return ApplicationData.Current.LocalFolder;
#endif
    }



    private void UpdateStartStopButtonUI()
    {
        if (!_isRunningStartStopBtn)
        {
            StartStopText.Text = "Stop monitoring";
            StartStopArrow.Foreground = "Red";
            StartStopButton.Background = "#ECF2F3";
        }
        else
        {
            StartStopText.Text = "Start monitoring";
            StartStopArrow.Foreground = "Black";
            StartStopButton.Background = "#C0D0D4";
        }
    }

    private bool _isRunningGlucoseBtn = false; // false = On state, true = Off state
    private async void GlucoseButton_Click(object sender, RoutedEventArgs e)
    {
        if(!_isRunningGlucoseBtn)
        {
            GlucoseButton.Background = "#7D7D7D";
            _isRunningGlucoseBtn = true;
        }
        else
        {
            _currentAnalyte = "Glu";
            GlucoseButton.Background = "#45B3CB";
            _isRunningGlucoseBtn = false;
            await RefreshTimePointDropdownAsync();
        }
        
    }
    private bool _isRunningLactateBtn = false; // false = On state, true = Off state
    private async void LactateButton_Click(object sender, RoutedEventArgs e)
    {
        if (!_isRunningLactateBtn)
        {
            
            LactateButton.Background = "#7D7D7D";
            _isRunningLactateBtn = true;
        }
        else
        {
            _currentAnalyte = "Lac";
            LactateButton.Background = "#ED7390";
            _isRunningLactateBtn = false;
            await RefreshTimePointDropdownAsync();
        }
    }
    private bool _isRunningpHBtn = false; // false = On state, true = Off state
    private async void pHButton_Click(object sender, RoutedEventArgs e)
    {
        if (!_isRunningpHBtn)
        {
            pHButton.Background = "#7D7D7D";
            _isRunningpHBtn = true;
        }
        else
        {
            pHButton.Background = "#E59850";
            _isRunningpHBtn = false;
            _currentAnalyte = "pH";
            await RefreshTimePointDropdownAsync();
        }
    }

    // Live plot data
    private readonly List<double> _liveXs = new();
    private readonly List<double> _liveYs = new();

    // Avoid refreshing too often to reduce UI stutter
    // Counter to refresh once every N samples
    private int _plotUpdateSkipCounter = 0;
    private const int PlotUpdateEveryNSamples = 5; // Refresh every 5 samples

    private async void ResearcherDashboard_Loaded(object sender, RoutedEventArgs e)
    {
        // Load CSV (e.g., Assets/Data/glucose_raw.csv)
        _rawData = await CsvLoader.LoadCsvAsync("Assets/Data/RawDataTest.csv");
        System.Diagnostics.Debug.WriteLine($"CSV loaded: {_rawData.Count} points");
        await RefreshTimePointDropdownAsync();  // 🔥 ComboBox 리스트 초기 갱신
        await RefreshConcentrationGraphsAsync();
        if (_rawData != null && _rawData.Count > 0)
        {
            // Convert List<DataPoint> -> double[]
            double[] xs = _rawData.Select(p => p.X).ToArray();
            double[] ys = _rawData.Select(p => p.Adc).ToArray();

            // Draw with ScottPlot
            WinUIPlot1.Plot.Clear();
            var initScatter = WinUIPlot1.Plot.Add.Scatter(xs, ys);
            initScatter.LineWidth = 0;
            initScatter.MarkerSize = 4;
            WinUIPlot1.Plot.YLabel("ADC");
            WinUIPlot1.Plot.XLabel("Time (ms)");
            WinUIPlot1.Plot.Axes.AutoScale();
            WinUIPlot1.Refresh();
        }
    }

    // Max number of points to display (e.g., 120 sec * 10 Hz = 1200)
    private const int MaxPoints = 1200;
    private void UpdateLivePlot(CaSample sample)
    {
        // X axis: time (ms), Y axis: ADC
        double x = sample.TimeMs;
        double y = sample.Adc;

        _liveXs.Add(x);
        _liveYs.Add(y);

        // Sliding window: keep only the latest MaxPoints
        if (_liveXs.Count > MaxPoints)
        {
            int removeCount = _liveXs.Count - MaxPoints;
            _liveXs.RemoveRange(0, removeCount);
            _liveYs.RemoveRange(0, removeCount);
        }

        // Refresh once every N samples to avoid over-drawing
        _plotUpdateSkipCounter++;
        if (_plotUpdateSkipCounter < PlotUpdateEveryNSamples)
            return;

        _plotUpdateSkipCounter = 0;

        // Simple implementation: Clear and re-add Scatter each refresh
        // (Fine for data sizes in the hundreds to low thousands)
        WinUIPlot1.Plot.Clear();
        var liveScatter = WinUIPlot1.Plot.Add.Scatter(_liveXs.ToArray(), _liveYs.ToArray());
        liveScatter.LineWidth = 0;
        liveScatter.MarkerSize = 4;
        WinUIPlot1.Plot.YLabel("ADC");
        WinUIPlot1.Plot.XLabel("Time (ms)");
        WinUIPlot1.Plot.Axes.AutoScale();
        WinUIPlot1.Refresh();
    }

    // ── Concentration × Time graphs — one per channel ────────────────────────
    // Reads all Glu/Lac/pH CSV files for the current LiverID, computes the
    // average of the last 10 samples as I_final (µA), and plots each
    // channel as I_final (µA) vs time-label (hours since monitoring start).
    // X-axis = time in hours (tLabel from filename, e.g. "0.0", "1.0")
    // Y-axis = I_final µA (average of last 10 samples of that session)
    private async Task RefreshConcentrationGraphsAsync()
    {
        var localFolder = ApplicationData.Current.LocalFolder;
        var files = (await localFolder.GetFilesAsync())
            .Where(f => f.Name.EndsWith(".csv", StringComparison.OrdinalIgnoreCase))
            .ToList();

        // One list of (tHr, iUa) per channel index 0-5
        var series = new List<(double tHr, double iUa)>[6];
        for (int i = 0; i < 6; i++) series[i] = new();

        foreach (var file in files)
        {
            var nameNoExt = System.IO.Path.GetFileNameWithoutExtension(file.Name);
            var parts = nameNoExt.Split('_');
            if (parts.Length < 5) continue;

            // Filename: Analyte_tLabel_yyyyMMdd_HHmmss_LiverID_chN.csv
            string analytePart = parts[0];  // Glu / Lac / pH
            string tLabelPart  = parts[1];  // 0.0 / 1.0 etc

            // Channel number from last part (ch1, ch2 ... ch7)
            string chPart = parts[^1];
            if (!chPart.StartsWith("ch", StringComparison.OrdinalIgnoreCase)) continue;
            if (!int.TryParse(chPart[2..], out int chNum)) continue;

            // LiverID second-to-last
            string liverPart = parts[^2];
            if (!string.Equals(liverPart, _currentLiverId, StringComparison.OrdinalIgnoreCase)) continue;

            if (!double.TryParse(tLabelPart, NumberStyles.Any, CultureInfo.InvariantCulture, out double tHr)) continue;

            // Find which ChDef this file belongs to
            int defIdx = -1;
            for (int i = 0; i < ChDefs.Length; i++)
            {
                var d = ChDefs[i];
                if (d.Ch == chNum &&
                    string.Equals(d.Analyte, analytePart, StringComparison.OrdinalIgnoreCase))
                { defIdx = i; break; }
            }
            if (defIdx < 0) continue;

            var data = await CsvLoader.LoadFromStorageFileAsync(file);
            if (data == null || data.Count == 0) continue;

            var lastTen = data.TakeLast(10).ToList();
            double iUa = lastTen.Average(x => x.Current_uA);
            series[defIdx].Add((tHr, iUa));
        }

        // Sort each series by time
        for (int i = 0; i < 6; i++)
            series[i] = series[i].OrderBy(x => x.tHr).ToList();

        // Capture results before dispatching to UI thread — avoid closure-over-loop-variable
        var capturedSeries = series.Select(s => s.ToList()).ToArray();
        var capturedDefs   = ChDefs.ToArray();

        DispatcherQueue.TryEnqueue(() =>
        {
            ScottPlot.WinUI.WinUIPlot[] plots = { ChPlot1, ChPlot2, ChPlot3, ChPlot4, ChPlot5, ChPlot6 };

            for (int i = 0; i < 6; i++)
            {
                var def      = capturedDefs[i];
                var plot     = plots[i];
                var plotData = capturedSeries[i];
                plot.Plot.Clear();

                if (plotData.Count > 0)
                {
                    double[] xs = plotData.Select(p => p.tHr).ToArray();
                    double[] ys = plotData.Select(p => p.iUa).ToArray();
                    var color = ScottPlot.Color.FromHex(def.HexColor);

                    var sc = plot.Plot.Add.Scatter(xs, ys);
                    sc.Color      = color;
                    sc.LineWidth  = 2;
                    sc.MarkerSize = 8;
                }

                plot.Plot.Title(def.Label);
                plot.Plot.XLabel("Time (h)");
                plot.Plot.YLabel("I_final (µA)");
                plot.Plot.Axes.AutoScale();
                plot.Refresh();
            }
        });
    }
    //Fill the ComboBox
    private async Task RefreshTimePointDropdownAsync()
    {
        if (string.IsNullOrEmpty(_currentLiverId) || string.IsNullOrEmpty(_currentAnalyte))
            return;

        var localFolder = ApplicationData.Current.LocalFolder;
        var files = await localFolder.GetFilesAsync();

        var list = new List<TimeFileItem>();

        foreach (var file in files)
        {
            if (!file.Name.EndsWith(".csv", StringComparison.OrdinalIgnoreCase))
                continue;

            // e.g., Lac_0.0_20251204_103305_LiveraA120zZ.csv
            var nameNoExt = System.IO.Path.GetFileNameWithoutExtension(file.Name);
            var parts = nameNoExt.Split('_');

            if (parts.Length < 5)
                continue;

            string analytePart = parts[0];       // Lac
            string tLabelPart = parts[1];       // 0.0
            string liverPart = (parts.Length >= 6 && parts[^1].StartsWith("ch", StringComparison.OrdinalIgnoreCase))
                ? parts[^2]
                : parts[^1];

            if (!string.Equals(analytePart, _currentAnalyte, StringComparison.OrdinalIgnoreCase))
                continue;

            if (!string.Equals(liverPart, _currentLiverId, StringComparison.OrdinalIgnoreCase))
                continue;

            if (!double.TryParse(tLabelPart, NumberStyles.Any, CultureInfo.InvariantCulture, out double tHr))
                continue;

            list.Add(new TimeFileItem
            {
                TimeHours = tHr,
                File = file
            });
        }

        var ordered = list.OrderBy(x => x.TimeHours).ToList();

        TimePointComboBox.ItemsSource = ordered;
        TimePointComboBox.DisplayMemberPath = "Display";

        if (ordered.Count > 0)
            TimePointComboBox.SelectedIndex = 0;   // Auto-select the first time value (e.g., 0.0 hr)
    }

    private async void TimePointComboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (TimePointComboBox.SelectedItem is not TimeFileItem item)
            return;

        // Load data from selected CSV file
        var data = await CsvLoader.LoadFromStorageFileAsync(item.File);

        if (data == null || data.Count == 0)
            return;

        double[] xs = data.Select(p => p.X).ToArray();  //x-axis = time_x10 ms
        double[] ys = data.Select(p => p.Adc).ToArray();  // y-axis = ADC

        WinUIPlot1.Plot.Clear();
        var selectedScatter = WinUIPlot1.Plot.Add.Scatter(xs, ys);
        selectedScatter.LineWidth = 0;
        selectedScatter.MarkerSize = 4;
        WinUIPlot1.Plot.YLabel("ADC");
        WinUIPlot1.Plot.XLabel("Time (sec)");
        WinUIPlot1.Plot.Axes.AutoScale();
        WinUIPlot1.Refresh();
    }
}
