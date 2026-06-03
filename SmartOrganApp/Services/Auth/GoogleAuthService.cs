
using System.Text.Json;
using System.Text.Json.Serialization;
#if __ANDROID__ || __IOS__
using Microsoft.Maui.Authentication;
using Microsoft.Maui.Storage;
#endif

namespace SmartOrganApp.Services.Auth;

public sealed class GoogleAuthService(HttpClient httpClient, ILogger<GoogleAuthService> logger) : IGoogleAuthService
{
    private const string ClientId = "530051484786-gpnj41gcakfb3qh2uv67g5icpv3bgtlj.apps.googleusercontent.com";
    private const string Scope = "https://www.googleapis.com/auth/drive.file";
#if __ANDROID__
    // Android OAuth client in this project currently requires the package callback scheme.
    private const string RedirectUri = "com.minwoo.smartorgan1:/oauth2redirect";
#else
    // iOS/native redirect scheme based on reverse client-id.
    private const string RedirectUri = "com.googleusercontent.apps.530051484786-gpnj41gcakfb3qh2uv67g5icpv3bgtlj:/oauth2redirect";
#endif
    private const string AuthEndpoint = "https://accounts.google.com/o/oauth2/v2/auth";
    private const string TokenEndpoint = "https://oauth2.googleapis.com/token";

    private const string AccessTokenKey = "google_access_token";
    private const string RefreshTokenKey = "google_refresh_token";
    private const string AccessTokenExpiresAtKey = "google_access_token_expires_at_utc";

    public async Task<GoogleOAuthSession> LoginAsync(CancellationToken cancellationToken = default)
    {
#if !__ANDROID__ && !__IOS__
        throw new PlatformNotSupportedException("Google OAuth login via WebAuthenticator is only enabled on Android/iOS.");
#else
        var codeVerifier = PkceHelper.CreateCodeVerifier();
        var codeChallenge = PkceHelper.CreateCodeChallenge(codeVerifier);

        var authUrl = BuildAuthorizationUrl(codeChallenge);
        var callbackUrl = new Uri(RedirectUri);

        WebAuthenticatorResult authResult;
        try
        {
            authResult = await WebAuthenticator.Default.AuthenticateAsync(authUrl, callbackUrl);
        }
        catch (Exception ex)
        {
            throw new InvalidOperationException($"WebAuthenticator failed: {ex.Message}", ex);
        }

        if (!authResult.Properties.TryGetValue("code", out var authorizationCode) || string.IsNullOrWhiteSpace(authorizationCode))
        {
            throw new InvalidOperationException("Authorization code was not returned by Google.");
        }

        var tokenResponse = await ExchangeCodeAsync(authorizationCode, codeVerifier, cancellationToken)
            ?? throw new InvalidOperationException("Token endpoint returned an invalid response.");

        var expiresAtUtc = DateTimeOffset.UtcNow.AddSeconds(tokenResponse.ExpiresIn > 0 ? tokenResponse.ExpiresIn : 3600);

        await SaveTokenStateAsync(tokenResponse.AccessToken, expiresAtUtc, tokenResponse.RefreshToken);

        return new GoogleOAuthSession(tokenResponse.AccessToken, expiresAtUtc, tokenResponse.RefreshToken);
#endif
    }

    public async Task<string> GetAccessTokenAsync(bool forceRefresh = false, CancellationToken cancellationToken = default)
    {
#if !__ANDROID__ && !__IOS__
        throw new PlatformNotSupportedException("Google OAuth token retrieval via SecureStorage is only enabled on Android/iOS.");
#else
        if (forceRefresh)
        {
            var refreshedSession = await RefreshOrInteractiveLoginAsync(cancellationToken);
            return refreshedSession.AccessToken;
        }

        var cachedAccessToken = await SecureStorage.Default.GetAsync(AccessTokenKey);
        var cachedExpiryRaw = await SecureStorage.Default.GetAsync(AccessTokenExpiresAtKey);

        if (!string.IsNullOrWhiteSpace(cachedAccessToken)
            && DateTimeOffset.TryParse(cachedExpiryRaw, out var cachedExpiry)
            && cachedExpiry > DateTimeOffset.UtcNow.AddMinutes(1))
        {
            return cachedAccessToken;
        }

        var session = await RefreshOrInteractiveLoginAsync(cancellationToken);
        return session.AccessToken;
#endif
    }

