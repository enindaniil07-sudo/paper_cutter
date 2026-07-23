using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using Newtonsoft.Json;

namespace GenerateTestTft;

/// <summary>Rects from <c>TEST_PROJECT/design/layout.json</c> — shared with Python background renderer.</summary>
internal sealed class TestProjectLayoutFile
{
    [JsonProperty("screen")]
    public ScreenBlock Screen { get; set; } = new ScreenBlock();

    [JsonProperty("controls")]
    public Dictionary<string, LayoutRect> Controls { get; set; } = new Dictionary<string, LayoutRect>();

    internal sealed class ScreenBlock
    {
        [JsonProperty("width")]
        public int Width { get; set; } = 800;

        [JsonProperty("height")]
        public int Height { get; set; } = 480;
    }

    internal sealed class LayoutRect
    {
        [JsonProperty("x")]
        public int X { get; set; }

        [JsonProperty("y")]
        public int Y { get; set; }

        [JsonProperty("w")]
        public int W { get; set; }

        [JsonProperty("h")]
        public int H { get; set; }

        public Rectangle ToRectangle() => new Rectangle(X, Y, W, H);
    }

    public static TestProjectLayoutFile LoadOrDefault(string projectDir)
    {
        string path = Path.Combine(projectDir, "design", "layout.json");
        if (!File.Exists(path))
        {
            Console.WriteLine("WARNING: missing design\\layout.json — using built-in default layout.");
            return DefaultEmbedded();
        }

        try
        {
            var json = File.ReadAllText(path);
            var o = JsonConvert.DeserializeObject<TestProjectLayoutFile>(json);
            if (o?.Controls == null || o.Controls.Count == 0)
                return DefaultEmbedded();
            MergeMissingControls(o, DefaultEmbedded());
            return o;
        }
        catch (System.Exception ex)
        {
            Console.WriteLine("WARNING: could not parse design\\layout.json (" + ex.Message + ") — using default.");
            return DefaultEmbedded();
        }
    }

    public Rectangle Rect(string key)
    {
        if (Controls != null && Controls.TryGetValue(key, out var r) && r != null && r.W > 0 && r.H > 0)
            return r.ToRectangle();
        if (DefaultEmbedded().Controls.TryGetValue(key, out var d))
            return d.ToRectangle();
        throw new System.InvalidOperationException("Unknown layout control key: " + key);
    }

    private static void MergeMissingControls(TestProjectLayoutFile target, TestProjectLayoutFile defaults)
    {
        foreach (var kv in defaults.Controls)
        {
            if (!target.Controls.ContainsKey(kv.Key) || target.Controls[kv.Key] == null || target.Controls[kv.Key].W <= 0)
                target.Controls[kv.Key] = kv.Value;
        }
    }

    private static TestProjectLayoutFile DefaultEmbedded()
    {
        var o = new TestProjectLayoutFile();
        o.Screen.Width = 800;
        o.Screen.Height = 480;
        o.Controls = new Dictionary<string, LayoutRect>
        {
            ["kanalA_input"] = R(56, 128, 112, 40),
            ["kanalA_display"] = R(176, 126, 252, 46),
            ["kanalB_input"] = R(56, 236, 112, 40),
            ["kanalB_display"] = R(176, 234, 228, 46),
            ["btnB_minus"] = R(412, 236, 50, 42),
            ["btnB_plus"] = R(468, 236, 50, 42),
            ["actionA"] = R(600, 126, 172, 50),
            ["actionB"] = R(600, 244, 172, 50)
        };
        return o;
    }

    private static LayoutRect R(int x, int y, int w, int h) => new LayoutRect { X = x, Y = y, W = w, H = h };
}

/// <summary>Rects for <c>TEST_PROJECT_2</c> — <c>design/layout.json</c> keys <c>counter_display</c>, <c>btn_minus</c>, <c>btn_plus</c>.</summary>
internal sealed class TestProject2LayoutFile
{
    [JsonProperty("screen")]
    public TestProjectLayoutFile.ScreenBlock Screen { get; set; } = new TestProjectLayoutFile.ScreenBlock();

    [JsonProperty("controls")]
    public Dictionary<string, TestProjectLayoutFile.LayoutRect> Controls { get; set; } =
        new Dictionary<string, TestProjectLayoutFile.LayoutRect>();

