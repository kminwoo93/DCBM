namespace SmartOrganApp.Models;

public sealed record LocalCsvFileInfo(
    string FileName,
    string FullPath,
    long SizeBytes,
    DateTimeOffset ModifiedTimeUtc);
