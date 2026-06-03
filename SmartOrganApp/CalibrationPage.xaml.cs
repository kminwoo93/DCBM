using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using Plugin.BLE.Abstractions.Contracts;
using SmartOrganApp.Services.Auth;
using SmartOrganApp.Services.Drive;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Windows.Storage;

namespace SmartOrganApp;

/// <summary>
/// CalibrationPage — full calibration + sample measurement workflow.
///
/// ══════════════════════════════════════════════════════════════════════════
/// STEPS:
///   Step 1  PBS Rinse         — manual pump, no data (0x20/0x24/0x21)
///   Step 2  PBS Background    — 0x25, records ch1,2,3,5,6,7 × 60s, C=0
///   Step 3  Standard 1        — 0x26, known concentration C₁
///   Step 4  Standard 2        — 0x27, known concentration C₂
///   Step 5  Standard 3        — 0x28, known concentration C₃
///   Step 6  Regression        — fits y = a·x + b, optional matrix correction
///   Step 7  Sample Measurement — 0x30, measures actual organ sample
///
/// STOP MECHANISM (three-layer):
///   Layer 1 — App CTS cancelled → timer loop exits on next 1s tick
///   Layer 2 — App sets _isCollecting=false → BLE notify stops storing data
///   Layer 3 — App sends 0x00 → PIC firmware delay_ms_interruptible catches
///             it within 1ms and sets g_stop_requested=true → loop exits
///   Layer 4 — App sends 0x29 → PIC directly calls motor_stop() + sets flag
///   The motor stops within ~70ms of pressing Stop Early.
///
/// TIMER FIX:
///   The timer starts at 0s the moment the command is sent, NOT after waiting
///   for the PIC to send the first CH label. The channel tracking (via CH labels)
///   still works in the background — the timer just no longer waits for it.
///
/// ADC → nA CONVERSION (matches firmware ADC.c + lmp91000.h):
///   Vout_mV = code × 2048 / 1023
///   Vzero   = 2048 × 0.67 = 1372.16 mV  (INT_Z = 67%)
///   RTIA    = 120,000 Ω  (TIA_GAIN_120K)
///   I_uA    = (Vout_mV − Vzero_mV) / 1000 / RTIA × 1e6
///   I_nA    = I_uA × 1000
///
/// I_final = average of last 10 samples (last 1s = steady-state current)
///
/// CSV FILES:
///   Raw:       YYYYMMDD_HHMMSS_CHx_StepName_raw.csv   (time_s, current_nA)
///   Processed: processed_dataset.csv                   (running log, all steps)
///   Calib:     YYYYMMDD_HHMMSS_CHx_calibration.csv    (conc, I_final, slope, b, b2)
///   Sample:    YYYYMMDD_HHMMSS_sample.csv              (channel, Cs, Ss, b2)
/// ══════════════════════════════════════════════════════════════════════════
public sealed partial class CalibrationPage : Page
{
    // ── BLE ───────────────────────────────────────────────────────────────
    private readonly Guid _svcUuid = Guid.Parse("000000ff-0000-1000-8000-00805f9b34fb");
    private readonly Guid _rwUuid  = Guid.Parse("0000ff01-0000-1000-8000-00805f9b34fb");
    private readonly Guid _ntfUuid = Guid.Parse("0000ff02-0000-1000-8000-00805f9b34fb");
    private ICharacteristic? _rwChar;
    private ICharacteristic? _ntfChar;
    private IDevice? _device;

    // ── Google Drive + Auth services ──────────────────────────────────────
    private IGoogleDriveService?  _driveService;
    private IGoogleAuthService?   _authService;
    private bool _driveSignedIn;

    // ── ADC → nA constants (must match firmware) ──────────────────────────
    private const double VrefMv  = 3.3;
    private const double IzFrac  = 0.67;
    private const double RtiaOhm = 120_000.0;
    private double VzeroMv => VrefMv * IzFrac;

    // ── Channels and names ────────────────────────────────────────────────
    private static readonly int[]    Channels = { 1, 2, 3, 5, 6, 7 };
    private static readonly string[] ChNames  =
        { "Glucose-P", "Lactate-P", "pH-P", "Glucose-B", "Lactate-B", "pH-B" };

    // ── Steps 0-3 = PBS + Std1 + Std2 + Std3. Step 4 = Sample (Step 7) ──
    // _raw[stepIdx][chIdx] = list of (time_s, current_nA)
    private readonly List<(double tSec, double iNa)>[][] _raw;
    private readonly double?[][] _iFinal = new double?[5][];  // 5th = sample

    // ── Calibration fit per channel ───────────────────────────────────────
    private readonly double[] _a  = new double[6]; // slope
    private readonly double[] _b  = new double[6]; // standard intercept
    private readonly double[] _b2 = new double[6]; // matrix-corrected intercept

    // ── Concentrations [trialIdx 0-2][analyteIdx 0=Gluc,1=Lact,2=pH] ────
    private readonly double[,] _concs = new double[3, 3];

    // ── Step 7 spike concentrations [analyteIdx] ─────────────────────────
    private readonly double[] _sampleCs = new double[3];

    // ── Live buffer (current channel being recorded) ──────────────────────
    private readonly List<double> _liveT = new();
    private readonly List<double> _liveI = new();
    private volatile bool _isCollecting;

    // ── Step control ──────────────────────────────────────────────────────
    private CancellationTokenSource? _cts;
    private int _activeStepIdx;
    private volatile int _chProg;
    private readonly StringBuilder _txtBuf = new();

    // First t_ms per channel — subtract to normalise timestamps to 0s.
    // PIC t_ms runs continuously; this offset makes each channel start at 0s.
    private volatile uint _chFirstTms = uint.MaxValue;

    // ── Processed dataset (runs across all steps) ─────────────────────────
    private readonly List<(DateTime ts, int chIdx, double iNa)> _processed = new();

    // ─────────────────────────────────────────────────────────────────────
    public CalibrationPage()
    {
        this.InitializeComponent();

        // 5 step slots: PBS(0), Std1(1), Std2(2), Std3(3), Sample(4)
        _raw = new List<(double, double)>[5][];
        for (int s = 0; s < 5; s++)
        {
            _raw[s] = new List<(double, double)>[6];
            for (int c = 0; c < 6; c++)
                _raw[s][c] = new List<(double, double)>();
            _iFinal[s] = new double?[6];
        }
    }

