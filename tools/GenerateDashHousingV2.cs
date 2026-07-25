using System;
using System.IO;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;

// Revision 2: a lower electronics tray with an angled display bulkhead, plus
// a separate removable sloped cover. SolidWorks API dimensions are metres.
internal static class GenerateDashHousingV2
{
    private const double Mm = 0.001;

    private static void Log(string message)
    {
        Console.WriteLine(message);
        Console.Out.Flush();
    }

    private static void Check(bool ok, string operation)
    {
        if (!ok) throw new Exception("SolidWorks operation failed: " + operation);
    }

    private static Feature FindFeature(ModelDoc2 model, string name)
    {
        Feature feature = model.IFirstFeature();
        while (feature != null)
        {
            if (String.Equals(feature.Name, name, StringComparison.OrdinalIgnoreCase))
                return feature;
            feature = feature.IGetNextFeature();
        }
        throw new Exception("Feature not found: " + name);
    }

    private static Feature LastFeatureOfType(ModelDoc2 model, string typeName)
    {
        Feature feature = model.IFirstFeature();
        Feature match = null;
        while (feature != null)
        {
            if (String.Equals(feature.GetTypeName2(), typeName, StringComparison.OrdinalIgnoreCase))
                match = feature;
            feature = feature.IGetNextFeature();
        }
        if (match == null) throw new Exception("No feature of type " + typeName + " found.");
        return match;
    }

    private static Feature BeginEndRectangleSketch(
        ModelDoc2 model, Feature plane, string name,
        double x1Mm, double y1Mm, double x2Mm, double y2Mm)
    {
        model.ClearSelection2(true);
        Check(plane.Select2(false, 0), "select plane for " + name);
        model.SketchManager.InsertSketch(true);
        model.SketchManager.CreateCornerRectangle(
            x1Mm * Mm, y1Mm * Mm, 0.0,
            x2Mm * Mm, y2Mm * Mm, 0.0);
        model.SketchManager.InsertSketch(true);
        Feature sketch = LastFeatureOfType(model, "ProfileFeature");
        sketch.Name = name;
        return sketch;
    }

    private static Feature BeginEndRingSketch(
        ModelDoc2 model, Feature plane, string name,
        double ox1, double oy1, double ox2, double oy2,
        double ix1, double iy1, double ix2, double iy2)
    {
        model.ClearSelection2(true);
        Check(plane.Select2(false, 0), "select plane for " + name);
        model.SketchManager.InsertSketch(true);
        model.SketchManager.CreateCornerRectangle(
            ox1 * Mm, oy1 * Mm, 0.0, ox2 * Mm, oy2 * Mm, 0.0);
        model.SketchManager.CreateCornerRectangle(
            ix1 * Mm, iy1 * Mm, 0.0, ix2 * Mm, iy2 * Mm, 0.0);
        model.SketchManager.InsertSketch(true);
        Feature sketch = LastFeatureOfType(model, "ProfileFeature");
        sketch.Name = name;
        return sketch;
    }

    private static Feature BeginEndPolygonSketch(
        ModelDoc2 model, Feature plane, string name, double[,] pointsMm)
    {
        model.ClearSelection2(true);
        Check(plane.Select2(false, 0), "select plane for " + name);
        model.SketchManager.InsertSketch(true);
        int count = pointsMm.GetLength(0);
        for (int i = 0; i < count; ++i)
        {
            int j = (i + 1) % count;
            model.SketchManager.CreateLine(
                pointsMm[i, 0] * Mm, pointsMm[i, 1] * Mm, 0.0,
                pointsMm[j, 0] * Mm, pointsMm[j, 1] * Mm, 0.0);
        }
        model.SketchManager.InsertSketch(true);
        Feature sketch = LastFeatureOfType(model, "ProfileFeature");
        sketch.Name = name;
        return sketch;
    }

    private static Feature ExtrudeBoss(
        ModelDoc2 model, Feature sketch, double depth1Mm, double depth2Mm,
        bool doubleEnded, bool merge, string name)
    {
        model.ClearSelection2(true);
        Check(model.Extension.SelectByID2(
            sketch.Name, "SKETCH", 0, 0, 0, false, 0, null, 0),
            "select boss sketch " + name);
        Feature feature = model.FeatureManager.FeatureExtrusion3(
            !doubleEnded, false, false,
            0, 0, depth1Mm * Mm, depth2Mm * Mm,
            false, false, false, false, 0.0, 0.0,
            false, false, false, false,
            merge, true, true,
            (int)swStartConditions_e.swStartSketchPlane, 0.0, false);
        if (feature == null) throw new Exception("Could not create boss " + name);
        feature.Name = name;
        return feature;
    }

