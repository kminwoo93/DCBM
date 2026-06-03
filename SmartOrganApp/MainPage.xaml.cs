using System.Collections.ObjectModel;
using SmartOrganApp.Models;
using SmartOrganApp.Services.Auth;
using SmartOrganApp.Services.Drive;

namespace SmartOrganApp;

public sealed partial class MainPage : Page
{
    private readonly IGoogleAuthService _googleAuthService;
    private readonly IGoogleDriveService _googleDriveService;
    private readonly ILocalCsvService _localCsvService;

    public ObservableCollection<LocalCsvFileInfo> LocalCsvFiles { get; } = new();

    public MainPage()
    {
        InitializeComponent();

        var host = ((App)Application.Current).Host
            ?? throw new InvalidOperationException("App host is not initialized.");

        _googleAuthService = host.Services.GetRequiredService<IGoogleAuthService>();
        _googleDriveService = host.Services.GetRequiredService<IGoogleDriveService>();
        _localCsvService = host.Services.GetRequiredService<ILocalCsvService>();
    }

    private void Researcher_Click(object sender, RoutedEventArgs e)
    {
        Frame.Navigate(typeof(ResearcherDashboard));
    }

    private async void GoogleOAuthLoginTest_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            GoogleAuthStatusText.Text = "Google OAuth login in progress...";

            var session = await _googleAuthService.LoginAsync();
            var tokenPreviewLength = Math.Min(10, session.AccessToken.Length);
            var tokenPreview = session.AccessToken[..tokenPreviewLength];

            GoogleAuthStatusText.Text =
                $"Success\nToken(first 10): {tokenPreview}\nExpires(UTC): {session.ExpiresAtUtc:O}";
        }
        catch (Exception ex)
        {
            GoogleAuthStatusText.Text = $"OAuth login failed: {ex.Message}";
        }
    }

    private async void ListLocalCsv_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            DriveUploadStatusText.Text = "Listing CSV files from LocalFolder...";

            var files = await _localCsvService.ListCsvFilesInLocalFolderAsync(maxCount: 20);

            LocalCsvFiles.Clear();
            foreach (var file in files)
            {
                LocalCsvFiles.Add(file);
            }

            DriveUploadStatusText.Text = $"Found {files.Count} CSV file(s) (showing latest 20).";
            var local = Windows.Storage.ApplicationData.Current.LocalFolder;
            DriveUploadStatusText.Text = $"LocalFolder path: {local.Path}";
        }
        catch (Exception ex)
        {
            DriveUploadStatusText.Text = $"Failed to list local CSV files: {ex.Message}";
        }
    }

    private async void UploadSelectedCsv_Click(object sender, RoutedEventArgs e)
    {
        if (LocalCsvListView.SelectedItem is not LocalCsvFileInfo selected)
        {
            DriveUploadStatusText.Text = "Select one CSV file first.";
            return;
        }

        try
        {
            DriveUploadStatusText.Text = $"Upload start: {selected.FileName}";

            var uploaded = await _googleDriveService.UploadCsvAsync(selected.FullPath);

            DriveUploadStatusText.Text =
                $"Upload success: {uploaded.Name}\nDrive file id: {uploaded.Id}";
        }
        catch (Exception ex)
        {
            DriveUploadStatusText.Text = $"Upload failed: {ex.Message}";
        }
    }

    private async void UploadAllCsv_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            DriveUploadStatusText.Text = "Upload start: all local CSV files";

            var uploadedFiles = await _googleDriveService.UploadAllCsvInLocalFolderAsync();

            var uploadedIds = string.Join(", ", uploadedFiles.Select(x => x.Id).Where(x => !string.IsNullOrWhiteSpace(x)).Take(5));

            DriveUploadStatusText.Text =
                $"Upload success: {uploadedFiles.Count} file(s) uploaded.\nSample Drive ids: {uploadedIds}";
        }
        catch (Exception ex)
        {
            DriveUploadStatusText.Text = $"Upload all failed: {ex.Message}";
        }
    }
}
