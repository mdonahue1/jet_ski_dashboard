using System;
using System.Collections.Generic;
using System.IO;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;

// Generates a first-pass, fully editable SolidWorks concept for the Sea-Doo
// dashboard front cover. Dimensions in the SolidWorks API are metres.
class GenerateDashFrontCover
{
    const double Mm = 0.001;

    static void Log(string message)
    {
        Console.WriteLine(message);
        Console.Out.Flush();
    }

    static void Check(bool ok, string operation)
    {
        if (!ok) throw new Exception("SolidWorks operation failed: " + operation);
    }

    static Feature PlaneFeature(ModelDoc2 model, string name)
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

    static Feature LastTopLevelFeature(ModelDoc2 model)
    {
        Feature feature = model.IFirstFeature();
        Feature last = null;
        while (feature != null)
        {
            last = feature;
            feature = feature.IGetNextFeature();
        }
        if (last == null) throw new Exception("Document has no features.");
        return last;
    }

    static Feature LastFeatureOfType(ModelDoc2 model, string typeName)
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

    static Feature AddOffsetPlane(ModelDoc2 model, Feature basePlane, double offsetMm, string name)
    {
        model.ClearSelection2(true);
        Check(basePlane.Select2(false, 0), "select plane for " + name);
        object plane = model.FeatureManager.InsertRefPlane(
            (int)swRefPlaneReferenceConstraints_e.swRefPlaneReferenceConstraint_Distance,
            offsetMm * Mm, 0, 0.0, 0, 0.0);
        if (plane == null) throw new Exception("Could not create plane " + name);
        Feature feature = LastFeatureOfType(model, "RefPlane");
        feature.Name = name;
        return feature;
    }

    static Feature AddChamferedRectangle(
        ModelDoc2 model, Feature plane, string name,
        double planeDepthMm,
        double leftMm, double rightMm, double bottomMm, double topMm, double chamferMm)
    {
        model.ClearSelection2(true);
        Check(plane.Select2(false, 0), "select sketch plane " + name);
        model.SketchManager.InsertSketch(true);
        Sketch sketch = model.SketchManager.ActiveSketch;
        if (sketch == null) throw new Exception("Could not start sketch " + name);

        double l = leftMm * Mm;
        double r = rightMm * Mm;
        double b = bottomMm * Mm;
        double t = topMm * Mm;
        double c = chamferMm * Mm;
        // SketchManager takes coordinates in the active sketch coordinate
        // system: width is X, height is Y, and sketch-normal is Z.
        model.SketchManager.CreateLine(l, b, 0, r, b, 0);
        model.SketchManager.CreateLine(r, b, 0, r, t - c, 0);
        model.SketchManager.CreateLine(r, t - c, 0, r - c, t, 0);
        model.SketchManager.CreateLine(r - c, t, 0, l + c, t, 0);
        model.SketchManager.CreateLine(l + c, t, 0, l, t - c, 0);
        model.SketchManager.CreateLine(l, t - c, 0, l, b, 0);

        model.SketchManager.InsertSketch(true);
        Feature feature = LastFeatureOfType(model, "ProfileFeature");
        feature.Name = name;
        return feature;
    }

    static Feature AddRectangle(
        ModelDoc2 model, Feature plane, string name,
        double planeDepthMm,
        double leftMm, double rightMm, double bottomMm, double topMm)
    {
        model.ClearSelection2(true);
        Check(plane.Select2(false, 0), "select sketch plane " + name);
        model.SketchManager.InsertSketch(true);
        Sketch sketch = model.SketchManager.ActiveSketch;
        if (sketch == null) throw new Exception("Could not start sketch " + name);
        model.SketchManager.CreateCornerRectangle(
            leftMm * Mm, bottomMm * Mm, 0,
            rightMm * Mm, topMm * Mm, 0);
        model.SketchManager.InsertSketch(true);
        Feature feature = LastFeatureOfType(model, "ProfileFeature");
        feature.Name = name;
        return feature;
    }