    private static Feature ExtrudeCut(
        ModelDoc2 model, Feature sketch, double depth1Mm, double depth2Mm,
        bool doubleEnded, bool reverse, double startOffsetMm, string name)
    {
        model.ClearSelection2(true);
        Check(model.Extension.SelectByID2(
            sketch.Name, "SKETCH", 0, 0, 0, false, 0, null, 0),
            "select cut sketch " + name);
        int startType = startOffsetMm == 0.0
            ? (int)swStartConditions_e.swStartSketchPlane
            : (int)swStartConditions_e.swStartOffset;
        Feature feature = model.FeatureManager.FeatureCut4(
            !doubleEnded, false, reverse,
            0, 0, depth1Mm * Mm, depth2Mm * Mm,
            false, false, false, false, 0.0, 0.0,
            false, false, false, false,
            false, false, false,
            true, false, true,
            startType, startOffsetMm * Mm, false, false);
        if (feature == null) throw new Exception("Could not create cut " + name);
        feature.Name = name;
        return feature;
    }

    private static void AddProperty(ModelDoc2 model, string name, string value)
    {
        model.Extension.CustomPropertyManager[""].Add3(
            name, (int)swCustomInfoType_e.swCustomInfoText, value,
            (int)swCustomPropertyAddOption_e.swCustomPropertyReplaceValue);
    }

    private static void SavePart(ModelDoc2 model, string nativePath)
    {
        model.EditRebuild3();
        int errors = 0;
        int warnings = 0;
        Check(model.Extension.SaveAs(
            nativePath,
            (int)swSaveAsVersion_e.swSaveAsCurrentVersion,
            (int)swSaveAsOptions_e.swSaveAsOptions_Silent,
            null, ref errors, ref warnings),
            "save " + nativePath + " error=" + errors);

        string stem = Path.Combine(
            Path.GetDirectoryName(nativePath),
            Path.GetFileNameWithoutExtension(nativePath));
        model.Extension.SaveAs(
            stem + ".step", (int)swSaveAsVersion_e.swSaveAsCurrentVersion,
            (int)swSaveAsOptions_e.swSaveAsOptions_Silent,
            null, ref errors, ref warnings);
        model.Extension.SaveAs(
            stem + ".STL", (int)swSaveAsVersion_e.swSaveAsCurrentVersion,
            (int)swSaveAsOptions_e.swSaveAsOptions_Silent,
            null, ref errors, ref warnings);

        object[] bodies = ((PartDoc)model).GetBodies2(
            (int)swBodyType_e.swSolidBody, true) as object[];
        Log("SAVED " + nativePath + " BODIES=" + (bodies == null ? 0 : bodies.Length));
    }

    private static ModelDoc2 NewPart(SldWorks sw, string template)
    {
        ModelDoc2 model = sw.NewDocument(template, 0, 0.0, 0.0) as ModelDoc2;
        if (model == null) throw new Exception("Could not create SolidWorks part.");
        return model;
    }

    private static void BuildTray(SldWorks sw, string template, string outputDirectory)
    {
        ModelDoc2 model = NewPart(sw, template);
        try
        {
            Feature horizontal = FindFeature(model, "Front Plane");
            Feature side = FindFeature(model, "Right Plane");

            Feature floorSketch = BeginEndRectangleSketch(
                model, horizontal, "190 x 200 Tray Floor Sketch",
                -95.0, 0.0, 95.0, 200.0);
            ExtrudeBoss(model, floorSketch, 4.0, 0.0, false, false, "4 mm Tray Floor");

            // Sealed compartment begins behind the display. Seven-millimetre
            // walls leave sufficient material around a 3.2 mm gasket channel.
            Feature wallsSketch = BeginEndRingSketch(
                model, horizontal, "Electronics Compartment Wall Sketch",
                -95.0, 45.0, 95.0, 200.0,
                -88.0, 52.0, 88.0, 193.0);
            ExtrudeBoss(model, wallsSketch, 22.0, 0.0, false, true, "22 mm Perimeter Walls");

            Feature grooveSketch = BeginEndRingSketch(
                model, horizontal, "3 mm Cord Gasket Groove Sketch",
                -93.2, 46.8, 93.2, 198.2,
                -90.0, 50.0, 90.0, 195.0);
            ExtrudeCut(
                model, grooveSketch, 2.3, 0.0, false, false, 22.0,
                "3.2 x 2.3 mm Perimeter Gasket Groove");

            // Rider is toward decreasing Y. The panel leans rearward as it
            // rises and remains attached to the lower tray when the lid lifts.
            double[,] panelProfile = {
                // Right-plane sketch coordinates are height, then tray length.
                { -2.0, 20.0 },
                { -2.0, 25.0 },
                { -100.0, 54.0 },
                { -100.0, 49.0 }
            };
            Feature panelSketch = BeginEndPolygonSketch(
                model, side, "Angled Display Bulkhead Side Profile", panelProfile);
            ExtrudeBoss(
                model, panelSketch, 75.0, 75.0, true, true,
                "150 mm Wide Angled Display Bulkhead");

            AddProperty(model, "Revision", "2");
            AddProperty(model, "Rider direction", "Toward decreasing tray-length axis");
            AddProperty(model, "Tray envelope", "190 W x 200 L mm");
            AddProperty(model, "Electronics cavity", "176 W x 141 L x 18 H mm nominal");
            AddProperty(model, "Display bulkhead", "150 mm wide; rearward leaning");
            AddProperty(model, "Gasket", "3 mm round EPDM; closed-loop 3.2 W x 2.3 D mm groove");
            AddProperty(model, "Acrylic protector", "Future option; bezel not included in revision 2");

            SavePart(model, Path.Combine(outputDirectory, "jet_ski_dash_tray_v2.SLDPRT"));
        }
        finally
        {
            sw.CloseDoc(model.GetTitle());
        }
    }

