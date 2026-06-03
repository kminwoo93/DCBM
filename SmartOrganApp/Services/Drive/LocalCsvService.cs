using Windows.Storage;

namespace SmartOrganApp.Services.Drive;

public sealed class LocalCsvService : ILocalCsvService
{
    public async Task<IReadOnlyList<LocalCsvFileInfo>> ListCsvFilesInLocalFolderAsync(int? maxCount = null, CancellationToken ct = default)
    {
        ct.ThrowIfCancellationRequested();

        var localFolder = ApplicationData.Current.LocalFolder;
        var files = await localFolder.GetFilesAsync();

        var csvFiles = new List<LocalCsvFileInfo>();

        foreach (var file in files)
        {
            ct.ThrowIfCancellationRequested();

            if (!file.Name.EndsWith(".csv", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var properties = await file.GetBasicPropertiesAsync();

            csvFiles.Add(new LocalCsvFileInfo(
                file.Name,
                file.Path,
                (long)properties.Size,
                properties.DateModified));
        }

        var ordered = csvFiles
            .OrderByDescending(x => x.ModifiedTimeUtc)
            .ToList();

        if (maxCount is > 0)
        {
            ordered = ordered.Take(maxCount.Value).ToList();
        }

        return ordered;
    }
}
