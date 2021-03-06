//
// Copyright 2017 Animal Logic
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.//
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#include "test_usdmaya.h"
#include "AL/usdmaya/nodes/ProxyShape.h"
#include "AL/usdmaya/nodes/Transform.h"
#include "AL/usdmaya/nodes/Layer.h"
#include "AL/usdmaya/StageCache.h"

#include "maya/MFnTransform.h"
#include "maya/MSelectionList.h"
#include "maya/MGlobal.h"
#include "maya/MItDependencyNodes.h"
#include "maya/MDagModifier.h"
#include "maya/MFileIO.h"

#include "pxr/usd/usd/stage.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/xformCommonAPI.h"
#include "pxr/usd/usd/variantSets.h"

#include <fstream>

const char* const g_inactive =
"#usda 1.0\n"
"\n"
"def Xform \"root\"\n"
"{\n"
"    def ALMayaReference \"rig\" (\n"
"      active = false\n"
"    )\n"
"    {\n"
"      asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"    }\n"
"}\n";

const char* const g_active =
"#usda 1.0\n"
"\n"
"def Xform \"root\"\n"
"{\n"
"    def ALMayaReference \"rig\"\n"
"    {\n"
"      asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"    }\n"
"}\n";

const char* const g_variants =
"#usda 1.0\n"
"(\n"
"    defaultPrim = \"rig_variants\"\n"
")\n"
"\n"
"def Xform \"root\"\n"
"(\n"
"    variants = {\n"
"        string rig_technical = \"sphere\"\n"
"    }\n"
"    add variantSets = \"rig_technical\"\n"
")\n"
"{\n"
"    variantSet \"rig_technical\" = {\n"
"      \"sphere\"{\n"
"        def ALMayaReference \"rig\"\n"
"        {\n"
"           asset mayaReference = \"/tmp/AL_usdmaya_test_sphere.ma\"\n"
"           string mayaNamespace = \"dave\"\n"
"        }\n"
"      }\n"
"      \"cube\"{\n"
"        def ALMayaReference \"rig\"\n"
"        {\n"
"           asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"           string mayaNamespace = \"dave\"\n"
"        }\n"
"      }\n"
"      \"fredcube\"{\n"
"        def ALMayaReference \"rig\"\n"
"        {\n"
"           asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"           string mayaNamespace = \"fred\"\n"
"        }\n"
"      }\n"
"      \"cache\"{\n"
"        def Sphere \"rig\"\n"
"        {\n"
"          double radius = 1\n"
"        }\n"
"      }\n"
"    }\n"
"}\n";

const char* const g_customTransformType =
"#usda 1.0\n"
"\n"
"def Xform \"root\"\n"
"{\n"
"    def ALMayaReference \"rig\" (\n"
"      al_usdmaya_transformType = \"joint\"\n"
"    )\n"
"    {\n"
"      asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"    }\n"
"}\n";

const char* const g_duplicateTransformNames =
"#usda 1.0\n"
"\n"
"def Xform \"root\"\n"
"{\n"
"  def Xform \"one\"\n"
"  {\n"
"    def ALMayaReference \"rig\" (\n"
"      al_usdmaya_transformType = \"joint\"\n"
"    )\n"
"    {\n"
"      asset mayaReference = \"/tmp/AL_usdmaya_test_cube.ma\"\n"
"    }\n"
"  }\n"
"  def Xform \"two\"\n"
"  {\n"
"    def ALMayaReference \"rig\" (\n"
"      al_usdmaya_transformType = \"joint\"\n"
"    )\n"
"    {\n"
"      asset mayaReference = \"/tmp/AL_usdmaya_test_sphere.ma\"\n"
"    }\n"
"  }\n"
"}\n";