    static Feature AddLoftBoss(ModelDoc2 model, Feature first, Feature second, string name)
    {
        model.ClearSelection2(true);
        Check(model.Extension.SelectByID2(
            first.Name, "SKETCH", 0, 0, 0, false, 1, null, 0),
            "select first profile for " + name);
        Check(model.Extension.SelectByID2(
            second.Name, "SKETCH", 0, 0, 0, true, 1, null, 0),
            "select second profile for " + name);
        Feature feature = model.FeatureManager.InsertProtrusionBlend2(
            false, true, false, 1.0,
            0, 0, 1.0, 1.0, true, true,
            true, 4.0 * Mm, 0.0, 1,
            true, true, true,
            (int)swGuideCurveInfluence_e.swGuideCurveInfluenceNextGlobal);
        if (feature == null) throw new Exception("Could not create loft boss " + name);
        feature.Name = name;
        return feature;
    }

    static Feature AddBezelRingSketch(ModelDoc2 model, Feature plane, string name)
    {
        model.ClearSelection2(true);
        Check(plane.Select2(false, 0), "select bezel plane");
        model.SketchManager.InsertSketch(true);
        Sketch sketch = model.SketchManager.ActiveSketch;
        model.SketchManager.CreateCornerRectangle(-75.0 * Mm, 45.0 * Mm, 0,
                                                   75.0 * Mm, 155.0 * Mm, 0);
        model.SketchManager.CreateCornerRectangle(-50.5 * Mm, 71.0 * Mm, 0,
                                                   50.5 * Mm, 130.0 * Mm, 0);
        model.SketchManager.InsertSketch(true);
        Feature feature = LastFeatureOfType(model, "ProfileFeature");
        feature.Name = name;
        return feature;
    }

    static Feature AddGasketGrooveSketch(ModelDoc2 model, Feature plane, string name)
    {
        model.ClearSelection2(true);
        Check(plane.Select2(false, 0), "select gasket plane");
        model.SketchManager.InsertSketch(true);
        Sketch sketch = model.SketchManager.ActiveSketch;
        // Open-bottom inverted U: 3.2 mm channels for 3 mm cord.
        model.SketchManager.CreateCornerRectangle(-102.6 * Mm, 7.0 * Mm, 0,
                                                  -99.4 * Mm, 188.0 * Mm, 0);
        model.SketchManager.CreateCornerRectangle(99.4 * Mm, 7.0 * Mm, 0,
                                                  102.6 * Mm, 188.0 * Mm, 0);
        model.SketchManager.CreateCornerRectangle(-82.0 * Mm, 200.0 * Mm, 0,
                                                  82.0 * Mm, 203.2 * Mm, 0);
        model.SketchManager.InsertSketch(true);
        Feature feature = LastFeatureOfType(model, "ProfileFeature");
        feature.Name = name;
        return feature;
    }

    static Feature AddExtrudedBoss(ModelDoc2 model, Feature sketch, double depthMm, string name)
    {
        model.ClearSelection2(true);
        Check(model.Extension.SelectByID2(
            sketch.Name, "SKETCH", 0, 0, 0, false, 0, null, 0),
            "select boss sketch " + name);
        Feature feature = model.FeatureManager.FeatureExtrusion3(
            false, false, false, 0, 0, depthMm * Mm, 1.0 * Mm,
            false, false, false, false, 0.0, 0.0,
            false, false, false, false,
            true, true, true, 0, 0.0, false);
        if (feature == null) throw new Exception("Could not create extruded boss " + name);
        feature.Name = name;
        return feature;
    }

