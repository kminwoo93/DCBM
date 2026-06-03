using SmartOrganApp.Models;

namespace SmartOrganApp.Services.Drive;

public interface ILocalCsvService
{
    Task<IReadOnlyList<LocalCsvFileInfo>> ListCsvFilesInLocalFolderAsync(int? maxCount = null, CancellationToken ct = default);
}