TEST(ActiveInactive, duplicateTransformNames)
{
  MFileIO::newFile(true);

  // Prep us a maya reference file to use!

  // pCube1, pCubeShape1, polyCube1
  MGlobal::executeCommand("polyCube -w 1 -h 1 -d 1 -sd 1 -sh 1 -sw 1", false, false);
  MFileIO::saveAs("/tmp/AL_usdmaya_test_cube.ma", 0, true);
  MFileIO::newFile(true);

  // pSphere1, pSphereShape1, polySphere1
  MGlobal::executeCommand("polySphere", false, false);
  MFileIO::saveAs("/tmp/AL_usdmaya_test_sphere.ma", 0, true);
  MFileIO::newFile(true);

  // output a couple of usda files for testing (active and inactive)
  {
    std::ofstream os("/tmp/AL_usdmaya_duplicateTransformNames.usda");
    os << g_duplicateTransformNames;
  }

  {
    MString shapeName;
    MFnDagNode fn;
    MObject xform = fn.create("transform");
    MObject shape = fn.create("AL_usdmaya_ProxyShape", xform);
    shapeName = fn.name();

    AL::usdmaya::nodes::ProxyShape* proxy = (AL::usdmaya::nodes::ProxyShape*)fn.userNode();

    // force the stage to load
    proxy->filePathPlug().setString("/tmp/AL_usdmaya_duplicateTransformNames.usda");

    auto stage = proxy->getUsdStage();

    // stage should be valid
    EXPECT_TRUE(stage);

    {
      MObject node1 = proxy->findRequiredPath(SdfPath("/root/one/rig"));
      EXPECT_NE(MObject::kNullObj, node1);
      EXPECT_EQ(MFn::kJoint, node1.apiType());

      MObject node2 = proxy->findRequiredPath(SdfPath("/root/two/rig"));
      EXPECT_NE(MObject::kNullObj, node2);
      EXPECT_EQ(MFn::kJoint, node2.apiType());
    }
  }
  {
    MFileIO::saveAs("/tmp/AL_usdmaya_duplicateTransformNames.ma", 0, true);
    MFileIO::newFile(true);
    MFileIO::open("/tmp/AL_usdmaya_duplicateTransformNames.ma", 0, true);

    // one of the prims should be a joint
    MItDependencyNodes it(MFn::kPluginShape);
    MFnDagNode fn(it.item());

    AL::usdmaya::nodes::ProxyShape* proxy = (AL::usdmaya::nodes::ProxyShape*)fn.userNode();

    {
      MObject node1 = proxy->findRequiredPath(SdfPath("/root/one/rig"));
      EXPECT_NE(MObject::kNullObj, node1);
      EXPECT_EQ(MFn::kJoint, node1.apiType());

      MObject node2 = proxy->findRequiredPath(SdfPath("/root/two/rig"));
      EXPECT_NE(MObject::kNullObj, node2);
      EXPECT_EQ(MFn::kJoint, node2.apiType()) << node2.apiTypeStr()
                                              << " is not MFn::kJoint";

      EXPECT_NE(node1, node2);
    }
  }
}


