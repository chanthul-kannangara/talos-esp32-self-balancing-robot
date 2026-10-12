using System;
using System.Collections.Generic;
using System.IO;
using SolidWorks.Interop.sldworks;

public static class ReadTalosCad
{
    private static Dictionary<string, object> MassData(IModelDoc2 document)
    {
        IMassProperty mass = (IMassProperty)document.Extension.CreateMassProperty();
        mass.UseSystemUnits = true;
        return new Dictionary<string, object> {
            {"volume_m3", mass.Volume}, {"cad_assigned_mass_kg", mass.Mass},
            {"cad_assigned_density_kg_m3", mass.Density},
            {"center_local_m", mass.CenterOfMass},
            {"surface_area_m2", mass.SurfaceArea},
            {"inertia_centroid_kg_m2", mass.GetMomentOfInertia(0)}
        };
    }

    private static List<object> Cylinders(IPartDoc part)
    {
        var cylinders = new List<object>();
        object[] bodies = part.GetBodies2(0, true) as object[];
        if (bodies == null) return cylinders;
        foreach (object bodyObject in bodies)
        {
            IBody2 body = (IBody2)bodyObject;
            object[] faces = body.GetFaces() as object[];
            if (faces == null) continue;
            foreach (object faceObject in faces)
            {
                IFace2 face = (IFace2)faceObject;
                ISurface surface = (ISurface)face.GetSurface();
                if (!surface.IsCylinder()) continue;
                cylinders.Add(new Dictionary<string, object> {
                    {"params_local_m", surface.CylinderParams},
                    {"bbox_local_m", face.GetBox()}, {"area_m2", face.GetArea()}
                });
            }
        }
        return cylinders;
    }

    public static Dictionary<string, object> Read(string root, string output)
    {
        ISldWorks application = (ISldWorks)Activator.CreateInstance(Type.GetTypeFromProgID("SldWorks.Application"));
        var result = new Dictionary<string, object>();
        result["revision"] = application.RevisionNumber();
        application.Visible = false;
        int errors = 0;
        int warnings = 0;
        IModelDoc2 document = (IModelDoc2)application.OpenDoc6(Path.Combine(root, "Assem1.SLDASM"), 2, 67, "", ref errors, ref warnings);
        result["open_errors"] = errors;
        result["open_warnings"] = warnings;
        if (document == null) throw new InvalidOperationException("Cannot open assembly: " + errors);
        result["assembly_title"] = document.GetTitle();
        result["assembly_cad_mass"] = MassData(document);
        IAssemblyDoc assembly = (IAssemblyDoc)document;
        var components = new List<object>();
        foreach (object componentObject in (object[])assembly.GetComponents(false))
        {
            IComponent2 component = (IComponent2)componentObject;
            var item = new Dictionary<string, object> {
                {"name", component.Name2}, {"path", component.GetPathName()},
                {"transform", component.Transform2 == null ? null : component.Transform2.ArrayData},
                {"configuration", component.ReferencedConfiguration},
                {"suppression", component.GetSuppression()}
            };
            IModelDoc2 partDocument = (IModelDoc2)component.GetModelDoc2();
            if (partDocument != null)
            {
                item["mass"] = MassData(partDocument);
                IPartDoc part = (IPartDoc)partDocument;
                item["bbox_local_m"] = part.GetPartBox(true);
                string database = "";
                item["material"] = part.GetMaterialPropertyName2(component.ReferencedConfiguration, out database);
                item["material_database"] = database;
                item["cylinders"] = Cylinders(part);
                if (component.Name2.StartsWith("TALOS_Left_Leg_Link") || component.Name2.StartsWith("Left Wheel Leg"))
                {
                    int exportErrors = 0;
                    int exportWarnings = 0;
                    string destination = Path.Combine(output, component.Name2.Replace(' ', '_') + ".step");
                    bool saved = partDocument.Extension.SaveAs(destination, 0, 1, null, ref exportErrors, ref exportWarnings);
                    item["step_export"] = new Dictionary<string, object> {
                        {"success", saved}, {"path", destination}, {"errors", exportErrors}, {"warnings", exportWarnings}
                    };
                }
            }
            components.Add(item);
        }
        result["components"] = components;
        var mates = new List<object>();
        IFeature feature = (IFeature)document.FirstFeature();
        while (feature != null)
        {
            if (feature.GetTypeName2() == "MateGroup")
            {
                IFeature child = (IFeature)feature.GetFirstSubFeature();
                while (child != null)
                {
                    object specific = child.GetSpecificFeature2();
                    if (specific is IMate2)
                    {
                        IMate2 mate = (IMate2)specific;
                        var entities = new List<object>();
                        for (int entityIndex = 0; entityIndex < mate.GetMateEntityCount(); entityIndex++)
                        {
                            IMateEntity2 entity = (IMateEntity2)mate.MateEntity(entityIndex);
                            entities.Add(new Dictionary<string, object> {
                                {"component", entity.ReferenceComponent == null ? "assembly" : entity.ReferenceComponent.Name2},
                                {"reference_type", entity.ReferenceType2}, {"params", entity.EntityParams}
                            });
                        }
                        mates.Add(new Dictionary<string, object> {
                            {"name", child.Name}, {"type", mate.Type}, {"entities", entities}
                        });
                    }
                    child = (IFeature)child.GetNextSubFeature();
                }
            }
            feature = (IFeature)feature.GetNextFeature();
        }
        result["mates"] = mates;
        application.CloseDoc(document.GetTitle());
        application.ExitApp();
        return result;
    }
}