    protected override async void OnNavigatedTo(NavigationEventArgs e)
    {
        base.OnNavigatedTo(e);
        _device = ((App)Application.Current).ConnectedDevice;

        try
        {
            var services = ((App)Application.Current).Host?.Services;
            _driveService = services?.GetService<IGoogleDriveService>();
            _authService  = services?.GetService<IGoogleAuthService>();
        }
        catch (Exception ex) { Debug.WriteLine($"[CAL] Services: {ex.Message}"); }

        await RefreshDriveSignInStateAsync();

        if (_device == null) return;

        var svc  = await _device.GetServiceAsync(_svcUuid);
        _rwChar  = await svc.GetCharacteristicAsync(_rwUuid);
        _ntfChar = await svc.GetCharacteristicAsync(_ntfUuid);

        if (_ntfChar != null && _ntfChar.CanUpdate)
        {
            _ntfChar.ValueUpdated -= OnBle;
            _ntfChar.ValueUpdated += OnBle;
            await _ntfChar.StartUpdatesAsync();
        }
    }

    private void BackButton_Click(object sender, RoutedEventArgs e)
    {
        _cts?.Cancel();
        if (Frame.CanGoBack) Frame.GoBack();
    }

    // ══════════════════════════════════════════════════════════════════════
    // STEP 1 — PBS RINSE (manual, no recording)
    // ══════════════════════════════════════════════════════════════════════

    private async void PbsFast_Click(object sender, RoutedEventArgs e)
    {
        await Cmd(0x20);
        SetStatus(Step1StatusText, "PBS Fast running — 500 uL/min. Press Stop Pump when done.");
    }

    private async void PbsSlow_Click(object sender, RoutedEventArgs e)
    {
        await Cmd(0x24);
        SetStatus(Step1StatusText, "PBS Slow running — 20 uL/min. Press Stop Pump when done.");
    }

    private async void PbsStop_Click(object sender, RoutedEventArgs e)
    {
        await Cmd(0x21);
        SetStatus(Step1StatusText, "Pump stopped.");
    }

    // ══════════════════════════════════════════════════════════════════════
    // STEPS 2–5 START/STOP HANDLERS
    // ══════════════════════════════════════════════════════════════════════

    private async void Step2Start_Click(object sender, RoutedEventArgs e)
    {
        _activeStepIdx = 0;
        SetStepRunning(Step2StartButton, Step2StopButton, true);
        _cts = new CancellationTokenSource();
        await RunStepAsync(0x25, 0, Step2TimerText, Step2StatusText,
                           PbsPlot, "PBS_Background", _cts.Token);
        SetStepRunning(Step2StartButton, Step2StopButton, false);
    }

    private async void Step2Stop_Click(object sender, RoutedEventArgs e) =>
        await StopStep(Step2StartButton, Step2StopButton, Step2StatusText);

    private async void Step3Start_Click(object sender, RoutedEventArgs e)
    {
        if (!ParseConcs(T1GlucBox.Text, T1LactBox.Text, T1PhBox.Text, 0))
        { SetStatus(Step3StatusText, "⚠ Enter valid concentrations."); return; }
        _activeStepIdx = 1;
        SetStepRunning(Step3StartButton, Step3StopButton, true);
        _cts = new CancellationTokenSource();
        await RunStepAsync(0x26, 1, Step3TimerText, Step3StatusText,
                           Trial1Plot, "Standard_1", _cts.Token);
        SetStepRunning(Step3StartButton, Step3StopButton, false);
    }

    private async void Step3Stop_Click(object sender, RoutedEventArgs e) =>
        await StopStep(Step3StartButton, Step3StopButton, Step3StatusText);

    private async void Step4Start_Click(object sender, RoutedEventArgs e)
    {
        if (!ParseConcs(T2GlucBox.Text, T2LactBox.Text, T2PhBox.Text, 1))
        { SetStatus(Step4StatusText, "⚠ Enter valid concentrations."); return; }
        _activeStepIdx = 2;
        SetStepRunning(Step4StartButton, Step4StopButton, true);
        _cts = new CancellationTokenSource();
        await RunStepAsync(0x27, 2, Step4TimerText, Step4StatusText,
                           Trial2Plot, "Standard_2", _cts.Token);
        SetStepRunning(Step4StartButton, Step4StopButton, false);
    }

    private async void Step4Stop_Click(object sender, RoutedEventArgs e) =>
        await StopStep(Step4StartButton, Step4StopButton, Step4StatusText);

    private async void Step5Start_Click(object sender, RoutedEventArgs e)
    {
        if (!ParseConcs(T3GlucBox.Text, T3LactBox.Text, T3PhBox.Text, 2))
        { SetStatus(Step5StatusText, "⚠ Enter valid concentrations."); return; }
        _activeStepIdx = 3;
        SetStepRunning(Step5StartButton, Step5StopButton, true);
        _cts = new CancellationTokenSource();
        await RunStepAsync(0x28, 3, Step5TimerText, Step5StatusText,
                           Trial3Plot, "Standard_3", _cts.Token);
        SetStepRunning(Step5StartButton, Step5StopButton, false);
    }

    private async void Step5Stop_Click(object sender, RoutedEventArgs e) =>
        await StopStep(Step5StartButton, Step5StopButton, Step5StatusText);

    // ══════════════════════════════════════════════════════════════════════
    // STEP 7 — SAMPLE MEASUREMENT (0x30)
    // ══════════════════════════════════════════════════════════════════════