TEST(ActiveInactive, customTransformType)
{
  MFileIO::newFile(true);

  // Prep us a maya reference file to use!

  // pCube1, pCubeShape1, polyCube1
  MGlobal::executeCommand("polyCube -w 1 -h 1 -d 1 -sd 1 -sh 1 -sw 1", false, false);
  MFileIO::saveAs("/tmp/AL_usdmaya_test_cube.ma", 0, true);
  MFileIO::newFile(true);

  // output a couple of usda files for testing (active and inactive)
  {
    std::ofstream os("/tmp/AL_usdmaya_customTransformType.usda");
    os << g_customTransformType;
  }

  {
    MString shapeName;
    MFnDagNode fn;
    MObject xform = fn.create("transform");
    MObject shape = fn.create("AL_usdmaya_ProxyShape", xform);
    shapeName = fn.name();

    AL::usdmaya::nodes::ProxyShape* proxy = (AL::usdmaya::nodes::ProxyShape*)fn.userNode();

    // force the stage to load
    proxy->filePathPlug().setString("/tmp/AL_usdmaya_customTransformType.usda");

    auto stage = proxy->getUsdStage();

    // stage should be valid
    EXPECT_TRUE(stage);

    // should not be able to select the items in the reference file
    MSelectionList sl;
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(3, sl.length());
    sl.clear();

    MFileIO::saveAs("/tmp/AL_usdmaya_customTransformType.ma", 0, true);

    // one of the prims should be a joint
    MObject node = proxy->findRequiredPath(SdfPath("/root/rig"));
    EXPECT_NE(MObject::kNullObj, node);
    EXPECT_EQ(MFn::kJoint, node.apiType());

    // activate the prim
    MGlobal::executeCommand("AL_usdmaya_ActivatePrim -a false -pp \"/root/rig\" \"AL_usdmaya_ProxyShape1\"", false, false);

    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(0, sl.length());

    MFileIO::saveAs("/tmp/AL_usdmaya_customTransformTypeInactive.ma", 0, true);

    // activate the prim
    MGlobal::executeCommand("AL_usdmaya_ActivatePrim -a true -pp \"/root/rig\" \"AL_usdmaya_ProxyShape1\"", false, false);

    // should now be able to select the items in the reference file
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(3, sl.length());
    sl.clear();
  }

  {
    MFileIO::newFile(true);
    MFileIO::open("/tmp/AL_usdmaya_customTransformType.ma", 0, true);

    // one of the prims should be a joint
    MItDependencyNodes it(MFn::kPluginShape);
    MFnDagNode fn(it.item());

    AL::usdmaya::nodes::ProxyShape* proxy = (AL::usdmaya::nodes::ProxyShape*)fn.userNode();
    MObject node = proxy->findRequiredPath(SdfPath("/root/rig"));
    EXPECT_NE(MObject::kNullObj, node);
    EXPECT_EQ(MFn::kJoint, node.apiType());

    // should not be able to select the items in the reference file
    MSelectionList sl;
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(3, sl.length());
    sl.clear();

    // activate the prim
    MGlobal::executeCommand("AL_usdmaya_ActivatePrim -a false -pp \"/root/rig\" \"AL_usdmaya_ProxyShape1\"", false, false);
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(0, sl.length());

    // activate the prim
    MGlobal::executeCommand("AL_usdmaya_ActivatePrim -a true -pp \"/root/rig\" \"AL_usdmaya_ProxyShape1\"", false, false);
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(3, sl.length());
    sl.clear();
  }

  {
    MFileIO::newFile(true);
    MFileIO::open("/tmp/AL_usdmaya_customTransformTypeInactive.ma", 0, true);

    // one of the prims should be a joint
    MItDependencyNodes it(MFn::kPluginShape);
    MFnDagNode fn(it.item());

    AL::usdmaya::nodes::ProxyShape* proxy = (AL::usdmaya::nodes::ProxyShape*)fn.userNode();
    MObject node = proxy->findRequiredPath(SdfPath("/root/rig"));
    EXPECT_NE(MObject::kNullObj, node);
    EXPECT_EQ(MFn::kJoint, node.apiType());

    // should not be able to select the items in the reference file
    MSelectionList sl;
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(0, sl.length());

    // deactivate the prim
    MGlobal::executeCommand("AL_usdmaya_ActivatePrim -a true -pp \"/root/rig\" \"AL_usdmaya_ProxyShape1\"", false, false);
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(3, sl.length());
    sl.clear();

    // activate the prim
    MGlobal::executeCommand("AL_usdmaya_ActivatePrim -a false -pp \"/root/rig\" \"AL_usdmaya_ProxyShape1\"", false, false);
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(0, sl.length());
  }
}

