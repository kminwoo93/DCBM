using SmartOrganApp.Models;

namespace SmartOrganApp.Services.Drive;

public interface IGoogleDriveService
{
    Task<DriveFileInfo> UploadCsvAsync(string localFilePath, string? parentFolderId = null, CancellationToken ct = default);
    Task<IReadOnlyList<DriveFileInfo>> UploadAllCsvInLocalFolderAsync(string? parentFolderId = null, CancellationToken ct = default);
}