    private async void Step7Start_Click(object sender, RoutedEventArgs e)
    {
        // Parse spike concentrations Cs for each analyte
        if (!TryConc(S7GlucCsBox.Text, out _sampleCs[0]) ||
            !TryConc(S7LactCsBox.Text, out _sampleCs[1]) ||
            !TryConc(S7PhCsBox.Text,   out _sampleCs[2]))
        {
            SetStatus(Step7StatusText,
                "⚠ Enter valid spike concentrations (Cs) for all three analytes.");
            return;
        }

        _activeStepIdx = 4; // slot 4 = sample
        SetStepRunning(Step7StartButton, Step7StopButton, true);
        _cts = new CancellationTokenSource();
        await RunStepAsync(0x30, 4, Step7TimerText, Step7StatusText,
                           SamplePlot, "Sample", _cts.Token);

        // After recording, show I_final per channel and auto-fill Step 6
        // matrix correction boxes with the Glucose Set-P values
        ShowStep7Results();
        SetStepRunning(Step7StartButton, Step7StopButton, false);
    }

    private async void Step7Stop_Click(object sender, RoutedEventArgs e) =>
        await StopStep(Step7StartButton, Step7StopButton, Step7StatusText);

    // Display I_final results for each channel and auto-fill matrix correction
    private void ShowStep7Results()
    {
        var sb = new StringBuilder();
        sb.AppendLine($"{"Channel",-12}{"Analyte",-12}{"I_final (nA)"}");
        sb.AppendLine(new string('-', 38));

        for (int i = 0; i < Channels.Length; i++)
        {
            string iv = _iFinal[4][i].HasValue
                ? _iFinal[4][i]!.Value.ToString("F4", CultureInfo.InvariantCulture)
                : "N/A";
            sb.AppendLine($"CH{Channels[i],-10}{ChNames[i],-12}{iv}");
        }

        DispatcherQueue.TryEnqueue(() =>
        {
            Step7ResultsText.Text = sb.ToString();

            // Auto-fill Step 6 matrix correction boxes with Glucose Set-P (ch1)
            // Researcher can override these manually
            if (_iFinal[4][0].HasValue)
            {
                SampleSsBox.Text = _iFinal[4][0]!.Value.ToString("F4",
                    CultureInfo.InvariantCulture);
                SampleCsBox.Text = _sampleCs[0].ToString(CultureInfo.InvariantCulture);
            }
        });

        _ = SaveSampleCsvAsync();
    }

    // ══════════════════════════════════════════════════════════════════════
    // STOP EARLY — three-layer stop
    //
    // LAYER 1: _cts.Cancel() → app timer exits on its next 1s tick
    // LAYER 2: _isCollecting = false → BLE notify stops writing data
    // LAYER 3: Send 0x00 → firmware delay_ms_interruptible catches it
    //          within 1ms, sets g_stop_requested=true, loop exits,
    //          main.c calls motor_stop()
    // LAYER 4: Send 0x29 → firmware directly calls motor_stop() as backup
    //          (handles case where 0x00 was missed or arrived too late)
    //
    // Total time from button press to motor off: ~70ms worst case.
    // ══════════════════════════════════════════════════════════════════════
    private async Task StopStep(Button startBtn, Button stopBtn, TextBlock statusTb)
    {
        SetStatus(statusTb, "⏹ Stopping…");

        // Layer 1: cancel app timer immediately
        _cts?.Cancel();

        // Layer 2: stop BLE data collection immediately
        _isCollecting = false;

        // Layer 3: send 0x00 to stop the measurement loop on the PIC
        // The firmware checks for this every 1ms inside delay_ms_interruptible
        await Cmd(0x00);

        // Wait 20ms then send 0x00 again as safety net in case ISR missed it
        await Task.Delay(20);
        await Cmd(0x00);

        // Layer 4: send 0x29 — dedicated emergency stop that directly calls
        // motor_stop() and sets g_stop_requested in the firmware regardless
        // of where in the loop the PIC currently is
        await Task.Delay(20);
        await Cmd(0x29);

        SetStatus(statusTb, "⏹ Stopped. Motor off. Press Start to run again.");
        SetStepRunning(startBtn, stopBtn, false);
    }

