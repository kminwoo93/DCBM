using System;
using System.Collections.Generic;
using System.Text;

namespace SmartOrganApp.Models;

public class CaSample
{
    /// <summary>
    /// Packet type (e.g., 1 = CA sample, 2 = status)
    /// </summary>
    public byte Type { get; set; }

    /// <summary>
    /// Measurement time (ms)
    /// </summary>
    public uint TimeMs { get; set; }

    /// <summary>
    /// ADC code value
    /// </summary>
    public ushort Adc { get; set; }

    /// <summary>
    /// Current (uA)
    /// </summary>
    public double Current_uA { get; set; }
}