    static Feature AddExtrudedCut(ModelDoc2 model, Feature sketch, double depthMm, string name)
    {
        model.ClearSelection2(true);
        Check(model.Extension.SelectByID2(
            sketch.Name, "SKETCH", 0, 0, 0, false, 0, null, 0),
            "select cut sketch " + name);
        Feature feature = model.FeatureManager.FeatureCut4(
            true, false, true, 0, 0, depthMm * Mm, 0.0,
            false, false, false, false, 0.0, 0.0,
            false, false, false, false, false, false, false,
            true, false, true, 0, 0.0, false, false);
        if (feature == null) throw new Exception("Could not create extruded cut " + name);
        feature.Name = name;
        return feature;
    }

    static Feature AddLoftCut(ModelDoc2 model, Feature first, Feature second, string name)
    {
        model.ClearSelection2(true);
        Check(model.Extension.SelectByID2(
            first.Name, "SKETCH", 0, 0, 0, false, 1, null, 0),
            "select first cut profile for " + name);
        Check(model.Extension.SelectByID2(
            second.Name, "SKETCH", 0, 0, 0, true, 1, null, 0),
            "select second cut profile for " + name);
        Feature feature = model.FeatureManager.InsertCutBlend(
            false, true, false, 1.0,
            0, 0, true, 0.0, 0.0, 0,
            true, true);
        if (feature == null) throw new Exception("Could not create loft cut " + name);
        feature.Name = name;
        return feature;
    }

    static void AddCustomProperty(ModelDoc2 model, string name, string value)
    {
        model.Extension.CustomPropertyManager[""].Add3(
            name,
            (int)swCustomInfoType_e.swCustomInfoText,
            value,
            (int)swCustomPropertyAddOption_e.swCustomPropertyReplaceValue);
    }

    static void LogSketch(Feature feature)
    {
        Sketch sketch = feature.GetSpecificFeature2() as Sketch;
        if (sketch == null)
        {
            Log("SKETCH " + feature.Name + " has no sketch object");
            return;
        }
        object segmentsObj = sketch.GetSketchSegments();
        object[] segments = segmentsObj as object[];
        Log("SKETCH " + feature.Name + " segments=" + (segments == null ? 0 : segments.Length));
        if (segments != null && segments.Length > 0)
        {
            SketchLine line = segments[0] as SketchLine;
            if (line != null)
            {
                SketchPoint a = (SketchPoint)line.GetStartPoint2();
                SketchPoint b = (SketchPoint)line.GetEndPoint2();
                Log(String.Format("LINE ({0:F4},{1:F4},{2:F4}) to ({3:F4},{4:F4},{5:F4})",
                    a.X, a.Y, a.Z, b.X, b.Y, b.Z));
            }
        }
    }