    // ══════════════════════════════════════════════════════════════════════
    // CORE STEP RUNNER
    //
    // TIMER FIX:
    //   The 60s timer starts at 0s THE MOMENT the command byte is sent.
    //   It does NOT wait for the PIC to send the first CH label.
    //   Previously the code waited up to 30 seconds for _chProg > 0 before
    //   starting the timer — that was the source of the timer delay.
    //   The CH label tracking still works in the BLE notify callback —
    //   it just runs in parallel with the timer, not before it.
    //
    // PER CHANNEL:
    //   - Timer counts 0→60s, graph updates every 1s with current (nA) data
    //   - All samples stored in _raw[stepIdx][chIdx]
    //   - I_final = average of last 10 samples (steady-state, last 1s)
    //   - 1s gap between channels
    //
    // AFTER ALL CHANNELS:
    //   - Raw CSV saved (time_s, current_nA per channel)
    //   - Processed CSV appended
    // ══════════════════════════════════════════════════════════════════════
    private async Task RunStepAsync(
        byte cmd,
        int stepIdx,
        TextBlock timerTb,
        TextBlock statusTb,
        ScottPlot.WinUI.WinUIPlot plot,
        string stepName,
        CancellationToken token)
    {
        if (_rwChar == null)
        {
            SetStatus(statusTb, "⚠ Not connected to device.");
            return;
        }

        // Clear old data
        _activeStepIdx = stepIdx;
        _chProg = 0;


        for (int c = 0; c < 6; c++) _raw[stepIdx][c].Clear();
        _chProg = 0;
        lock (_liveT) { _liveT.Clear(); _liveI.Clear(); }
        _isCollecting = true;
        _txtBuf.Clear();

        DispatcherQueue.TryEnqueue(() => { plot.Plot.Clear(); plot.Refresh(); });
        SetStatus(statusTb, $"Sending command to device…");

        // Send command — PIC starts pump + measurement
        await Cmd(cmd);

        // ── TIMER STARTS HERE — immediately after sending the command ──────
        // No waiting for CH labels first. Timer shows real elapsed time.
        SetStatus(statusTb, $"{stepName} — starting, waiting for first channel…");

        for (int chIdx = 0; chIdx < Channels.Length; chIdx++)
        {
            if (token.IsCancellationRequested) break;

            int ch = Channels[chIdx];

            // Reset timestamp offset — each channel must start at 0.0s
            // (PIC t_ms runs continuously; _chFirstTms corrects for this)
            _chFirstTms = uint.MaxValue;

            // Set _chProg so BLE callback writes into the right raw slot
            _chProg = chIdx + 1;

            // Clear live graph buffer for this channel
            lock (_liveT) { _liveT.Clear(); _liveI.Clear(); }

            SetStatus(statusTb,
                $"{stepName} — CH{ch} {ChNames[chIdx]} ({chIdx + 1}/6) — recording…");

            // ── Active 60s collection window ──────────────────────────────
            // Graph refreshes every 1s. Timer counts 0→60s.
            // Only samples with tSec ≤ 60.0 are stored (see OnBle).
            for (int s = 0; s <= 60 && !token.IsCancellationRequested; s++)
            {
                int cap = s;
                DispatcherQueue.TryEnqueue(() => timerTb.Text = $"{cap}s / 60s");
                RefreshLiveGraph(plot);
                if (s < 60) await SafeDelay(1000, token);
            }

            if (token.IsCancellationRequested) break;

            DispatcherQueue.TryEnqueue(() => timerTb.Text = "60s / 60s");

            // ── 3s drain — collect late BLE packets (ESP32 buffer flush) ──
            SetStatus(statusTb, $"{stepName} — CH{ch} finishing… ({chIdx + 1}/6)");
            for (int d = 0; d < 3 && !token.IsCancellationRequested; d++)
                await SafeDelay(1000, token);

            if (token.IsCancellationRequested) break;

            // ── Compute I_final (avg last 10 samples, steady-state) ───────
            var chRaw = _raw[stepIdx][chIdx];
            double? iFin = null;
            if (chRaw.Count >= 10)
                iFin = chRaw.TakeLast(10).Average(x => x.iNa);
            else if (chRaw.Count > 0)
                iFin = chRaw.Average(x => x.iNa);

            _iFinal[stepIdx][chIdx] = iFin;
            if (iFin.HasValue)
                _processed.Add((DateTime.Now, chIdx, iFin.Value));

            // ── Save CSV immediately, fire-and-forget the Drive upload ────
            // We save locally first (instant), then upload in the background.
            // This way the app never blocks waiting for Drive — if upload is
            // slow the next channel still starts on time.
            _ = SaveSingleChannelCsvAsync(stepIdx, chIdx, stepName);

            // ── 3s gap before next channel ────────────────────────────────
            if (chIdx < Channels.Length - 1)
            {
                SetStatus(statusTb, $"{stepName} — next channel in 3s…");
                DispatcherQueue.TryEnqueue(() => timerTb.Text = "0s / 60s");
                await SafeDelay(3000, token);
            }
        }

        _isCollecting = false;

        if (token.IsCancellationRequested) return;

        DispatcherQueue.TryEnqueue(() => timerTb.Text = "60s / 60s");
        SetStatus(statusTb, $"✅ {stepName} complete — saving…");

        await SaveProcessedCsvAsync(stepName);

        SetStatus(statusTb, $"✅ {stepName} complete.");
    }

    // ── Refresh live graph with current buffer ────────────────────────────
    private void RefreshLiveGraph(ScottPlot.WinUI.WinUIPlot plot)
    {
        double[] xs, ys;
        lock (_liveT)
        {
            if (_liveT.Count < 2) return;
            xs = _liveT.ToArray();
            ys = _liveI.ToArray();
        }

        DispatcherQueue.TryEnqueue(() =>
        {
            plot.Plot.Clear();
            var sc = plot.Plot.Add.Scatter(xs, ys);
            sc.LineWidth = 1; sc.MarkerSize = 0;
            plot.Plot.XLabel("Time (s)");
            plot.Plot.YLabel("Current (uA)");
            plot.Plot.Axes.AutoScale();
            plot.Refresh();
        });
    }

    // ══════════════════════════════════════════════════════════════════════
    // BLE NOTIFY CALLBACK
    //
    // TEXT PACKETS — channel labels + step markers (all printable ASCII)
    //   "CH1", "CH2"… → advance _chProg, flush live→raw for previous channel
    //   "END"         → flush last channel
    //
    // BINARY ADC PACKETS — 7 bytes each
    //   byte 0   = type (0=data, 1=begin, 2=end)
    //   bytes 1-4 = time_ms uint32 LE
    //   bytes 5-6 = ADC code uint16 LE
    //   → Convert time_ms to seconds for X axis (starts at 0 per channel)
    //   → Convert ADC → nA using AdcToNa()
    //   → Store in _liveT/_liveI AND directly in _raw[step][ch]
    // ══════════════════════════════════════════════════════════════════════
    private void OnBle(object sender,
        Plugin.BLE.Abstractions.EventArgs.CharacteristicUpdatedEventArgs e)
    {


        var bytes = e.Characteristic.Value;
        if (bytes == null || bytes.Length == 0) return;

        Debug.WriteLine(
    $"[BLE RX] bytes={bytes.Length}, isCollecting={_isCollecting}, " +
    $"_activeStepIdx={_activeStepIdx}, _chProg={_chProg}");

        bool isText = bytes.All(b => (b >= 32 && b <= 126) || b == '\n' || b == '\r');

        if (isText)
        {
            _txtBuf.Append(Encoding.ASCII.GetString(bytes));
            string buf = _txtBuf.ToString();

            for (int i = 0; i < Channels.Length; i++)
            {
                if (!buf.Contains($"CH{Channels[i]}")) continue;

                if (_chProg > 0)
                    FlushLive(_activeStepIdx, _chProg - 1);
                Debug.WriteLine(
    $"[CH DETECT] label=CH{Channels[i]}, " +
    $"new _chProg={_chProg}, step={_activeStepIdx}");

                lock (_liveT) { _liveT.Clear(); _liveI.Clear(); }
                _chProg = i + 1;
                _txtBuf.Clear();
                Debug.WriteLine($"[CAL] CH{Channels[i]} label received");
                return;
            }

            if (buf.Contains("END") || buf.Contains("SAMPLE_END"))
            {
                if (_chProg > 0)
                    FlushLive(_activeStepIdx, _chProg - 1);
                _chProg = 6;
                _txtBuf.Clear();
            }
            return;
        }

        if (!_isCollecting) return;

        const int pkt = 7;
        for (int off = 0; off + pkt <= bytes.Length; off += pkt)
        {
            if (bytes[off] != 0) continue; // type 0 = data sample

            uint   tMs  = BitConverter.ToUInt32(bytes, off + 1);
            ushort code = BitConverter.ToUInt16(bytes, off + 5);

            // ── Normalise timestamp ───────────────────────────────────────
            // PIC t_ms runs continuously across ALL channels — never resets.
            // We record the first t_ms for each channel and subtract it so
            // every channel always starts at exactly 0.0s in the CSV/graph.
            if (_chFirstTms == uint.MaxValue)
                _chFirstTms = tMs;
            double tSec = (tMs - _chFirstTms) / 1000.0;

            // ── Cap at 60s — discard late BLE packets beyond the window ───
            // The drain phase collects up to 7s of late data, but we only
            // accept samples that arrived within the 60s measurement window.
            // This prevents anomalous large timestamps in the CSV.
            if (tSec > 60.0) continue;

            // Convert ADC → µA (returned by AdcTomicroA, stored in iNa field)
            double imicroA = AdcTomicroA(code);

            lock (_liveT)
            {
                _liveT.Add(tSec);
                _liveI.Add(imicroA);
            }

            if (_chProg > 0 && _chProg <= 6)
                _raw[_activeStepIdx][_chProg - 1].Add((tSec, imicroA));
        }
    }

