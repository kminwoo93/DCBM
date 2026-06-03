using System;


namespace SmartOrganApp.Models;

public class DataPointModel
{
    // 그래프 그릴 때 공통으로 쓰는 값
    // → 항상 "시간 vs 전류"로 쓰고 싶다면 이렇게 두면 편해
    public double X { get; set; }   // 보통 t_ms
    public double Y { get; set; }   // 보통 I_uA

    // 원래 CSV의 각 컬럼을 개별로 보관
    public double TimeMs { get; set; }      // t_ms
    public double Adc { get; set; }         // ADC
    public double Current_uA { get; set; }  // I_uA

}
public class TimeFileItem
    {
        public double TimeHours { get; set; }
        public StorageFile File { get; set; } = default!;

        public string Display => $"{TimeHours:0.0} hr";
    }
