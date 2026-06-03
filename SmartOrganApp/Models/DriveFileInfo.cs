namespace SmartOrganApp.Models;

public sealed partial record DriveFileInfo(
    string Id,
    string Name,
    string MimeType,
    long? Size,
    DateTimeOffset? CreatedTime,
    string? WebViewLink);