    private void FlushLive(int stepIdx, int chIdx)
    {
        lock (_liveT)
        {
            var dest = _raw[stepIdx][chIdx];
            dest.Clear();
            for (int i = 0; i < _liveT.Count; i++)
                dest.Add((_liveT[i], _liveI[i]));
            _chFirstTms = uint.MaxValue;   // reset offset for next channel
        }
    }

    private double AdcTomicroA(ushort code)
    {
        double voutMv = code * VrefMv / 1023.0;
        double iUa = (voutMv - VzeroMv) / RtiaOhm;
        return iUa;
    }

    // ══════════════════════════════════════════════════════════════════════
    // STEP 6 — LINEAR REGRESSION + MATRIX CORRECTION
    //
    // x = [0, C₁, C₂, C₃]   (PBS=0 + 3 standards)
    // y = [I_PBS, I₁, I₂, I₃] (I_final in nA)
    // Fit: y = a·x + b
    //
    // Matrix correction (optional — can use Step 7 I_final as Ss):
    //   b₂ = Ss − a·Cs
    //   Updated curve: y = a·x + b₂
    // ══════════════════════════════════════════════════════════════════════
    private async void RunRegression_Click(object sender, RoutedEventArgs e)
    {
        var results = new StringBuilder();
        results.AppendLine(
            $"{"CH",-5}{"Analyte",-12}{"Slope a",-13}{"b",-12}{"b₂",-12}{"R²"}");
        results.AppendLine(new string('-', 60));

        GlucosePlot.Plot.Clear();
        LactatePlot.Plot.Clear();
        PhPlot.Plot.Clear();

        double sampleCs = 0.0;
        double sampleSs = 0.0;
        bool hasSample = TryConc(SampleCsBox.Text, out sampleCs) &&
                         TryConc(SampleSsBox.Text, out sampleSs) &&
                         sampleCs > 0;

        var defs = new[]
        {
            new { Name="Glucose", Col=0, ChP=0, ChB=3,
                  Graph=GlucosePlot, ColorP="#2196F3", ColorB="#64B5F6" },
            new { Name="Lactate", Col=1, ChP=1, ChB=4,
                  Graph=LactatePlot, ColorP="#E91E63", ColorB="#F48FB1" },
            new { Name="pH",      Col=2, ChP=2, ChB=5,
                  Graph=PhPlot,     ColorP="#4CAF50", ColorB="#A5D6A7" },
        };

        bool anyData = false;

        foreach (var d in defs)
        {
            double[] concs = { 0.0, _concs[0, d.Col], _concs[1, d.Col], _concs[2, d.Col] };

            var chs = new[]
            {
                new { Label=$"CH{Channels[d.ChP]}", Name=ChNames[d.ChP],
                      Idx=d.ChP, Color=d.ColorP },
                new { Label=$"CH{Channels[d.ChB]}", Name=ChNames[d.ChB],
                      Idx=d.ChB, Color=d.ColorB },
            };

            foreach (var ch in chs)
            {
                double?[] vals =
                {
                    _iFinal[0][ch.Idx], _iFinal[1][ch.Idx],
                    _iFinal[2][ch.Idx], _iFinal[3][ch.Idx],
                };

                if (vals.Any(v => v == null))
                {
                    results.AppendLine(
                        $"{ch.Label,-5}{ch.Name,-12}  (missing — run steps 2–5 first)");
                    continue;
                }

                anyData = true;
                double[] ys = vals.Select(v => v!.Value).ToArray();

                // Least-squares regression
                double xMean = concs.Average(), yMean = ys.Average();
                double num = 0, den = 0;
                for (int i = 0; i < 4; i++)
                {
                    num += (concs[i] - xMean) * (ys[i] - yMean);
                    den += (concs[i] - xMean) * (concs[i] - xMean);
                }
                double a = den != 0 ? num / den : 0;
                double b = yMean - a * xMean;

                // R²
                double ssTot = ys.Sum(y => (y - yMean) * (y - yMean));
                double ssRes = 0;
                for (int i = 0; i < 4; i++)
                { double pred = a * concs[i] + b; ssRes += (ys[i] - pred) * (ys[i] - pred); }
                double r2 = ssTot > 0 ? 1.0 - ssRes / ssTot : 1.0;

                // Matrix correction
                double b2 = hasSample ? sampleSs - a * sampleCs : b;

                _a[ch.Idx]  = a;
                _b[ch.Idx]  = b;
                _b2[ch.Idx] = b2;

                results.AppendLine(
                    $"{ch.Label,-5}{ch.Name,-12}  {a,+9:F3}  {b,8:F2}  {b2,8:F2}  {r2:F4}");

                // Plot
                var colour  = ScottPlot.Color.FromHex(ch.Color);
                var scatter = d.Graph.Plot.Add.Scatter(concs, ys);
                scatter.Color = colour; scatter.MarkerSize = 8;
                scatter.LineWidth = 0; scatter.Label = ch.Label;

                double xMin = concs.Min(), xMax = concs.Max();
                var fitLine = d.Graph.Plot.Add.Scatter(
                    new[] { xMin, xMax },
                    new[] { a * xMin + b, a * xMax + b });
                fitLine.Color = colour; fitLine.MarkerSize = 0; fitLine.LineWidth = 1.5f;

                if (hasSample && Math.Abs(b2 - b) > 0.001)
                {
                    var fitCorr = d.Graph.Plot.Add.Scatter(
                        new[] { xMin, xMax },
                        new[] { a * xMin + b2, a * xMax + b2 });
                    fitCorr.Color = colour; fitCorr.MarkerSize = 0;
                    fitCorr.LineWidth = 3.0f;
                    fitCorr.Label = $"{ch.Label} (b₂)";
                }
            }

            if (anyData)
            {
                d.Graph.Plot.XLabel($"{d.Name} Concentration (mM)");
                d.Graph.Plot.YLabel("Current (uA)");
                d.Graph.Plot.Title($"{d.Name} — y = a·x + b");
                d.Graph.Plot.ShowLegend();
                d.Graph.Plot.Axes.AutoScale();
            }
            d.Graph.Refresh();
        }

        RegressionResultsText.Text = results.ToString();
        await SaveCalibrationCsvAsync();
    }