    private static void BuildCover(SldWorks sw, string template, string outputDirectory)
    {
        ModelDoc2 model = NewPart(sw, template);
        try
        {
            Feature side = FindFeature(model, "Right Plane");

            // The front lip reaches over the upper edge of the display as a
            // sunshade. The roof slopes down toward the rear of the ski.
            double[,] outerProfile = {
                // Right-plane sketch coordinates are height, then tray length.
                { -22.0, 40.0 },
                { -22.0, 200.0 },
                { -60.0, 200.0 },
                { -105.0, 40.0 }
            };
            Feature outerSketch = BeginEndPolygonSketch(
                model, side, "Sloped Cover Outer Side Profile", outerProfile);
            ExtrudeBoss(
                model, outerSketch, 95.0, 95.0, true, false,
                "Sloped Cover Blank");

            // This through-width cavity opens the bottom and rider-facing end,
            // leaving a 4 mm sloped roof, 3 mm side skirts, and rear wall.
            double[,] innerProfile = {
                { -18.0, 38.0 },
                { -18.0, 193.0 },
                { -56.0, 193.0 },
                { -101.0, 38.0 }
            };
            Feature cavitySketch = BeginEndPolygonSketch(
                model, side, "Open Bottom Cover Cavity Profile", innerProfile);
            ExtrudeCut(
                model, cavitySketch, 88.0, 88.0, true, false, 0.0,
                "Open Bottom and Front Cover Cavity");

            // Low front cross-rail completes the gasket compression loop while
            // keeping the rider-facing display area above it unobstructed.
            double[,] frontSealRail = {
                { -22.0, 45.0 },
                { -22.0, 52.0 },
                { -25.0, 52.0 },
                { -25.0, 45.0 }
            };
            Feature railSketch = BeginEndPolygonSketch(
                model, side, "Front Gasket Compression Rail Profile", frontSealRail);
            ExtrudeBoss(
                model, railSketch, 95.0, 95.0, true, true,
                "Front Gasket Compression Rail");

            AddProperty(model, "Revision", "2");
            AddProperty(model, "Cover role", "Removable electronics lid and display sunshade");
            AddProperty(model, "Nominal roof", "4 mm");
            AddProperty(model, "Nominal side walls", "7 mm at gasket interface");
            AddProperty(model, "Seal land", "Continuous underside land over tray perimeter groove");

            SavePart(model, Path.Combine(outputDirectory, "jet_ski_dash_cover_v2.SLDPRT"));
        }
        finally
        {
            sw.CloseDoc(model.GetTitle());
        }
    }

    public static int Main(string[] args)
    {
        string workspace = args.Length > 0
            ? Path.GetFullPath(args[0])
            : Directory.GetCurrentDirectory();
        string outputDirectory = Path.Combine(workspace, "dash_housing_work", "revision_2");
        Directory.CreateDirectory(outputDirectory);
        string template =
            @"C:\ProgramData\SOLIDWORKS\SOLIDWORKS 2026\templates\Part.PRTDOT";

        SldWorks sw = new SldWorks();
        sw.Visible = false;
        sw.CommandInProgress = true;
        sw.UserControlBackground = true;
        Log("START SolidWorks " + sw.RevisionNumber());
        try
        {
            BuildTray(sw, template, outputDirectory);
            BuildCover(sw, template, outputDirectory);
            Log("REVISION 2 COMPLETE " + outputDirectory);
            return 0;
        }
        finally
        {
            sw.ExitApp();
        }
    }
}
