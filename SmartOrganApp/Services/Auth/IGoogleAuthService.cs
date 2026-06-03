namespace SmartOrganApp.Services.Auth;

public interface IGoogleAuthService
{
    Task<GoogleOAuthSession> LoginAsync(CancellationToken cancellationToken = default);
    Task<string> GetAccessTokenAsync(bool forceRefresh = false, CancellationToken cancellationToken = default);
}

public sealed record GoogleOAuthSession(string AccessToken, DateTimeOffset ExpiresAtUtc, string? RefreshToken);