    // ══════════════════════════════════════════════════════════════════════
    // GOOGLE DRIVE SIGN-IN
    // ══════════════════════════════════════════════════════════════════════

    private async void DriveSignIn_Click(object sender, RoutedEventArgs e)
    {
        if (_authService is null) { ShowDriveBanner("Auth service unavailable.", error: true); return; }
        if (_driveSignedIn) { ShowDriveBanner("Already connected.", error: false); return; }
        if (DriveButtonText is not null)   DriveButtonText.Text   = "Signing in…";
        if (DriveSignInButton is not null) DriveSignInButton.IsEnabled = false;
        try
        {
            await _authService.LoginAsync();
            _driveSignedIn = true;
            ShowDriveBanner("Connected to Google Drive ✓", error: false);
        }
        catch (Exception ex) { ShowDriveBanner($"Sign-in failed: {ex.Message}", error: true); }
        finally { await RefreshDriveSignInStateAsync(); }
    }

    private async Task RefreshDriveSignInStateAsync()
    {
        if (_authService is null) { UpdateDriveButton(false); return; }
        try { _driveSignedIn = !string.IsNullOrEmpty(await _authService.GetAccessTokenAsync()); }
        catch { _driveSignedIn = false; }
        UpdateDriveButton(_driveSignedIn);
        if (_driveSignedIn) ShowDriveBanner("Drive connected — files saved automatically.", error: false);
    }

    private void UpdateDriveButton(bool on) => DispatcherQueue.TryEnqueue(() =>
    {
        if (DriveButtonText is not null)   DriveButtonText.Text   = on ? "Drive: Connected" : "Sign in to Drive";
        if (DriveButtonIcon is not null)   DriveButtonIcon.Text   = on ? "✓" : "☁";
        if (DriveSignInButton is not null)
        {
            DriveSignInButton.Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(
                on ? Microsoft.UI.Colors.SeaGreen : Microsoft.UI.ColorHelper.FromArgb(255, 66, 133, 244));
            DriveSignInButton.IsEnabled = true;
        }
    });

    private void ShowDriveBanner(string msg, bool error) => DispatcherQueue.TryEnqueue(() =>
    {
        if (DriveStatusText   is not null) DriveStatusText.Text       = msg;
        if (DriveStatusBanner is not null)
        {
            DriveStatusBanner.Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(
                error ? Microsoft.UI.ColorHelper.FromArgb(255, 255, 235, 238)
                      : Microsoft.UI.ColorHelper.FromArgb(255, 232, 245, 233));
            DriveStatusBanner.Visibility = Microsoft.UI.Xaml.Visibility.Visible;
        }
    });

    // ══════════════════════════════════════════════════════════════════════
    // CSV SAVERS + GOOGLE DRIVE UPLOAD
    //
    // STRATEGY:
    //   1. Save the CSV to ApplicationData.Current.LocalFolder (always works)
    //   2. Upload the saved file to Google Drive via IGoogleDriveService
    //      Upload is best-effort — if Drive is unavailable/not signed in,
    //      the local file is kept and a warning is logged.
    //
    // FILES PRODUCED (in Google Drive root, or SmartOrgan folder if set):
    //   Raw:       YYYYMMDD_HHMMSS_CHx_StepName_raw.csv
    //              → time_s, current_nA  (one per channel per step, ~600 rows)
    //   Processed: processed_dataset.csv
    //              → growing log: timestamp, step, channel, analyte, I_final_uA
    //   Calib:     YYYYMMDD_HHMMSS_CHx_AnalyteName_calibration.csv
    //              → label, conc_mM, I_final_uA  + slope/b/b2 in header
    //   Sample:    YYYYMMDD_HHMMSS_sample.csv
    //              → channel, analyte, Cs_mM, I_final_uA
    //   Time-Conc: time_concentration.csv
    //              → growing log: timestamp, step, channel, analyte,
    //                             I_final_uA, concentration_mM
    //              (C = (I_final - b2) / a — the monitoring output)
    // ══════════════════════════════════════════════════════════════════════

    // ── Upload a local StorageFile to Google Drive (best-effort) ─────────
    private async Task UploadToDriveAsync(StorageFile file)
    {
        if (_driveService is null)
        {
            Debug.WriteLine("[CAL] Drive: service not available, skipping upload.");
            return;
        }
        try
        {
            var info = await _driveService.UploadCsvAsync(file.Path);
            Debug.WriteLine($"[CAL] Drive: uploaded {info.Name} (id={info.Id})");
        }
        catch (Exception ex)
        {
            // Non-fatal — local file already saved
            Debug.WriteLine($"[CAL] Drive upload failed for {file.Name}: {ex.Message}");
        }
    }