    public static TestProject2LayoutFile LoadOrDefault(string projectDir)
    {
        string path = Path.Combine(projectDir, "design", "layout.json");
        if (!File.Exists(path))
        {
            Console.WriteLine("WARNING: missing design\\layout.json — using TEST_PROJECT_2 default counter layout.");
            return DefaultEmbedded();
        }

        try
        {
            var json = File.ReadAllText(path);
            var o = JsonConvert.DeserializeObject<TestProject2LayoutFile>(json);
            if (o?.Controls == null || o.Controls.Count == 0)
                return DefaultEmbedded();
            MergeMissing(o, DefaultEmbedded());
            return o;
        }
        catch (Exception ex)
        {
            Console.WriteLine("WARNING: could not parse design\\layout.json (" + ex.Message + ") — using default counter layout.");
            return DefaultEmbedded();
        }
    }

    public Rectangle Rect(string key)
    {
        if (Controls != null && Controls.TryGetValue(key, out var r) && r != null && r.W > 0 && r.H > 0)
            return r.ToRectangle();
        if (DefaultEmbedded().Controls.TryGetValue(key, out var d))
            return d.ToRectangle();
        throw new InvalidOperationException("Unknown TEST_PROJECT_2 layout control key: " + key);
    }

    private static void MergeMissing(TestProject2LayoutFile target, TestProject2LayoutFile defaults)
    {
        foreach (var kv in defaults.Controls)
        {
            if (!target.Controls.ContainsKey(kv.Key) || target.Controls[kv.Key] == null || target.Controls[kv.Key].W <= 0)
                target.Controls[kv.Key] = kv.Value;
        }
    }

    private static TestProject2LayoutFile DefaultEmbedded()
    {
        var o = new TestProject2LayoutFile();
        o.Screen.Width = 800;
        o.Screen.Height = 480;
        o.Controls = new Dictionary<string, TestProjectLayoutFile.LayoutRect>
        {
            ["counter_display"] = R(290, 132, 220, 140),
            ["btn_minus"] = R(264, 300, 120, 72),
            ["btn_plus"] = R(416, 300, 120, 72)
        };
        return o;
    }

    private static TestProjectLayoutFile.LayoutRect R(int x, int y, int w, int h) =>
        new TestProjectLayoutFile.LayoutRect { X = x, Y = y, W = w, H = h };
}

/// <summary>Rects for <c>TEST_PROJECT_STAR_STOP</c> — <c>btn_star</c> / <c>btn_stop</c> in <c>design/layout.json</c>.</summary>
internal sealed class TestProjectStarStopLayoutFile
{
    [JsonProperty("screen")]
    public TestProjectLayoutFile.ScreenBlock Screen { get; set; } = new TestProjectLayoutFile.ScreenBlock();

    [JsonProperty("controls")]
    public Dictionary<string, TestProjectLayoutFile.LayoutRect> Controls { get; set; } =
        new Dictionary<string, TestProjectLayoutFile.LayoutRect>();

    public static TestProjectStarStopLayoutFile LoadOrDefault(string projectDir)
    {
        string path = Path.Combine(projectDir, "design", "layout.json");
        if (!File.Exists(path))
        {
            Console.WriteLine("WARNING: missing design\\layout.json — using STAR_STOP default layout.");
            return DefaultEmbedded();
        }

        try
        {
            var json = File.ReadAllText(path);
            var o = JsonConvert.DeserializeObject<TestProjectStarStopLayoutFile>(json);
            if (o?.Controls == null || o.Controls.Count == 0)
                return DefaultEmbedded();
            MergeMissing(o, DefaultEmbedded());
            return o;
        }
        catch (Exception ex)
        {
            Console.WriteLine("WARNING: could not parse design\\layout.json (" + ex.Message + ") — using STAR_STOP default.");
            return DefaultEmbedded();
        }
    }

    public Rectangle Rect(string key)
    {
        if (Controls != null && Controls.TryGetValue(key, out var r) && r != null && r.W > 0 && r.H > 0)
            return r.ToRectangle();
        if (DefaultEmbedded().Controls.TryGetValue(key, out var d))
            return d.ToRectangle();
        throw new InvalidOperationException("Unknown STAR_STOP layout control key: " + key);
    }

    private static void MergeMissing(TestProjectStarStopLayoutFile target, TestProjectStarStopLayoutFile defaults)
    {
        foreach (var kv in defaults.Controls)
        {
            if (!target.Controls.ContainsKey(kv.Key) || target.Controls[kv.Key] == null || target.Controls[kv.Key].W <= 0)
                target.Controls[kv.Key] = kv.Value;
        }
    }

    private static TestProjectStarStopLayoutFile DefaultEmbedded()
    {
        var o = new TestProjectStarStopLayoutFile();
        o.Screen.Width = 800;
        o.Screen.Height = 480;
        o.Controls = new Dictionary<string, TestProjectLayoutFile.LayoutRect>
        {
            ["btn_star"] = R(56, 168, 330, 220),
            ["btn_stop"] = R(414, 168, 330, 220)
        };
        return o;
    }