    private async Task<GoogleOAuthSession> RefreshOrInteractiveLoginAsync(CancellationToken cancellationToken)
    {
#if !__ANDROID__ && !__IOS__
        throw new PlatformNotSupportedException("Google OAuth token retrieval via SecureStorage is only enabled on Android/iOS.");
#else
        var refreshToken = await SecureStorage.Default.GetAsync(RefreshTokenKey);

        if (!string.IsNullOrWhiteSpace(refreshToken))
        {
            var refreshResponse = await RefreshAccessTokenAsync(refreshToken, cancellationToken);

            if (refreshResponse is not null && !string.IsNullOrWhiteSpace(refreshResponse.AccessToken))
            {
                var expiresAtUtc = DateTimeOffset.UtcNow.AddSeconds(refreshResponse.ExpiresIn > 0 ? refreshResponse.ExpiresIn : 3600);
                var resolvedRefreshToken = string.IsNullOrWhiteSpace(refreshResponse.RefreshToken)
                    ? refreshToken
                    : refreshResponse.RefreshToken;

                await SaveTokenStateAsync(refreshResponse.AccessToken, expiresAtUtc, resolvedRefreshToken);
                return new GoogleOAuthSession(refreshResponse.AccessToken, expiresAtUtc, resolvedRefreshToken);
            }
        }

        return await LoginAsync(cancellationToken);
#endif
    }

    private static Uri BuildAuthorizationUrl(string codeChallenge)
    {
        var url =
            $"{AuthEndpoint}?response_type=code" +
            $"&client_id={Uri.EscapeDataString(ClientId)}" +
            $"&redirect_uri={Uri.EscapeDataString(RedirectUri)}" +
            $"&scope={Uri.EscapeDataString(Scope)}" +
            $"&access_type=offline" +
            $"&prompt=consent" +
            $"&code_challenge={Uri.EscapeDataString(codeChallenge)}" +
            $"&code_challenge_method=S256";

        return new Uri(url);
    }

    private async Task<TokenResponse?> ExchangeCodeAsync(string authorizationCode, string codeVerifier, CancellationToken cancellationToken)
    {
        using var request = new HttpRequestMessage(HttpMethod.Post, TokenEndpoint)
        {
            Content = new FormUrlEncodedContent(new Dictionary<string, string>
            {
                ["grant_type"] = "authorization_code",
                ["code"] = authorizationCode,
                ["client_id"] = ClientId,
                ["redirect_uri"] = RedirectUri,
                ["code_verifier"] = codeVerifier
            })
        };

        using var response = await httpClient.SendAsync(request, cancellationToken);
        var payload = await response.Content.ReadAsStringAsync(cancellationToken);

        if (!response.IsSuccessStatusCode)
        {
            logger.LogWarning("Google token exchange failed ({StatusCode}): {Payload}", response.StatusCode, payload);
            throw new InvalidOperationException($"Token exchange failed ({(int)response.StatusCode}): {payload}");
        }

        return JsonSerializer.Deserialize<TokenResponse>(payload);
    }

    private async Task<TokenResponse?> RefreshAccessTokenAsync(string refreshToken, CancellationToken cancellationToken)
    {
        using var request = new HttpRequestMessage(HttpMethod.Post, TokenEndpoint)
        {
            Content = new FormUrlEncodedContent(new Dictionary<string, string>
            {
                ["grant_type"] = "refresh_token",
                ["refresh_token"] = refreshToken,
                ["client_id"] = ClientId
            })
        };

        using var response = await httpClient.SendAsync(request, cancellationToken);
        var payload = await response.Content.ReadAsStringAsync(cancellationToken);

        if (!response.IsSuccessStatusCode)
        {
            logger.LogWarning("Google refresh token request failed ({StatusCode}): {Payload}", response.StatusCode, payload);
            return null;
        }

        return JsonSerializer.Deserialize<TokenResponse>(payload);
    }

    private static async Task SaveTokenStateAsync(string accessToken, DateTimeOffset expiresAtUtc, string? refreshToken)
    {
#if __ANDROID__ || __IOS__
        await SecureStorage.Default.SetAsync(AccessTokenKey, accessToken);
        await SecureStorage.Default.SetAsync(AccessTokenExpiresAtKey, expiresAtUtc.ToString("O"));

        if (!string.IsNullOrWhiteSpace(refreshToken))
        {
            await SecureStorage.Default.SetAsync(RefreshTokenKey, refreshToken);
        }
#else
        _ = accessToken;
        _ = expiresAtUtc;
        _ = refreshToken;
#endif
    }

    private sealed record TokenResponse(
        [property: JsonPropertyName("access_token")] string AccessToken,
        [property: JsonPropertyName("refresh_token")] string? RefreshToken,
        [property: JsonPropertyName("expires_in")] int ExpiresIn);
}