    static void Main(string[] args)
    {
        string workspace = args.Length > 0
            ? Path.GetFullPath(args[0])
            : Directory.GetCurrentDirectory();
        string outputDir = Path.Combine(workspace, "dash_housing_work");
        Directory.CreateDirectory(outputDir);
        string partPath = Path.Combine(outputDir, "jet_ski_dash_front_cover_v1.SLDPRT");
        string stepPath = Path.Combine(outputDir, "jet_ski_dash_front_cover_v1.step");
        string stlPath = Path.Combine(outputDir, "jet_ski_dash_front_cover_v1.STL");
        string template = @"C:\ProgramData\SOLIDWORKS\SOLIDWORKS 2026\templates\Part.PRTDOT";

        SldWorks sw = new SldWorks();
        sw.Visible = false;
        sw.CommandInProgress = true;
        sw.UserControlBackground = true;
        Log("START SolidWorks " + sw.RevisionNumber());
        ModelDoc2 model = (ModelDoc2)sw.NewDocument(template, 0, 0.0, 0.0);
        if (model == null) throw new Exception("Could not create a SolidWorks part.");

        try
        {
            // The existing housing uses X for width, Y for depth, and Z for
            // height, so profiles belong on the Top plane (normal to Y).
            Feature rearPlane = PlaneFeature(model, "Top Plane");
            Feature frontPlane = AddOffsetPlane(model, rearPlane, 55.0, "Front Profile Plane");
            Log("PLANES outer");

            // The rear overlaps the existing 190 x 200 mm base. The slightly
            // larger 202 x 204 mm outline gives a useful sealing lip.
            Feature rearOuter = AddChamferedRectangle(
                model, rearPlane, "Rear Outer Interface",
                0.0,
                -101.0, 101.0, -2.0, 202.0, 18.0);
            Feature frontOuter = AddChamferedRectangle(
                model, frontPlane, "Front Snub Profile",
                -55.0,
                -75.0, 75.0, 45.0, 155.0, 12.0);
            Log("PROFILE rear=" + rearOuter.Name + "/" + rearOuter.GetTypeName2()
                + " front=" + frontOuter.Name + "/" + frontOuter.GetTypeName2());
            LogSketch(rearOuter);
            LogSketch(frontOuter);
            AddLoftBoss(model, rearOuter, frontOuter, "Angular Snub Nose");
            Log("BOSS outer loft");

            // A 3 mm front ring gives the display a protected reveal while the
            // surrounding thin loft becomes the top-and-side sun shade.
            Feature bezelSketch = AddBezelRingSketch(model, frontPlane, "Front Bezel Ring Sketch");
            AddExtrudedBoss(model, bezelSketch, 3.0, "Front Display Bezel");
            Log("BOSS front display bezel");

            // Three shallow channels retain 3 mm round EPDM cord. They stop
            // above the bottom edge so water has a gravity drain path.
            Feature grooveSketch = AddGasketGrooveSketch(model, rearPlane, "Three-Sided Gasket Groove Sketch");
            AddExtrudedCut(model, grooveSketch, 2.4, "Three-Sided Gasket Groove");
            Log("CUT three-sided gasket groove");

            AddCustomProperty(model, "Design intent", "Snub-nose front cover for Sea-Doo digital dashboard");
            AddCustomProperty(model, "Existing base envelope", "190 x 200 x 84 mm");
            AddCustomProperty(model, "Cover depth", "55 mm");
            AddCustomProperty(model, "Nominal wall", "3-5 mm");
            AddCustomProperty(model, "Gasket", "3 mm round EPDM; top and sides only; 3.2 x 2.4 mm channels");
            AddCustomProperty(model, "Display opening", "101 x 59 mm - verify against physical module before final print");

            model.EditRebuild3();
            Log("REBUILT");
            int saveErrors = 0;
            int saveWarnings = 0;
            bool saved = model.Extension.SaveAs(
                partPath,
                (int)swSaveAsVersion_e.swSaveAsCurrentVersion,
                (int)swSaveAsOptions_e.swSaveAsOptions_Silent,
                null, ref saveErrors, ref saveWarnings);
            Check(saved, "save native part (error " + saveErrors + ")");
            Log("SAVED native");

            model.Extension.SaveAs(
                stepPath, (int)swSaveAsVersion_e.swSaveAsCurrentVersion,
                (int)swSaveAsOptions_e.swSaveAsOptions_Silent,
                null, ref saveErrors, ref saveWarnings);
            model.Extension.SaveAs(
                stlPath, (int)swSaveAsVersion_e.swSaveAsCurrentVersion,
                (int)swSaveAsOptions_e.swSaveAsOptions_Silent,
                null, ref saveErrors, ref saveWarnings);
            Log("SAVED exports");

            object bodiesObj = ((PartDoc)model).GetBodies2(
                (int)swBodyType_e.swSolidBody, true);
            int bodyCount = bodiesObj == null ? 0 : ((object[])bodiesObj).Length;
            Console.WriteLine("CREATED " + partPath);
            Console.WriteLine("BODIES " + bodyCount);
            Console.WriteLine("STEP " + stepPath);
            Console.WriteLine("STL " + stlPath);
        }
        finally
        {
            sw.CloseDoc(model.GetTitle());
            sw.ExitApp();
        }
    }
}