    // ── Helper: save text to LocalFolder and upload to Drive ─────────────
    private async Task<StorageFile> SaveLocalAsync(string fileName, string content)
    {
        var folder = ApplicationData.Current.LocalFolder;
        var file   = await folder.CreateFileAsync(fileName, CreationCollisionOption.ReplaceExisting);
        await FileIO.WriteTextAsync(file, content);
        Debug.WriteLine($"[CAL] Saved locally: {file.Path}");
        return file;
    }

    private async Task SaveAndUploadAsync(string fileName, string content)
    {
        var file = await SaveLocalAsync(fileName, content);
        await UploadToDriveAsync(file);
    }

    // ── Helper: append to a growing local file and re-upload ─────────────
    private async Task AppendAndUploadAsync(string fileName, string header, string newRows)
    {
        var folder = ApplicationData.Current.LocalFolder;

        // Read existing content if file already exists
        string existing = "";
        try
        {
            var existingFile = await folder.GetFileAsync(fileName);
            existing = await FileIO.ReadTextAsync(existingFile);
        }
        catch { /* file does not exist yet — start fresh */ }

        string combined = string.IsNullOrEmpty(existing)
            ? header + newRows
            : existing.TrimEnd() + "" + newRows;

        var file = await SaveLocalAsync(fileName, combined);
        await UploadToDriveAsync(file);
    }

    // ── 1a. Save a single channel immediately after it finishes ──────────
    // Called inside the per-channel loop so each file is saved and uploaded
    // to Drive BEFORE the next channel starts. Data is safe before moving on.
    private async Task SaveSingleChannelCsvAsync(int stepIdx, int chIdx, string stepName)
    {
        var samples = _raw[stepIdx][chIdx];
        if (samples.Count == 0) return;

        var now    = DateTime.Now;
        string ts  = now.ToString("yyyyMMdd_HHmmss");
        string liverId = ((App)Application.Current).CurrentLiverId;
        if (string.IsNullOrWhiteSpace(liverId)) liverId = "LiverUnknown";
        string safeName = stepName.Replace(" ", "_");

        var sb = new StringBuilder();
        sb.AppendLine("# SmartOrgan Raw Measurement");
        sb.AppendLine($"# Step: {stepName}  Channel: CH{Channels[chIdx]}  Analyte: {ChNames[chIdx]}");
        sb.AppendLine($"# Recorded: {now:yyyy-MM-dd HH:mm:ss}");
        sb.AppendLine($"# Samples: {samples.Count}");
        sb.AppendLine($"# I_final (avg last 10 samples): {_iFinal[stepIdx][chIdx]:F4} uA");
        sb.AppendLine("time_s,current_uA");
        foreach (var (t, i) in samples)
            sb.AppendLine($"{t:F3},{i:F4}");

        string fn = $"{liverId}_{ts}_{safeName}_CH{Channels[chIdx]}.csv";
        await SaveAndUploadAsync(fn, sb.ToString());
        Debug.WriteLine($"[CAL] Saved+uploaded: {fn} ({samples.Count} samples)");
    }

