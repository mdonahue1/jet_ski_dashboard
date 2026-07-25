using System;
using System.IO;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;

internal static class InspectSolidWorksPart
{
    private const string PartPath =
        @"D:\SOLIDWORKS\solidworks parts and assemblies\jet_ski_dash_housing.SLDPRT";
    private const string ExportPath =
        @"C:\Users\miner\STM32CubeIDE\workspace_1.19.0\Jet_Ski_Dash_v1\dash_housing_work\source_geometry.step";
    private const string StlPath =
        @"C:\Users\miner\STM32CubeIDE\workspace_1.19.0\Jet_Ski_Dash_v1\dash_housing_work\source_geometry.stl";

    public static int Main()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(ExportPath));
        var sw = new SldWorks { Visible = false };
        int errors = 0;
        int warnings = 0;
        ModelDoc2 model = sw.OpenDoc6(
            PartPath,
            (int)swDocumentTypes_e.swDocPART,
            (int)swOpenDocOptions_e.swOpenDocOptions_Silent,
            "",
            ref errors,
            ref warnings);

        if (model == null)
        {
            Console.Error.WriteLine("OPEN_FAILED errors={0} warnings={1}", errors, warnings);
            sw.ExitApp();
            return 1;
        }

        Console.WriteLine("TITLE " + model.GetTitle());
        Console.WriteLine("PATH " + model.GetPathName());
        var part = (PartDoc)model;
        var box = (double[])part.GetPartBox(true);
        for (int i = 0; i < box.Length; i++)
            box[i] *= 1000.0;
        Console.WriteLine("BOUNDING_BOX_MM " + string.Join(",", box));
        Console.WriteLine("SIZE_MM {0:F3},{1:F3},{2:F3}",
            box[3] - box[0], box[4] - box[1], box[5] - box[2]);

        object[] bodies = (object[])part.GetBodies2((int)swBodyType_e.swSolidBody, true);
        Console.WriteLine("SOLID_BODY_COUNT " + (bodies == null ? 0 : bodies.Length));
        if (bodies != null)
        {
            foreach (Body2 body in bodies)
            {
                var bodyBox = (double[])body.GetBodyBox();
                for (int i = 0; i < bodyBox.Length; i++)
                    bodyBox[i] *= 1000.0;
                Console.WriteLine("BODY {0} BOX_MM {1}", body.Name, string.Join(",", bodyBox));
            }
        }

        Feature feature = (Feature)model.FirstFeature();
        while (feature != null)
        {
            Console.WriteLine("FEATURE {0} | {1} | SUPPRESSED={2}",
                feature.Name, feature.GetTypeName2(), feature.IsSuppressed());
            Feature sub = (Feature)feature.GetFirstSubFeature();
            while (sub != null)
            {
                Console.WriteLine("  SUBFEATURE {0} | {1}", sub.Name, sub.GetTypeName2());
                sub = (Feature)sub.GetNextSubFeature();
            }
            feature = (Feature)feature.GetNextFeature();
        }

        int saveErrors = 0;
        int saveWarnings = 0;
        bool saved = model.Extension.SaveAs(
            ExportPath,
            (int)swSaveAsVersion_e.swSaveAsCurrentVersion,
            (int)swSaveAsOptions_e.swSaveAsOptions_Silent,
            null,
            ref saveErrors,
            ref saveWarnings);
        Console.WriteLine("STEP_EXPORT ok={0} errors={1} warnings={2} path={3}",
            saved, saveErrors, saveWarnings, ExportPath);

        saveErrors = 0;
        saveWarnings = 0;
        bool stlSaved = model.Extension.SaveAs(
            StlPath,
            (int)swSaveAsVersion_e.swSaveAsCurrentVersion,
            (int)swSaveAsOptions_e.swSaveAsOptions_Silent,
            null,
            ref saveErrors,
            ref saveWarnings);
        Console.WriteLine("STL_EXPORT ok={0} errors={1} warnings={2} path={3}",
            stlSaved, saveErrors, saveWarnings, StlPath);

        sw.CloseDoc(model.GetTitle());
        sw.ExitApp();
        return saved ? 0 : 2;
    }
}
