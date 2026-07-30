using System;
using System.IO;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;

// Revision 3: a lower electronics tray with an inward-angled, windowed display
// bulkhead, plus
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

    private static Feature AddOffsetPlane(
        ModelDoc2 model, Feature basePlane, double offsetMm, string name)
    {
        model.ClearSelection2(true);
        Check(basePlane.Select2(false, 0), "select base plane for " + name);
        object plane = model.FeatureManager.InsertRefPlane(
            (int)swRefPlaneReferenceConstraints_e.swRefPlaneReferenceConstraint_Distance,
            offsetMm * Mm, 0, 0.0, 0, 0.0);
        if (plane == null) throw new Exception("Could not create plane " + name);
        Feature feature = LastFeatureOfType(model, "RefPlane");
        feature.Name = name;
        RefPlane referencePlane = feature.GetSpecificFeature2() as RefPlane;
        if (referencePlane != null)
        {
            MathTransform transform = referencePlane.Transform;
            double[] data = transform == null ? null : transform.ArrayData as double[];
            if (data != null && data.Length >= 12)
                Log(String.Format(
                    "PLANE {0} translation=({1:F4},{2:F4},{3:F4})",
                    name, data[9], data[10], data[11]));
        }
        return feature;
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

    private static Feature BeginEndCircleSketch(
        ModelDoc2 model, Feature plane, string name,
        double centerXmm, double centerYmm, double diameterMm)
    {
        model.ClearSelection2(true);
        Check(plane.Select2(false, 0), "select plane for " + name);
        model.SketchManager.InsertSketch(true);
        model.SketchManager.CreateCircleByRadius(
            centerXmm * Mm, centerYmm * Mm, 0.0,
            diameterMm * 0.5 * Mm);
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

    private static Feature LoftCut(
        ModelDoc2 model, Feature first, Feature second, string name)
    {
        model.ClearSelection2(true);
        Check(model.Extension.SelectByID2(
            first.Name, "SKETCH", 0, 0, 0, false, 1, null, 0),
            "select first loft-cut profile " + name);
        Check(model.Extension.SelectByID2(
            second.Name, "SKETCH", 0, 0, 0, true, 1, null, 0),
            "select second loft-cut profile " + name);
        Feature feature = model.FeatureManager.InsertCutBlend(
            false, true, false, 1.0,
            0, 0, false, 0.0, 0.0, 0,
            true, true);
        if (feature == null) throw new Exception("Could not create loft cut " + name);
        feature.Name = name;
        return feature;
    }

    private static Feature ChamferLongOuterRoofEdges(
        ModelDoc2 model, double chamferMm, string name)
    {
        object[] bodies = ((PartDoc)model).GetBodies2(
            (int)swBodyType_e.swSolidBody, true) as object[];
        if (bodies == null || bodies.Length != 1)
            throw new Exception("Expected one cover body before " + name);

        object[] edges = ((Body2)bodies[0]).GetEdges() as object[];
        int selected = 0;
        model.ClearSelection2(true);
        if (edges != null)
        {
            foreach (object edgeObject in edges)
            {
                Edge edge = edgeObject as Edge;
                if (edge == null) continue;
                Vertex start = edge.GetStartVertex() as Vertex;
                Vertex end = edge.GetEndVertex() as Vertex;
                if (start == null || end == null) continue;
                double[] a = start.GetPoint() as double[];
                double[] b = end.GetPoint() as double[];
                if (a == null || b == null) continue;

                double averageAbsX = (Math.Abs(a[0]) + Math.Abs(b[0])) * 0.5;
                double lengthY = Math.Abs(a[1] - b[1]);
                double averageZ = (a[2] + b[2]) * 0.5;
                if (Math.Abs(averageAbsX - 95.0 * Mm) < 0.6 * Mm
                    && lengthY > 20.0 * Mm
                    && averageZ > 50.0 * Mm)
                {
                    Entity entity = edge as Entity;
                    Check(
                        entity != null && entity.Select4(selected > 0, null),
                        "select roof edge " + name);
                    ++selected;
                }
            }
        }
        if (selected != 4)
            throw new Exception(
                "Expected four outer shade/roof edges for " + name
                + ", selected " + selected);

        Feature feature = model.FeatureManager.InsertFeatureChamfer(
            0,
            (int)swChamferType_e.swChamferAngleDistance,
            chamferMm * Mm, Math.PI / 4.0, 0.0,
            0.0, 0.0, 0.0);
        if (feature == null) throw new Exception("Could not create chamfer " + name);
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
                -95.0, -8.0, 95.0, 200.0);
            ExtrudeBoss(model, floorSketch, 4.0, 0.0, false, false, "4 mm Tray Floor");

            // Sealed compartment begins behind the display. Seven-millimetre
            // walls leave sufficient material for an external cord-gasket
            // channel while preserving a broad, flat wall top.
            Feature wallsSketch = BeginEndRingSketch(
                model, horizontal, "Electronics Compartment Wall Sketch",
                -95.0, 25.0, 95.0, 200.0,
                -88.0, 32.0, 88.0, 193.0);
            ExtrudeBoss(model, wallsSketch, 22.0, 0.0, false, true, "22 mm Perimeter Walls");

            Feature grooveSketch = BeginEndRingSketch(
                model, horizontal, "External Wall Cord Gasket Groove Sketch",
                -96.0, 24.0, 96.0, 201.0,
                -92.7, 27.3, 92.7, 197.7);
            ExtrudeCut(
                model, grooveSketch, 3.2, 0.0, false, false, 17.2,
                "3.2 High x 2.3 Deep External Perimeter Gasket Groove");

            // The ceramic GPS patch is approximately 19.12 mm square and
            // 6 mm thick. It lies face-up on this left-wall shelf. The square
            // guide ring and two small top lips form a serviceable snap cradle
            // while leaving the antenna lead free toward the enclosure center.
            Feature antennaShelfPlane = AddOffsetPlane(
                model, horizontal, 12.0, "GPS Antenna Shelf Plane");
            Feature antennaShelfSketch = BeginEndRectangleSketch(
                model, antennaShelfPlane, "24 mm GPS Antenna Shelf Sketch",
                -88.0, 44.0, -64.0, 68.0);
            ExtrudeBoss(
                model, antennaShelfSketch, 2.0, 0.0, false, true,
                "2 mm GPS Antenna Shelf");

            Feature antennaGuidePlane = AddOffsetPlane(
                model, horizontal, 13.5, "GPS Antenna Guide Plane");
            Feature antennaGuideSketch = BeginEndRingSketch(
                model, antennaGuidePlane, "20 mm GPS Antenna Guide Ring Sketch",
                -88.0, 44.0, -64.0, 68.0,
                -86.0, 46.0, -66.0, 66.0);
            ExtrudeBoss(
                model, antennaGuideSketch, 8.5, 0.0, false, true,
                "8.5 mm GPS Antenna Guide Ring");

            Feature antennaClipPlane = AddOffsetPlane(
                model, horizontal, 20.4, "GPS Antenna Clip Plane");
            Feature leftAntennaClipSketch = BeginEndRectangleSketch(
                model, antennaClipPlane, "Left GPS Antenna Snap Lip Sketch",
                -86.2, 48.0, -83.5, 64.0);
            ExtrudeBoss(
                model, leftAntennaClipSketch, 1.4, 0.0, false, true,
                "Left GPS Antenna Snap Lip");
            Feature rightAntennaClipSketch = BeginEndRectangleSketch(
                model, antennaClipPlane, "Right GPS Antenna Snap Lip Sketch",
                -68.5, 48.0, -65.8, 64.0);
            ExtrudeBoss(
                model, rightAntennaClipSketch, 1.4, 0.0, false, true,
                "Right GPS Antenna Snap Lip");

            // Rider is toward decreasing Y. The panel leans rearward as it
            // rises and remains attached to the lower tray when the lid lifts.
            double[,] panelProfile = {
                // Right-plane sketch coordinates are height, then tray length.
                { -2.0, 15.0 },
                { -2.0, 21.0 },
                { -100.0, 66.0 },
                { -100.0, 60.0 }
            };
            Feature panelSketch = BeginEndPolygonSketch(
                model, side, "Angled Display Bulkhead Side Profile", panelProfile);
            ExtrudeBoss(
                model, panelSketch, 95.0, 95.0, true, true,
                "190 mm Wide Angled Display Bulkhead");

            // Opening through the tilted panel. Its side profile follows the
            // panel angle, so the cut is approximately normal to the display.
            double[,] screenOpeningProfile = {
                { -28.2, 25.0 },
                { -28.2, 35.0 },
                { -80.8, 59.2 },
                { -80.8, 49.2 }
            };
            Feature screenOpeningSketch = BeginEndPolygonSketch(
                model, side, "92 x 58 Display Opening Side Profile",
                screenOpeningProfile);
            ExtrudeCut(
                model, screenOpeningSketch, 46.0, 46.0, true, false, 0.0,
                "92 x 58 mm Display Opening");

            // A round, grommet-ready harness exit sits low on the rider-facing
            // panel. It carries 12 V and sensor wiring forward without putting
            // a hole through the original cubby lid.
            Feature vertical = FindFeature(model, "Top Plane");
            Feature cablePassageSketch = BeginEndCircleSketch(
                model, vertical, "18 mm Front Harness Grommet Sketch",
                0.0, -15.0, 18.0);
            ExtrudeCut(
                model, cablePassageSketch, 40.0, 40.0, true, false, 0.0,
                "18 mm Front 12 V and Sensor Harness Passage");

            // Clip the screen holder's two upper corners in front view.
            double[,] leftPanelChamfer = {
                { -95.0, -100.0 },
                { -80.0, -100.0 },
                { -95.0, -85.0 }
            };
            Feature leftPanelChamferSketch = BeginEndPolygonSketch(
                model, vertical, "Left Screen Holder Corner Chamfer Sketch",
                leftPanelChamfer);
            ExtrudeCut(
                model, leftPanelChamferSketch, 110.0, 110.0, true, false, 0.0,
                "Left Screen Holder Corner Chamfer");

            double[,] rightPanelChamfer = {
                { 95.0, -100.0 },
                { 80.0, -100.0 },
                { 95.0, -85.0 }
            };
            Feature rightPanelChamferSketch = BeginEndPolygonSketch(
                model, vertical, "Right Screen Holder Corner Chamfer Sketch",
                rightPanelChamfer);
            ExtrudeCut(
                model, rightPanelChamferSketch, 110.0, 110.0, true, false, 0.0,
                "Right Screen Holder Corner Chamfer");

            AddProperty(model, "Revision", "3");
            AddProperty(model, "Rider direction", "Toward decreasing tray-length axis");
            AddProperty(model, "Tray envelope", "190 W x 200 L mm");
            AddProperty(model, "Electronics cavity", "176 W x 141 L x 18 H mm nominal");
            AddProperty(model, "Display bulkhead", "190 mm full width; inward leaning; 15 mm upper corner clips");
            AddProperty(model, "Display opening", "92 W x 58 H mm nominal for 94.38 x 61 mm screen");
            AddProperty(model, "Harness passage", "18 mm round grommet hole below screen for 12 V and sensor wiring");
            AddProperty(model, "Gasket", "3 mm round EPDM; external-wall closed-loop groove, 3.2 H x 2.3 D mm");
            AddProperty(model, "GPS antenna cradle", "Left inside wall; 20 x 20 x 6 mm nominal ceramic patch");
            AddProperty(model, "Acrylic protector", "Future option; mounting land reserved, retaining bezel not included");
            AddProperty(model, "Acrylic allowance", "Extended upper shade and 23 mm lower nose ahead of panel");

            SavePart(model, Path.Combine(outputDirectory, "jet_ski_dash_tray_v3.SLDPRT"));
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
                { -22.0, 25.0 },
                { -22.0, 200.0 },
                { -60.0, 200.0 },
                { -108.0, 60.0 },
                { -108.0, 35.0 }
            };
            Feature outerSketch = BeginEndPolygonSketch(
                model, side, "Sloped Cover Outer Side Profile", outerProfile);
            ExtrudeBoss(
                model, outerSketch, 95.0, 95.0, true, false,
                "Sloped Cover Blank");

            // This through-width cavity opens the bottom and rider-facing end,
            // leaving a 4 mm sloped roof, 3 mm side skirts, and rear wall.
            double[,] innerProfile = {
                { -18.0, 23.0 },
                { -18.0, 193.0 },
                { -48.0, 193.0 },
                { -96.0, 60.0 },
                { -96.0, 33.0 }
            };
            Feature cavitySketch = BeginEndPolygonSketch(
                model, side, "Open Bottom Cover Cavity Profile", innerProfile);
            ExtrudeCut(
                model, cavitySketch, 80.0, 80.0, true, false, 0.0,
                "Open Bottom and Front Cover Cavity");

            // A true edge chamfer follows both sloped roof edges continuously
            // from the extended shade to the rear wall.
            ChamferLongOuterRoofEdges(
                model, 12.0, "12 mm Full-Length Shape-Defining Roof Chamfers");

            AddProperty(model, "Revision", "3");
            AddProperty(model, "Cover role", "Removable electronics lid and display sunshade");
            AddProperty(model, "Nominal roof", "12 mm near the shape-defining chamfers");
            AddProperty(model, "Nominal side walls", "15 mm near the shape-defining chamfers");
            AddProperty(model, "Seal land", "Continuous underside land over tray perimeter groove");
            AddProperty(model, "Sunshade projection", "Approximately 25 mm ahead of display top");

            SavePart(model, Path.Combine(outputDirectory, "jet_ski_dash_cover_v3.SLDPRT"));
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
        string outputDirectory = Path.Combine(workspace, "dash_housing_work", "revision_3");
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
            Log("REVISION 3 COMPLETE " + outputDirectory);
            return 0;
        }
        finally
        {
            sw.ExitApp();
        }
    }
}