    private static TestProjectLayoutFile.LayoutRect R(int x, int y, int w, int h) =>
        new TestProjectLayoutFile.LayoutRect { X = x, Y = y, W = w, H = h };
}

/// <summary>Rects for <c>PAPER_CUTTER</c> — paper roll cutter HMI (DMG80480T050_02WTC).</summary>
internal sealed class PaperCutterLayoutFile
{
    [JsonProperty("screen")]
    public TestProjectLayoutFile.ScreenBlock Screen { get; set; } = new TestProjectLayoutFile.ScreenBlock();

    [JsonProperty("controls")]
    public Dictionary<string, TestProjectLayoutFile.LayoutRect> Controls { get; set; } =
        new Dictionary<string, TestProjectLayoutFile.LayoutRect>();

    public static PaperCutterLayoutFile LoadOrDefault(string projectDir)
    {
        string path = Path.Combine(projectDir, "design", "layout.json");
        if (!File.Exists(path))
        {
            Console.WriteLine("WARNING: missing design\\layout.json — using PAPER_CUTTER default layout.");
            return DefaultEmbedded();
        }

        try
        {
            var json = File.ReadAllText(path);
            var o = JsonConvert.DeserializeObject<PaperCutterLayoutFile>(json);
            if (o?.Controls == null || o.Controls.Count == 0)
                return DefaultEmbedded();
            MergeMissing(o, DefaultEmbedded());
            return o;
        }
        catch (Exception ex)
        {
            Console.WriteLine("WARNING: could not parse design\\layout.json (" + ex.Message + ") — using PAPER_CUTTER default.");
            return DefaultEmbedded();
        }
    }

    public Rectangle Rect(string key)
    {
        if (Controls != null && Controls.TryGetValue(key, out var r) && r != null && r.W > 0 && r.H > 0)
            return r.ToRectangle();
        if (DefaultEmbedded().Controls.TryGetValue(key, out var d))
            return d.ToRectangle();
        throw new InvalidOperationException("Unknown PAPER_CUTTER layout control key: " + key);
    }

    private static void MergeMissing(PaperCutterLayoutFile target, PaperCutterLayoutFile defaults)
    {
        foreach (var kv in defaults.Controls)
        {
            if (!target.Controls.ContainsKey(kv.Key) || target.Controls[kv.Key] == null || target.Controls[kv.Key].W <= 0)
                target.Controls[kv.Key] = kv.Value;
        }
    }

    private static PaperCutterLayoutFile DefaultEmbedded()
    {
        var o = new PaperCutterLayoutFile();
        o.Screen.Width = 800;
        o.Screen.Height = 480;
        o.Controls = new Dictionary<string, TestProjectLayoutFile.LayoutRect>
        {
            ["target_display"] = R(44, 88, 308, 88),
            ["target_touch"] = R(44, 88, 308, 88),
            ["travel_display"] = R(416, 88, 340, 88),
            ["speed_ms_display"] = R(160, 270, 168, 48),
            ["speed_rpm_display"] = R(520, 270, 168, 48),
            ["progress_bar"] = R(28, 330, 744, 44),
            ["btn_start"] = R(28, 404, 220, 56),
            ["btn_reset"] = R(290, 404, 220, 56),
            ["btn_stop"] = R(552, 404, 220, 56),
            ["btn_open"] = R(200, 176, 400, 128),
            ["target_hi"] = R(40, 52, 150, 56),
            ["target_lo"] = R(190, 52, 150, 56),
            ["kb_display"] = R(160, 120, 480, 72),
            ["kb_hi"] = R(120, 108, 200, 52),
            ["kb_lo"] = R(320, 108, 200, 52),
            ["kb_1"] = R(180, 210, 120, 52),
            ["kb_2"] = R(320, 210, 120, 52),
            ["kb_3"] = R(460, 210, 120, 52),
            ["kb_4"] = R(180, 272, 120, 52),
            ["kb_5"] = R(320, 272, 120, 52),
            ["kb_6"] = R(460, 272, 120, 52),
            ["kb_7"] = R(180, 334, 120, 52),
            ["kb_8"] = R(320, 334, 120, 52),
            ["kb_9"] = R(460, 334, 120, 52),
            ["kb_del"] = R(180, 396, 120, 52),
            ["kb_0"] = R(320, 396, 120, 52),
            ["kb_ok"] = R(460, 396, 120, 52),
            ["kb_cancel"] = R(600, 210, 120, 238)
        };
        return o;
    }

    private static TestProjectLayoutFile.LayoutRect R(int x, int y, int w, int h) =>
        new TestProjectLayoutFile.LayoutRect { X = x, Y = y, W = w, H = h };
}
