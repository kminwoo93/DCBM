using System.Net;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using SmartOrganApp.Services.Auth;

namespace SmartOrganApp.Services.Drive;

public sealed class GoogleDriveService(
    HttpClient httpClient,
    IGoogleAuthService googleAuthService,
    ILocalCsvService localCsvService,
    ILogger<GoogleDriveService> logger) : IGoogleDriveService
{
    private const string MultipartUploadEndpoint = "https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart&fields=id,name,mimeType,size,createdTime,webViewLink";

    public async Task<DriveFileInfo> UploadCsvAsync(string localFilePath, string? parentFolderId = null, CancellationToken ct = default)
    {
        if (string.IsNullOrWhiteSpace(localFilePath))
        {
            throw new ArgumentException("Local file path must not be empty.", nameof(localFilePath));
        }

        if (!File.Exists(localFilePath))
        {
            throw new FileNotFoundException($"CSV file not found: {localFilePath}", localFilePath);
        }

        var fileName = Path.GetFileName(localFilePath);
        var fileBytes = await File.ReadAllBytesAsync(localFilePath, ct);

        var responsePayload = await SendMultipartUploadWithRetryAsync(fileName, fileBytes, parentFolderId, ct);

        var uploadResponse = JsonSerializer.Deserialize<DriveUploadResponse>(responsePayload)
            ?? throw new InvalidOperationException("Google Drive returned an empty response.");

        return uploadResponse.ToDriveFileInfo();
    }

    public async Task<IReadOnlyList<DriveFileInfo>> UploadAllCsvInLocalFolderAsync(string? parentFolderId = null, CancellationToken ct = default)
    {
        var localCsvFiles = await localCsvService.ListCsvFilesInLocalFolderAsync(maxCount: null, ct);
        var results = new List<DriveFileInfo>(localCsvFiles.Count);

        foreach (var csvFile in localCsvFiles)
        {
            ct.ThrowIfCancellationRequested();
            var uploaded = await UploadCsvAsync(csvFile.FullPath, parentFolderId, ct);
            results.Add(uploaded);
        }

        return results;
    }

    private async Task<string> SendMultipartUploadWithRetryAsync(string fileName, byte[] fileBytes, string? parentFolderId, CancellationToken ct)
    {
        var token = await googleAuthService.GetAccessTokenAsync(forceRefresh: false, cancellationToken: ct);
        var firstAttempt = await SendMultipartUploadAsync(token, fileName, fileBytes, parentFolderId, ct);

        if (firstAttempt.StatusCode != HttpStatusCode.Unauthorized)
        {
            return await ParseSuccessOrThrowAsync(firstAttempt, ct);
        }

        logger.LogInformation("Google Drive upload got 401. Re-acquiring token and retrying once.");

        var refreshedToken = await googleAuthService.GetAccessTokenAsync(forceRefresh: true, cancellationToken: ct);
        var retryAttempt = await SendMultipartUploadAsync(refreshedToken, fileName, fileBytes, parentFolderId, ct);

        return await ParseSuccessOrThrowAsync(retryAttempt, ct);
    }

    private async Task<HttpResponseMessage> SendMultipartUploadAsync(string accessToken, string fileName, byte[] fileBytes, string? parentFolderId, CancellationToken ct)
    {
        var metadata = new Dictionary<string, object?>
        {
            ["name"] = fileName
        };

        if (!string.IsNullOrWhiteSpace(parentFolderId))
        {
            metadata["parents"] = new[] { parentFolderId };
        }

        var metadataJson = JsonSerializer.Serialize(metadata);

        using var request = new HttpRequestMessage(HttpMethod.Post, MultipartUploadEndpoint);
        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", accessToken);

        var multipartContent = new MultipartContent("related");

        var metadataContent = new StringContent(metadataJson, Encoding.UTF8, "application/json");
        multipartContent.Add(metadataContent);

        var fileContent = new ByteArrayContent(fileBytes);
        fileContent.Headers.ContentType = new MediaTypeHeaderValue("text/csv");
        multipartContent.Add(fileContent);

        request.Content = multipartContent;

        return await httpClient.SendAsync(request, ct);
    }

    private static async Task<string> ParseSuccessOrThrowAsync(HttpResponseMessage response, CancellationToken ct)
    {
        using (response)
        {
            var payload = await response.Content.ReadAsStringAsync(ct);

            if (!response.IsSuccessStatusCode)
            {
                throw new InvalidOperationException($"Google Drive upload failed ({(int)response.StatusCode}): {payload}");
            }

            return payload;
        }
    }

}
internal sealed partial record DriveUploadResponse(
[property: JsonPropertyName("id")] string? Id,
[property: JsonPropertyName("name")] string? Name,
[property: JsonPropertyName("mimeType")] string? MimeType,
[property: JsonPropertyName("size")] string? Size,
[property: JsonPropertyName("createdTime")] DateTimeOffset? CreatedTime,
[property: JsonPropertyName("webViewLink")] string? WebViewLink)
{
    public DriveFileInfo ToDriveFileInfo()
    {
        long? parsedSize = long.TryParse(Size, out var size) ? size : null;

        return new DriveFileInfo(
            Id ?? string.Empty,
            Name ?? string.Empty,
            MimeType ?? "text/csv",
            parsedSize,
            CreatedTime,
            WebViewLink);
    }
}