    // ── 1b. Bulk raw CSV saver ────────────────────────────────────────────
    private async Task SaveRawCsvAsync(int stepIdx, string stepName)
    {
        string ts = DateTime.Now.ToString("yyyyMMdd_HHmmss");

        for (int chIdx = 0; chIdx < Channels.Length; chIdx++)
        {
            var samples = _raw[stepIdx][chIdx];
            if (samples.Count == 0) continue;

            var sb = new StringBuilder();
            sb.AppendLine($"# SmartOrgan Raw Measurement");
            sb.AppendLine($"# Step: {stepName}  Channel: CH{Channels[chIdx]}  Analyte: {ChNames[chIdx]}");
            sb.AppendLine($"# Recorded: {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
            sb.AppendLine($"# I_final (avg last 10 samples): {_iFinal[stepIdx][chIdx]:F4} uA");
            sb.AppendLine("time_s,current_uA");
            foreach (var (t, i) in samples)
                sb.AppendLine($"{t:F3},{i:F4}");

            //string fn = $"{ts}_CH{Channels[chIdx]}_{stepName}_raw.csv";
            string liverId = ((App)Application.Current).CurrentLiverId;

            if (string.IsNullOrWhiteSpace(liverId))
                liverId = "LiverUnknown";

            string safeStepName = stepName.Replace(" ", "_");

            string fn = $"{liverId}_{ts}_{safeStepName}_CH{Channels[chIdx]}.csv";
            await SaveAndUploadAsync(fn, sb.ToString());
        }
    }

    // ── 2. Processed dataset — I_final per channel per step, growing ──────
    private async Task SaveProcessedCsvAsync(string stepName)
    {
        string header =
            "# SmartOrgan Processed Dataset — I_final per channel per step\r\n" +
            "\r\n" +
            "timestamp,step,channel,analyte,I_final_uA\r\n";

        // Build only the rows from the current step
        var rows = new StringBuilder();
        int startIdx = Math.Max(0, _processed.Count - Channels.Length);
        for (int i = startIdx; i < _processed.Count; i++)
        {
            var (stamp, chIdx, iNa) = _processed[i];
            rows.AppendLine(
                $"{stamp:yyyy-MM-dd HH:mm:ss},{stepName},CH{Channels[chIdx]},{ChNames[chIdx]},{iNa:F4}");
        }
        string liverId = ((App)Application.Current).CurrentLiverId;
        if (string.IsNullOrWhiteSpace(liverId))
        {
            liverId = "LiverUnknown";
        }
        string fileName = $"{liverId}_processed_dataset.csv";
        await AppendAndUploadAsync(fileName, header, rows.ToString());
    }

    // ── 3. Calibration CSV — conc vs I_final + slope/b/b2 in header ──────
    private async Task SaveCalibrationCsvAsync()
    {
        string ts  = DateTime.Now.ToString("yyyyMMdd_HHmmss");
        int[] acol = { 0, 1, 2, 0, 1, 2 };

        for (int chIdx = 0; chIdx < Channels.Length; chIdx++)
        {
            var sb = new StringBuilder();
            sb.AppendLine($"# SmartOrgan Calibration Curve");
            sb.AppendLine($"# Channel: CH{Channels[chIdx]}  Analyte: {ChNames[chIdx]}");
            sb.AppendLine($"# Saved: {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
            sb.AppendLine($"# slope_a={_a[chIdx]:F6}  intercept_b={_b[chIdx]:F4}  corrected_b2={_b2[chIdx]:F4}");
            sb.AppendLine($"# Standard equation:       I_nA = a * C_mM + b");
            sb.AppendLine($"# Matrix-corrected eq:     I_nA = a * C_mM + b2");
            sb.AppendLine("label,concentration_mM,I_final_uA");

            string pbsI = _iFinal[0][chIdx].HasValue
                ? _iFinal[0][chIdx]!.Value.ToString("F4", CultureInfo.InvariantCulture) : "N/A";
            sb.AppendLine($"PBS,0,{pbsI}");

            for (int t = 0; t < 3; t++)
            {
                double c  = _concs[t, acol[chIdx]];
                string iv = _iFinal[t + 1][chIdx].HasValue
                    ? _iFinal[t + 1][chIdx]!.Value.ToString("F4", CultureInfo.InvariantCulture) : "N/A";
                sb.AppendLine($"Std{t + 1},{c.ToString(CultureInfo.InvariantCulture)},{iv}");
            }

            string fn = $"{ts}_CH{Channels[chIdx]}_{ChNames[chIdx]}_calibration.csv";
            await SaveAndUploadAsync(fn, sb.ToString());
        }

        // Also save and upload the time-concentration monitoring output
        await SaveTimeConcentrationCsvAsync();

        Debug.WriteLine("[CAL] Calibration CSVs saved and uploaded.");
    }

    // ── 4. Sample CSV — Cs and Ss per channel from Step 7 ────────────────
    private async Task SaveSampleCsvAsync()
    {
        string ts  = DateTime.Now.ToString("yyyyMMdd_HHmmss");
        int[] acol = { 0, 1, 2, 0, 1, 2 };

        var sb = new StringBuilder();
        sb.AppendLine($"# SmartOrgan Sample Measurement (Step 7)");
        sb.AppendLine($"# Recorded: {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
        sb.AppendLine($"# Cs = known spike concentration  Ss = measured I_final");
        sb.AppendLine("channel,analyte,Cs_mM,I_final_nA_Ss");

        for (int chIdx = 0; chIdx < Channels.Length; chIdx++)
        {
            double cs = _sampleCs[acol[chIdx]];
            string ss = _iFinal[4][chIdx].HasValue
                ? _iFinal[4][chIdx]!.Value.ToString("F4", CultureInfo.InvariantCulture) : "N/A";
            sb.AppendLine(
                $"CH{Channels[chIdx]},{ChNames[chIdx]},{cs.ToString(CultureInfo.InvariantCulture)},{ss}");
        }

        string fn = $"{ts}_sample.csv";
        await SaveAndUploadAsync(fn, sb.ToString());
    }

    // ── 5. Time-Concentration CSV — monitoring output, growing file ───────
    // C = (I_final - b2) / a  (matrix-corrected calibration curve)
    // This is the "Monitoring Output" from the spec diagram — the final
    // result that tells the researcher what concentration is in each channel.
    private async Task SaveTimeConcentrationCsvAsync()
    {
        if (_a.All(x => x == 0)) return; // no calibration fit yet

        string header =
            "# SmartOrgan Time-Concentration Monitoring Output" +
            "# C = (I_final - b2) / a  (matrix-corrected calibration)" +
            "timestamp,step,channel,analyte,I_final_uA,concentration_mM";

        var rows = new StringBuilder();
        int[] acol = { 0, 1, 2, 0, 1, 2 };
        string[] stepNames = { "PBS_Background", "Standard_1", "Standard_2", "Standard_3", "Sample" };

        for (int stepIdx = 0; stepIdx < 5; stepIdx++)
        {
            for (int chIdx = 0; chIdx < Channels.Length; chIdx++)
            {
                if (!_iFinal[stepIdx][chIdx].HasValue) continue;
                double iNa = _iFinal[stepIdx][chIdx]!.Value;
                double a   = _a[chIdx];
                double b2  = _b2[chIdx];
                if (Math.Abs(a) < 1e-12) continue;

                double concMm = (iNa - b2) / a;

                rows.AppendLine(
                    $"{DateTime.Now:yyyy-MM-dd HH:mm:ss},{stepNames[stepIdx]}," +
                    $"CH{Channels[chIdx]},{ChNames[chIdx]}," +
                    $"{iNa:F4},{concMm:F4}");
            }
        }

        if (rows.Length > 0)
            await AppendAndUploadAsync("time_concentration.csv", header, rows.ToString());

        Debug.WriteLine("[CAL] Time-concentration CSV updated and uploaded.");
    }

    // ══════════════════════════════════════════════════════════════════════
    // HELPERS
    // ══════════════════════════════════════════════════════════════════════

    private void SetStepRunning(Button startBtn, Button stopBtn, bool running)
    {
        DispatcherQueue.TryEnqueue(() =>
        {
            startBtn.IsEnabled = !running;
            stopBtn.IsEnabled  =  running;
        });
    }

    private void SetStatus(TextBlock tb, string text) =>
        DispatcherQueue.TryEnqueue(() => tb.Text = text);

    private bool ParseConcs(string g, string l, string p, int trial)
    {
        if (!TryConc(g, out double gv) ||
            !TryConc(l, out double lv) ||
            !TryConc(p, out double pv)) return false;
        _concs[trial, 0] = gv;
        _concs[trial, 1] = lv;
        _concs[trial, 2] = pv;
        return true;
    }

    private static bool TryConc(string? s, out double v) =>
        double.TryParse(s?.Trim(), NumberStyles.Any,
                        CultureInfo.InvariantCulture, out v) && v >= 0;

    private async Task Cmd(byte b)
    {
        if (_rwChar == null) return;
        await _rwChar.WriteAsync(new byte[] { b });
        Debug.WriteLine($"[CAL] → 0x{b:X2}");
    }

    private static Task SafeDelay(int ms, CancellationToken token) =>
        Task.Delay(ms, token).ContinueWith(_ => { });
}