TEST(ActiveInactive, disable)
{
  MFileIO::newFile(true);

  // Prep us a maya reference file to use!

  // pCube1, pCubeShape1, polyCube1
  MGlobal::executeCommand("polyCube -w 1 -h 1 -d 1 -sd 1 -sh 1 -sw 1", false, false);
  MFileIO::saveAs("/tmp/AL_usdmaya_test_cube.ma", 0, true);
  MFileIO::newFile(true);

  // pSphere1, pSphereShape1, polySphere1
  MGlobal::executeCommand("polySphere", false, false);
  MFileIO::saveAs("/tmp/AL_usdmaya_test_sphere.ma", 0, true);
  MFileIO::newFile(true);

  // output a couple of usda files for testing (active and inactive)
  {
    std::ofstream os("/tmp/AL_usdmaya_activePrim.usda");
    os << g_active;
  }
  {
    std::ofstream os("/tmp/AL_usdmaya_inactivePrim.usda");
    os << g_inactive;
  }
  {
    std::ofstream os("/tmp/AL_usdmaya_variants.usda");
    os << g_variants;
  }

  {
    MString shapeName;
    MFnDagNode fn;
    MObject xform = fn.create("transform");
    MObject shape = fn.create("AL_usdmaya_ProxyShape", xform);
    shapeName = fn.name();

    AL::usdmaya::nodes::ProxyShape* proxy = (AL::usdmaya::nodes::ProxyShape*)fn.userNode();

    // force the stage to load
    proxy->filePathPlug().setString("/tmp/AL_usdmaya_inactivePrim.usda");

    auto stage = proxy->getUsdStage();

    // stage should be valid
    EXPECT_TRUE(stage);

    // should not be able to select the items in the reference file
    MSelectionList sl;
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(0, sl.length());

    // activate the prim
    MGlobal::executeCommand("AL_usdmaya_ActivatePrim -a true -pp \"/root/rig\" \"AL_usdmaya_ProxyShape1\"", false, false);

    // should now be able to select the items in the reference file
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(3, sl.length());
    sl.clear();

    // activate the prim
    MGlobal::executeCommand("AL_usdmaya_ActivatePrim -a false -pp \"/root/rig\" \"AL_usdmaya_ProxyShape1\"", false, false);

    // should now be able to select the items in the reference file
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(0, sl.length());

    // activate the prim
    MGlobal::executeCommand("AL_usdmaya_ActivatePrim -a true -pp \"/root/rig\" \"AL_usdmaya_ProxyShape1\"", false, false);

    // should now be able to select the items in the reference file
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(3, sl.length());
    sl.clear();
  }

  MFileIO::newFile(true);

  {
    MString shapeName;
    MFnDagNode fn;
    MObject xform = fn.create("transform");
    MObject shape = fn.create("AL_usdmaya_ProxyShape", xform);
    shapeName = fn.name();

    AL::usdmaya::nodes::ProxyShape* proxy = (AL::usdmaya::nodes::ProxyShape*)fn.userNode();

    // force the stage to load
    proxy->filePathPlug().setString("/tmp/AL_usdmaya_activePrim.usda");

    auto stage = proxy->getUsdStage();

    // stage should be valid
    EXPECT_TRUE(stage);

    // should be able to select the items in the reference file
    MSelectionList sl;
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(3, sl.length());
    sl.clear();

    // activate the prim
    MGlobal::executeCommand("AL_usdmaya_ActivatePrim -a false -pp \"/root/rig\" \"AL_usdmaya_ProxyShape1\"", false, false);

    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(0, sl.length());

    // activate the prim
    MGlobal::executeCommand("AL_usdmaya_ActivatePrim -a true -pp \"/root/rig\" \"AL_usdmaya_ProxyShape1\"", false, false);

    // should now be able to select the items in the reference file
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(3, sl.length());
    sl.clear();

    // activate the prim
    MGlobal::executeCommand("AL_usdmaya_ActivatePrim -a false -pp \"/root/rig\" \"AL_usdmaya_ProxyShape1\"", false, false);

    // should now be able to select the items in the reference file
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(0, sl.length());
  }

  MFileIO::newFile(true);

  {
    MString shapeName;
    MFnDagNode fn;
    MObject xform = fn.create("transform");
    MObject shape = fn.create("AL_usdmaya_ProxyShape", xform);
    shapeName = fn.name();

    AL::usdmaya::nodes::ProxyShape* proxy = (AL::usdmaya::nodes::ProxyShape*)fn.userNode();

    // force the stage to load
    proxy->filePathPlug().setString("/tmp/AL_usdmaya_variants.usda");

    auto stage = proxy->getUsdStage();

    // stage should be valid
    EXPECT_TRUE(stage);

    // the sphere variant is the default, so that should exist in the scene
    MSelectionList sl;
    EXPECT_TRUE(bool(sl.add("dave:pSphere1")));
    EXPECT_TRUE(bool(sl.add("dave:pSphereShape1")));
    EXPECT_TRUE(bool(sl.add("dave:polySphere1")));
    EXPECT_EQ(3, sl.length());
    sl.clear();

    UsdPrim prim = stage->GetPrimAtPath(SdfPath("/root"));
    if(prim)
    {
      UsdVariantSet actualSet = prim.GetVariantSet("rig_technical");
      if(actualSet)
      {
        // should be able to set the variant to a cube
        EXPECT_TRUE(actualSet.SetVariantSelection("cube"));

        // sphere should not be there, but the cube should be
        EXPECT_FALSE(bool(sl.add("dave:pSphere1")));
        EXPECT_FALSE(bool(sl.add("dave:pSphereShape1")));
        EXPECT_FALSE(bool(sl.add("dave:polySphere1")));
        EXPECT_TRUE(bool(sl.add("dave:pCube1")));
        EXPECT_TRUE(bool(sl.add("dave:pCubeShape1")));
        EXPECT_TRUE(bool(sl.add("dave:polyCube1")));
        EXPECT_EQ(3, sl.length());
        sl.clear();

        // should be able to set the variant back to a sphere
        EXPECT_TRUE(actualSet.SetVariantSelection("sphere"));

        // sphere should not be there, but the cube should be
        EXPECT_TRUE(bool(sl.add("dave:pSphere1")));
        EXPECT_TRUE(bool(sl.add("dave:pSphereShape1")));
        EXPECT_TRUE(bool(sl.add("dave:polySphere1")));
        EXPECT_FALSE(bool(sl.add("dave:pCube1")));
        EXPECT_FALSE(bool(sl.add("dave:pCubeShape1")));
        EXPECT_FALSE(bool(sl.add("dave:polyCube1")));
        EXPECT_EQ(3, sl.length());
        sl.clear();

        // should be able to set the variant back to a sphere
        EXPECT_TRUE(actualSet.SetVariantSelection("cube"));

        // sphere should not be there, but the cube should be
        EXPECT_FALSE(bool(sl.add("dave:pSphere1")));
        EXPECT_FALSE(bool(sl.add("dave:pSphereShape1")));
        EXPECT_FALSE(bool(sl.add("dave:polySphere1")));
        EXPECT_TRUE(bool(sl.add("dave:pCube1")));
        EXPECT_TRUE(bool(sl.add("dave:pCubeShape1")));
        EXPECT_TRUE(bool(sl.add("dave:polyCube1")));
        EXPECT_EQ(3, sl.length());
        sl.clear();

        // should be able to set the variant back to a sphere
        EXPECT_TRUE(actualSet.SetVariantSelection("fredcube"));

        // sphere should not be there, but the cube should be
        EXPECT_FALSE(bool(sl.add("dave:pSphere1")));
        EXPECT_FALSE(bool(sl.add("dave:pSphereShape1")));
        EXPECT_FALSE(bool(sl.add("dave:polySphere1")));
        EXPECT_FALSE(bool(sl.add("dave:pCube1")));
        EXPECT_FALSE(bool(sl.add("dave:pCubeShape1")));
        EXPECT_FALSE(bool(sl.add("dave:polyCube1")));
        EXPECT_TRUE(bool(sl.add("fred:pCube1")));
        EXPECT_TRUE(bool(sl.add("fred:pCubeShape1")));
        EXPECT_TRUE(bool(sl.add("fred:polyCube1")));
        sl.clear();

        // should be able to set the variant back to a sphere
        EXPECT_TRUE(actualSet.SetVariantSelection("cube"));

        // sphere should not be there, but the cube should be
        EXPECT_FALSE(bool(sl.add("dave:pSphere1")));
        EXPECT_FALSE(bool(sl.add("dave:pSphereShape1")));
        EXPECT_FALSE(bool(sl.add("dave:polySphere1")));
        EXPECT_TRUE(bool(sl.add("dave:pCube1")));
        EXPECT_TRUE(bool(sl.add("dave:pCubeShape1")));
        EXPECT_TRUE(bool(sl.add("dave:polyCube1")));
        EXPECT_FALSE(bool(sl.add("fred:pCube1")));
        EXPECT_FALSE(bool(sl.add("fred:pCubeShape1")));
        EXPECT_FALSE(bool(sl.add("fred:polyCube1")));
        EXPECT_EQ(3, sl.length());
        sl.clear();

        // should be able to set the variant back to a sphere
        EXPECT_TRUE(actualSet.SetVariantSelection("cache"));

        // sphere should not be there, but the cube should be
        EXPECT_FALSE(bool(sl.add("dave:pSphere1")));
        EXPECT_FALSE(bool(sl.add("dave:pSphereShape1")));
        EXPECT_FALSE(bool(sl.add("dave:polySphere1")));
        EXPECT_FALSE(bool(sl.add("dave:pCube1")));
        EXPECT_FALSE(bool(sl.add("dave:pCubeShape1")));
        EXPECT_FALSE(bool(sl.add("dave:polyCube1")));
        EXPECT_FALSE(bool(sl.add("fred:pCube1")));
        EXPECT_FALSE(bool(sl.add("fred:pCubeShape1")));
        EXPECT_FALSE(bool(sl.add("fred:polyCube1")));
        EXPECT_EQ(0, sl.length());

        // With any luck, the transform chain above the prim should have been removed leaving us with no
        // AL_usdmaya_transforms in the scene
        {
          MItDependencyNodes iter(MFn::kPluginTransformNode);
          EXPECT_TRUE(iter.isDone());
        }

        // Now when we set the variant back to a maya reference, we should be in a situation where the
        // transform chain has re-appeared, and the correct reference has been imported into the scene
        EXPECT_TRUE(actualSet.SetVariantSelection("cube"));

        // sphere should not be there, but the cube should be
        EXPECT_FALSE(bool(sl.add("dave:pSphere1")));
        EXPECT_FALSE(bool(sl.add("dave:pSphereShape1")));
        EXPECT_FALSE(bool(sl.add("dave:polySphere1")));
        EXPECT_TRUE(bool(sl.add("dave:pCube1")));
        EXPECT_TRUE(bool(sl.add("dave:pCubeShape1")));
        EXPECT_TRUE(bool(sl.add("dave:polyCube1")));
        EXPECT_FALSE(bool(sl.add("fred:pCube1")));
        EXPECT_FALSE(bool(sl.add("fred:pCubeShape1")));
        EXPECT_FALSE(bool(sl.add("fred:polyCube1")));
        EXPECT_EQ(3, sl.length());
        sl.clear();

        // check to make sure the transform chain is back
        {
          MItDependencyNodes iter(MFn::kPluginTransformNode);
          EXPECT_FALSE(iter.isDone());
        }
      }
    }
  }

  MFileIO::newFile(true);

  // * load an active reference
  // * deactivate it
  // * save the scene
  // * load the file back up
  // * activate the reference
  {
    MString shapeName;

    {
      MFnDagNode fn;
      MObject xform = fn.create("transform");
      MObject shape = fn.create("AL_usdmaya_ProxyShape", xform);
      shapeName = fn.name();

      AL::usdmaya::nodes::ProxyShape* proxy = (AL::usdmaya::nodes::ProxyShape*)fn.userNode();

      // force the stage to load
      proxy->filePathPlug().setString("/tmp/AL_usdmaya_activePrim.usda");

      auto stage = proxy->getUsdStage();

      // stage should be valid
      EXPECT_TRUE(stage);

      // should not be able to select the items in the reference file
      MSelectionList sl;
      EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
      EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
      EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
      EXPECT_EQ(3, sl.length());
      sl.clear();

      // activate the prim
      MGlobal::executeCommand("AL_usdmaya_ActivatePrim -a false -pp \"/root/rig\" \"AL_usdmaya_ProxyShape1\"", false, false);

      // should now be able to select the items in the reference file
      EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
      EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
      EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
      EXPECT_EQ(0, sl.length());

      MFileIO::saveAs("/tmp/AL_usdmaya_inactive_prim.ma", 0, true);

      MFileIO::newFile(true);
    }

    MFileIO::open("/tmp/AL_usdmaya_inactive_prim.ma", 0, true);

    MFnDagNode fn;
    MSelectionList sl;
    sl.add(shapeName);
    MObject shape;
    sl.getDependNode(0, shape);
    sl.clear();

    EXPECT_EQ(MStatus(MS::kSuccess), fn.setObject(shape));

    AL::usdmaya::nodes::ProxyShape* proxy = (AL::usdmaya::nodes::ProxyShape*)fn.userNode();
    EXPECT_TRUE(proxy != 0);

    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_FALSE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(0, sl.length());

    // activate the prim, this should ensure the
    MGlobal::executeCommand("AL_usdmaya_ActivatePrim -a true -pp \"/root/rig\" \"AL_usdmaya_ProxyShape1\"", false, false);

    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCube1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:pCubeShape1")));
    EXPECT_TRUE(bool(sl.add("AL_usdmaya_test_cube:polyCube1")));
    EXPECT_EQ(3, sl.length());
    sl.clear();
  }
}

