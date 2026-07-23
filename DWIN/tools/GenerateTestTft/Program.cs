using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.Serialization.Formatters.Binary;
using Newtonsoft.Json.Linq;
using BizDraw.ConfigInput;
using BizDraw.ConfigShow;
using BizDraw.Core;
using BizDraw.Objects;

namespace GenerateTestTft;

/// <summary>
/// Aligns with DwinTerminal <see cref="DocumentSpace.SaveConfigtft"/> / <see cref="DocumentSpace.ReadConfigtft"/>:
/// page TFT path is <c>{project}\TFT\{IMG value}.tft</c> where <c>[IMG]</c> value is the picture file name (e.g. <c>00.bmp</c> → <c>00.bmp.tft</c>), not <c>00.tft</c>.
/// Single <c>[IMG]</c> line: <c>DWprj.tft</c> is a byte copy of that page TFT so it cannot drift from what DwinTerminal paints (it loads the page from <c>TFT\</c>).
/// Run: <c>dotnet run --project tools/GenerateTestTft -- "d:\...\TEST_PROJECT"</c>
/// Optional: <c>--add-second-var-icon</c> — duplicate the first variable-icon <see cref="DrawRectangle"/> (<see cref="IconShow"/>).
/// Optional: <c>--add-data-variable-display</c> — add **数量变量显示**: <see cref="VarInput"/> (<c>f13Type=1</c>) + <see cref="DataTextShow"/> (<c>f13Type=106</c>) sharing a new VP at bottom-left if the page has no <see cref="VarInput"/> yet (extra <see cref="DataTextShow"/> alone does not block).
/// Optional: <c>--add-centered-data-variable-display</c> — add another VarInput + <see cref="DataTextShow"/> pair **centered** on the page (new VP / SP offset; skipped if <c>DataVarMid</c> already present).
/// Optional: <c>--append-corner-data-slider</c> — append corner <see cref="DataTextShow"/> (<c>VP=0000</c>, <c>100×100</c> at <c>227,87</c>) + <see cref="SliderShow"/> beside it if <c>VP=0000</c> display is missing.
/// Optional: <c>--add-data-text-show-beside-first</c> — append one more <b>display-only</b> <see cref="DataTextShow"/> (<c>f13Type=106</c>) to the right of the <b>first</b> existing <see cref="DataTextShow"/> (same size; new VP; <see cref="ApplyDgusDataTextShowLikeManualQuantityDisplay"/>).
/// Optional: <c>--verify</c> — read-back check.
/// Optional: <c>--data-text-show-size=WxH</c> — set <see cref="DrawRectangle.Rectangle"/> width/height for each <see cref="DataTextShow"/> host (position unchanged); call <see cref="DataTextShow.SetPosition"/>.
/// Optional: <c>--slider-next-to-data-text</c> — move the first <see cref="SliderShow"/> beside the first anchor display, or <b>add</b> a slider if none exists. Anchor = first <see cref="DataTextShow"/> (<c>f13≥100</c>) if any, else first other <see cref="ShowBase"/> host (e.g. <see cref="AnimateShow"/>) with <c>f13≥100</c> that is not a slider; random size; <see cref="SliderShow.SetPosition"/>.
/// Optional: <c>--add-centered-bit-button</c> — append a centered <b>位按钮</b> (DwinTerminal <c>BitButton</c>, internal — created via reflection). New VP from <see cref="NextFreeVp"/>; host <c>DrawRectangle.f13Type</c> must be <b>16</b> (verified against a hand-placed control in DwinTerminal; not 11).
/// Optional: <c>--reset-to-animation-icon</c> — remove <b>all</b> draw objects from the page TFT, then add one centered <see cref="AnimateShow"/> (<c>f13Type=101</c>, <c>cfg_ICOAnim</c> / 动画图标显示). Use alone; needs matching <c>*.icl</c> in <c>DWIN_SET</c> (default <c>Icon_lib=23</c>).
/// Optional: <c>--init-cool-dual-panel</c> — on the <b>first</b> <c>[IMG]</c> page only: two <see cref="BitButton"/>s + two <see cref="VarInput"/> / <see cref="DataTextShow"/> number lines, fixed VP/SP map, styled hosts; writes <c>VP_MEMORY_MAP.md</c>. If <c>DWIN_SET\\01.bmp</c>…<c>04.bmp</c> exist (same order as extra <c>[IMG]</c> lines), BitButtons use <c>Pic_Id</c>/<c>Pic_On</c> 1/2 and 3/4 for picture-list graphics; pack those images into e.g. <c>40.icl</c> separately for VP/icon APIs.
/// Optional: <c>--init-test-project-master</c> — <see cref="TEST_PROJECT"/>: widgets placed from <c>design\\layout.json</c> (same JSON as <c>render_test_project_from_layout.py</c> for <c>00.bmp</c>). Two channels (A/B) VarInput+DataTextShow, <b>Б_минус</b>/<b>Б_плюс</b>, two action <see cref="BitButton"/>s; writes <c>VP_MEMORY_MAP.md</c>.
/// Optional: <c>--init-test-project-2-counter</c> — <b>TEST_PROJECT_2</b>: single <see cref="DataTextShow"/> (VP <c>0x6030</c>, 0–9) + two <see cref="BitButton"/>s (− / + at <c>0x6070</c> / <c>0x6072</c>); rects from <c>design\\layout.json</c>; **released** on <c>00.bmp</c>; <c>01.bmp</c>/<c>02.bmp</c> are **full-screen** Pic_On layers (minus / plus pressed in-place, <c>Pic_Id=0</c>, <c>Pic_On=1/2</c>); writes <c>VP_MEMORY_MAP.md</c>.
/// Optional: <c>--init-test-project-3-counter</c> — <b>TEST_PROJECT_3</b>: <see cref="ArtTextShow"/> (artistic variable, VP <c>0x6030</c>, icons in <c>24.icl</c>) + two <see cref="IncManager"/> incremental-adjust touches on the same VP (0–9, no MCU). Layout keys match TEST_PROJECT_2; writes <c>VP_MEMORY_MAP.md</c>.
/// Optional: <c>--init-star-stop</c> — <b>TEST_PROJECT_STAR_STOP</b>: two large <see cref="BitButton"/>s (<b>СТАР</b> / <b>СТОП</b>, VP <c>0x6080</c> / <c>0x6082</c>); <c>01.bmp</c>/<c>02.bmp</c> full-screen Pic_On; writes <c>VP_MEMORY_MAP.md</c>.
/// Optional: <c>--init-paper-cutter</c> — <b>PAPER_CUTTER</b> (DMG80480T050_02WTC): target/travel <see cref="ArtTextShow"/> (<c>24.icl</c>), speed <see cref="ArtTextShow"/> (<c>25.icl</c>), progress <see cref="IconShow"/> (<c>26.icl</c> 0–100%), adjustment + control <see cref="BitButton"/>s; writes <c>VP_MEMORY_MAP.md</c>.
/// Optional: <c>--init-button-keypad</c> — <b>BUTTON_KEYPAD</b>: one <see cref="BitButton"/> on page 0 with <c>Pic_Next=10</c>; page <c>10.bmp</c> is the numeric keypad (reuses PAPER_CUTTER keypad widgets); writes <c>VP_MEMORY_MAP.md</c>.
/// Optional: <c>--init-meter-tablo</c> — <b>METER_TABLO</b>: left-top meters <see cref="ArtTextShow"/> (VP <c>0x6000</c>) + touch <see cref="BitButton"/> <c>Pic_Next=10</c>; keypad page same as BUTTON_KEYPAD; writes <c>VP_MEMORY_MAP.md</c>.
/// Optional: <c>--init-clean-tablo</c> — <b>CLEAN_TABLO</b>: left/right meter boards, speed m/s + rpm, RESET/STOP BitButtons; tap ЗАДАНО → keypad <c>10.bmp</c>; writes <c>VP_MEMORY_MAP.md</c>.
/// <para><b>Preview visibility:</b> DwinTerminal <c>Form_Preview</c> only paints <c>ShowBase</c> when <c>DrawRectangle.f13Type</c> ≥ 100; variable icon display uses <b>100</b> (<c>cfg_ICOvar</c>). <c>IconShow</c> with <c>f13Type</c> 0 is skipped in preview.</para>
/// </summary>
internal static class Program
{
    private static int Main(string[] args)
    {
        var flags = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var pathArgs = new List<string>();
        int? dataTextShowW = null;
        int? dataTextShowH = null;
        foreach (var a in args)
        {
            if (string.IsNullOrWhiteSpace(a))
                continue;
            var t = a.Trim();
            if (t.StartsWith("--data-text-show-size=", StringComparison.OrdinalIgnoreCase))
            {
                var rest = t.Substring("--data-text-show-size=".Length).Trim();
                var parts = rest.Split(new[] { 'x', 'X' }, 2, StringSplitOptions.None);
                if (parts.Length == 2 &&
                    int.TryParse(parts[0].Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var sw) &&
                    int.TryParse(parts[1].Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var sh) &&
                    sw > 0 && sh > 0)
                {
                    dataTextShowW = sw;
                    dataTextShowH = sh;
                }
                else
                    Console.Error.WriteLine("Ignored invalid --data-text-show-size= (expected WxH, positive integers).");

                continue;
            }

            if (t.StartsWith("--", StringComparison.Ordinal))
                flags.Add(t);
            else
                pathArgs.Add(t);
        }

        string projectDir = pathArgs.Count > 0
            ? Path.GetFullPath(pathArgs[0].Trim().Trim('"'))
            : Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "TEST_PROJECT");

        string hmiPath = Path.Combine(projectDir, "DWprj.hmi");
        if (!File.Exists(hmiPath))
        {
            Console.Error.WriteLine("Missing " + hmiPath);
            return 1;
        }

        ReadHmi(hmiPath, out int width, out int height, out ushort spAddress, out List<string> pictureNames);
        if (pictureNames.Count == 0)
            pictureNames.Add("00.bmp");

        string imageDir = Path.Combine(projectDir, "image");
        Directory.CreateDirectory(imageDir);
        foreach (string pic in pictureNames)
        {
            string dwinSrc = Path.Combine(projectDir, "DWIN_SET", pic);
            string imgSrc = Path.Combine(imageDir, pic);
            string dst = Path.Combine(imageDir, pic);
            string src;
            if (File.Exists(dwinSrc) && File.Exists(imgSrc))
                src = File.GetLastWriteTimeUtc(dwinSrc) >= File.GetLastWriteTimeUtc(imgSrc) ? dwinSrc : imgSrc;
            else if (File.Exists(dwinSrc))
                src = dwinSrc;
            else if (File.Exists(imgSrc))
                src = imgSrc;
            else
            {
                Console.Error.WriteLine("Missing picture (need DWIN_SET\\" + pic + " or image\\" + pic + ")");
                return 1;
            }

            if (!string.Equals(Path.GetFullPath(src), Path.GetFullPath(dst), StringComparison.OrdinalIgnoreCase))
                File.Copy(src, dst, overwrite: true);
        }

        string tftDir = Path.Combine(projectDir, "TFT");
        Directory.CreateDirectory(tftDir);

        // Canvas uses TFT\{IMG}.tft (grid Cell[2]); DWprj.tft is loaded into currentDocument — sizes often drift if DwinTerminal saved one path only.
        // Do not overwrite DWprj here (page TFT may be stale vs DWprj); each successful run still copies page TFT → DWprj.tft at the end for single [IMG].
        if (pictureNames.Count == 1)
        {
            string solePageTftEarly = Path.Combine(tftDir, pictureNames[0] + ".tft");
            string prjTftEarly = Path.Combine(projectDir, "DWprj.tft");
            if (File.Exists(solePageTftEarly) && File.Exists(prjTftEarly))
            {
                long lenPage = new FileInfo(solePageTftEarly).Length;
                long lenPrj = new FileInfo(prjTftEarly).Length;
                if (lenPage != lenPrj)
                    Console.WriteLine("WARNING: page TFT vs DWprj.tft size differs (" + lenPage + " vs " + lenPrj + " bytes). Editor canvas loads TFT\\" + pictureNames[0] + ".tft — if counts mismatch DwinTerminal, that file is source of truth.");
            }
        }

        var bf = new BinaryFormatter();
        Document firstPageDoc = null;

        string primaryPic = pictureNames[0];
        foreach (string pic in pictureNames)
        {
            bool isPrimaryPicture = string.Equals(pic, primaryPic, StringComparison.OrdinalIgnoreCase);
            string tftPath = Path.Combine(tftDir, pic + ".tft");
            bool tftExisted = File.Exists(tftPath);
            long tftPrevLen = tftExisted ? new FileInfo(tftPath).Length : 0L;
            Document page = LoadOrCreatePageDocument(bf, tftPath, projectDir, width, height, pic);
            if (page == null)
            {
                Console.Error.WriteLine("Abort: failed to load existing TFT (see warning above): " + tftPath);
                return 1;
            }

            if (tftExisted && tftPrevLen > 2500 && page.Items.Count == 0 && !HasMutationBeyondVerify(flags))
            {
                Console.Error.WriteLine(
                    "Abort: TFT deserialized to 0 controls but file is " + tftPrevLen +
                    " bytes (formatter/version mismatch or corrupt). Will not overwrite. Re-open/resave in DwinTerminal, or pass a layout flag (e.g. --reset-to-animation-icon). " +
                    tftPath);
                return 1;
            }

            if (isPrimaryPicture &&
                flags.Contains("--reset-to-animation-icon") &&
                TryResetToCenteredAnimationIcon(page))
                Console.WriteLine("Reset page: cleared controls + centered Animation icon (AnimateShow) on " + pic);

            if (isPrimaryPicture &&
                flags.Contains("--init-cool-dual-panel") &&
                TryInitCoolDualPanel(page, width, height, spAddress, projectDir))
                Console.WriteLine("Initialized cool dual-panel layout (2 BitButtons + 2 number lines) on " + pic);

            if (isPrimaryPicture &&
                flags.Contains("--init-test-project-master") &&
                TryInitTestProjectMasterLayout(page, width, height, spAddress, projectDir))
                Console.WriteLine("Initialized TEST_PROJECT master layout (channels A/B, B +/-, actions) on " + pic);

            if (isPrimaryPicture &&
                flags.Contains("--init-test-project-2-counter") &&
                TryInitTestProject2Counter(page, width, height, spAddress, projectDir))
                Console.WriteLine("Initialized TEST_PROJECT_2 counter (0-9, +/-) on " + pic);

            if (isPrimaryPicture &&
                flags.Contains("--init-test-project-3-counter") &&
                TryInitTestProject3CounterIncAdjust(page, width, height, spAddress, projectDir))
                Console.WriteLine("Initialized TEST_PROJECT_3 (ArtTextShow + IncManager on VP 6030) on " + pic);

            if (isPrimaryPicture &&
                flags.Contains("--init-star-stop") &&
                TryInitTestProjectStarStop(page, width, height, spAddress, projectDir))
                Console.WriteLine("Initialized STAR/STOP (Cyrillic) BitButtons on " + pic);

            if (isPrimaryPicture &&
                flags.Contains("--init-paper-cutter") &&
                TryInitPaperCutter(page, width, height, spAddress, projectDir))
                Console.WriteLine("Initialized PAPER_CUTTER layout on " + pic);

            if (isPrimaryPicture &&
                flags.Contains("--init-button-keypad") &&
                TryInitButtonKeypad(page, width, height, projectDir))
                Console.WriteLine("Initialized BUTTON_KEYPAD main button on " + pic);

            if (isPrimaryPicture &&
                flags.Contains("--init-meter-tablo") &&
                TryInitMeterTablo(page, width, height, spAddress, projectDir))
                Console.WriteLine("Initialized METER_TABLO meters board on " + pic);

            if (isPrimaryPicture &&
                flags.Contains("--init-clean-tablo") &&
                TryInitCleanTablo(page, width, height, spAddress, projectDir))
                Console.WriteLine("Initialized CLEAN_TABLO layout on " + pic);

            if (flags.Contains("--init-meter-tablo") &&
                string.Equals(pic, "10.bmp", StringComparison.OrdinalIgnoreCase) &&
                TryInitMeterTabloKeypad(page, width, height, spAddress, projectDir))
                Console.WriteLine("Initialized METER_TABLO keypad page on " + pic);

            if (flags.Contains("--init-clean-tablo") &&
                string.Equals(pic, "10.bmp", StringComparison.OrdinalIgnoreCase) &&
                TryInitCleanTabloKeypad(page, width, height, spAddress, projectDir))
                Console.WriteLine("Initialized CLEAN_TABLO keypad page on " + pic);

            if (flags.Contains("--init-clean-tablo") &&
                string.Equals(pic, "16.bmp", StringComparison.OrdinalIgnoreCase) &&
                TryInitCleanTabloSettings(page, width, height, spAddress, projectDir))
                Console.WriteLine("Initialized CLEAN_TABLO settings page on " + pic);

            if ((flags.Contains("--init-paper-cutter") || flags.Contains("--init-button-keypad")) &&
                string.Equals(pic, "10.bmp", StringComparison.OrdinalIgnoreCase) &&
                TryInitPaperCutterKeypad(page, width, height, spAddress, projectDir))
                Console.WriteLine("Initialized keypad page on " + pic);

            if (flags.Contains("--add-second-var-icon") &&
                TryAddSecondVariableIconDisplay(page, spAddress))
                Console.WriteLine("Added second variable icon display on " + pic);

            if (flags.Contains("--add-data-variable-display") &&
                TryAddDataVariableDisplay(page, spAddress))
                Console.WriteLine("Added data variable display (VarInput + DataTextShow) on " + pic);

            if (flags.Contains("--add-centered-data-variable-display") &&
                TryAddCenteredDataVariableDisplay(page, spAddress))
                Console.WriteLine("Added centered data variable display (VarInput + DataTextShow) on " + pic);

            if (flags.Contains("--append-corner-data-slider") &&
                TryAppendCornerDataTextAndSlider(page, width, height, spAddress))
                Console.WriteLine("Appended corner data text + slider on " + pic);

            if (flags.Contains("--add-data-text-show-beside-first") &&
                TryAddDataTextShowBesideFirst(page, width, height))
                Console.WriteLine("Added DataTextShow beside first DataTextShow on " + pic);

            ResyncShowAndInputPositions(page);
            if (DedupeDataTextShowVPs(page))
                Console.WriteLine("Deduped duplicate DataTextShow VP on " + pic);
            ResyncShowAndInputPositions(page);
            NormalizeDrawRectanglesForEditor(page);

            if (isPrimaryPicture && flags.Contains("--init-cool-dual-panel"))
                ApplyCoolDualPanelChrome(page);

            if (isPrimaryPicture && flags.Contains("--init-test-project-master"))
                ApplyTestProjectMasterChrome(page);

            if (isPrimaryPicture && flags.Contains("--init-test-project-2-counter"))
                ApplyTestProject2Chrome(page);

            if (isPrimaryPicture && flags.Contains("--init-test-project-3-counter"))
                ApplyTestProject3Chrome(page);

            if (isPrimaryPicture && flags.Contains("--init-star-stop"))
                ApplyStarStopChrome(page);

            if (dataTextShowW.HasValue && dataTextShowH.HasValue &&
                ApplyDataTextShowRectangleSize(page, dataTextShowW.Value, dataTextShowH.Value))
                Console.WriteLine("Set DataTextShow rectangle size to " + dataTextShowW + "x" + dataTextShowH + " on " + pic);

            if (flags.Contains("--slider-next-to-data-text"))
            {
                if (TryPlaceSliderBesideDataTextRandom(page, width, height))
                    Console.WriteLine("Moved slider beside display anchor on " + pic);
                else if (TryAddSliderBesideDataTextRandom(page, width, height, spAddress))
                    Console.WriteLine("Added slider beside display anchor on " + pic);
            }

            if (flags.Contains("--add-centered-bit-button") &&
                TryAddCenteredBitButton(page, width, height))
                Console.WriteLine("Added centered Bit button (BitButton) on " + pic);

            if (tftExisted && tftPrevLen > 6000 && page.Items.Count == 0)
            {
                Console.Error.WriteLine("Abort: refusing to write empty Document over large TFT (" + tftPrevLen + " bytes): " + tftPath);
                return 1;
            }

            using (var fs = new FileStream(tftPath, FileMode.Create, FileAccess.Write))
                bf.Serialize(fs, page);
            page.SetDirtyFlag(false);
            Console.WriteLine("Wrote " + tftPath + " (" + new FileInfo(tftPath).Length + " bytes)");

            firstPageDoc ??= page;
        }

