using Android;
using Android.App;
using Android.Content.PM;
using Android.OS;
using AndroidX.Core.App;
using AndroidX.Core.Content;
using System.Linq;

namespace SmartOrganApp.Droid
{
    public static class BlePermissionsHelper
    {
        const int RequestId = 1001;

        public static bool HasBlePermissions(Activity activity)
        {
            // 공통적으로 확인할 권한들
            var permsToCheck = new[]
            {
                Manifest.Permission.AccessFineLocation,
                Manifest.Permission.AccessCoarseLocation,
            }.ToList();

            if (Build.VERSION.SdkInt >= BuildVersionCodes.S)
            {
                // Android 12+ 에서만 존재하는 상수는 여기 안에서만 사용
                permsToCheck.Add(Manifest.Permission.BluetoothScan);
                permsToCheck.Add(Manifest.Permission.BluetoothConnect);
            }

            return permsToCheck.All(p =>
                ContextCompat.CheckSelfPermission(activity, p) == Permission.Granted);
        }

        public static void RequestBlePermissions(Activity activity)
        {
            var permsToRequest = new[]
            {
                Manifest.Permission.AccessFineLocation,
                Manifest.Permission.AccessCoarseLocation,
            }.ToList();

            if (Build.VERSION.SdkInt >= BuildVersionCodes.S)
            {
                permsToRequest.Add(Manifest.Permission.BluetoothScan);
                permsToRequest.Add(Manifest.Permission.BluetoothConnect);
            }

            ActivityCompat.RequestPermissions(activity, permsToRequest.ToArray(), RequestId);
        }
    }
}
