using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Text.RegularExpressions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Navigation;
using Windows.Foundation;
using Windows.Foundation.Collections;

//// The Blank Page item template is documented at https://go.microsoft.com/fwlink/?LinkId=234238

namespace SmartOrganApp;

public sealed partial class LiverIDPage : Page
{
    public ObservableCollection<string> LiverIds { get; } = new();
    public LiverIDPage()
    {
        this.InitializeComponent();
        this.DataContext = this; // ★ 꼭 필요!!
    }
    protected override async void OnNavigatedTo(NavigationEventArgs e)
    {
        base.OnNavigatedTo(e);
        LiverIds.Clear();
        //Assume CSV files are in LocalFolder
        StorageFolder folder = ApplicationData.Current.LocalFolder;
        var files = await folder.GetFilesAsync();

        var csvFiles = files.Where(f => f.FileType.ToLower() == ".csv");

        foreach (var file in csvFiles)
        {
            string liverId = ParseLiverIdFromFileName(file.DisplayName);
            if (!string.IsNullOrEmpty(liverId) && !LiverIds.Contains(liverId))
            {
                LiverIds.Add(liverId);
            }
        }

    }
    private void BackButton_Click(object sender, RoutedEventArgs e)
    {
        if (Frame.CanGoBack)
            Frame.GoBack();
    }
    /// <summary>
    /// 파일명에서 LiverID 추출
    /// 예: LiverA_20251203_120000 이런 패턴이면 '_' 앞부분을 LiverID로 사용
    /// </summary>
    private string ParseLiverIdFromFileName(string displayName)
    {

        //예시 2(Regex 사용해서 Liver로 시작하는 부분만 뽑기)
        var match = Regex.Match(displayName, @"(Liver[0-9A-Za-z]+)$");
        if (match.Success)
        {
            return match.Value;
        }
        return null;
    }

    private void LiverIdListView_ItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is string selectedId)
        {
            ((App)Application.Current).CurrentLiverId = selectedId;
            Frame.GoBack();
        }
    }


    private void AddLiverIdButton_Click(object sender, RoutedEventArgs e)
    {
        var newId = NewLiverIdTextBox.Text?.Trim();

        if (string.IsNullOrEmpty(newId))
            return;

        // 사용자가 숫자만 입력해도 Liver01 형태로 자동 변환
        if (!newId.StartsWith("Liver", StringComparison.OrdinalIgnoreCase))
        {
            newId = "Liver" + newId;
        }

        // 리스트에 없으면 추가
        if (!LiverIds.Contains(newId))
        {
            LiverIds.Add(newId);
        }

    // 전역 Liver ID 업데이트
    ((App)Application.Current).CurrentLiverId = newId;

        Frame.GoBack();
    }
}
