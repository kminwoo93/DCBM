using Android.App;
using Android.Content;
using Android.Content.PM;
using Microsoft.Maui.Authentication;

namespace SmartOrganApp.Droid;

[Activity(NoHistory = true, Exported = true, LaunchMode = LaunchMode.SingleTop)]
[IntentFilter(
    [Intent.ActionView],
    Categories = [Intent.CategoryDefault, Intent.CategoryBrowsable],
    DataScheme = "com.minwoo.smartorgan1",
    DataPath = "/oauth2redirect")]
public class WebAuthenticationCallbackActivity : WebAuthenticatorCallbackActivity
{
}
