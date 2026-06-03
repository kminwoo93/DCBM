using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Threading.Tasks;
using Windows.Storage;
using SmartOrganApp.Models;

namespace SmartOrganApp.Services
{
    public static class CsvLoader
    {
        public static async Task<List<DataPointModel>> LoadCsvAsync(string relativePath)
        {
            var result = new List<DataPointModel>();

            try
            {
                // ex) relativePath = "Assets/Data/RawDataTest.csv"
                var uri = new Uri($"ms-appx:///{relativePath}");
                StorageFile file = await StorageFile.GetFileFromApplicationUriAsync(uri);

                using var stream = await file.OpenReadAsync();
                using var reader = new StreamReader(stream.AsStreamForRead());

                string? line;
                var culture = CultureInfo.InvariantCulture;

                bool headerSeen = false;
                int columnCount = 0;

                while ((line = await reader.ReadLineAsync()) != null)
                {
                    if (string.IsNullOrWhiteSpace(line))
                        continue;

                    // 메타데이터 라인("# ..." )은 무조건 스킵
                    if (line.StartsWith("#"))
                        continue;

                    var parts = line.Split(',');
                    if (parts.Length == 0)
                        continue;

                    // 첫 번째 (주석 아닌) 줄은 헤더라고 가정하고 스킵
                    if (!headerSeen)
                    {
                        headerSeen = true;
                        columnCount = parts.Length;  // 2열인지 3열인지 기록
                        continue;
                    }

                    // 실제 데이터 라인
                    if (parts.Length < columnCount)
                        continue;

                    if (columnCount == 2)
                    {
                        // 예: Time,Current
                        if (double.TryParse(parts[0], NumberStyles.Float, culture, out double t) &&
                            double.TryParse(parts[1], NumberStyles.Float, culture, out double i))
                        {
                            result.Add(new DataPointModel
                            {
                                // 그래프용
                                X = t,        // 시간
                                Y = i,        // 전류

                                // 상세 컬럼
                                TimeMs = t,
                                Current_uA = i
                            });
                        }
                    }
                    else if (columnCount >= 3)
                    {
                        // 예: t_ms,ADC,I_uA
                        if (double.TryParse(parts[0], NumberStyles.Float, culture, out double t_ms) &&
                            double.TryParse(parts[1], NumberStyles.Float, culture, out double adc) &&
                            double.TryParse(parts[2], NumberStyles.Float, culture, out double i_uA))
                        {
                            result.Add(new DataPointModel
                            {
                                // 그래프용: 항상 "시간 vs 전류" 로 통일
                                X = t_ms,
                                Y = i_uA,

                                // 상세 컬럼
                                TimeMs = t_ms,
                                Adc = adc,
                                Current_uA = i_uA
                            });
                        }
                    }
                }

                System.Diagnostics.Debug.WriteLine($"[CsvLoader] Loaded {result.Count} points from {relativePath}");
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"[CsvLoader] ERROR loading {relativePath}: {ex}");
            }

            return result;
        }

        // StorageFile에서 읽는 버전도 비슷하게 쓰고 있으면, 이 로직을 그대로 복붙해서 쓰면 됨.
        public static async Task<List<DataPointModel>> LoadFromStorageFileAsync(StorageFile file)
        {
            var result = new List<DataPointModel>();

            try
            {
                using var stream = await file.OpenReadAsync();
                using var reader = new StreamReader(stream.AsStreamForRead());

                string? line;
                var culture = CultureInfo.InvariantCulture;

                bool headerSeen = false;
                int columnCount = 0;

                while ((line = await reader.ReadLineAsync()) != null)
                {
                    if (string.IsNullOrWhiteSpace(line))
                        continue;

                    if (line.StartsWith("#"))
                        continue;

                    var parts = line.Split(',');
                    if (parts.Length == 0)
                        continue;

                    if (!headerSeen)
                    {
                        headerSeen = true;
                        columnCount = parts.Length;
                        continue;
                    }

                    if (parts.Length < columnCount)
                        continue;

                    if (columnCount == 2)
                    {
                        if (double.TryParse(parts[0], NumberStyles.Float, culture, out double t) &&
                            double.TryParse(parts[1], NumberStyles.Float, culture, out double i))
                        {
                            result.Add(new DataPointModel
                            {
                                X = t,
                                Y = i,
                                TimeMs = t,
                                Current_uA = i
                            });
                        }
                    }
                    else if (columnCount >= 3)
                    {
                        if (double.TryParse(parts[0], NumberStyles.Float, culture, out double t_ms) &&
                            double.TryParse(parts[1], NumberStyles.Float, culture, out double adc) &&
                            double.TryParse(parts[2], NumberStyles.Float, culture, out double i_uA))
                        {
                            result.Add(new DataPointModel
                            {
                                X = t_ms,
                                Y = i_uA,
                                TimeMs = t_ms,
                                Adc = adc,
                                Current_uA = i_uA
                            });
                        }
                    }
                }

                System.Diagnostics.Debug.WriteLine($"[CsvLoader] Loaded {result.Count} points from file: {file.Name}");
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"[CsvLoader] ERROR loading from StorageFile {file.Name}: {ex}");
            }

            return result;
        }
    }
}