        // Early generator used TFT\00.tft; DwinTerminal always uses TFT\{IMG value}.tft (e.g. 00.bmp.tft).
        string legacyTft = Path.Combine(tftDir, "00.tft");
        if (File.Exists(legacyTft))
        {
            try
            {
                File.Delete(legacyTft);
                Console.WriteLine("Removed legacy " + legacyTft);
            }
            catch
            {
                /* ignore */
            }
        }

        string prjTft = Path.Combine(projectDir, "DWprj.tft");
        // DwinTerminal loads per-page design from TFT\{IMG}.tft (see ReadConfigtft). If DWprj.tft and that file
        // diverge (e.g. tool rewrote one while DwinTerminal had the other open), the canvas shows the TFT file.
        // Single-page projects: copy bytes so DWprj.tft always matches the page TFT exactly.
        if (pictureNames.Count == 1)
        {
            string solePageTft = Path.Combine(tftDir, pictureNames[0] + ".tft");
            File.Copy(solePageTft, prjTft, overwrite: true);
            if (firstPageDoc != null)
                firstPageDoc.SetDirtyFlag(false);
            Console.WriteLine("Wrote " + prjTft + " (synced from " + solePageTft + ", " + new FileInfo(prjTft).Length + " bytes)");
        }
        else
        {
            var prjDoc = firstPageDoc ?? new Document("page00")
            {
                Width = width,
                Height = height,
                Picpix = 1,
                FilePath = projectDir,
                filename = ""
            };
            using (var fs = new FileStream(prjTft, FileMode.Create, FileAccess.Write))
                bf.Serialize(fs, prjDoc);
            prjDoc.SetDirtyFlag(false);
            Console.WriteLine("Wrote " + prjTft + " (" + new FileInfo(prjTft).Length + " bytes)");
        }

        if (flags.Contains("--verify"))
        {
            Console.WriteLine("--- verify (read-back) ---");
            VerifyDwinSetPreviewAssets(Path.Combine(projectDir, "DWIN_SET"));
            foreach (string pic in pictureNames)
                VerifyPageTftReadback(Path.Combine(tftDir, pic + ".tft"), bf);
            Verify14ShowFileHeader(Path.Combine(projectDir, "DWIN_SET", "14ShowFile.bin"));
        }

        if (flags.Contains("--init-cool-dual-panel"))
            WriteVpMemoryMap(projectDir, spAddress, width, height);

        if (flags.Contains("--init-test-project-master"))
            WriteTestProjectMasterVpMap(projectDir, spAddress, width, height);

        if (flags.Contains("--init-test-project-2-counter"))
            WriteTestProject2VpMap(projectDir, spAddress, width, height);

        if (flags.Contains("--init-test-project-3-counter"))
            WriteTestProject3VpMap(projectDir, spAddress, width, height);

        if (flags.Contains("--init-star-stop"))
            WriteStarStopVpMap(projectDir, spAddress, width, height);

        if (flags.Contains("--init-paper-cutter"))
            WritePaperCutterVpMap(projectDir, spAddress, width, height);

        if (flags.Contains("--init-button-keypad"))
            WriteButtonKeypadVpMap(projectDir, spAddress, width, height);

        if (flags.Contains("--init-meter-tablo"))
            WriteMeterTabloVpMap(projectDir, spAddress, width, height);

        if (flags.Contains("--init-clean-tablo"))
            WriteCleanTabloVpMap(projectDir, spAddress, width, height);

        return 0;
    }

    /// <summary>True when args include any flag besides <c>--verify</c> (layout/tool flags that can repopulate a broken TFT).</summary>
    private static bool HasMutationBeyondVerify(HashSet<string> flags)
    {
        foreach (var x in flags)
        {
            if (!x.StartsWith("--", StringComparison.Ordinal))
                continue;
            if (string.Equals(x, "--verify", StringComparison.OrdinalIgnoreCase))
                continue;
            return true;
        }

        return false;
    }

    /// <summary><see cref="BizDraw.Objects.GraphicsList.Add"/> inserts at index 0; generator code must append so existing objects are preserved.</summary>
    private static void AppendDrawObject(Document doc, DrawObject item) => doc.Items.Insert(doc.Items.Count, item);

    /// <summary>
    /// DwinTerminal <see cref="BizDraw.Controls.Form_Preview"/> calls <see cref="ShowBase.SetPosition"/> before <see cref="ShowBase.GetData"/>; if <see cref="DataTextShow.Var_Position"/>
    /// is out of sync with <see cref="DrawRectangle.Rectangle"/>, preview draws glyphs at wrong coords (often looks like “nothing”).
    /// </summary>
    private static void ResyncShowAndInputPositions(Document doc)
    {
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r)
                continue;
            var rect = r.Rectangle;
            if (rect.Width <= 0 || rect.Height <= 0)
                continue;
            switch (r.ConfigObject)
            {
                case ShowBase sb:
                    sb.SetPosition(rect);
                    // ArtTextShow.SetPosition updates Var_Position but leaves DisPositon at ctor "0,0"
                    // — DGUS then paints sample digits at the screen origin (outside the display box).
                    if (sb is ArtTextShow art)
                    {
                        ApplyArtTextInset(art, rect);
                        SyncArtTextDisPositon(art);
                    }
                    break;
                case InputBase ib:
                    ib.SetPosition(rect);
                    break;
            }
        }
    }

    private static void SyncArtTextDisPositon(ArtTextShow art)
    {
        art.DisPositon = art.Var_Position.X.ToString(CultureInfo.InvariantCulture) + "," +
                         art.Var_Position.Y.ToString(CultureInfo.InvariantCulture);
    }

    /// <summary>
    /// Keep artistic digits inside the painted board: pad left-aligned starts, nudge Y for icon height.
    /// </summary>
    private static void ApplyArtTextInset(ArtTextShow art, Rectangle rect, int iconHeightPx = 80)
    {
        int iconH = Math.Max(16, iconHeightPx);
        int y = rect.Top + Math.Max(0, (rect.Height - iconH) / 2);
        if (art.TxtAlign == PcArtAlignRight)
        {
            // Right edge inside the board (DWIN X = right edge of glyph string)
            art.Var_Position = new Cpoint
            {
                X = (ushort)Math.Max(0, rect.Left + rect.Width - 8),
                Y = (ushort)y
            };
        }
        else if (art.TxtAlign == PcArtAlignLeft && rect.Width > 40)
        {
            art.Var_Position = new Cpoint
            {
                X = (ushort)(rect.Left + 16),
                Y = (ushort)y
            };
        }
        else if (rect.Height > iconH)
        {
            art.Var_Position = new Cpoint
            {
                X = art.Var_Position.X,
                Y = (ushort)y
            };
        }
    }

    /// <summary>Visible outline on the design surface (<see cref="DrawArea"/> only draws rectangles, not glyph fill).</summary>
    private static void NormalizeDrawRectanglesForEditor(Document doc)
    {
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r)
                continue;
            r.DisMode = DrawRectangle.TDismode.ForeColor;
            if (r.PenWidth < 1)
                r.PenWidth = 1;
        }
    }

    /// <summary><c>Form_Preview.GetAscBitmap</c> requires a <c>*.HZK</c> in <c>DWIN_SET</c> (see <see cref="BizDraw.Globel.ProjectPathName"/>).</summary>
    private static void VerifyDwinSetPreviewAssets(string dwinSetDir)
    {
        Console.WriteLine("DWIN_SET: " + dwinSetDir);
        if (!Directory.Exists(dwinSetDir))
        {
            Console.WriteLine("  WARNING: DWIN_SET folder missing — preview cannot load fonts/icons.");
            return;
        }

        var hasHzk = false;
        try
        {
            foreach (var f in Directory.EnumerateFiles(dwinSetDir, "*.HZK", SearchOption.TopDirectoryOnly))
            {
                hasHzk = true;
                break;
            }

            if (!hasHzk)
            {
                foreach (var _ in Directory.EnumerateFiles(dwinSetDir, "*.hzk", SearchOption.TopDirectoryOnly))
                {
                    hasHzk = true;
                    break;
                }
            }
        }
        catch
        {
            /* ignore */
        }

        if (!hasHzk)
            Console.WriteLine("  WARNING: no *.HZK in DWIN_SET — Form_Preview will not paint DataTextShow text (empty/transparent). Copy an ASCII font (e.g. 0_DWIN_ASC.HZK, ≥3MB) from DGUS into DWIN_SET.");

        var hasIcl = false;
        try
        {
            foreach (var _ in Directory.EnumerateFiles(dwinSetDir, "*.icl", SearchOption.TopDirectoryOnly))
            {
                hasIcl = true;
                break;
            }
        }
        catch
        {
            /* ignore */
        }

        if (!hasIcl)
            Console.WriteLine("  NOTE: no *.icl in DWIN_SET — SliderShow / icon preview may lack bitmap source.");
    }

    private static void VerifyPageTftReadback(string tftPath, BinaryFormatter bf)
    {
        Console.WriteLine("TFT: " + tftPath);
        if (!File.Exists(tftPath))
        {
            Console.WriteLine("  (missing)");
            return;
        }

        Document doc;
        try
        {
            using var fs = new FileStream(tftPath, FileMode.Open, FileAccess.Read);
            doc = (Document)bf.Deserialize(fs);
        }
        catch (Exception ex)
        {
            Console.WriteLine("  deserialize FAILED: " + ex.Message);
            return;
        }

        Console.WriteLine("  Document: Name=" + doc.Name + " Size=" + doc.Width + "x" + doc.Height + " Items=" + doc.Items.Count);
        var idx = 0;
        foreach (DrawObject o in doc.Items)
        {
            if (o is DrawRectangle r)
            {
                var co = r.ConfigObject;
                var coName = co == null ? "<null>" : co.GetType().FullName;
                Console.WriteLine("  [" + idx + "] DrawRectangle f13Type=" + r.f13Type + " Rect=" + r.Rectangle + " ConfigObject=" + coName);
                if (co is IconShow ic)
                {
                    Console.WriteLine("       IconShow VarStrPoint=" + ic.VarStrPoint + " Var_Point=0x" + ic.Var_Point.ToString("X4") + " Desc=" + ic.VarDescAddress + " Pic_Id=" + ic.Pic_Id);
                    if (r.f13Type < 100)
                        Console.WriteLine("       WARNING: f13Type<100 — DwinTerminal preview will not draw this IconShow.");
                    if (r.f13Type != F13VariableIconDisplay && r.f13Type >= 100)
                        Console.WriteLine("       NOTE: f13Type is " + r.f13Type + " (variable icon editor expects " + F13VariableIconDisplay + ").");
                }
                else if (co is DataTextShow dt)
                {
                    Console.WriteLine("       DataTextShow VP=" + dt.VarStrPoint + " VarType=" + dt.VarType + " N_Int=" + dt.N_Int + " N_Dot=" + dt.N_Dot + " Lib_Id=" + dt.Lib_Id + " Pic_Id=" + dt.Pic_Id);
                    if (r.f13Type != F13DataVariableDisplay)
                        Console.WriteLine("       NOTE: f13Type=" + r.f13Type + " (data variable display expects " + F13DataVariableDisplay + ").");
                }
                else if (co is ArtTextShow at)
                {
                    Console.WriteLine(
                        "       ArtTextShow VP=" + at.VarStrPoint + " ICOFileName=" + (at.ICOFileName ?? "") + " Icon_lib=" +
                        at.Icon_lib + " Icon0=" + at.Icon0 + " N_Int=" + at.N_Int + " N_Dot=" + at.N_Dot + " VarType=" + at.VarType + " TxtAlign=" +
                        at.TxtAlign + " DisPositon=" + (at.DisPositon ?? "") + " f13Type=" + r.f13Type);
                    if (r.f13Type != F13ArtisticVariableDisplay)
                        Console.WriteLine(
                            "       NOTE: f13Type=" + r.f13Type + " (artistic variable expects " + F13ArtisticVariableDisplay + ").");
                }
                else if (co is VarInput vi)
                    Console.WriteLine("       VarInput VP=" + vi.VarStrPoint + " Var_Type=" + vi.Var_Type + " f13Type=" + r.f13Type);
                else if (co is SliderShow sl)
                {
                    Console.WriteLine("       SliderShow VP=" + sl.VarStrPoint + " Mode=" + sl.Mode + " V=" + sl.V_Begin + ".." + sl.V_End + " Icon_lib=" + sl.Icon_lib);
                    if (r.f13Type != F13SliderDisplay && r.f13Type >= 100)
                        Console.WriteLine("       NOTE: f13Type=" + r.f13Type + " (slider display expects " + F13SliderDisplay + ").");
                }
                else if (co is AnimateShow an)
                {
                    Console.WriteLine("       AnimateShow VP=" + an.VarStrPoint + " Icon_lib=" + an.Icon_lib + " Icon_Start=" + an.Icon_Start + " Icon_End=" + an.Icon_End + " V=" + an.V_Start + ".." + an.V_Stop);
                    if (r.f13Type != F13AnimationIconDisplay && r.f13Type >= 100)
                        Console.WriteLine("       NOTE: f13Type=" + r.f13Type + " (animation icon expects " + F13AnimationIconDisplay + ").");
                }
                else if (co is IncManager inc)
                {
                    Console.WriteLine(
                        "       IncManager VP(target)=" + inc.VarStrPoint + " VP_Mode=" + inc.VP_Mode + " Adj_Mode=" +
                        inc.Adj_Mode + " Step=" + inc.Adj_Step + " Min=" + inc.V_Min + " Max=" + inc.V_Max + " Key_Mode=" +
                        inc.Key_Mode + " Pic_Id=" + inc.Pic_Id + " Pic_On=" + inc.Pic_On + " f13Type=" + r.f13Type);
                    if (r.f13Type != F13IncrementalAdjustment)
                        Console.WriteLine(
                            "       NOTE: f13Type=" + r.f13Type + " (incremental adjustment expects " +
                            F13IncrementalAdjustment + ").");
                }
                else if (IsBitButtonConfig(co) && co is InputBase bbi)
                {
                    Console.WriteLine(
                        "       BitButton VP=" + bbi.VarStrPoint + " Pic_Id=" + bbi.Pic_Id + " Pic_On=" + bbi.Pic_On +
                        " f13Type=" + r.f13Type);
                    if (r.f13Type != F13BitButton && r.f13Type < 100)
                        Console.WriteLine("       NOTE: f13Type=" + r.f13Type + " (tool uses " + F13BitButton + " for Bit button).");
                }
            }
            else
                Console.WriteLine("  [" + idx + "] " + o.GetType().FullName);
            idx++;
        }

        if (doc.Items.Count == 0)
            Console.WriteLine("  (no draw objects — empty design surface)");
        else
            VerifyPaperCutterWidgetTypes(doc);
    }

    /// <summary>PAPER_CUTTER/CLEAN_TABLO: digit boards UINT16 or LONG32 (MCU U32 meters).</summary>
    private static void VerifyPaperCutterWidgetTypes(Document doc)
    {
        // PAPER_CUTTER has Target_m + Travel_m; METER_TABLO uses Meter_m — skip that project.
        bool paperCutter = false;
        bool hasTravel = false;
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r || r.ConfigObject is not ArtTextShow at)
                continue;
            if (at.Var_Name == "Target_m")
                paperCutter = true;
            if (at.Var_Name == "Travel_m")
                hasTravel = true;
        }

        if (!paperCutter || !hasTravel)
            return;

        int errors = 0;
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r)
                continue;
            if (r.ConfigObject is ArtTextShow art)
            {
                // CLEAN_TABLO meters are 32-bit (до 99999); speed stays UINT16.
                bool okType = art.Var_Name is "Target_m" or "Travel_m"
                    ? (art.VarType == PcArtVarTypeLong32 || art.VarType == PcArtVarTypeUInt16)
                    : art.VarType == PcArtVarTypeUInt16;
                if (!okType)
                {
                    Console.WriteLine(
                        "  ERROR: ArtTextShow " + art.Var_Name + " VarType=" + art.VarType);
                    errors++;
                }
            }
        }

        if (errors == 0)
            Console.WriteLine("  PAPER_CUTTER types OK (ArtTextShow UINT16/LONG32).");
        else
            Console.WriteLine("  PAPER_CUTTER type check: " + errors + " error(s).");
    }

    private static void Verify14ShowFileHeader(string path14)
    {
        Console.WriteLine("14: " + path14);
        if (!File.Exists(path14))
        {
            Console.WriteLine("  (missing — run DGUS Generate in DwinTerminal)");
            return;
        }

        var buf = new byte[16];
        using (var fs = new FileStream(path14, FileMode.Open, FileAccess.Read))
        {
            if (fs.Read(buf, 0, 16) != 16)
            {
                Console.WriteLine("  read header FAILED");
                return;
            }
        }

        if (buf[0] != 0x14)
            Console.WriteLine("  WARNING: byte[0] expected 0x14, got 0x" + buf[0].ToString("X2"));
        var tag = System.Text.Encoding.ASCII.GetString(buf, 1, 6);
        if (tag != "DGUS_2")
            Console.WriteLine("  WARNING: bytes[1..6] expected DGUS_2, got \"" + tag + "\"");
        Console.WriteLine("  OK: header 0x14 + \"" + tag + "\" sub=" + buf[7].ToString("X2") + " meta bytes[8-9]=0x" + buf[8].ToString("X2") + buf[9].ToString("X2"));
    }

    private static Document LoadOrCreatePageDocument(
        BinaryFormatter bf,
        string tftPath,
        string projectDir,
        int width,
        int height,
        string picFileName)
    {
        if (File.Exists(tftPath))
        {
            try
            {
                using var fs = new FileStream(tftPath, FileMode.Open, FileAccess.Read);
                var loaded = (Document)bf.Deserialize(fs);
                loaded.FilePath = projectDir;
                loaded.Width = width;
                loaded.Height = height;
                loaded.Picpix = 1;
                if (string.IsNullOrEmpty(loaded.filename))
                    loaded.filename = "";
                EnsureVariableIconDisplayF13(loaded);
                EnsureDataVariableDisplayF13(loaded);
                EnsureAnimationIconF13(loaded);
                EnsureBitButtonF13(loaded);
                EnsureArtTextShowF13(loaded);
                EnsureIncManagerF13(loaded);
                ResyncShowAndInputPositions(loaded);
                NormalizeDrawRectanglesForEditor(loaded);
                DedupeDataTextShowVPs(loaded);
                ResyncShowAndInputPositions(loaded);
                return loaded;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("ERROR: could not deserialize " + tftPath + ": " + ex.Message);
                if (ex.InnerException != null)
                    Console.Error.WriteLine("       Inner: " + ex.InnerException.Message);
                return null;
            }
        }

        return new Document("page00")
        {
            Width = width,
            Height = height,
            Picpix = 1,
            FilePath = projectDir,
            filename = ""
        };
    }

    /// <summary>Duplicate first variable-icon rectangle (IconShow) if exactly one exists.</summary>
    private static bool TryAddSecondVariableIconDisplay(Document doc, ushort spBaseHint)
    {
        DrawRectangle templateRect = null;
        IconShow templateIcon = null;
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r)
                continue;
            if (r.ConfigObject is not IconShow ic)
                continue;
            templateRect = r;
            templateIcon = ic;
            break;
        }

        if (templateRect == null || templateIcon == null)
            return false;

        var iconCount = 0;
        foreach (DrawObject o in doc.Items)
        {
            if (o is DrawRectangle r && r.ConfigObject is IconShow)
                iconCount++;
        }

        if (iconCount >= 2)
            return false;

        var newIcon = (IconShow)templateIcon.Clone();
        ushort vp = templateIcon.Var_Point;
        newIcon.VarStrPoint = (vp + 1).ToString("X4", CultureInfo.InvariantCulture);

        ushort desc = templateIcon.Desc_Point;
        if (desc != ushort.MaxValue && desc != 0)
            newIcon.VarDescAddress = (desc + 16).ToString("X4", CultureInfo.InvariantCulture);
        else
            newIcon.VarDescAddress = spBaseHint.ToString("X4", CultureInfo.InvariantCulture);

        var rect = templateRect.Rectangle;
        int nx = rect.X + 120;
        if (nx + rect.Width > doc.Width)
            nx = Math.Max(0, rect.X - 120);
        if (nx < 0)
            nx = 20;
        int ny = rect.Y + 80;
        if (ny + rect.Height > doc.Height)
            ny = Math.Max(0, rect.Y - 80);
        if (ny < 0)
            ny = 20;

        var nr = new DrawRectangle(nx, ny, rect.Width, rect.Height, templateRect.Drawbtn)
        {
            Rectangle = new System.Drawing.Rectangle(nx, ny, rect.Width, rect.Height),
            ScreenSize = templateRect.ScreenSize,
            DisMode = templateRect.DisMode,
            Color = templateRect.Color,
            BColor = templateRect.BColor,
            PenWidth = templateRect.PenWidth,
            f13Type = templateRect.f13Type >= 100 ? templateRect.f13Type : F13VariableIconDisplay
        };
        nr.ConfigObject = newIcon;
        newIcon.Pic_Id = templateIcon.Pic_Id;
        newIcon.SetPosition(nr.Rectangle);

        AppendDrawObject(doc, nr);
        EnsureVariableIconDisplayF13(doc);
        doc.SetDirtyFlag(true);
        return true;
    }

    /// <summary>
    /// DwinTerminal <c>Form_Preview</c> only draws <c>ShowBase</c> when <c>f13Type</c> ≥ 100; property editor maps variable icon to <b>100</b> (<c>cfg_ICOvar</c> / <see cref="IconShow"/>).
    /// </summary>
    private const int F13VariableIconDisplay = 100;

    private static void EnsureVariableIconDisplayF13(Document doc)
    {
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r || r.ConfigObject is not IconShow)
                continue;
            if (r.f13Type < 100)
                r.f13Type = F13VariableIconDisplay;
        }
    }

    /// <summary><c>Btn_Property</c> paired slider <see cref="SliderShow"/> host uses <b>102</b> (<c>cfg_ICOSlider</c>).</summary>
    private const int F13SliderDisplay = 102;

    /// <summary><c>Btn_Property.button5_Click</c> case 106 — <c>cfg_DisData</c> / 数量变量显示.</summary>
    private const int F13DataVariableDisplay = 106;

    /// <summary><c>Btn_Property.button3_Click</c> — paired data entry rectangle.</summary>
    private const int F13VarDataInput = 1;

    /// <summary><c>Btn_Property.button5_Click</c> case 101 — <c>cfg_ICOAnim</c> / 动画图标显示.</summary>
    private const int F13AnimationIconDisplay = 101;

    private static void EnsureDataVariableDisplayF13(Document doc)
    {
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r)
                continue;
            if (r.ConfigObject is DataTextShow && r.f13Type != F13DataVariableDisplay)
                r.f13Type = F13DataVariableDisplay;
            if (r.ConfigObject is VarInput && r.f13Type != F13VarDataInput && r.f13Type < 100)
                r.f13Type = F13VarDataInput;
        }
    }

    private static void EnsureAnimationIconF13(Document doc)
    {
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r || r.ConfigObject is not AnimateShow)
                continue;
            if (r.f13Type < 100)
                r.f13Type = F13AnimationIconDisplay;
        }
    }

    /// <summary><c>Btn_Property.button5_Click</c> case 103 — <c>cfg_ICOArt</c> / 艺术字变量 (artistic variable).</summary>
    private const int F13ArtisticVariableDisplay = 103;

    /// <summary><c>Btn_Property.button5_Click</c> case 3 — <c>cfg_IncAdjustment</c> / incremental adjustment touch.</summary>
    private const int F13IncrementalAdjustment = 3;

    /// <summary>Touch <b>位按钮</b> — DwinTerminal persists <c>DrawRectangle.f13Type</c> <b>16</b> for <c>BitButton</c> (confirmed from <c>TEST_PROJECT</c> hand-placed control; <c>Btn_Property.button5_Click</c> has no dedicated <c>cfg_*</c> case for it).</summary>
    private const int F13BitButton = 16;

    private static void EnsureIncManagerF13(Document doc)
    {
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r || r.ConfigObject is not IncManager)
                continue;
            if (r.f13Type != F13IncrementalAdjustment)
                r.f13Type = F13IncrementalAdjustment;
        }
    }

    private static void EnsureBitButtonF13(Document doc)
    {
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r || !IsBitButtonConfig(r.ConfigObject))
                continue;
            if (r.f13Type >= 100)
                continue;
            if (r.f13Type != F13BitButton)
                r.f13Type = F13BitButton;
        }
    }

    /// <summary>
    /// DwinTerminal **Icon File** / <c>cfg_ICOArt</c> uses <see cref="ShowBase.ICOFileName"/> (e.g. <c>24.icl</c>), not only
    /// <see cref="ArtTextShow.Icon_lib"/>. <see cref="ArtTextShow.CreatFromByte"/> sets <c>ICOFileName</c> from a disk scan; generated
    /// objects must set both or the property grid shows no library and Generate can mis-bind.
    /// </summary>
    private static void BindArtTextShowIconLibrary(ArtTextShow art, byte iconLibraryId)
    {
        art.Icon_lib = iconLibraryId;
        art.ICOFileName = iconLibraryId.ToString(CultureInfo.InvariantCulture) + ".icl";
    }

    private static void EnsureArtTextShowF13(Document doc)
    {
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r || r.ConfigObject is not ArtTextShow art)
                continue;
            if (r.f13Type != F13ArtisticVariableDisplay)
                r.f13Type = F13ArtisticVariableDisplay;
            if (string.IsNullOrWhiteSpace(art.ICOFileName) && art.Icon_lib > 0)
                art.ICOFileName = art.Icon_lib.ToString(CultureInfo.InvariantCulture) + ".icl";
        }
    }

    private static bool IsBitButtonConfig(object cfg) =>
        cfg != null && string.Equals(cfg.GetType().Name, "BitButton", StringComparison.Ordinal);

    /// <summary>Internal <c>BizDraw.ConfigInput.BitButton</c> — instantiate via reflection.</summary>
    private static InputBase CreateBitButtonInput()
    {
        var t = typeof(VarInput).Assembly.GetType("BizDraw.ConfigInput.BitButton", throwOnError: true);
        return (InputBase)Activator.CreateInstance(t)!;
    }

    private static void SetBitButtonFields(InputBase bb, byte bitPos, byte adjMode)
    {
        var ty = bb.GetType();
        var bp = ty.GetProperty("Bit_Pos", BindingFlags.Instance | BindingFlags.Public);
        var am = ty.GetProperty("Adj_Mode", BindingFlags.Instance | BindingFlags.Public);
        var fe = ty.GetProperty("FEorFD", BindingFlags.Instance | BindingFlags.Public);
        bp?.SetValue(bb, bitPos, null);
        am?.SetValue(bb, adjMode, null);
        // FE0D + Adj_Mode=1: press writes bit=1 and auto-uploads VP to MCU over UART.
        // Adj_Mode=0 writes 0 → MCU `if (v)` never fires; FD0D skips UART notify.
        fe?.SetValue(bb, true, null);
    }

    /// <summary>Add one centered bit button if the page has no <c>BitButton</c> yet.</summary>
    private static bool TryAddCenteredBitButton(Document doc, int docWidth, int docHeight)
    {
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r)
                continue;
            if (IsBitButtonConfig(r.ConfigObject))
                return false;
        }

        const int bw = 120;
        const int bh = 48;
        int x0 = (docWidth - bw) / 2;
        int y0 = (docHeight - bh) / 2;
        if (x0 < 0)
            x0 = 0;
        if (y0 < 0)
            y0 = 0;
        var rect = new Rectangle(x0, y0, bw, bh);
        ushort vp = NextFreeVp(doc, 0x6020);
        string vpHex = vp.ToString("X4", CultureInfo.InvariantCulture);

        InputBase bit = CreateBitButtonInput();
        bit.Var_Name = "";
        bit.VarStrPoint = vpHex;
        bit.Pic_Id = 0;
        bit.Pic_Next = -1;
        bit.Pic_On = -1;
        SetBitButtonFields(bit, bitPos: 0, adjMode: 1);

        var screen = new Point(docWidth, docHeight);
        var rHost = new DrawRectangle(rect.X, rect.Y, rect.Width, rect.Height, false)
        {
            Rectangle = rect,
            ScreenSize = screen,
            f13Type = F13BitButton,
            ConfigObject = bit
        };
        bit.SetPosition(rect);
        AppendDrawObject(doc, rHost);
        EnsureBitButtonF13(doc);
        doc.SetDirtyFlag(true);
        return true;
    }

    /// <summary>Fixed VPs for <see cref="TryInitCoolDualPanel"/> (predictable MCU map).</summary>
    private const ushort VpCoolLine1 = 0x6030;

    private const ushort VpCoolLine2 = 0x6034;
    private const ushort VpCoolBtnA = 0x6060;
    private const ushort VpCoolBtnB = 0x6064;

    /// <summary>Two number lines (VarInput + DataTextShow) + two BitButtons; slate cards + accent borders.</summary>
    private static bool TryInitCoolDualPanel(Document doc, int docWidth, int docHeight, ushort spBase, string projectDir)
    {
        ClearAllDrawObjects(doc);
        var screen = new Point(docWidth, docHeight);
        const int left = 52;
        const int inW = 124;
        const int inH = 42;
        const int gap = 18;
        const int showW = 268;
        const int showH = 46;
        var showX = left + inW + gap;
        const int row1Y = 108;
        const int row2Y = 218;
        ushort spLine1 = (ushort)(spBase + 0x100);
        ushort spLine2 = (ushort)(spBase + 0x140);
        AppendNumberLinePair(doc, screen, new Rectangle(left, row1Y, inW, inH), new Rectangle(showX, row1Y - 2, showW, showH),
            VpCoolLine1, spLine1, "Line1Input", "Line1Display");
        AppendNumberLinePair(doc, screen, new Rectangle(left, row2Y, inW, inH), new Rectangle(showX, row2Y - 2, showW, showH),
            VpCoolLine2, spLine2, "Line2Input", "Line2Display");

        const int btnW = 128;
        const int btnH = 54;
        var btnX = Math.Max(16, docWidth - btnW - 40);
        string dwinSet = Path.Combine(projectDir, "DWIN_SET");
        bool gfx = File.Exists(Path.Combine(dwinSet, "01.bmp")) &&
                   File.Exists(Path.Combine(dwinSet, "02.bmp")) &&
                   File.Exists(Path.Combine(dwinSet, "03.bmp")) &&
                   File.Exists(Path.Combine(dwinSet, "04.bmp"));
        if (gfx)
            Console.WriteLine(
                "Cool panel: BitButtons use picture indices 1/2 and 3/4 (Pic_Id/Pic_On). List 00.bmp then 01.bmp..04.bmp in [IMG] in that order so indices match DWIN_SET assets.");

        short aPic = gfx ? (short)1 : (short)0;
        short aOn = gfx ? (short)2 : (short)-1;
        short bPic = gfx ? (short)3 : (short)0;
        short bOn = gfx ? (short)4 : (short)-1;
        AppendBitButtonForCoolPanel(doc, screen, new Rectangle(btnX, row1Y - 4, btnW, btnH), VpCoolBtnA, "BtnA", 0, aPic, aOn);
        AppendBitButtonForCoolPanel(doc, screen, new Rectangle(btnX, row2Y - 4, btnW, btnH), VpCoolBtnB, "BtnB", 0, bPic, bOn);

        EnsureDataVariableDisplayF13(doc);
        EnsureBitButtonF13(doc);
        doc.SetDirtyFlag(true);
        return true;
    }

    private const ushort VpTestKanalA = 0x6030;
    private const ushort VpTestKanalB = 0x6034;
    private const ushort VpTestActA = 0x6060;
    private const ushort VpTestActB = 0x6064;
    private const ushort VpTestBminus = 0x6070;
    private const ushort VpTestBplus = 0x6072;

    private const ushort VpCounter2Value = 0x6030;
    private const ushort VpCounter2Minus = 0x6070;
    private const ushort VpCounter2Plus = 0x6072;

    private const ushort VpStarStopStar = 0x6080;
    private const ushort VpStarStopStop = 0x6082;

    private const ushort VpPcTarget = 0x6000;
    private const ushort VpPcTravel = 0x6010;
    private const ushort VpPcSpeedMs = 0x6020;
    private const ushort VpPcSpeedRpm = 0x6024;
    private const ushort VpPcProgress = 0x6030;
    private const ushort VpPcCmdStart = 0x6050;
    private const ushort VpPcCmdStop = 0x6051;
    private const ushort VpPcCmdReset = 0x6052;
    private const ushort VpPcCmdKeypad = 0x6053;
    private const ushort VpPcKbBuf = 0x6080;
    private const ushort VpPcKb1 = 0x60A1;
    private const ushort VpPcKb2 = 0x60A2;
    private const ushort VpPcKb3 = 0x60A3;
    private const ushort VpPcKb4 = 0x60A4;
    private const ushort VpPcKb5 = 0x60A5;
    private const ushort VpPcKb6 = 0x60A6;
    private const ushort VpPcKb7 = 0x60A7;
    private const ushort VpPcKb8 = 0x60A8;
    private const ushort VpPcKb9 = 0x60A9;
    private const ushort VpPcKb0 = 0x60AA;
    private const ushort VpPcKbDel = 0x60AB;
    private const ushort VpPcKbOk = 0x60AC;
    private const ushort VpPcKbCancel = 0x60AD;

    // CLEAN_TABLO — classic 0x60xx (same as paper_cutter / FE0D touch bin)
    private const ushort VpCtTarget = 0x6000;
    private const ushort VpCtTravel = 0x6010;
    private const ushort VpCtSpeedMs = 0x6020;
    private const ushort VpCtSpeedRpm = 0x6024;
    private const ushort VpCtProgress = 0x6030;
    private const ushort VpCtCmdStart = 0x6050;
    private const ushort VpCtCmdStop = 0x6051;
    private const ushort VpCtCmdReset = 0x6052;
    private const ushort VpCtCmdKeypad = 0x6053;
    private const ushort VpCtCmdSettings = 0x6055;
    private const ushort VpCtKbBuf = 0x6080;
    private const ushort VpCtKb1 = 0x60A1;
    private const ushort VpCtKb2 = 0x60A2;
    private const ushort VpCtKb3 = 0x60A3;
    private const ushort VpCtKb4 = 0x60A4;
    private const ushort VpCtKb5 = 0x60A5;
    private const ushort VpCtKb6 = 0x60A6;
    private const ushort VpCtKb7 = 0x60A7;
    private const ushort VpCtKb8 = 0x60A8;
    private const ushort VpCtKb9 = 0x60A9;
    private const ushort VpCtKb0 = 0x60AA;
    private const ushort VpCtKbDel = 0x60AB;
    private const ushort VpCtKbOk = 0x60AC;
    private const ushort VpCtKbCancel = 0x60AD;

    /// <summary>DGUS ArtTextShow combo index: unsigned 16-bit word (无符号整数 2字节).</summary>
    private const byte PcArtVarTypeUInt16 = 5;

    /// <summary>DGUS ArtTextShow combo index: long integer 4 bytes (长整数).</summary>
    private const byte PcArtVarTypeLong32 = 1;

    /// <summary>ArtTextShow TxtAlign: 0=left, 1=right, 2=center.</summary>
    private const byte PcArtAlignLeft = 0;
    private const byte PcArtAlignRight = 1;

    /// <summary>IncManager Key_Mode: 0 = hold continuous, 1 = one step per press.</summary>
    private const byte PcIncKeyModeHold = 0;

    /// <summary>Max target VP raw (0.1 m units): 32767 = 3276.7 m; matches signed V_Max in IncManager.</summary>
    private const short PcTargetVpMax = 32767;

    private const ushort PcProgressSteps = 101; // 0..100% по 1%
    private const ushort PcProgressIconBase = 70;
    private const ushort PcProgressIconMax = 170; // 70 + 101 - 1
    private const ushort PcProgressVpMax = 100; // VP 0..100 = 0..100% от «ЗАДАНО»

    /// <summary>BUTTON_KEYPAD page 0: one BitButton opens keypad page 10.</summary>
    private static bool TryInitButtonKeypad(Document doc, int docWidth, int docHeight, string projectDir)
    {
        ClearAllDrawObjects(doc);
        var lay = PaperCutterLayoutFile.LoadOrDefault(projectDir);
        var screen = new Point(docWidth, docHeight);
        string dwinSet = Path.Combine(projectDir, "DWIN_SET");
        bool gfx = File.Exists(Path.Combine(dwinSet, "01.bmp"));
        short picOn = gfx ? (short)1 : (short)-1;
        if (gfx)
            Console.WriteLine("BUTTON_KEYPAD: idle on 00.bmp; Pic_On=1; Pic_Next=10 (keypad popup).");
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("btn_open"), VpPcCmdKeypad, "Btn_open", 0, 0, picOn, picNext: 10);
        EnsureBitButtonF13(doc);
        doc.SetDirtyFlag(true);
        return true;
    }

    /// <summary>CLEAN_TABLO: ЗАДАНО/ОСТАЛОСЬ + speed + RESET/STOP; Run starts on shaft motion.</summary>
    private static bool TryInitCleanTablo(Document doc, int docWidth, int docHeight, ushort spBase, string projectDir)
    {
        ClearAllDrawObjects(doc);
        var lay = PaperCutterLayoutFile.LoadOrDefault(projectDir);
        var screen = new Point(docWidth, docHeight);
        string dwinSet = Path.Combine(projectDir, "DWIN_SET");
        if (!File.Exists(Path.Combine(dwinSet, "24.icl")))
            Console.WriteLine("WARNING: DWIN_SET\\24.icl missing — pack digits first");
        if (!File.Exists(Path.Combine(dwinSet, "25.icl")))
            Console.WriteLine("WARNING: DWIN_SET\\25.icl missing — pack digits first");

        ushort spTarget = 0x5100;
        ushort spTravel = 0x5110;
        ushort spSpeedMs = 0x5120;

        AppendArtTextShowFormatted(doc, screen, lay.Rect("target_display"), VpCtTarget, spTarget, "Target_m", 24, 30, 5, 0,
            PcArtVarTypeLong32, PcArtAlignRight, showLeadingZeros: true, iconHeightPx: 72);
        AppendArtTextShowFormatted(doc, screen, lay.Rect("travel_display"), VpCtTravel, spTravel, "Travel_m", 24, 30, 5, 0,
            PcArtVarTypeLong32, PcArtAlignRight, showLeadingZeros: true, iconHeightPx: 72);
        AppendArtTextShowFormatted(doc, screen, lay.Rect("speed_ms_display"), VpCtSpeedMs, spSpeedMs, "Speed_ms", 24, 30, 2, 2,
            PcArtVarTypeUInt16, PcArtAlignRight, showLeadingZeros: false, iconHeightPx: 72);

        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("target_touch"), VpCtCmdKeypad, "Cmd_keypad", 0, 0, (short)-1, picNext: 10);

        bool gfxCtrl = File.Exists(Path.Combine(projectDir, "image", "01.bmp")) &&
                       File.Exists(Path.Combine(projectDir, "image", "02.bmp"));
        short rOn = gfxCtrl ? (short)1 : (short)-1;
        short tOn = gfxCtrl ? (short)2 : (short)-1;
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("btn_reset"), VpCtCmdReset, "Cmd_reset", 0, 0, rOn);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("btn_stop"), VpCtCmdStop, "Cmd_stop", 0, 0, tOn);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("btn_settings"), VpCtCmdSettings, "Cmd_settings", 0, 0, (short)-1, picNext: 16);

        // Progress fill (26.icl icons 70..170); keep on page 0 only via Pic_Id.
        if (lay.Controls != null && lay.Controls.ContainsKey("progress_bar"))
            AppendIconShowProgress(doc, screen, lay.Rect("progress_bar"), VpCtProgress, 0x5140, "Progress");

        EnsureArtTextShowF13(doc);
        EnsureBitButtonF13(doc);
        EnsureVariableIconDisplayF13(doc);
        doc.SetDirtyFlag(true);
        Console.WriteLine("CLEAN_TABLO: VP 6000/6010/6020/6030 + RESET/STOP/gear, background ICL=32");
        return true;
    }

    /// <summary>CLEAN_TABLO page 16 — braking distance ArtText VP 6090 (LONG32).</summary>
    private static bool TryInitCleanTabloSettings(Document doc, int docWidth, int docHeight, ushort spBase, string projectDir)
    {
        ClearAllDrawObjects(doc);
        var lay = PaperCutterLayoutFile.LoadOrDefault(projectDir);
        var screen = new Point(docWidth, docHeight);
        if (lay.Controls == null || !lay.Controls.ContainsKey("brake_display"))
        {
            Console.WriteLine("WARNING: layout missing brake_display — page 16 empty");
            return true;
        }
        AppendArtTextShowFormatted(doc, screen, lay.Rect("brake_display"), 0x6090, 0x5190, "Brake_m", 24, 30, 5, 0,
            PcArtVarTypeLong32, PcArtAlignRight, showLeadingZeros: true, iconHeightPx: 72);
        foreach (DrawObject o in doc.Items)
        {
            if (o is DrawRectangle r && r.ConfigObject is ArtTextShow art && art.Var_Name == "Brake_m")
                art.Pic_Id = 16;
        }
        EnsureArtTextShowF13(doc);
        doc.SetDirtyFlag(true);
        Console.WriteLine("CLEAN_TABLO settings page 16: ArtText VP6090 LONG32 Pic_Id=16");
        return true;
    }

    /// <summary>CLEAN_TABLO page 10 keypad — classic VP 0x60A1–60AD / buffer 6080.</summary>
    private static bool TryInitCleanTabloKeypad(Document doc, int docWidth, int docHeight, ushort spBase, string projectDir)
    {
        ClearAllDrawObjects(doc);
        var lay = PaperCutterLayoutFile.LoadOrDefault(projectDir);
        var screen = new Point(docWidth, docHeight);
        ushort spKb = 0x5180;
        AppendArtTextShowFormatted(doc, screen, lay.Rect("kb_display"), VpCtKbBuf, spKb, "Kb_buf", 24, 30, 5, 0,
            PcArtVarTypeUInt16, PcArtAlignLeft, showLeadingZeros: false, iconHeightPx: 56);
        foreach (DrawObject o in doc.Items)
        {
            if (o is DrawRectangle r && r.ConfigObject is ArtTextShow art && art.Var_Name == "Kb_buf")
                art.Pic_Id = 10;
        }
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_1"), VpCtKb1, "Kb_1", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_2"), VpCtKb2, "Kb_2", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_3"), VpCtKb3, "Kb_3", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_4"), VpCtKb4, "Kb_4", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_5"), VpCtKb5, "Kb_5", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_6"), VpCtKb6, "Kb_6", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_7"), VpCtKb7, "Kb_7", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_8"), VpCtKb8, "Kb_8", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_9"), VpCtKb9, "Kb_9", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_0"), VpCtKb0, "Kb_0", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_del"), VpCtKbDel, "Kb_del", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_cancel"), VpCtKbCancel, "Kb_cancel", 0, 0, -1, picNext: 0);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_ok"), VpCtKbOk, "Kb_ok", 0, 0, -1, picNext: 0);
        EnsureArtTextShowF13(doc);
        EnsureBitButtonF13(doc);
        doc.SetDirtyFlag(true);
        return true;
    }

    private static void WriteCleanTabloVpMap(string projectDir, ushort spBase, int width, int height)
    {
        string path = Path.Combine(projectDir, "VP_MEMORY_MAP.md");
        var lines = new List<string>
        {
            "# VP / SP — CLEAN_TABLO",
            "",
            "Полный экран: ЗАДАНО / ОСТАЛОСЬ / м/с + СБРОС / СТОП. Тап **ЗАДАНО** → стр. **10**.",
            "Режим Run — автоматически при первых импульсах энкодера (вал запускают вручную).",
            "",
            "Фон страниц: **`32.icl`** (T5LCFG слот **0x20**).",
            "",
            "**MCU:** `D:\\paper_cutter\\BlackPill\\paper_cutter\\CLEAN_TABLO_FW\\`",
            "",
            "**Resolution** " + width + "\u00d7" + height + ", **SPADDRESS** 0x" + spBase.ToString("X4", CultureInfo.InvariantCulture) + ".",
            "",
            "| Role | VP | Widget |",
            "|------|-----|--------|",
            "| Задано | 6000 | ArtTextShow |",
            "| Осталось | 6010 | ArtTextShow |",
            "| Скорость м/с | 6020 | ArtTextShow ×0.01 |",
            "| СТОП / СБРОС | 6051 / 6052 | BitButton |",
            "| Open keypad | 6053 | BitButton Pic_Next=10 |",
            "| KB buffer | 6080 | ArtTextShow |",
            "| Keys / OK / Cancel | 60A1–60AD | BitButton |",
        };
        File.WriteAllText(path, string.Join("\r\n", lines) + "\r\n");
        Console.WriteLine("Wrote " + path);
    }

    /// <summary>METER_TABLO page 0: left-top meters ArtText + invisible touch → keypad page 10.</summary>
    private static bool TryInitMeterTablo(Document doc, int docWidth, int docHeight, ushort spBase, string projectDir)
    {
        ClearAllDrawObjects(doc);
        var lay = PaperCutterLayoutFile.LoadOrDefault(projectDir);
        var screen = new Point(docWidth, docHeight);
        string dwinSet = Path.Combine(projectDir, "DWIN_SET");
        if (!File.Exists(Path.Combine(dwinSet, "24.icl")))
            Console.WriteLine("WARNING: DWIN_SET\\24.icl missing — pack digits first");
        ushort spTarget = (ushort)(spBase + 0x100);
        AppendArtTextShowFormatted(doc, screen, lay.Rect("target_display"), VpPcTarget, spTarget, "Meter_m", 24, 30, 5, 0,
            PcArtVarTypeUInt16, PcArtAlignRight);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("target_touch"), VpPcCmdKeypad, "Cmd_keypad", 0, 0, (short)-1, picNext: 10);
        EnsureArtTextShowF13(doc);
        EnsureBitButtonF13(doc);
        doc.SetDirtyFlag(true);
        Console.WriteLine("METER_TABLO: ArtText VP6000 uint16 max 65000 m + touch Pic_Next=10");
        return true;
    }

    /// <summary>METER_TABLO page 10: uint16 keypad buffer VP 6080.</summary>
    private static bool TryInitMeterTabloKeypad(Document doc, int docWidth, int docHeight, ushort spBase, string projectDir)
    {
        ClearAllDrawObjects(doc);
        var lay = PaperCutterLayoutFile.LoadOrDefault(projectDir);
        var screen = new Point(docWidth, docHeight);
        ushort spKb = (ushort)(spBase + 0x180);
        AppendArtTextShowFormatted(doc, screen, lay.Rect("kb_display"), VpPcKbBuf, spKb, "Kb_buf", 24, 30, 5, 0,
            PcArtVarTypeUInt16, PcArtAlignLeft, showLeadingZeros: false, iconHeightPx: 56);
        foreach (DrawObject o in doc.Items)
        {
            if (o is DrawRectangle r && r.ConfigObject is ArtTextShow art && art.Var_Name == "Kb_buf")
                art.Pic_Id = 10;
        }
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_1"), VpPcKb1, "Kb_1", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_2"), VpPcKb2, "Kb_2", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_3"), VpPcKb3, "Kb_3", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_4"), VpPcKb4, "Kb_4", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_5"), VpPcKb5, "Kb_5", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_6"), VpPcKb6, "Kb_6", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_7"), VpPcKb7, "Kb_7", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_8"), VpPcKb8, "Kb_8", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_9"), VpPcKb9, "Kb_9", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_0"), VpPcKb0, "Kb_0", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_del"), VpPcKbDel, "Kb_del", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_cancel"), VpPcKbCancel, "Kb_cancel", 0, 0, -1, picNext: 0);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_ok"), VpPcKbOk, "Kb_ok", 0, 0, -1, picNext: 0);
        EnsureArtTextShowF13(doc);
        EnsureBitButtonF13(doc);
        doc.SetDirtyFlag(true);
        return true;
    }

    private static void WriteMeterTabloVpMap(string projectDir, ushort spBase, int width, int height)
    {
        string path = Path.Combine(projectDir, "VP_MEMORY_MAP.md");
        var lines = new List<string>
        {
            "# VP / SP — METER_TABLO",
            "",
            "Left-top **ЗАДАНО, м** (VP **6000**, uint16, whole meters, max **65000**). Tap → page **10**. Needs **METER_TABLO_FW**.",
            "",
            "**Resolution** " + width + "\u00d7" + height + ", **SPADDRESS** 0x" + spBase.ToString("X4", CultureInfo.InvariantCulture) + ".",
            "",
            "## VP",
            "",
            "| Role | VP hex | Widget | Notes |",
            "|--------|--------|--------|--------|",
            "| Meters | 6000 | ArtTextShow | N_Int=5, max 65000 |",
            "| Open keypad | 6053 | BitButton | Pic_Next=10 |",
            "| Keypad buffer | 6080 | ArtTextShow | Page 10 |",
            "| Digits / Del / OK / Cancel | 60A1–60AD | BitButton | OK/Cancel Pic_Next=0 |",
            "",
            "Build: **`BuildFromDesign.ps1`**. Firmware: **`BlackPill/METER_TABLO_FW`**.",
            ""
        };
        File.WriteAllText(path, string.Join("\r\n", lines) + "\r\n");
        Console.WriteLine("Wrote " + path);
    }

    private static void WriteButtonKeypadVpMap(string projectDir, ushort spBase, int width, int height)
    {
        string path = Path.Combine(projectDir, "VP_MEMORY_MAP.md");
        var lines = new List<string>
        {
            "# VP / SP — BUTTON_KEYPAD",
            "",
            "Main page button opens **page 10** keypad (`Pic_Next=10`). OK / Cancel return to page 0.",
            "",
            "**Resolution** " + width + "\u00d7" + height + ", **SPADDRESS** 0x" + spBase.ToString("X4", CultureInfo.InvariantCulture) + ".",
            "",
            "## VP",
            "",
            "| Role | VP hex | Widget | Notes |",
            "|--------|--------|--------|--------|",
            "| Open keypad | 6053 | BitButton | Page 0, Pic_Next=10, Pic_On=1 |",
            "| Keypad buffer | 6080 | ArtTextShow | Page 10, 24.icl icons 30–39 |",
            "| Digits 1–9,0,Del,OK,Cancel | 60A1–60AD | BitButton | Page 10; OK/Cancel Pic_Next=0 |",
            "",
            "## Pictures",
            "",
            "| File | Role |",
            "|------|------|",
            "| 00.bmp | Main idle |",
            "| 01.bmp | Main button pressed (Pic_On) |",
            "| 10.bmp | Keypad popup |",
            "",
            "Build with **`BuildFromDesign.ps1`**. Touch/show patches: **`gen_button_keypad_touch_bin.py`**, **`fix_button_keypad_show_pages.py`**.",
            ""
        };
        File.WriteAllText(path, string.Join("\r\n", lines) + "\r\n");
        Console.WriteLine("Wrote " + path);
    }

    /// <summary>PAPER_CUTTER: meters displays, speed, progress icon, adjustment + control buttons.</summary>
    private static bool TryInitPaperCutter(Document doc, int docWidth, int docHeight, ushort spBase, string projectDir)
    {
        ClearAllDrawObjects(doc);
        var lay = PaperCutterLayoutFile.LoadOrDefault(projectDir);
        if (lay.Screen.Width != docWidth || lay.Screen.Height != docHeight)
            Console.WriteLine(
                "WARNING: design/layout.json screen " + lay.Screen.Width + "x" + lay.Screen.Height + " != document " +
                docWidth + "x" + docHeight + " — controls still placed from JSON.");
        var screen = new Point(docWidth, docHeight);
        string dwinSet = Path.Combine(projectDir, "DWIN_SET");
        if (!File.Exists(Path.Combine(dwinSet, "24.icl")))
            Console.WriteLine("WARNING: DWIN_SET\\24.icl missing — run gen_paper_cutter_digits_large.py + pack_paper_cutter_icl.py");
        if (!File.Exists(Path.Combine(dwinSet, "25.icl")))
            Console.WriteLine("WARNING: DWIN_SET\\25.icl missing — run gen_paper_cutter_digits_small.py + pack_paper_cutter_icl.py");
        if (!File.Exists(Path.Combine(dwinSet, "26.icl")))
            Console.WriteLine("WARNING: DWIN_SET\\26.icl missing — run gen_paper_cutter_progress.py + pack_paper_cutter_icl.py");

        ushort spTarget = (ushort)(spBase + 0x100);
        ushort spTravel = (ushort)(spBase + 0x110);
        ushort spSpeedMs = (ushort)(spBase + 0x120);
        ushort spSpeedRpm = (ushort)(spBase + 0x130);
        ushort spProgress = (ushort)(spBase + 0x140);

        // ArtTextShow (24/25.icl) — digit boards; IconShow for progress.
        AppendArtTextShowFormatted(doc, screen, lay.Rect("target_display"), VpPcTarget, spTarget, "Target_m", 24, 30, 4, 1);
        AppendArtTextShowFormatted(doc, screen, lay.Rect("travel_display"), VpPcTravel, spTravel, "Travel_m", 24, 30, 4, 1);
        AppendArtTextShowFormatted(doc, screen, lay.Rect("speed_ms_display"), VpPcSpeedMs, spSpeedMs, "Speed_ms", 25, 50, 2, 2);
        AppendArtTextShowFormatted(doc, screen, lay.Rect("speed_rpm_display"), VpPcSpeedRpm, spSpeedRpm, "Speed_rpm", 25, 50, 4, 0);
        AppendIconShowProgress(doc, screen, lay.Rect("progress_bar"), VpPcProgress, spProgress, "Progress_pct",
            PcProgressVpMax, PcProgressIconBase, PcProgressIconMax);

        // Tap ЗАДАНО → Pic_Next=10 (panel). MCU syncs VP only — no setPage (UART page switch flaky).
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("target_touch"), VpPcCmdKeypad, "Cmd_keypad", 0, 0, (short)-1, picNext: 10);

        bool gfxCtrl = File.Exists(Path.Combine(dwinSet, "01.bmp")) &&
                       File.Exists(Path.Combine(dwinSet, "02.bmp")) &&
                       File.Exists(Path.Combine(dwinSet, "03.bmp"));
        if (gfxCtrl)
            Console.WriteLine(
                "PAPER_CUTTER controls: idle on 00.bmp; СТАРТ stays on main; ЗАДАНО Pic_Next=10 (keypad).");

        short sOn = gfxCtrl ? (short)1 : (short)-1;
        short rOn = gfxCtrl ? (short)2 : (short)-1;
        short tOn = gfxCtrl ? (short)3 : (short)-1;
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("btn_start"), VpPcCmdStart, "Cmd_start", 0, 0, sOn);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("btn_reset"), VpPcCmdReset, "Cmd_reset", 0, 0, rOn);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("btn_stop"), VpPcCmdStop, "Cmd_stop", 0, 0, tOn);

        EnsureArtTextShowF13(doc);
        EnsureVariableIconDisplayF13(doc);
        EnsureBitButtonF13(doc);
        doc.SetDirtyFlag(true);
        return true;
    }

    /// <summary>Page 10: numeric keypad + ArtTextShow buffer VP 6080.</summary>
    private static bool TryInitPaperCutterKeypad(Document doc, int docWidth, int docHeight, ushort spBase, string projectDir)
    {
        ClearAllDrawObjects(doc);
        var lay = PaperCutterLayoutFile.LoadOrDefault(projectDir);
        var screen = new Point(docWidth, docHeight);
        ushort spKb = (ushort)(spBase + 0x180);
        AppendArtTextShowFormatted(doc, screen, lay.Rect("kb_display"), VpPcKbBuf, spKb, "Kb_buf", 24, 30, 4, 1,
            PcArtVarTypeUInt16, PcArtAlignLeft);
        foreach (DrawObject o in doc.Items)
        {
            if (o is DrawRectangle r && r.ConfigObject is ArtTextShow art && art.Var_Name == "Kb_buf")
                art.Pic_Id = 10;
        }
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_1"), VpPcKb1, "Kb_1", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_2"), VpPcKb2, "Kb_2", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_3"), VpPcKb3, "Kb_3", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_4"), VpPcKb4, "Kb_4", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_5"), VpPcKb5, "Kb_5", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_6"), VpPcKb6, "Kb_6", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_7"), VpPcKb7, "Kb_7", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_8"), VpPcKb8, "Kb_8", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_9"), VpPcKb9, "Kb_9", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_0"), VpPcKb0, "Kb_0", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_del"), VpPcKbDel, "Kb_del", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_cancel"), VpPcKbCancel, "Kb_cancel", 0, 0, -1, picNext: 0);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("kb_ok"), VpPcKbOk, "Kb_ok", 0, 0, -1, picNext: 0);
        EnsureArtTextShowF13(doc);
        EnsureBitButtonF13(doc);
        doc.SetDirtyFlag(true);
        return true;
    }

    /// <summary>PAPER_CUTTER number board helper kept for other tools; not used on main PC page.</summary>
    private static void AppendDataTextShowPc(
        Document doc,
        Point screen,
        Rectangle rectShow,
        ushort vp,
        ushort spDesc,
        string displayName,
        byte nInt,
        byte nDot,
        int fontSize,
        byte align)
    {
        string vpHex = vp.ToString("X4", CultureInfo.InvariantCulture);
        string spHex = spDesc.ToString("X4", CultureInfo.InvariantCulture);
        var show = new DataTextShow
        {
            Var_Name = displayName,
            VarStrPoint = vpHex,
            VarDescAddress = spHex,
            N_Int = nInt,
            N_Dot = nDot,
            VarType = PcArtVarTypeUInt16,
            Lib_Id = 0,
            FontSize = (byte)Math.Min(255, Math.Max(8, fontSize)),
            Align = align,
            V_Len = 0,
            String_Uint = "",
            Modify = 0,
            zeroDisplay = 0,
            Pic_Id = 0,
            IsShowOnPopMenu = false,
            TextColor = "FFFF"
        };
        var rShow = new DrawRectangle(rectShow.X, rectShow.Y, rectShow.Width, rectShow.Height, false)
        {
            Rectangle = rectShow,
            ScreenSize = screen,
            f13Type = F13DataVariableDisplay,
            ConfigObject = show
        };
        show.SetPosition(rectShow);
        AppendDrawObject(doc, rShow);
    }

    private static void AppendArtTextShowFormatted(
        Document doc,
        Point screen,
        Rectangle rectShow,
        ushort vp,
        ushort spDesc,
        string displayName,
        byte iconLib,
        ushort icon0,
        byte nInt,
        byte nDot,
        byte varType = PcArtVarTypeUInt16,
        byte txtAlign = PcArtAlignRight,
        bool showLeadingZeros = true,
        int iconHeightPx = 80)
    {
        string vpHex = vp.ToString("X4", CultureInfo.InvariantCulture);
        string spHex = spDesc.ToString("X4", CultureInfo.InvariantCulture);
        var art = new ArtTextShow
        {
            Var_Name = displayName,
            VarStrPoint = vpHex,
            VarDescAddress = spHex
        };
        BindArtTextShowIconLibrary(art, iconLib);
        art.Icon0 = icon0;
        art.Icon_Mod = 0;
        art.N_Int = nInt;
        art.N_Dot = nDot;
        art.VarType = varType;
        art.TxtAlign = txtAlign;
        art.Pic_Id = 0;
        art.isShow0 = showLeadingZeros;
        art.BackMode = 0;
        art.ICONLight = 100;
        art.BackLight = 100;
        var rShow = new DrawRectangle(rectShow.X, rectShow.Y, rectShow.Width, rectShow.Height, false)
        {
            Rectangle = rectShow,
            ScreenSize = screen,
            f13Type = F13ArtisticVariableDisplay,
            ConfigObject = art
        };
        art.SetPosition(rectShow);
        ApplyArtTextInset(art, rectShow, iconHeightPx);
        SyncArtTextDisPositon(art);
        AppendDrawObject(doc, rShow);
    }

    private static void AppendIconShowProgress(
        Document doc,
        Point screen,
        Rectangle rect,
        ushort vp,
        ushort spDesc,
        string name,
        ushort vMax = 100,
        ushort iconMin = 70,
        ushort iconMax = 170)
    {
        string vpHex = vp.ToString("X4", CultureInfo.InvariantCulture);
        string spHex = spDesc.ToString("X4", CultureInfo.InvariantCulture);
        var ic = new IconShow
        {
            Var_Name = name,
            VarStrPoint = vpHex,
            VarDescAddress = spHex,
            V_Min = 0,
            V_Max = vMax,
            Icon_Min = iconMin,
            Icon_Max = iconMax,
            Icon_lib = 26,
            Mode = 0,
            BackMode = 0,
            Pic_Id = 0
        };
        ic.ICOFileName = "26.icl";
        var rHost = new DrawRectangle(rect.X, rect.Y, rect.Width, rect.Height, false)
        {
            Rectangle = rect,
            ScreenSize = screen,
            f13Type = F13VariableIconDisplay,
            ConfigObject = ic
        };
        ic.SetPosition(rect);
        AppendDrawObject(doc, rHost);
    }

    /// <summary>Incremental adjust on <paramref name="targetVp"/>. Step in 0.1 m units (unsigned 16-bit word).</summary>
    private static void AppendIncAdjustForPaperCutter(
        Document doc,
        Point screen,
        Rectangle rect,
        ushort targetVp,
        string name,
        byte adjMode,
        ushort step,
        short picOn = -1)
    {
        var inc = new IncManager();
        inc.Var_Name = name;
        inc.VarStrPoint = targetVp.ToString("X4", CultureInfo.InvariantCulture);
        inc.VP_Mode = 0;
        inc.Adj_Mode = adjMode;
        inc.Return_Mode = 0;
        inc.Adj_Step = step;
        inc.V_Min = 0;
        inc.V_Max = PcTargetVpMax;
        inc.Key_Mode = PcIncKeyModeHold; // удержание → непрерывный шаг
        inc.Key_Delay = 20;              // пауза между шагами при удержании (~быстро, не «дребезг»)
        inc.FEorFD = true;
        inc.Pic_Id = 0;
        inc.Pic_Next = -1;
        inc.Pic_On = picOn;
        var rHost = new DrawRectangle(rect.X, rect.Y, rect.Width, rect.Height, false)
        {
            Rectangle = rect,
            ScreenSize = screen,
            f13Type = F13IncrementalAdjustment,
            ConfigObject = inc
        };
        inc.SetPosition(rect);
        AppendDrawObject(doc, rHost);
    }

    private static void WritePaperCutterVpMap(string projectDir, ushort spBase, int width, int height)
    {
        string path = Path.Combine(projectDir, "VP_MEMORY_MAP.md");
        var lines = new List<string>
        {
            "# VP / SP — PAPER_CUTTER (резка бумаги, DMG80480T050_02WTC)",
            "",
            "Layout: **`design/layout.json`**. Tool: **`GenerateTestTft --init-paper-cutter`**. Background: **`scripts/render_paper_cutter_screen.py`**.",
            "",
            "**Resolution** " + width + "\u00d7" + height + ", **SPADDRESS** 0x" + spBase.ToString("X4", CultureInfo.InvariantCulture) + ".",
            "",
            "## Единицы (для прошивки Black Pill)",
            "",
            "| Величина | VP | Единица VP | Отображение |",
            "|----------|-----|------------|-------------|",
            "| Заданная длина | 6000 | 0.1 м (десятые) | `XXXX.X` м, **24.icl** |",
            "| Пройденный путь энкодера | 6010 | 0.1 м | `XXXX.X` м, **24.icl** |",
            "| Скорость | 6020 | 0.01 м/с | `XX.XX` м/с, **25.icl** |",
            "| Обороты | 6024 | 1 об/мин | `XXXX` об/мин, **25.icl** |",
            "| Ход (прогресс) | 6030 | 0–100 (=0–100 % от VP 6000) | иконка **70..170** в **26.icl** |",
            "",
            "## Тип данных (обязательно в DGUS)",
            "",
            "Все числовые поля — **одно слово VP (2 байта)**, **VarType = 无符号整数 (2 bytes)** (combo index **5**).",
            "**Не использовать:** long / extra long / VP high/low byte — иначе мусор и «откат» значений.",
            "",
            "| Виджет | VP | N_Int | N_Dot | VarType | Выравнивание |",
            "|--------|-----|------:|------:|--------:|--------------|",
            "| ЗАДАНО | 6000 | 4 | 1 | 5 | вправо |",
            "| ПРОХОД | 6010 | 4 | 1 | 5 | вправо |",
            "| Скорость м/с | 6020 | 2 | 2 | 5 | вправо |",
            "| Обороты | 6024 | 4 | 0 | 5 | вправо |",
            "| Прогресс | 6030 | — | — | IconShow 0..100 | — |",
            "",
            "## VP — отображение",
            "",
            "| Роль | VP hex | Длина | Виджет | ICL |",
            "|------|--------|-------|--------|-----|",
            "| Задано, м | 6000 | **1 слово** | ArtTextShow | **24.icl** icons 30–40 |",
            "| Проход, м | 6010 | **1 слово** | ArtTextShow | **24.icl** icons 30–40 |",
            "| Скорость, м/с | 6020 | **1 слово** | ArtTextShow | **25.icl** icons 50–60 |",
            "| Обороты, об/мин | 6024 | **1 слово** | ArtTextShow | **25.icl** icons 50–59 |",
            "| Прогресс | 6030 | **1 слово** | IconShow | **26.icl** icons **70–170** (101 кадр, 0–100 %) |",
            "",
            "## VP — ввод «ЗАДАНО» (клавиатура, страница **10**)",
            "",
            "Кнопка **ВВОД ЧИСЛА** (VP **6053**) → MCU открывает страницу **10**. Ввод в **0.1 м** (125 = 12.5 м).",
            "",
            "| Клавиша | VP | Код MCU |",
            "|---------|-----|---------|",
            "| 1..9 | 60A1..60A9 | цифры |",
            "| 0 | 60AA | 0 |",
            "| ⌫ | 60AB | backspace |",
            "| OK | 60AC | применить → VP 6000, страница 0 |",
            "| ОТМЕНА | 60AD | страница 0 без записи |",
            "| Буфер на клавиатуре | **6080** | ArtTextShow |",
            "",
            "## VP — команды (BitButton + Pic_On)",
            "",
            "Снизу: **СТАРТ** | **СБРОС** | **СТОП**. Плюс **ВВОД ЧИСЛА**.",
            "",
            "| Кнопка | VP | Pic_On | Действие MCU |",
            "|--------|-----|--------|--------------|",
            "| СТАРТ | 6050 | 1 | RUN, lock ЗАДАНО |",
            "| СБРОС | 6052 | 2 | Обнулить всё, снять ошибки |",
            "| СТОП | 6051 | 3 | HOLD |",
            "| Табло ЗАДАНО (touch) | 6053 | — | Страница клавиатуры |",
            "",
            "## Ошибки (overlay сверху, страницы 11/12)",
            "",
            "| Fault | Страница | Текст |",
            "|-------|----------|-------|",
            "| EncReverse | **11** | ОШИБКА ВРАЩЕНИЯ |",
            "| EncNoPulse / EncImplausible | **12** | ОШИБКА ЭНКОДЕРА |",
            "",
            "MCU: регистры **0xE8/0xE9**. Сброс снимает overlay.",
            "",
            "## Калибровка",
            "",
            "Ролик **Ø80 мм**, энкодер 1000 PPR ×4 → `PULSES_PER_M ≈ 15915`.",
            "",
            "## SP (описатели формата)",
            "",
            "| Назначение | SP hex |",
            "|------------|--------|",
            "| Формат «задано» | " + (spBase + 0x100).ToString("X4", CultureInfo.InvariantCulture) + " |",
            "| Формат «проход» | " + (spBase + 0x110).ToString("X4", CultureInfo.InvariantCulture) + " |",
            "| Формат скорости м/с | " + (spBase + 0x120).ToString("X4", CultureInfo.InvariantCulture) + " |",
            "| Формат об/мин | " + (spBase + 0x130).ToString("X4", CultureInfo.InvariantCulture) + " |",
            "| Формат прогресса | " + (spBase + 0x140).ToString("X4", CultureInfo.InvariantCulture) + " |",
            "",
            "## ICL (отдельный файл на каждый формат глифов)",
            "",
            "| Файл | Содержимое | Скрипт |",
            "|------|------------|--------|",
            "| **23.icl** | Фон `00.bmp`–`09.bmp` (нажатия) | `pack_dwin_set_screen_to_icl.py` |",
            "| **24.icl** | Крупные цифры 30–39, точка 40 | `gen_paper_cutter_digits_large.py` |",
            "| **25.icl** | Мелкие цифры 50–59, точка 60 | `gen_paper_cutter_digits_small.py` |",
            "| **26.icl** | Ползунок **101 кадр** (иконки 70–170, VP 0–100) | `gen_paper_cutter_progress.py` |",
            "",
            "**Важно:** не смешивать крупные и мелкие глифы в одном `.icl`.",
            "",
            "## f13Type",
            "",
            "| Виджет | f13Type |",
            "|--------|--------:|",
            "| ArtTextShow | 103 |",
            "| DataTextShow | 106 |",
            "| IconShow (прогресс) | 100 |",
            "| BitButton | 16 |",
            "",
            "## После правок",
            "",
            "DGUS → **Save** → **Generate** → обновить `13TouchFile.bin`, `14ShowFile.bin`, `22_Config.bin`.",
            ""
        };
        var lay = PaperCutterLayoutFile.LoadOrDefault(projectDir);
        lines.Add("## Geometry (`controls`)");
        lines.Add("");
        lines.Add("| key | X | Y | W | H |");
        lines.Add("|-----|--:|--:|--:|--:|");
        foreach (var key in new[]
        {
            "target_display", "target_touch", "travel_display",
            "speed_ms_display", "speed_rpm_display", "progress_bar",
            "btn_start", "btn_reset", "btn_stop",
            "kb_display", "kb_1", "kb_0", "kb_ok", "kb_cancel"
        })
        {
            var rr = lay.Rect(key);
            lines.Add("| `" + key + "` | " + rr.X + " | " + rr.Y + " | " + rr.Width + " | " + rr.Height + " |");
        }

        lines.Add("");
        File.WriteAllLines(path, lines);
        Console.WriteLine("Wrote " + path);
    }

    /// <summary>Two large Cyrillic <see cref="BitButton"/>s; layout from <c>TestProjectStarStopLayoutFile</c>.</summary>
    private static bool TryInitTestProjectStarStop(Document doc, int docWidth, int docHeight, ushort spBase, string projectDir)
    {
        ClearAllDrawObjects(doc);
        var lay = TestProjectStarStopLayoutFile.LoadOrDefault(projectDir);
        if (lay.Screen.Width != docWidth || lay.Screen.Height != docHeight)
            Console.WriteLine(
                "WARNING: design/layout.json screen " + lay.Screen.Width + "x" + lay.Screen.Height + " != document " +
                docWidth + "x" + docHeight + " — controls still placed from JSON.");
        var screen = new Point(docWidth, docHeight);
        string dwinSet = Path.Combine(projectDir, "DWIN_SET");
        bool gfx = File.Exists(Path.Combine(dwinSet, "01.bmp")) &&
                   File.Exists(Path.Combine(dwinSet, "02.bmp"));
        if (gfx)
            Console.WriteLine(
                "STAR_STOP: idle on 00.bmp; BitButtons Pic_Id=0 / Pic_On=1 (\u0421\u0422\u0410\u0420) and 2 (\u0421\u0422\u041e\u041f). [IMG]: 00,01,02 — see VP_MEMORY_MAP.md.");

        short sPic = 0;
        short sOn = gfx ? (short)1 : (short)-1;
        short tPic = 0;
        short tOn = gfx ? (short)2 : (short)-1;
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("btn_star"), VpStarStopStar, "Btn_star", 0, sPic, sOn);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("btn_stop"), VpStarStopStop, "Btn_stop", 0, tPic, tOn);
        EnsureBitButtonF13(doc);
        doc.SetDirtyFlag(true);
        return true;
    }

    private static void ApplyStarStopChrome(Document doc)
    {
        var slate = Color.FromArgb(255, 28, 36, 58);
        var mint = Color.FromArgb(255, 100, 230, 190);
        var rose = Color.FromArgb(255, 255, 130, 155);
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r)
                continue;
            var nm = r.ConfigObject switch
            {
                InputBase ib when IsBitButtonConfig(ib) => ib.Var_Name,
                _ => ""
            };
            if (nm.Length == 0)
                continue;
            if (r.ConfigObject is InputBase ibBtn && IsBitButtonConfig(ibBtn) && ibBtn.Pic_Id != 0)
                continue;
            if (r.ConfigObject is InputBase ibBtn2 && IsBitButtonConfig(ibBtn2) && ibBtn2.Pic_On > (short)0)
                continue;
            r.DisMode = DrawRectangle.TDismode.Backcolor;
            if (string.Equals(nm, "Btn_star", StringComparison.Ordinal))
            {
                r.BColor = slate;
                r.Color = mint;
                r.PenWidth = 2;
                continue;
            }

            if (string.Equals(nm, "Btn_stop", StringComparison.Ordinal))
            {
                r.BColor = slate;
                r.Color = rose;
                r.PenWidth = 2;
            }
        }
    }

    private static void WriteStarStopVpMap(string projectDir, ushort spBase, int width, int height)
    {
        string path = Path.Combine(projectDir, "VP_MEMORY_MAP.md");
        var lines = new List<string>
        {
            "# VP — TEST_PROJECT_STAR_STOP (\u0421\u0422\u0410\u0420 / \u0421\u0422\u041e\u041f)",
            "",
            "Layout: **`design/layout.json`**. Tool: **`GenerateTestTft --init-star-stop`**. **`00.bmp`**: idle (`scripts/render_star_stop_screen.py`). **`01.bmp` / `02.bmp`**: full-screen Pic_On (`scripts/gen_star_stop_pressed.py`).",
            "",
            "**Resolution** " + width + "x" + height + ", **SPADDRESS** 0x" + spBase.ToString("X4", CultureInfo.InvariantCulture) + ".",
            "",
            "## VP (BitButton)",
            "",
            "| Role | VP hex | Length | Widget | Notes |",
            "|--------|--------|--------|--------|--------|",
            "| \u0421\u0422\u0410\u0420 (start) | 6080 | 1 word | BitButton | Touch feedback via Pic_On=1 |",
            "| \u0421\u0422\u041e\u041f (stop) | 6082 | 1 word | BitButton | Touch feedback via Pic_On=2 |",
            "",
            "## f13Type",
            "",
            "| BitButton | 16 |",
            "",
            "## BitButton picture indices",
            "",
            "Released on **`00.bmp`**. **`01.bmp`**: \u0421\u0422\u0410\u0420 pressed; **`02.bmp`**: \u0421\u0422\u041e\u041f pressed. **`[IMG]`**: `00.bmp`, `01.bmp`, `02.bmp`. **Pic_Id=0**, **Pic_On=1** / **2**.",
            "",
            "## ICL",
            "",
            "Pack with **`scripts/pack_dwin_set_screen_to_icl.py --project ...`**. **`BuildFromDesign.ps1`** runs **`sync_test_project_2_t5lcfg.py`** (same T5L preset). Regenerate **13 / 14 / 22** in DGUS after edits.",
            ""
        };
        var lay = TestProjectStarStopLayoutFile.LoadOrDefault(projectDir);
        lines.Add("## Geometry (`controls`)");
        lines.Add("");
        lines.Add("| key | X | Y | W | H |");
        lines.Add("|-----|--:|--:|--:|--:|");
        foreach (var key in new[] { "btn_star", "btn_stop" })
        {
            var rr = lay.Rect(key);
            lines.Add("| `" + key + "` | " + rr.X + " | " + rr.Y + " | " + rr.Width + " | " + rr.Height + " |");
        }

        lines.Add("");
        File.WriteAllLines(path, lines);
        Console.WriteLine("Wrote " + path);
    }

    /// <summary>Rects from <c>design/layout.json</c> (same file as Python background renderer).</summary>
    private static bool TryInitTestProjectMasterLayout(Document doc, int docWidth, int docHeight, ushort spBase, string projectDir)
    {
        ClearAllDrawObjects(doc);
        var lay = TestProjectLayoutFile.LoadOrDefault(projectDir);
        if (lay.Screen.Width != docWidth || lay.Screen.Height != docHeight)
            Console.WriteLine(
                "WARNING: design/layout.json screen " + lay.Screen.Width + "x" + lay.Screen.Height + " != document " +
                docWidth + "x" + docHeight + " — controls still placed from JSON.");
        var screen = new Point(docWidth, docHeight);
        ushort spA = (ushort)(spBase + 0x100);
        ushort spB = (ushort)(spBase + 0x140);
        AppendNumberLinePair(doc, screen, lay.Rect("kanalA_input"), lay.Rect("kanalA_display"),
            VpTestKanalA, spA, "\u041a\u0430\u043d\u0430\u043b\u0410_\u0432\u0432\u043e\u0434", "\u041a\u0430\u043d\u0430\u043b\u0410_\u043e\u0442\u043e\u0431\u0440");
        AppendNumberLinePair(doc, screen, lay.Rect("kanalB_input"), lay.Rect("kanalB_display"),
            VpTestKanalB, spB, "\u041a\u0430\u043d\u0430\u043b\u0411_\u0432\u0432\u043e\u0434", "\u041a\u0430\u043d\u0430\u043b\u0411_\u043e\u0442\u043e\u0431\u0440", nInt: 3, nDot: 1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("btnB_minus"), VpTestBminus, "\u0411_\u043c\u0438\u043d\u0443\u0441", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("btnB_plus"), VpTestBplus, "\u0411_\u043f\u043b\u044e\u0441", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("actionA"), VpTestActA, "\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u0435\u0410", 0, 0, -1);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("actionB"), VpTestActB, "\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u0435\u0411", 0, 0, -1);
        EnsureDataVariableDisplayF13(doc);
        EnsureBitButtonF13(doc);
        doc.SetDirtyFlag(true);
        return true;
    }

    /// <summary>Single-digit counter + two BitButtons; layout from <c>TestProject2LayoutFile</c>.</summary>
    private static bool TryInitTestProject2Counter(Document doc, int docWidth, int docHeight, ushort spBase, string projectDir)
    {
        ClearAllDrawObjects(doc);
        var lay = TestProject2LayoutFile.LoadOrDefault(projectDir);
        if (lay.Screen.Width != docWidth || lay.Screen.Height != docHeight)
            Console.WriteLine(
                "WARNING: design/layout.json screen " + lay.Screen.Width + "x" + lay.Screen.Height + " != document " +
                docWidth + "x" + docHeight + " — controls still placed from JSON.");
        var screen = new Point(docWidth, docHeight);
        ushort spDisp = (ushort)(spBase + 0x100);
        AppendDataTextShowOnly(doc, screen, lay.Rect("counter_display"), VpCounter2Value, spDisp, "Counter_value", nInt: 1, fontSize: 80, align: 1);

        string dwinSet = Path.Combine(projectDir, "DWIN_SET");
        bool gfx = File.Exists(Path.Combine(dwinSet, "01.bmp")) &&
                   File.Exists(Path.Combine(dwinSet, "02.bmp"));
        if (gfx)
            Console.WriteLine(
                "Counter page: released buttons on 00.bmp; BitButtons Pic_Id=0 / Pic_On=1 (minus) and 2 (plus). [IMG]: 00,01,02 — see VP_MEMORY_MAP.md.");

        short mPic = 0;
        short mOn = gfx ? (short)1 : (short)-1;
        short pPic = 0;
        short pOn = gfx ? (short)2 : (short)-1;
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("btn_minus"), VpCounter2Minus, "Btn_minus", 0, mPic, mOn);
        AppendBitButtonForCoolPanel(doc, screen, lay.Rect("btn_plus"), VpCounter2Plus, "Btn_plus", 0, pPic, pOn);
        EnsureDataVariableDisplayF13(doc);
        EnsureBitButtonF13(doc);
        doc.SetDirtyFlag(true);
        return true;
    }

    /// <summary>
    /// TEST_PROJECT_3: <see cref="ArtTextShow"/> (artistic variable, icons in <c>24.icl</c>) + two <see cref="IncManager"/>
    /// incremental-adjust touches on VP <c>0x6030</c> (Adj_Mode 0 = minus, 1 = plus). No MCU required for 0–9.
    /// </summary>
    private static bool TryInitTestProject3CounterIncAdjust(Document doc, int docWidth, int docHeight, ushort spBase, string projectDir)
    {
        ClearAllDrawObjects(doc);
        var lay = TestProject2LayoutFile.LoadOrDefault(projectDir);
        if (lay.Screen.Width != docWidth || lay.Screen.Height != docHeight)
            Console.WriteLine(
                "WARNING: design/layout.json screen " + lay.Screen.Width + "x" + lay.Screen.Height + " != document " +
                docWidth + "x" + docHeight + " — controls still placed from JSON.");
        var screen = new Point(docWidth, docHeight);
        ushort spDisp = (ushort)(spBase + 0x100);
        AppendArtTextShowCounter(doc, screen, lay.Rect("counter_display"), VpCounter2Value, spDisp, "Counter_art");

        string dwinSet = Path.Combine(projectDir, "DWIN_SET");
        bool gfx = File.Exists(Path.Combine(dwinSet, "01.bmp")) &&
                   File.Exists(Path.Combine(dwinSet, "02.bmp"));
        bool artIcl = File.Exists(Path.Combine(dwinSet, "24.icl"));
        if (gfx)
            Console.WriteLine(
                "TEST_PROJECT_3: IncManager adjusts VP 6030; Pic_Id=0 / Pic_On=1 (DEC) and 2 (INC). [IMG]: 00,01,02 — see VP_MEMORY_MAP.md.");
        if (!artIcl)
            Console.WriteLine(
                "WARNING: DWIN_SET\\24.icl missing — run scripts\\gen_test_project_3_art_digits.py and pack_test_project_3_digit_icl.py (BuildFromDesign.ps1) so ArtTextShow digit icons load.");

        short mPic = 0;
        short mOn = gfx ? (short)1 : (short)-1;
        short pPic = 0;
        short pOn = gfx ? (short)2 : (short)-1;
        AppendIncAdjustForCounter(doc, screen, lay.Rect("btn_minus"), VpCounter2Value, "Inc_minus", adjMode: 0, mPic, mOn);
        AppendIncAdjustForCounter(doc, screen, lay.Rect("btn_plus"), VpCounter2Value, "Inc_plus", adjMode: 1, pPic, pOn);
        EnsureArtTextShowF13(doc);
        EnsureIncManagerF13(doc);
        doc.SetDirtyFlag(true);
        return true;
    }

    private static void AppendIncAdjustForCounter(
        Document doc,
        Point screen,
        Rectangle rect,
        ushort targetVp,
        string name,
        byte adjMode,
        short picId,
        short picOn)
    {
        var inc = new IncManager();
        inc.Var_Name = name;
        inc.VarStrPoint = targetVp.ToString("X4", CultureInfo.InvariantCulture);
        inc.VP_Mode = 0;
        inc.Adj_Mode = adjMode;
        inc.Return_Mode = 0;
        inc.Adj_Step = 1;
        inc.V_Min = 0;
        inc.V_Max = 9;
        inc.Key_Mode = 0;
        inc.Key_Delay = 0;
        inc.FEorFD = true;
        inc.Pic_Id = picId;
        inc.Pic_Next = -1;
        inc.Pic_On = picOn;
        var rHost = new DrawRectangle(rect.X, rect.Y, rect.Width, rect.Height, false)
        {
            Rectangle = rect,
            ScreenSize = screen,
            f13Type = F13IncrementalAdjustment,
            ConfigObject = inc
        };
        inc.SetPosition(rect);
        AppendDrawObject(doc, rHost);
    }

    private static void AppendDataTextShowOnly(
        Document doc,
        Point screen,
        Rectangle rectShow,
        ushort vp,
        ushort spDesc,
        string displayName,
        int nInt,
        int fontSize,
        byte align)
    {
        string vpHex = vp.ToString("X4", CultureInfo.InvariantCulture);
        string spHex = spDesc.ToString("X4", CultureInfo.InvariantCulture);
        var show = new DataTextShow
        {
            VarStrPoint = vpHex,
            VarDescAddress = spHex
        };
        ApplyDgusDataTextShowLikeManualQuantityDisplay(show, displayName);
        show.N_Int = (byte)nInt;
        show.N_Dot = 0;
        show.FontSize = (byte)Math.Min(255, Math.Max(8, fontSize));
        show.Align = align;
        var rShow = new DrawRectangle(rectShow.X, rectShow.Y, rectShow.Width, rectShow.Height, false)
        {
            Rectangle = rectShow,
            ScreenSize = screen,
            f13Type = F13DataVariableDisplay,
            ConfigObject = show
        };
        show.SetPosition(rectShow);
        AppendDrawObject(doc, rShow);
    }

    /// <summary>
    /// Artistic variable display: VP holds integer; icons <c>Icon0</c>..<c>Icon0+N</c> in <c>Icon_lib</c> (see <c>24.icl</c>, ids 30–39).
    /// </summary>
    private static void AppendArtTextShowCounter(
        Document doc,
        Point screen,
        Rectangle rectShow,
        ushort vp,
        ushort spDesc,
        string displayName)
    {
        string vpHex = vp.ToString("X4", CultureInfo.InvariantCulture);
        string spHex = spDesc.ToString("X4", CultureInfo.InvariantCulture);
        var art = new ArtTextShow
        {
            Var_Name = displayName,
            VarStrPoint = vpHex,
            VarDescAddress = spHex
        };
        BindArtTextShowIconLibrary(art, 24);
        art.Icon0 = 30;
        art.Icon_Mod = 0;
        art.N_Int = 1;
        art.N_Dot = 0;
        art.VarType = 3;
        art.TxtAlign = 2;
        art.Pic_Id = 0;
        art.isShow0 = true;
        art.BackMode = 0;
        var rShow = new DrawRectangle(rectShow.X, rectShow.Y, rectShow.Width, rectShow.Height, false)
        {
            Rectangle = rectShow,
            ScreenSize = screen,
            f13Type = F13ArtisticVariableDisplay,
            ConfigObject = art
        };
        art.SetPosition(rectShow);
        SyncArtTextDisPositon(art);
        AppendDrawObject(doc, rShow);
    }

    private static void ApplyTestProject2Chrome(Document doc)
    {
        var cyan = Color.FromArgb(255, 100, 180, 220);
        var green = Color.FromArgb(255, 90, 200, 140);
        var dark = Color.FromArgb(255, 20, 24, 32);
        var slate = Color.FromArgb(255, 36, 42, 56);
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r)
                continue;
            var nm = r.ConfigObject switch
            {
                DataTextShow dt => dt.Var_Name,
                InputBase ib when IsBitButtonConfig(ib) => ib.Var_Name,
                _ => ""
            };
            if (nm.Length == 0)
                continue;
            if (r.ConfigObject is InputBase ibBtn && IsBitButtonConfig(ibBtn) && ibBtn.Pic_Id != 0)
                continue;
            if (r.ConfigObject is InputBase ibBtn2 && IsBitButtonConfig(ibBtn2) && ibBtn2.Pic_On > (short)0)
                continue;
            r.DisMode = DrawRectangle.TDismode.Backcolor;
            if (string.Equals(nm, "Counter_value", StringComparison.Ordinal))
            {
                r.BColor = dark;
                r.Color = cyan;
                r.PenWidth = 2;
                continue;
            }

            if (string.Equals(nm, "Btn_minus", StringComparison.Ordinal))
            {
                r.BColor = slate;
                r.Color = cyan;
                r.PenWidth = 2;
                continue;
            }

            if (string.Equals(nm, "Btn_plus", StringComparison.Ordinal))
            {
                r.BColor = slate;
                r.Color = green;
                r.PenWidth = 2;
            }
        }
    }

    private static void ApplyTestProject3Chrome(Document doc)
    {
        var amberLine = Color.FromArgb(255, 230, 170, 90);
        var mintLine = Color.FromArgb(255, 110, 210, 170);
        var dark = Color.FromArgb(255, 12, 10, 14);
        var slate = Color.FromArgb(255, 32, 24, 28);
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r)
                continue;
            var nm = r.ConfigObject switch
            {
                ArtTextShow at => at.Var_Name,
                DataTextShow dt => dt.Var_Name,
                IncManager im => im.Var_Name,
                _ => ""
            };
            if (nm.Length == 0)
                continue;
            if (r.ConfigObject is IncManager imPic && imPic.Pic_Id != 0)
                continue;
            if (r.ConfigObject is IncManager imOn && imOn.Pic_On > (short)0)
                continue;
            r.DisMode = DrawRectangle.TDismode.Backcolor;
            if (string.Equals(nm, "Counter_art", StringComparison.Ordinal))
            {
                r.BColor = dark;
                r.Color = amberLine;
                r.PenWidth = 2;
                continue;
            }

            if (string.Equals(nm, "Inc_minus", StringComparison.Ordinal))
            {
                r.BColor = slate;
                r.Color = amberLine;
                r.PenWidth = 2;
                continue;
            }

            if (string.Equals(nm, "Inc_plus", StringComparison.Ordinal))
            {
                r.BColor = slate;
                r.Color = mintLine;
                r.PenWidth = 2;
            }
        }
    }

    private static void WriteTestProject2VpMap(string projectDir, ushort spBase, int width, int height)
    {
        string path = Path.Combine(projectDir, "VP_MEMORY_MAP.md");
        var spDisp = (ushort)(spBase + 0x100);
        var lines = new List<string>
        {
            "# VP / SP — TEST_PROJECT_2 (0–9 counter)",
            "",
            "TFT layout from **`design/layout.json`**, tool: **`GenerateTestTft --init-test-project-2-counter`**. **`00.bmp`**: released buttons + chrome (`scripts/render_test_project_2_counter.py`). **`01.bmp` / `02.bmp`**: **full-screen** Pic_On images (`scripts/gen_test_project_2_button_bmps.py` — one button pressed, same coords as `00.bmp`).",
            "",
            "**Resolution** " + width + "\u00d7" + height + ", **SPADDRESS** 0x" + spBase.ToString("X4", CultureInfo.InvariantCulture) + ".",
            "",
            "## VP",
            "",
            "| Role | VP hex | Length | Widget | Notes |",
            "|--------|--------|--------|--------|--------|",
            "| Counter value (0–9) | 6030 | 1 word | DataTextShow | `N_Int=1`. MCU writes/clamps 0..9. |",
            "| Minus | 6070 | 1 word | BitButton | MCU decrements 6030 on touch. |",
            "| Plus | 6072 | 1 word | BitButton | MCU increments 6030 on touch. |",
            "",
            "## SP (DataTextShow)",
            "",
            "| Use | SP hex |",
            "|--------|--------|",
            "| Counter display format | " + spDisp.ToString("X4", CultureInfo.InvariantCulture) + " |",
            "",
            "## f13Type",
            "",
            "| DataTextShow | 106 |",
            "| BitButton | 16 |",
            "",
            "## BitButton picture indices",
            "",
            "Released look is **painted on `00.bmp`**. **`01.bmp`** / **`02.bmp`** are **full-screen** copies with **minus** / **plus** pressed in the touch rects. **`[IMG]`** order: `00.bmp`, `01.bmp`, `02.bmp`. BitButtons: **Pic_Id=0** / **Pic_On=1** (minus) and **Pic_On=2** (plus).",
            "",
            "## ICL (icon libraries)",
            "",
            "Build **`DWIN_SET\\\\23.icl`** from this project only: **`scripts\\\\pack_dwin_set_screen_to_icl.py`** packs **`00.bmp`–`02.bmp`** (JPEG inside DGUS_3 container). Flash slot **23** matches typical **`T5LCFG.CFG`** byte **0x08** (value **0x17** = 23). Icon ids **0..2** match filenames. Use **`BuildFromDesign.ps1`**. **`scripts\\\\sync_test_project_2_t5lcfg.py`** reapplies the manual **800×480** display row. After layout changes, **DGUS → Generate** must refresh **`13TouchFile.bin` / `14ShowFile.bin` / `22_Config.bin`** (see **`DGUS_GENERATE_FOR_DOWNLOAD.txt`**). For variable-icon / animation controls, set **Icon_lib** to **23** in DwinTerminal if you point them at this library.",
            "",
            "## Touch",
            "",
            "`13TouchFile.bin` here is a template — **rebuild touch keys in DwinTerminal** for final hardware.",
            ""
        };
        var lay = TestProject2LayoutFile.LoadOrDefault(projectDir);
        lines.Add("## Geometry (`controls`)");
        lines.Add("");
        lines.Add("| key | X | Y | W | H |");
        lines.Add("|-----|--:|--:|--:|--:|");
        foreach (var key in new[] { "counter_display", "btn_minus", "btn_plus" })
        {
            var rr = lay.Rect(key);
            lines.Add("| `" + key + "` | " + rr.X + " | " + rr.Y + " | " + rr.Width + " | " + rr.Height + " |");
        }

        lines.Add("");
        string layoutJsonPath = Path.Combine(projectDir, "design", "layout.json");
        if (File.Exists(layoutJsonPath))
        {
            try
            {
                var jo = JObject.Parse(File.ReadAllText(layoutJsonPath));
                if (jo["decor"] is JObject dec)
                {
                    lines.Add("## decor (bitmap-only — `render_test_project_2_counter.py`)");
                    lines.Add("");
                    lines.Add("| key | X | Y | W | H |");
                    lines.Add("|-----|--:|--:|--:|--:|");
                    foreach (var prop in dec.Properties())
                    {
                        if (prop.Value is not JObject o)
                            continue;
                        int x = o["x"]?.Value<int>() ?? 0;
                        int y = o["y"]?.Value<int>() ?? 0;
                        int ww = o["w"]?.Value<int>() ?? 0;
                        int hh = o["h"]?.Value<int>() ?? 0;
                        lines.Add("| `" + prop.Name + "` | " + x + " | " + y + " | " + ww + " | " + hh + " |");
                    }

                    lines.Add("");
                }
            }
            catch
            {
                /* ignore */
            }
        }

        File.WriteAllLines(path, lines);
        Console.WriteLine("Wrote " + path);
    }

    private static void WriteTestProject3VpMap(string projectDir, ushort spBase, int width, int height)
    {
        string path = Path.Combine(projectDir, "VP_MEMORY_MAP.md");
        var spDisp = (ushort)(spBase + 0x100);
        var lines = new List<string>
        {
            "# VP / SP — TEST_PROJECT_3 (artistic variable + incremental adjustment)",
            "",
            "**Display:** **`ArtTextShow`** (`f13Type=103`) on VP **`0x6030`**, **N_Int=1**, **`ICOFileName=24.icl`** / **Icon_lib=24**, **Icon0=30** (icons **30–39** = digits **0–9** in **`24.icl`**). DwinTerminal **Icon File** reads **`ShowBase.ICOFileName`**, not **`Icon_lib`** alone — the generator sets both. Generate icons with **`scripts/gen_test_project_3_art_digits.py`** and pack with **`scripts/pack_test_project_3_digit_icl.py`** (also run from **`BuildFromDesign.ps1`**).",
            "",
            "**Touch:** two **`IncManager`** incremental-adjust controls (`f13Type=3`), both targeting VP **`0x6030`**: **Adj_Mode 0** = DEC, **Adj_Mode 1** = INC; **Adj_Step=1**, **V_Min=0**, **V_Max=9**. **Pic_On** uses full-screen **`01.bmp` / `02.bmp`** (see **`gen_test_project_3_button_bmps.py`**).",
            "",
            "**Resolution** " + width + "\u00d7" + height + ", **SPADDRESS** 0x" + spBase.ToString("X4", CultureInfo.InvariantCulture) + ".",
            "",
            "## VP",
            "",
            "| Role | VP hex | Widget | Notes |",
            "|--------|--------|--------|--------|",
            "| Counter digit 0–9 | 6030 | ArtTextShow + 2× IncManager | Single **word**; artistic display maps value to icon **Icon0 + value** in **24.icl**; IncManager adjusts same VP on touch. |",
            "",
            "## SP (description pointer for ArtTextShow)",
            "",
            "| Use | SP hex |",
            "|--------|--------|",
            "| Artistic / format block | " + spDisp.ToString("X4", CultureInfo.InvariantCulture) + " |",
            "",
            "## f13Type",
            "",
            "| ArtTextShow (艺术字变量) | 103 |",
            "| Incremental Adjustment | 3 |",
            "",
            "## ICL files in DWIN_SET",
            "",
            "| File | Role |",
            "|------|------|",
            "| **`23.icl`** | Page backgrounds **`00.bmp`–`02.bmp`** (icons 0–2), **`pack_dwin_set_screen_to_icl.py`**. |",
            "| **`24.icl`** | Digit glyphs **30.png–39.png** (icon ids **30–39**), **`pack_test_project_3_digit_icl.py`**. |",
            "",
            "## Picture indices (incremental-adjust skins)",
            "",
            "Released on **`00.bmp`**; **`01.bmp`** / **`02.bmp`** full-screen pressed DEC / INC. **`[IMG]`**: `00.bmp`, `01.bmp`, `02.bmp`. **Pic_Id=0**, **Pic_On=1** (DEC), **Pic_On=2** (INC).",
            "",
            "## After editing TFT or layout",
            "",
            "Open **`DWprj.hmi`** in DGUS → **Save** → **Generate** so **`13TouchFile.bin`**, **`14ShowFile.bin`**, **`22_Config.bin`** match the current controls.",
            "",
            "## Touch",
            "",
            "Regenerate **13** in DGUS after moving controls. Template **13** copied from **TEST_PROJECT** is not valid until you **Generate** from this **`.hmi`**.",
            ""
        };
        var lay = TestProject2LayoutFile.LoadOrDefault(projectDir);
        lines.Add("## Geometry (`controls`)");
        lines.Add("");
        lines.Add("| key | X | Y | W | H |");
        lines.Add("|-----|--:|--:|--:|--:|");
        foreach (var key in new[] { "counter_display", "btn_minus", "btn_plus" })
        {
            var rr = lay.Rect(key);
            lines.Add("| `" + key + "` | " + rr.X + " | " + rr.Y + " | " + rr.Width + " | " + rr.Height + " |");
        }

        lines.Add("");
        string layoutJsonPath = Path.Combine(projectDir, "design", "layout.json");
        if (File.Exists(layoutJsonPath))
        {
            try
            {
                var jo = JObject.Parse(File.ReadAllText(layoutJsonPath));
                if (jo["decor"] is JObject dec)
                {
                    lines.Add("## decor (bitmap-only — `render_test_project_3_counter.py`)");
                    lines.Add("");
                    lines.Add("| key | X | Y | W | H |");
                    lines.Add("|-----|--:|--:|--:|--:|");
                    foreach (var prop in dec.Properties())
                    {
                        if (prop.Value is not JObject o)
                            continue;
                        int x = o["x"]?.Value<int>() ?? 0;
                        int y = o["y"]?.Value<int>() ?? 0;
                        int ww = o["w"]?.Value<int>() ?? 0;
                        int hh = o["h"]?.Value<int>() ?? 0;
                        lines.Add("| `" + prop.Name + "` | " + x + " | " + y + " | " + ww + " | " + hh + " |");
                    }

                    lines.Add("");
                }
            }
            catch
            {
                /* ignore */
            }
        }

        File.WriteAllLines(path, lines);
        Console.WriteLine("Wrote " + path);
    }

    private static void ApplyTestProjectMasterChrome(Document doc)
    {
        var cyan = Color.FromArgb(255, 78, 205, 196);
        var amber = Color.FromArgb(255, 244, 162, 96);
        var dark = Color.FromArgb(255, 34, 38, 52);
        var slate = Color.FromArgb(255, 40, 46, 60);
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r)
                continue;
            var nm = r.ConfigObject switch
            {
                VarInput vi => vi.Var_Name,
                DataTextShow dt => dt.Var_Name,
                InputBase ib when IsBitButtonConfig(ib) => ib.Var_Name,
                _ => ""
            };
            if (nm.Length == 0)
                continue;
            if (r.ConfigObject is InputBase ibBtn && IsBitButtonConfig(ibBtn) && ibBtn.Pic_Id != 0)
                continue;
            r.DisMode = DrawRectangle.TDismode.Backcolor;
            if (nm.IndexOf("\u0411_\u043c", StringComparison.Ordinal) >= 0 || nm.IndexOf("\u0411_\u043f", StringComparison.Ordinal) >= 0)
            {
                r.BColor = Color.FromArgb(255, 48, 52, 68);
                r.Color = slate;
                r.PenWidth = 1;
                continue;
            }

            if (nm.IndexOf("\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u0435\u0410", StringComparison.Ordinal) >= 0)
            {
                r.BColor = Color.FromArgb(255, 26, 32, 44);
                r.Color = cyan;
                r.PenWidth = 3;
                continue;
            }

            if (nm.IndexOf("\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u0435\u0411", StringComparison.Ordinal) >= 0)
            {
                r.BColor = dark;
                r.Color = amber;
                r.PenWidth = 2;
                continue;
            }

            if (nm.IndexOf("\u041a\u0430\u043d\u0430\u043b\u0410", StringComparison.Ordinal) >= 0)
            {
                r.BColor = dark;
                r.Color = cyan;
                r.PenWidth = 2;
                continue;
            }

            if (nm.IndexOf("\u041a\u0430\u043d\u0430\u043b\u0411", StringComparison.Ordinal) >= 0)
            {
                r.BColor = dark;
                r.Color = amber;
                r.PenWidth = 2;
                continue;
            }
        }
    }

    private static void WriteTestProjectMasterVpMap(string projectDir, ushort spBase, int width, int height)
    {
        string path = Path.Combine(projectDir, "VP_MEMORY_MAP.md");
        var spA = (ushort)(spBase + 0x100);
        var spB = (ushort)(spBase + 0x140);
        var lines = new List<string>
        {
            "# VP / SP — TEST_PROJECT (master screen)",
            "",
            "\u041a\u043e\u043e\u0440\u0434\u0438\u043d\u0430\u0442\u044b \u0438 \u0440\u0430\u0437\u043c\u0435\u0440\u044b \u0432\u0438\u0434\u0436\u0435\u0442\u043e\u0432: **`design\\\\layout.json`** (\u043d\u0435\u0439\u0440\u043e\u0441\u0435\u0442\u044c + `validate_test_project_layout.py`). \u0424\u043e\u043d 00.bmp: **`neural_master_png_to_00_bmp.py`**. \u0420\u0430\u0437\u043c\u0435\u0442\u043a\u0430 TFT: **`--init-test-project-master`**.",
            "",
            "**\u0420\u0430\u0437\u043c\u0435\u0440** " + width + "\u00d7" + height + ", **SPADDRESS** 0x" + spBase.ToString("X4", CultureInfo.InvariantCulture) + ".",
            "",
            "## VP",
            "",
            "|\u041b\u043e\u0433\u0438\u043a\u0430 | VP hex | \u0414\u043b\u0438\u043d\u0430 | \u0412\u0438\u0434\u0436\u0435\u0442 | \u0417\u0430\u043c\u0435\u0442\u043a\u0438 |",
            "|--------|--------|--------|--------|--------|",
            "|\u041a\u0430\u043d\u0430\u043b A \u0437\u043d\u0430\u0447\u0435\u043d\u0438\u0435 | 6030 | 1 \u0441\u043b\u043e\u0432\u043e | VarInput + DataTextShow | \u0426\u0435\u043b\u044b\u0435, N_Int=2 |",
            "|\u0423\u0441\u0442\u0430\u0432\u043a\u0430 B (\u0442\u0435\u043c\u043f.) | 6034 | 1 \u0441\u043b\u043e\u0432\u043e | VarInput + DataTextShow | N_Int=3, N_Dot=1 (\u043f\u0440\u0438\u043c\u0435\u0440 0935 \u2192 93,5) |",
            "|\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u0435 A | 6060 | 1 | BitButton | \u0411\u0438\u0442 0 |",
            "|\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u0435 B | 6064 | 1 | BitButton | \u0411\u0438\u0442 0 |",
            "|\u0411 \u043c\u0438\u043d\u0443\u0441 | 6070 | 1 | BitButton | MCU: \u0443\u043c\u0435\u043d\u044c\u0448\u0438\u0442\u044c 6034 / \u0438\u043c\u043f\u0443\u043b\u044c\u0441 |",
            "|\u0411 \u043f\u043b\u044e\u0441 | 6072 | 1 | BitButton | MCU: \u0443\u0432\u0435\u043b\u0438\u0447\u0438\u0442\u044c 6034 / \u0438\u043c\u043f\u0443\u043b\u044c\u0441 |",
            "",
            "## SP (\u0442\u043e\u043b\u044c\u043a\u043e DataTextShow)",
            "",
            "|\u041a\u0430\u043d\u0430\u043b | SP hex |",
            "|--------|--------|",
            "|A \u043e\u0442\u043e\u0431\u0440\u0430\u0436\u0435\u043d\u0438\u0435 | " + spA.ToString("X4", CultureInfo.InvariantCulture) + " |",
            "|B \u043e\u0442\u043e\u0431\u0440\u0430\u0436\u0435\u043d\u0438\u0435 | " + spB.ToString("X4", CultureInfo.InvariantCulture) + " |",
            "",
            "## f13Type",
            "",
            "|VarInput|1|",
            "|DataTextShow (\u043a\u043e\u043b\u0438\u0447\u0435\u0441\u0442\u0432\u043e)|106|",
            "|BitButton|16|",
            ""
        };
        var lay = TestProjectLayoutFile.LoadOrDefault(projectDir);
        lines.Add("## \u0413\u0435\u043e\u043c\u0435\u0442\u0440\u0438\u044f (\u0438\u0437 design/layout.json)");
        lines.Add("");
        lines.Add("| key | X | Y | W | H |");
        lines.Add("|-----|--:|--:|--:|--:|");
        foreach (var key in new[]
                 {
                     "kanalA_input", "kanalA_display", "kanalB_input", "kanalB_display", "btnB_minus", "btnB_plus",
                     "actionA", "actionB"
                 })
        {
            var rr = lay.Rect(key);
            lines.Add("| `" + key + "` | " + rr.X + " | " + rr.Y + " | " + rr.Width + " | " + rr.Height + " |");
        }

        lines.Add("");
        string layoutJsonPath = Path.Combine(projectDir, "design", "layout.json");
        if (File.Exists(layoutJsonPath))
        {
            try
            {
                var jo = JObject.Parse(File.ReadAllText(layoutJsonPath));
                if (jo["decor"] is JObject dec)
                {
                    lines.Add("## decor (`design/layout.json`)");
                    lines.Add("");
                    lines.Add("| key | X | Y | W | H |");
                    lines.Add("|-----|--:|--:|--:|--:|");
                    foreach (var prop in dec.Properties())
                    {
                        if (prop.Value is not JObject o)
                            continue;
                        int x = o["x"]?.Value<int>() ?? 0;
                        int y = o["y"]?.Value<int>() ?? 0;
                        int ww = o["w"]?.Value<int>() ?? 0;
                        int hh = o["h"]?.Value<int>() ?? 0;
                        lines.Add("| `" + prop.Name + "` | " + x + " | " + y + " | " + ww + " | " + hh + " |");
                    }

                    lines.Add("");
                }
            }
            catch
            {
                /* ignore decor parse */
            }
        }

        File.WriteAllLines(path, lines);
        Console.WriteLine("Wrote " + path);
    }

    private static void AppendNumberLinePair(
        Document doc,
        Point screen,
        Rectangle rectIn,
        Rectangle rectShow,
        ushort vp,
        ushort spDesc,
        string inputName,
        string displayName,
        int nInt = 2,
        int nDot = 0)
    {
        string vpHex = vp.ToString("X4", CultureInfo.InvariantCulture);
        string spHex = spDesc.ToString("X4", CultureInfo.InvariantCulture);
        var vi = new VarInput
        {
            VarStrPoint = vpHex,
            Var_Name = inputName,
            N_Int = (byte)nInt,
            N_Dot = (byte)nDot,
            Var_Type = 0,
            FEorFD = true,
            Lib_Id = 0,
            Font_Hor = 16
        };
        vi.TextColor = "F800";
        var rIn = new DrawRectangle(rectIn.X, rectIn.Y, rectIn.Width, rectIn.Height, false)
        {
            Rectangle = rectIn,
            ScreenSize = screen,
            f13Type = F13VarDataInput,
            ConfigObject = vi
        };
        vi.Pic_Id = 0;
        vi.SetPosition(rectIn);

        var show = new DataTextShow
        {
            VarStrPoint = vpHex,
            VarDescAddress = spHex
        };
        ApplyDgusDataTextShowLikeManualQuantityDisplay(show, displayName);
        if (nDot > 0)
        {
            show.N_Int = (byte)nInt;
            show.N_Dot = (byte)nDot;
        }

        var rShow = new DrawRectangle(rectShow.X, rectShow.Y, rectShow.Width, rectShow.Height, false)
        {
            Rectangle = rectShow,
            ScreenSize = screen,
            f13Type = F13DataVariableDisplay,
            ConfigObject = show
        };
        show.SetPosition(rectShow);

        AppendDrawObject(doc, rIn);
        AppendDrawObject(doc, rShow);
    }

    private static void AppendBitButtonForCoolPanel(
        Document doc,
        Point screen,
        Rectangle rect,
        ushort vp,
        string name,
        byte bitPos,
        short picId,
        short picOn,
        short picNext = -1)
    {
        string vpHex = vp.ToString("X4", CultureInfo.InvariantCulture);
        InputBase bit = CreateBitButtonInput();
        bit.Var_Name = name;
        bit.VarStrPoint = vpHex;
        bit.Pic_Id = picId;
        bit.Pic_Next = picNext;
        bit.Pic_On = picOn;
        // Adj_Mode 3 = inching: press→1, release→0 (always uploads). Mode 1 sticks at 1 → no re-upload.
        SetBitButtonFields(bit, bitPos, 3);
        var rHost = new DrawRectangle(rect.X, rect.Y, rect.Width, rect.Height, false)
        {
            Rectangle = rect,
            ScreenSize = screen,
            f13Type = F13BitButton,
            ConfigObject = bit
        };
        bit.SetPosition(rect);
        AppendDrawObject(doc, rHost);
    }

    private static void ApplyCoolDualPanelChrome(Document doc)
    {
        var accent = Color.FromArgb(255, 130, 180, 255);
        var dark = Color.FromArgb(255, 34, 38, 52);
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r)
                continue;
            var nm = r.ConfigObject switch
            {
                VarInput vi => vi.Var_Name,
                DataTextShow dt => dt.Var_Name,
                InputBase ib when IsBitButtonConfig(ib) => ib.Var_Name,
                _ => ""
            };
            if (nm != "Line1Input" && nm != "Line2Input" && nm != "Line1Display" && nm != "Line2Display" && nm != "BtnA" &&
                nm != "BtnB")
                continue;
            if (r.ConfigObject is InputBase ibBtn && IsBitButtonConfig(ibBtn) && ibBtn.Pic_Id != 0)
                continue;
            r.DisMode = DrawRectangle.TDismode.Backcolor;
            r.BColor = dark;
            r.Color = accent;
            r.PenWidth = 2;
        }
    }

    private static void WriteVpMemoryMap(string projectDir, ushort spBase, int width, int height)
    {
        string path = Path.Combine(projectDir, "VP_MEMORY_MAP.md");
        var sp1 = (ushort)(spBase + 0x100);
        var sp2 = (ushort)(spBase + 0x140);
        var lines = new List<string>
        {
            "# VP / SP memory map — cool dual-panel",
            "",
            "Generated by `GenerateTestTft` **`--init-cool-dual-panel`**. Screen **" + width + "×" + height + "**, HMI **SPADDRESS=0x" +
            spBase.ToString("X4", CultureInfo.InvariantCulture) + "**.",
            "",
            "## VP table (DGUS variable pointers, 16-bit words)",
            "",
            "| Logical name | VP (hex) | VP (dec) | Word length | Host control | Programming notes |",
            "|----------------|----------|----------|---------------|--------------|-------------------|",
            "| **ValueLine1** | `6030` | " + ToDecHex(0x6030) + " | 1 word (16-bit) | `VarInput` + `DataTextShow` | Integer entry (`Var_Type=0`, `N_Int=2`, `N_Dot=0`). Touch field writes value; display reads same VP. |",
            "| **ValueLine2** | `6034` | " + ToDecHex(0x6034) + " | 1 word | `VarInput` + `DataTextShow` | Same format as line 1; separate value. |",
            "| **BtnA_word** | `6060` | " + ToDecHex(0x6060) + " | 1 word | `BitButton` (`BtnA`) | BitButton toggles **bit** `Bit_Pos=0` in this VP (adjust mode 0 = set 0 per factory defaults). Use DGUS write-bit or read-modify-write in MCU. |",
            "| **BtnB_word** | `6064` | " + ToDecHex(0x6064) + " | 1 word | `BitButton` (`BtnB`) | Same as BtnA on independent VP. |",
            "",
            "## SP (description / format) pointers — `DataTextShow` only",
            "",
            "| Line | SP (hex) | SP (dec) | Used by |",
            "|------|----------|----------|---------|",
            "| Line 1 display | `" + sp1.ToString("X4", CultureInfo.InvariantCulture) + "` | " + ToDecHex(sp1) + " | `Line1Display` `VarDescAddress` |",
            "| Line 2 display | `" + sp2.ToString("X4", CultureInfo.InvariantCulture) + "` | " + ToDecHex(sp2) + " | `Line2Display` `VarDescAddress` |",
            "",
            "## Touch / `f13Type` (DwinTerminal host)",
            "",
            "| Host | `f13Type` |",
            "|------|-----------|",
            "| `VarInput` | **1** |",
            "| `DataTextShow` (quantity) | **106** |",
            "| `BitButton` | **16** |",
            "",
            "## MCU checklist",
            "",
            "1. **Poll or event**: read `0x6030` / `0x6034` for the two numeric values (big-endian word per DGUS serial protocol).",
            "2. **Write values**: same VPs to push display updates when MCU owns the numbers.",
            "3. **Buttons**: after touch, DGUS updates `0x6060` / `0x6064` per BitButton rules — verify on hardware with your `13TouchFile.bin` build.",
            "4. Reserve **no other widgets** on these VPs unless you extend this map and re-run the generator.",
            ""
        };
        string ds = Path.Combine(projectDir, "DWIN_SET");
        if (File.Exists(Path.Combine(ds, "01.bmp")) && File.Exists(Path.Combine(ds, "04.bmp")))
        {
            lines.Add("## BitButton picture graphics (optional)");
            lines.Add("");
            lines.Add("When `DWIN_SET\\01.bmp` … `04.bmp` exist and `[IMG]` lists `00.bmp` then `01`…`04` in order, **BtnA** uses **Pic_Id=1 / Pic_On=2**, **BtnB** uses **Pic_Id=3 / Pic_On=4** (DwinTerminal picture-list indices).");
            lines.Add("");
            lines.Add("Pack the same four BMPs into **`40.icl`** (library id **40**, icon ids **1**–**4** from filenames `01.bmp`…`04.bmp`) for DGUS VP icon APIs or other controls; copy `40.icl` into `DWIN_SET\\`.");
            lines.Add("");
        }

        File.WriteAllLines(path, lines);
        Console.WriteLine("Wrote " + path);
    }

    private static string ToDecHex(ushort v) => v.ToString(CultureInfo.InvariantCulture) + " (0x" + v.ToString("X4", CultureInfo.InvariantCulture) + ")";

    /// <summary>Remove every <see cref="DrawObject"/> from <see cref="Document.Items"/> (blank canvas).</summary>
    private static void ClearAllDrawObjects(Document doc)
    {
        doc.Items.Clear();
        doc.SetDirtyFlag(true);
    }

    /// <summary>Clear the page, then add one centered <see cref="AnimateShow"/> (VP from <see cref="NextFreeVp"/>).</summary>
    private static bool TryResetToCenteredAnimationIcon(Document doc)
    {
        ClearAllDrawObjects(doc);
        return TryAddCenteredAnimationIcon(doc);
    }

    /// <summary>Single <see cref="AnimateShow"/> in the middle of <paramref name="doc"/>; <c>Icon_lib=23</c> (place <c>23.icl</c> in <c>DWIN_SET</c> for preview).</summary>
    private static bool TryAddCenteredAnimationIcon(Document doc)
    {
        const int box = 160;
        int x0 = (doc.Width - box) / 2;
        int y0 = (doc.Height - box) / 2;
        if (x0 < 0)
            x0 = 0;
        if (y0 < 0)
            y0 = 0;
        var rect = new Rectangle(x0, y0, box, box);
        ushort vp = NextFreeVp(doc, 0x6020);
        string vpHex = vp.ToString("X4", CultureInfo.InvariantCulture);

        var anim = new AnimateShow
        {
            VarStrPoint = vpHex,
            VarDescAddress = "FFFF",
            Var_Name = "Animation icon",
            V_Start = 0,
            V_Stop = 1000,
            Icon_Start = 0,
            Icon_End = 15,
            Icon_Stop = 15,
            Icon_lib = 23,
            Mode = 0,
            RestartMode = 0
        };
        var screen = new Point(doc.Width, doc.Height);
        var r = new DrawRectangle(rect.X, rect.Y, rect.Width, rect.Height, false)
        {
            Rectangle = rect,
            ScreenSize = screen,
            f13Type = F13AnimationIconDisplay,
            ConfigObject = anim
        };
        ((ShowBase)anim).Pic_Id = 0;
        anim.SetPosition(rect);
        AppendDrawObject(doc, r);
        EnsureAnimationIconF13(doc);
        doc.SetDirtyFlag(true);
        return true;
    }

    /// <summary>
    /// Snapshot from DwinTerminal manual **数量变量显示** (single <see cref="DataTextShow"/>, host <see cref="DrawRectangle"/> f13=106):
    /// FontSize 16, <see cref="DataTextShow.V_Len"/> 0, <c>N_Int=2</c>, <c>TextColor=F800</c> → <see cref="DataTextShow.Color"/> <c>0xF800</c>, <see cref="ShowBase.VarDescAddress"/> FFFF, <see cref="ShowBase.Sp_Mode"/> true (ctor).
    /// </summary>
    /// <summary>Manual copies in DwinTerminal often reuse the same <see cref="DataTextShow.Var_Point"/>; only the first keeps it, later ones get new VPs.</summary>
    private static bool DedupeDataTextShowVPs(Document doc)
    {
        var taken = new HashSet<ushort>();
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r)
                continue;
            switch (r.ConfigObject)
            {
                case VarInput vi:
                    taken.Add(vi.VarPoint);
                    break;
                case DataTextShow dt:
                    taken.Add(dt.Var_Point);
                    break;
                case IconShow ic:
                    taken.Add(ic.Var_Point);
                    break;
                case SliderShow sl:
                    taken.Add(sl.Var_Point);
                    break;
            }
        }

        var seenDataVp = new HashSet<ushort>();
        var any = false;
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r || r.ConfigObject is not DataTextShow dt)
                continue;
            ushort vp = dt.Var_Point;
            if (seenDataVp.Add(vp))
                continue;
            ushort candidate = 0x6020;
            while (taken.Contains(candidate))
                candidate += 2;
            dt.VarStrPoint = candidate.ToString("X4", CultureInfo.InvariantCulture);
            taken.Add(candidate);
            dt.SetPosition(r.Rectangle);
            any = true;
        }

        if (any)
            doc.SetDirtyFlag(true);
        return any;
    }

    private static void ApplyDgusDataTextShowLikeManualQuantityDisplay(DataTextShow show, string varName)
    {
        show.Var_Name = varName;
        show.N_Int = 2;
        show.N_Dot = 0;
        show.VarType = 0;
        show.Lib_Id = 0;
        show.FontSize = 16;
        show.Align = 0;
        show.V_Len = 0;
        show.String_Uint = "";
        show.Modify = 0;
        show.zeroDisplay = 0;
        show.Pic_Id = 0;
        show.IsShowOnPopMenu = false;
        show.TextColor = "F800";
    }

    /// <summary>Append a second <b>display-only</b> <see cref="DataTextShow"/> next to the first on the page (same rectangle size; new VP; SP <c>FFFF</c>).</summary>
    private static bool TryAddDataTextShowBesideFirst(Document doc, int docWidth, int docHeight)
    {
        DrawRectangle host = null;
        foreach (DrawObject o in doc.Items)
        {
            if (o is DrawRectangle r && r.ConfigObject is DataTextShow)
            {
                host = r;
                break;
            }
        }

        if (host == null)
            return false;

        var dr = host.Rectangle;
        int w = dr.Width;
        int h = dr.Height;
        if (w < 8 || h < 8)
            return false;

        const int gap = 10;
        int x = dr.Right + gap;
        int y = dr.Y + (dr.Height - h) / 2;
        if (x + w > docWidth)
        {
            x = dr.Left - gap - w;
            if (x < 0)
            {
                x = dr.X;
                y = dr.Bottom + gap;
                if (y + h > docHeight)
                    y = Math.Max(0, dr.Y - gap - h);
            }
        }

        x = Math.Max(0, Math.Min(x, docWidth - w));
        y = Math.Max(0, Math.Min(y, docHeight - h));
        var nr = new Rectangle(x, y, w, h);

        ushort vp = NextFreeVp(doc, 0x6020);
        string vpHex = vp.ToString("X4", CultureInfo.InvariantCulture);

        var show = new DataTextShow
        {
            VarStrPoint = vpHex,
            VarDescAddress = "FFFF"
        };
        ApplyDgusDataTextShowLikeManualQuantityDisplay(show, "Data variables");

        var rNew = new DrawRectangle(nr.X, nr.Y, nr.Width, nr.Height, false)
        {
            Rectangle = nr,
            ScreenSize = host.ScreenSize,
            DisMode = host.DisMode,
            Color = host.Color,
            BColor = host.BColor,
            PenWidth = Math.Max(1, host.PenWidth),
            f13Type = F13DataVariableDisplay,
            ConfigObject = show
        };
        show.SetPosition(nr);
        AppendDrawObject(doc, rNew);
        doc.SetDirtyFlag(true);
        return true;
    }

    /// <summary>First display host to attach a slider to: <see cref="DataTextShow"/> if present, else first <see cref="ShowBase"/> (not <see cref="SliderShow"/>) with <c>f13Type≥100</c>.</summary>
    private static DrawRectangle FindFirstSliderAnchorHost(Document doc)
    {
        foreach (DrawObject o in doc.Items)
        {
            if (o is DrawRectangle r && r.ConfigObject is DataTextShow && r.f13Type >= 100)
                return r;
        }

        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r || r.f13Type < 100)
                continue;
            if (r.ConfigObject is SliderShow)
                continue;
            if (r.ConfigObject is ShowBase)
                return r;
        }

        return null;
    }

    /// <summary>Random width/height; place track to the right of <paramref name="dr"/> if it fits, else left, else below/above.</summary>
    private static Rectangle ComputeSliderRectangleBesideData(Rectangle dr, int docWidth, int docHeight, Random rnd, out int sw, out int sh)
    {
        sw = rnd.Next(65, 176);
        sh = rnd.Next(32, 97);
        sw = Math.Min(sw, docWidth - 8);
        sh = Math.Min(sh, docHeight - 8);
        if (sw < 40)
            sw = 40;
        if (sh < 24)
            sh = 24;

        const int gap = 10;
        int x;
        int y;
        if (dr.Right + gap + sw <= docWidth)
        {
            x = dr.Right + gap;
            y = dr.Y + (dr.Height - sh) / 2;
        }
        else if (dr.Left - gap - sw >= 0)
        {
            x = dr.Left - gap - sw;
            y = dr.Y + (dr.Height - sh) / 2;
        }
        else
        {
            x = Math.Min(dr.X, Math.Max(0, docWidth - sw));
            y = dr.Bottom + gap;
            if (y + sh > docHeight)
                y = Math.Max(0, dr.Y - gap - sh);
        }

        x = Math.Max(0, Math.Min(x, docWidth - sw));
        y = Math.Max(0, Math.Min(y, docHeight - sh));
        return new Rectangle(x, y, sw, sh);
    }

    /// <summary>First <see cref="SliderShow"/> track: beside anchor display (<see cref="FindFirstSliderAnchorHost"/>); random size; <see cref="SliderShow.SetPosition"/> for X_Begin/X_End/Y.</summary>
    private static bool TryPlaceSliderBesideDataTextRandom(Document doc, int docWidth, int docHeight)
    {
        DrawRectangle dataHost = FindFirstSliderAnchorHost(doc);
        DrawRectangle sliderHost = null;
        SliderShow slider = null;
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r)
                continue;
            if (sliderHost == null && r.ConfigObject is SliderShow ss)
            {
                sliderHost = r;
                slider = ss;
            }
        }

        if (dataHost == null || sliderHost == null || slider == null)
            return false;

        var rnd = new Random();
        var nr = ComputeSliderRectangleBesideData(dataHost.Rectangle, docWidth, docHeight, rnd, out _, out _);
        sliderHost.Rectangle = nr;
        slider.SetPosition(nr);
        if (sliderHost.f13Type < 100)
            sliderHost.f13Type = F13SliderDisplay;
        doc.SetDirtyFlag(true);
        return true;
    }

    /// <summary>If no <see cref="SliderShow"/> yet, add one beside anchor display (<see cref="FindFirstSliderAnchorHost"/>) with its own VP.</summary>
    private static bool TryAddSliderBesideDataTextRandom(Document doc, int docWidth, int docHeight, ushort spBase)
    {
        foreach (DrawObject o in doc.Items)
        {
            if (o is DrawRectangle r && r.ConfigObject is SliderShow)
                return false;
        }

        DrawRectangle dataHost = FindFirstSliderAnchorHost(doc);
        if (dataHost == null)
            return false;

        var rnd = new Random();
        var nr = ComputeSliderRectangleBesideData(dataHost.Rectangle, docWidth, docHeight, rnd, out _, out _);
        ushort vp = NextFreeVp(doc, 0x6040);
        string vpHex = vp.ToString("X4", CultureInfo.InvariantCulture);
        ushort spDesc = (ushort)(spBase + 0x30);
        if (spDesc == ushort.MaxValue)
            spDesc = (ushort)(spBase + 0x10);

        var ss = new SliderShow
        {
            Var_Name = "SliderVar",
            VarStrPoint = vpHex,
            VarDescAddress = spDesc.ToString("X4", CultureInfo.InvariantCulture),
            Mode = 0,
            VP_Mod = 0,
            V_Begin = 0,
            V_End = 1000,
            Icon_Id = 0,
            X_Adj = 0,
            Icon_Mod = 0
        };
        ((ShowBase)ss).Pic_Id = 0;
        ss.SetPosition(nr);

        var rSlider = new DrawRectangle(nr.X, nr.Y, nr.Width, nr.Height, false)
        {
            Rectangle = nr,
            ScreenSize = dataHost.ScreenSize,
            DisMode = dataHost.DisMode,
            Color = dataHost.Color,
            BColor = dataHost.BColor,
            PenWidth = dataHost.PenWidth,
            f13Type = F13SliderDisplay,
            ConfigObject = ss
        };

        AppendDrawObject(doc, rSlider);
        doc.SetDirtyFlag(true);
        return true;
    }

    /// <summary>Resize each <see cref="DrawRectangle"/> that hosts <see cref="DataTextShow"/>; keeps top-left; updates preview layout.</summary>
    private static bool ApplyDataTextShowRectangleSize(Document doc, int width, int height)
    {
        var any = false;
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r || r.ConfigObject is not DataTextShow dt)
                continue;
            var old = r.Rectangle;
            var nr = new Rectangle(old.X, old.Y, width, height);
            r.Rectangle = nr;
            dt.SetPosition(nr);
            doc.SetDirtyFlag(true);
            any = true;
        }

        return any;
    }

    /// <summary>Add <see cref="VarInput"/> + <see cref="DataTextShow"/> sharing VP if the page has no <see cref="VarInput"/> yet (display-only <see cref="DataTextShow"/> is OK).</summary>
    private static bool TryAddDataVariableDisplay(Document doc, ushort spBase)
    {
        foreach (DrawObject o in doc.Items)
        {
            if (o is DrawRectangle r && r.ConfigObject is VarInput)
                return false;
        }

        ushort vp = NextFreeVp(doc, 0x6020);
        string vpHex = vp.ToString("X4", CultureInfo.InvariantCulture);
        ushort spDesc = (ushort)(spBase + 0x20);
        if (spDesc == ushort.MaxValue)
            spDesc = (ushort)(spBase + 0x10);

        int y = Math.Max(20, doc.Height - 120);
        var rectIn = new System.Drawing.Rectangle(20, y, 100, 36);
        var rectShow = new System.Drawing.Rectangle(130, y, 140, 36);

        var vi = new VarInput
        {
            VarStrPoint = vpHex,
            N_Int = 2,
            N_Dot = 0,
            Var_Type = 0,
            FEorFD = true,
            Lib_Id = 0,
            Font_Hor = 16
        };
        vi.TextColor = "F800";
        var rIn = new DrawRectangle(rectIn.X, rectIn.Y, rectIn.Width, rectIn.Height, false)
        {
            Rectangle = rectIn,
            ScreenSize = new System.Drawing.Point(doc.Width, doc.Height),
            f13Type = F13VarDataInput,
            ConfigObject = vi
        };
        ((InputBase)vi).Pic_Id = 0;
        vi.SetPosition(rectIn);

        var show = new DataTextShow
        {
            VarStrPoint = vpHex,
            VarDescAddress = spDesc.ToString("X4", CultureInfo.InvariantCulture)
        };
        ApplyDgusDataTextShowLikeManualQuantityDisplay(show, "Data variables");
        var rShow = new DrawRectangle(rectShow.X, rectShow.Y, rectShow.Width, rectShow.Height, false)
        {
            Rectangle = rectShow,
            ScreenSize = new System.Drawing.Point(doc.Width, doc.Height),
            f13Type = F13DataVariableDisplay,
            ConfigObject = show
        };
        show.SetPosition(rectShow);

        AppendDrawObject(doc, rIn);
        AppendDrawObject(doc, rShow);
        EnsureDataVariableDisplayF13(doc);
        doc.SetDirtyFlag(true);
        return true;
    }

    /// <summary>Corner <see cref="DataTextShow"/> (<c>VP=0000</c>) + <see cref="SliderShow"/> to its right (random size), appended at end of list.</summary>
    private static bool TryAppendCornerDataTextAndSlider(Document doc, int docWidth, int docHeight, ushort spBase)
    {
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r || r.ConfigObject is not DataTextShow dt)
                continue;
            if (dt.Var_Point == 0)
                return false;
        }

        var dataRect = new Rectangle(227, 87, 100, 100);
        var screen = new Point(docWidth, docHeight);
        var show = new DataTextShow
        {
            VarStrPoint = "0000",
            VarDescAddress = "FFFF"
        };
        ApplyDgusDataTextShowLikeManualQuantityDisplay(show, "Data variables");
        var rData = new DrawRectangle(dataRect.X, dataRect.Y, dataRect.Width, dataRect.Height, false)
        {
            Rectangle = dataRect,
            ScreenSize = screen,
            f13Type = F13DataVariableDisplay,
            ConfigObject = show
        };
        show.SetPosition(dataRect);
        AppendDrawObject(doc, rData);

        var rnd = new Random();
        var nr = ComputeSliderRectangleBesideData(dataRect, docWidth, docHeight, rnd, out _, out _);
        ushort vp = NextFreeVp(doc, 0x6040);
        string vpHex = vp.ToString("X4", CultureInfo.InvariantCulture);
        ushort spDesc = (ushort)(spBase + 0x30);
        if (spDesc == ushort.MaxValue)
            spDesc = (ushort)(spBase + 0x10);

        var ss = new SliderShow
        {
            Var_Name = "SliderVar",
            VarStrPoint = vpHex,
            VarDescAddress = spDesc.ToString("X4", CultureInfo.InvariantCulture),
            Mode = 0,
            VP_Mod = 0,
            V_Begin = 0,
            V_End = 1000,
            Icon_Id = 0,
            X_Adj = 0,
            Icon_Mod = 0
        };
        ((ShowBase)ss).Pic_Id = 0;
        ss.SetPosition(nr);

        var rSlider = new DrawRectangle(nr.X, nr.Y, nr.Width, nr.Height, false)
        {
            Rectangle = nr,
            ScreenSize = screen,
            DisMode = rData.DisMode,
            Color = rData.Color,
            BColor = rData.BColor,
            PenWidth = rData.PenWidth,
            f13Type = F13SliderDisplay,
            ConfigObject = ss
        };
        AppendDrawObject(doc, rSlider);
        doc.SetDirtyFlag(true);
        return true;
    }

    /// <summary>Add <see cref="VarInput"/> + <see cref="DataTextShow"/> centered horizontally and vertically (new VP; SP offset <c>+0x50</c> from <paramref name="spBase"/>).</summary>
    private static bool TryAddCenteredDataVariableDisplay(Document doc, ushort spBase)
    {
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r || r.ConfigObject is not DataTextShow dt)
                continue;
            if (string.Equals(dt.Var_Name, "DataVarMid", StringComparison.Ordinal))
                return false;
        }

        ushort vp = NextFreeVp(doc, 0x6020);
        string vpHex = vp.ToString("X4", CultureInfo.InvariantCulture);
        ushort spDesc = (ushort)(spBase + 0x50);
        if (spDesc == ushort.MaxValue)
            spDesc = (ushort)(spBase + 0x18);

        const int wIn = 100;
        const int hIn = 36;
        const int wShow = 140;
        const int hShow = 36;
        const int gap = 12;
        int groupW = wIn + gap + wShow;
        int x0 = (doc.Width - groupW) / 2;
        int y0 = (doc.Height - Math.Max(hIn, hShow)) / 2;
        if (x0 < 4)
            x0 = 4;
        if (y0 < 4)
            y0 = 4;

        var rectIn = new Rectangle(x0, y0, wIn, hIn);
        var rectShow = new Rectangle(x0 + wIn + gap, y0, wShow, hShow);
        var screen = new Point(doc.Width, doc.Height);

        var vi = new VarInput
        {
            VarStrPoint = vpHex,
            N_Int = 2,
            N_Dot = 0,
            Var_Type = 0,
            FEorFD = true,
            Lib_Id = 0,
            Font_Hor = 16
        };
        vi.TextColor = "F800";
        var rIn = new DrawRectangle(rectIn.X, rectIn.Y, rectIn.Width, rectIn.Height, false)
        {
            Rectangle = rectIn,
            ScreenSize = screen,
            f13Type = F13VarDataInput,
            ConfigObject = vi
        };
        ((InputBase)vi).Pic_Id = 0;
        vi.SetPosition(rectIn);

        var show = new DataTextShow
        {
            VarStrPoint = vpHex,
            VarDescAddress = spDesc.ToString("X4", CultureInfo.InvariantCulture)
        };
        ApplyDgusDataTextShowLikeManualQuantityDisplay(show, "DataVarMid");
        var rShow = new DrawRectangle(rectShow.X, rectShow.Y, rectShow.Width, rectShow.Height, false)
        {
            Rectangle = rectShow,
            ScreenSize = screen,
            f13Type = F13DataVariableDisplay,
            ConfigObject = show
        };
        show.SetPosition(rectShow);

        AppendDrawObject(doc, rIn);
        AppendDrawObject(doc, rShow);
        EnsureDataVariableDisplayF13(doc);
        doc.SetDirtyFlag(true);
        return true;
    }

    private static ushort NextFreeVp(Document doc, ushort start)
    {
        var used = new HashSet<ushort>();
        foreach (DrawObject o in doc.Items)
        {
            if (o is not DrawRectangle r)
                continue;
            switch (r.ConfigObject)
            {
                case VarInput vi:
                    used.Add(vi.VarPoint);
                    break;
                case InputBase ib:
                    if (ushort.TryParse(ib.VarStrPoint, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var vIn))
                        used.Add(vIn);
                    break;
                case DataTextShow dt:
                    used.Add(dt.Var_Point);
                    break;
                case IconShow ic:
                    used.Add(ic.Var_Point);
                    break;
                case SliderShow sl:
                    used.Add(sl.Var_Point);
                    break;
                case AnimateShow an:
                    used.Add(an.Var_Point);
                    break;
            }
        }

        var c = start;
        while (used.Contains(c))
            c += 2;
        return c;
    }

    /// <summary>Read INIT SCREENDSIZE, SPADDRESS (hex), and [IMG] values (picture file names).</summary>
    private static void ReadHmi(string hmiPath, out int width, out int height, out ushort spAddress, out List<string> pictureNames)
    {
        width = 800;
        height = 480;
        spAddress = 0x5000;
        pictureNames = new List<string>();
        var lines = File.ReadAllLines(hmiPath);
        var inImg = false;
        foreach (var raw in lines)
        {
            var line = raw.Trim();
            if (line.Length == 0)
                continue;
            if (line.StartsWith("[", StringComparison.Ordinal))
            {
                inImg = string.Equals(line, "[IMG]", StringComparison.OrdinalIgnoreCase);
                continue;
            }

            if (!inImg)
            {
                if (line.StartsWith("SCREENDSIZE=", StringComparison.OrdinalIgnoreCase))
                {
                    var v = line.Substring("SCREENDSIZE=".Length).Trim();
                    var parts = v.Split('X');
                    if (parts.Length >= 2 &&
                        int.TryParse(parts[0].Trim(), out var w) &&
                        int.TryParse(parts[1].Trim(), out var h))
                    {
                        width = w;
                        height = h;
                    }
                }
                else if (line.StartsWith("SPADDRESS=", StringComparison.OrdinalIgnoreCase))
                {
                    var v = line.Substring("SPADDRESS=".Length).Trim();
                    if (ushort.TryParse(v, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var sp))
                        spAddress = sp;
                    else if (ushort.TryParse(v, NumberStyles.Integer, CultureInfo.InvariantCulture, out sp))
                        spAddress = sp;
                }

                continue;
            }

            var eq = line.IndexOf('=');
            if (eq <= 0)
                continue;
            var val = line.Substring(eq + 1).Trim();
            if (val.Length > 0)
                pictureNames.Add(val);
        }
    }
}
