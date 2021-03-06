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
#include "pxr/usdImaging/usdImaging/version.h"
#if (USD_IMAGING_API_VERSION >= 7)
  #include "pxr/usdImaging/usdImagingGL/hdEngine.h"
#else
  #include "pxr/usdImaging/usdImaging/hdEngine.h"
#endif

#include "AL/maya/CodeTimings.h"
#include "AL/usdmaya/DebugCodes.h"
#include "AL/usdmaya/Metadata.h"
#include "AL/usdmaya/StageCache.h"
#include "AL/usdmaya/StageData.h"
#include "AL/usdmaya/TypeIDs.h"
#include "AL/usdmaya/Utils.h"
#include "AL/usdmaya/cmds/ProxyShapePostLoadProcess.h"
#include "AL/usdmaya/fileio/SchemaPrims.h"
#include "AL/usdmaya/fileio/TransformIterator.h"
#include "AL/usdmaya/nodes/ProxyShape.h"
#include "AL/usdmaya/nodes/Layer.h"
#include "AL/usdmaya/nodes/Transform.h"
#include "AL/usdmaya/nodes/TransformationMatrix.h"

#include "maya/MFileIO.h"
#include "maya/MFnPluginData.h"
#include "maya/MHWGeometryUtilities.h"
#include "maya/MItDependencyNodes.h"
#include "maya/MPlugArray.h"

#include "pxr/base/arch/systemInfo.h"
#include "pxr/base/tf/fileUtils.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/usd/stageCacheContext.h"

// printf debugging
#if 0 || AL_ENABLE_TRACE
# define Trace(X) std::cout << X << std::endl;
#else
# define Trace(X)
#endif

namespace AL {
namespace usdmaya {
namespace nodes {

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::serialiseTranslatorContext()
{
  serializedTrCtxPlug().setValue(m_schemaNodeDB.context()->serialise());
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::deserialiseTranslatorContext()
{
  MString value;
  serializedTrCtxPlug().getValue(value);
  m_schemaNodeDB.context()->deserialise(value);
}

//----------------------------------------------------------------------------------------------------------------------
static std::string resolvePath(const std::string& filePath)
{
  ArResolver& resolver = ArGetResolver();

  return resolver.Resolve(filePath);
}

//----------------------------------------------------------------------------------------------------------------------
static void beforeSaveScene(void* clientData)
{
  ProxyShape* proxyShape =  static_cast<ProxyShape *>(clientData);
  UsdStageRefPtr stage = proxyShape->getUsdStage();

  if(stage)
  {
    std::string serializeSessionLayerStr;
    stage->GetSessionLayer()->ExportToString(&serializeSessionLayerStr);

    MPlug serializeSessionLayerPlug(proxyShape->thisMObject(), proxyShape->serializedSessionLayer());
    serializeSessionLayerPlug.setValue(convert(serializeSessionLayerStr));

    proxyShape->serialiseTranslatorContext();
    proxyShape->serialiseTransformRefs();
    proxyShape->serialiseSchemaPrims();

    // prior to saving, serialize any modified layers
    MFnDependencyNode fn;
    MItDependencyNodes iter(MFn::kPluginDependNode);
    for(; !iter.isDone(); iter.next())
    {
      fn.setObject(iter.item());
      if(fn.typeId() == Layer::kTypeId)
      {
        Trace("serialising layer: " << fn.name().asChar());
        Layer* layerPtr = (Layer*)fn.userNode();
        layerPtr->populateSerialisationAttributes();
      }
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------
AL_MAYA_DEFINE_NODE(ProxyShape, AL_USDMAYA_PROXYSHAPE, AL_usdmaya);

MObject ProxyShape::m_filePath = MObject::kNullObj;
MObject ProxyShape::m_primPath = MObject::kNullObj;
MObject ProxyShape::m_excludePrimPaths = MObject::kNullObj;
MObject ProxyShape::m_time = MObject::kNullObj;
MObject ProxyShape::m_timeOffset = MObject::kNullObj;
MObject ProxyShape::m_timeScalar = MObject::kNullObj;
MObject ProxyShape::m_outTime = MObject::kNullObj;
MObject ProxyShape::m_complexity = MObject::kNullObj;
MObject ProxyShape::m_outStageData = MObject::kNullObj;
MObject ProxyShape::m_displayGuides = MObject::kNullObj;
MObject ProxyShape::m_displayRenderGuides = MObject::kNullObj;
MObject ProxyShape::m_layers = MObject::kNullObj;
MObject ProxyShape::m_serializedSessionLayer = MObject::kNullObj;
MObject ProxyShape::m_serializedArCtx = MObject::kNullObj;
MObject ProxyShape::m_serializedTrCtx = MObject::kNullObj;
MObject ProxyShape::m_unloaded = MObject::kNullObj;
MObject ProxyShape::m_drivenPrimPaths = MObject::kNullObj;
MObject ProxyShape::m_drivenTranslate = MObject::kNullObj;
MObject ProxyShape::m_drivenScale = MObject::kNullObj;
MObject ProxyShape::m_drivenRotate = MObject::kNullObj;
MObject ProxyShape::m_drivenRotateOrder = MObject::kNullObj;
MObject ProxyShape::m_drivenVisibility = MObject::kNullObj;
MObject ProxyShape::m_inDrivenTransformsData = MObject::kNullObj;
MObject ProxyShape::m_ambient = MObject::kNullObj;
MObject ProxyShape::m_diffuse = MObject::kNullObj;
MObject ProxyShape::m_specular = MObject::kNullObj;
MObject ProxyShape::m_emission = MObject::kNullObj;
MObject ProxyShape::m_shininess = MObject::kNullObj;
MObject ProxyShape::m_serializedRefCounts = MObject::kNullObj;
MObject ProxyShape::m_serializedSchemaPrims = MObject::kNullObj;

//----------------------------------------------------------------------------------------------------------------------
Layer* ProxyShape::getLayer()
{
  MPlug plug(thisMObject(), m_layers);
  MFnDependencyNode fn;

  MPlugArray plugs;
  if(plug.connectedTo(plugs, true, true))
  {
    if(plugs.length())
    {
      if(plugs[0].node().apiType() == MFn::kPluginDependNode)
      {
        if(fn.setObject(plugs[0].node()))
        {
          if(fn.typeId() == Layer::kTypeId)
          {
            return (Layer*)fn.userNode();
          }
          else
          {
            MGlobal::displayError(MString("Invalid connection found on attribute") + plug.name());
          }
        }
        else
        {
          MGlobal::displayError(MString("Invalid connection found on attribute") + plug.name());
        }
      }
      else
      {
        MGlobal::displayError(MString("Invalid connection found on attribute") + plug.name());
      }
    }
  }
  return 0;
}

//----------------------------------------------------------------------------------------------------------------------
Layer* ProxyShape::findLayer(SdfLayerHandle handle)
{
  LAYER_HANDLE_CHECK(handle);
  if(handle)
  {
    Trace("ProxyShape::findLayer: " << handle->GetIdentifier());
    Layer* layer = getLayer();
    if(layer)
    {
      return layer->findLayer(handle);
    }
  }
  // we shouldn't really be able to get here!
  return 0;
}

//----------------------------------------------------------------------------------------------------------------------
MString ProxyShape::findLayerMayaName(SdfLayerHandle handle)
{
  LAYER_HANDLE_CHECK(handle);
  if(handle)
  {
    Trace("ProxyShape::findLayerMayaName: " << handle->GetIdentifier());
    Layer* node = findLayer(handle);
    if(node)
    {
      MFnDependencyNode fn(node->thisMObject());
      return fn.name();
    }
  }
  return MString();
}

//----------------------------------------------------------------------------------------------------------------------
UsdPrim ProxyShape::getUsdPrim(MDataBlock& dataBlock) const
{
  Trace("ProxyShape::getUsdPrim");
  UsdPrim usdPrim;
  StageData* outData = inputDataValue<StageData>(dataBlock, m_outStageData);
  if(outData)
  {
    if(outData->stage)
    {
      usdPrim = (outData->primPath.IsEmpty()) ?
                 outData->stage->GetPseudoRoot() :
                 outData->stage->GetPrimAtPath(outData->primPath);
    }
  }
  return usdPrim;
}

//----------------------------------------------------------------------------------------------------------------------
SdfPathVector ProxyShape::getExcludePrimPaths() const
{
  Trace("ProxyShape::getExcludePrimPaths");

  SdfPathVector result;

  MString paths = excludePrimPathsPlug().asString();
  if(paths.length())
  {
    const char* begin = paths.asChar();
    const char* end = paths.asChar() + paths.length();
    const char* iter = std::find(begin, end, ',');
    while(iter != end)
    {
      result.push_back(SdfPath(std::string(begin, iter)));
      begin = iter + 1;
      iter = std::find(begin, end, ',');
    }
    result.push_back(SdfPath(std::string(begin, end)));
  }
  return result;
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::constructGLImagingEngine()
{
  Trace("ProxyShape::constructGLImagingEngine");
  if (MGlobal::mayaState() != MGlobal::kBatch)
  {
    if(m_stage)
    {
      // delete previous instance
      if(m_engine)
      {
        m_engine->InvalidateBuffers();
        delete m_engine;
      }

      // combine the excluded paths
      SdfPathVector excludedGeometryPaths;
      excludedGeometryPaths.reserve(m_excludedTaggedGeometry.size() + m_excludedGeometry.size());
      excludedGeometryPaths.assign(m_excludedTaggedGeometry.begin(), m_excludedTaggedGeometry.end());
      excludedGeometryPaths.insert(excludedGeometryPaths.end(), m_excludedGeometry.begin(), m_excludedGeometry.end());

      //
      m_engine = new UsdImagingGLHdEngine(m_path, excludedGeometryPaths);
    }
  }
}


//----------------------------------------------------------------------------------------------------------------------
MStatus ProxyShape::setDependentsDirty(const MPlug& plugBeingDirtied, MPlugArray& plugs)
{
  if(plugBeingDirtied == m_time || plugBeingDirtied == m_timeOffset || plugBeingDirtied == m_timeScalar)
  {
    plugs.append(outTimePlug());
    return MS::kSuccess;
  }
  if(plugBeingDirtied == m_filePath)
  {
    MHWRender::MRenderer::setGeometryDrawDirty(thisMObject(), true);
  }
  if (plugBeingDirtied.array() == m_inDrivenTransformsData)
  {
    m_drivenTransformsDirty = true;
    MHWRender::MRenderer::setGeometryDrawDirty(thisMObject(), true);
  }
  return MPxSurfaceShape::setDependentsDirty(plugBeingDirtied, plugs);
}

//----------------------------------------------------------------------------------------------------------------------
MStatus ProxyShape::preEvaluation(const MDGContext & context, const MEvaluationNode& evaluationNode)
{
  if( !context.isNormal() )
      return MStatus::kFailure;
  MStatus status;
  if (evaluationNode.dirtyPlugExists(m_inDrivenTransformsData, &status) && status)
  {
    m_drivenTransformsDirty = true;
    MHWRender::MRenderer::setGeometryDrawDirty(thisMObject(), true);
  }
  return MStatus::kSuccess;
}

//----------------------------------------------------------------------------------------------------------------------
bool ProxyShape::getRenderAttris(void* pattribs, const MHWRender::MFrameContext& drawRequest, const MDagPath& objPath)
{
  UsdImagingGLEngine::RenderParams& attribs = *(UsdImagingGLEngine::RenderParams*)pattribs;
  uint32_t displayStyle = drawRequest.getDisplayStyle();
  uint32_t displayStatus = MHWRender::MGeometryUtilities::displayStatus(objPath);

  // set wireframe colour
  MColor wireColour = MHWRender::MGeometryUtilities::wireframeColor(objPath);
  attribs.wireframeColor = GfVec4f(wireColour.r, wireColour.g, wireColour.b, wireColour.a);

  // determine the shading mode
  const uint32_t wireframeOnShaded1 = (MHWRender::MFrameContext::kWireFrame | MHWRender::MFrameContext::kGouraudShaded);
  const uint32_t wireframeOnShaded2 = (MHWRender::MFrameContext::kWireFrame | MHWRender::MFrameContext::kFlatShaded);
  if((displayStyle & wireframeOnShaded1) == wireframeOnShaded1 ||
     (displayStyle & wireframeOnShaded2) == wireframeOnShaded2) {
    attribs.drawMode = UsdImagingGLEngine::DRAW_WIREFRAME_ON_SURFACE;
  }
  else
  if(displayStyle & MHWRender::MFrameContext::kWireFrame) {
    attribs.drawMode = UsdImagingGLEngine::DRAW_WIREFRAME;
  }
  else
#if MAYA_API_VERSION >= 201600
  if(displayStyle & MHWRender::MFrameContext::kFlatShaded) {
    attribs.drawMode = UsdImagingGLEngine::DRAW_SHADED_FLAT;
    if ((displayStatus == MHWRender::kActive) ||
        (displayStatus == MHWRender::kLead) ||
        (displayStatus == MHWRender::kHilite)) {
      attribs.drawMode = UsdImagingGLEngine::DRAW_WIREFRAME_ON_SURFACE;
    }
  }
  else
#endif
  if(displayStyle & MHWRender::MFrameContext::kGouraudShaded) {
    attribs.drawMode = UsdImagingGLEngine::DRAW_SHADED_SMOOTH;
    if ((displayStatus == MHWRender::kActive) ||
        (displayStatus == MHWRender::kLead) ||
        (displayStatus == MHWRender::kHilite)) {
      attribs.drawMode = UsdImagingGLEngine::DRAW_WIREFRAME_ON_SURFACE;
    }
  }
  else
  if(displayStyle & MHWRender::MFrameContext::kBoundingBox) {
    attribs.drawMode = UsdImagingGLEngine::DRAW_POINTS;
  }

  // set the time for the scene
  attribs.frame = outTimePlug().asMTime().as(MTime::uiUnit());

#if MAYA_API_VERSION >= 201603
  if(displayStyle & MHWRender::MFrameContext::kBackfaceCulling) {
    attribs.cullStyle = UsdImagingGLEngine::CULL_STYLE_BACK;
  }
  else {
    attribs.cullStyle = UsdImagingGLEngine::CULL_STYLE_NOTHING;
  }
#else
  attribs.cullStyle = UsdImagingGLEngine::CULL_STYLE_NOTHING;
#endif

  const float complexities[] = {1.05f, 1.15f, 1.25f, 1.35f, 1.45f, 1.55f, 1.65f, 1.75f, 1.9f}; 
  attribs.complexity = complexities[complexityPlug().asInt()];
  attribs.showGuides = displayGuidesPlug().asBool();
  return true;
}

//----------------------------------------------------------------------------------------------------------------------
ProxyShape::ProxyShape()
  : MPxSurfaceShape(), maya::NodeHelper(), m_schemaNodeDB(this)
{
  Trace("ProxyShape::ProxyShape");
  m_beforeSaveSceneId = MSceneMessage::addCallback(MSceneMessage::kBeforeSave, beforeSaveScene, this);
  m_onSelectionChanged = MEventMessage::addEventCallback(MString("SelectionChanged"), onSelectionChanged, this);

  TfWeakPtr<ProxyShape> me(this);

  m_variantChangedNoticeKey = TfNotice::Register(me, &ProxyShape::variantSelectionListener, m_stage);
  m_objectsChangedNoticeKey = TfNotice::Register(me, &ProxyShape::onObjectsChanged, m_stage);
  m_editTargetChanged = TfNotice::Register(me, &ProxyShape::onEditTargetChanged, m_stage);

}

//----------------------------------------------------------------------------------------------------------------------
ProxyShape::~ProxyShape()
{
  Trace("ProxyShape::~ProxyShape");
  MSceneMessage::removeCallback(m_beforeSaveSceneId);
  MNodeMessage::removeCallback(m_attributeChanged);
  MEventMessage::removeCallback(m_onSelectionChanged);
  TfNotice::Revoke(m_variantChangedNoticeKey);
  TfNotice::Revoke(m_objectsChangedNoticeKey);
  TfNotice::Revoke(m_editTargetChanged);
  if(m_engine)
  {
    m_engine->InvalidateBuffers();
    delete m_engine;
  }
}

//----------------------------------------------------------------------------------------------------------------------
static const char* const rotate_order_strings[] =
{
  "xyz",
  "yzx",
  "zxy",
  "xzy",
  "yxz",
  "zyx",
  0
};

//----------------------------------------------------------------------------------------------------------------------
static const int16_t rotate_order_values[] =
{
  0,
  1,
  2,
  3,
  4,
  5,
  -1
};

//----------------------------------------------------------------------------------------------------------------------
MStatus ProxyShape::initialise()
{
  Trace("ProxyShape::initialise");

  const char* errorString = "ProxyShape::initialize";
  try
  {
    setNodeType(kTypeName);
    addFrame("USD Proxy Shape Node");
    m_serializedSessionLayer = addStringAttr("serializedSessionLayer", "ssl", kCached|kReadable|kWritable|kStorable|kHidden);

    m_serializedArCtx = addStringAttr("serializedArCtx", "arcd", kCached|kReadable|kWritable|kStorable|kHidden);
    m_filePath = addFilePathAttr("filePath", "fp", kCached | kReadable | kWritable | kStorable | kAffectsAppearance, kLoad, "USD Files (*.usd*) (*.usd*);;Alembic Files (*.abc)");
    m_primPath = addStringAttr("primPath", "pp", kCached | kReadable | kWritable | kStorable | kAffectsAppearance);
    m_excludePrimPaths = addStringAttr("excludePrimPaths", "epp", kCached | kReadable | kWritable | kStorable | kAffectsAppearance);
    m_complexity = addInt32Attr("complexity", "cplx", 0, kCached | kConnectable | kReadable | kWritable | kAffectsAppearance | kKeyable | kStorable);
    setMinMax(m_complexity, 0, 8, 0, 4);
    m_outStageData = addDataAttr("outStageData", "od", StageData::kTypeId, kInternal | kReadable | kWritable | kAffectsAppearance);
    m_displayGuides = addBoolAttr("displayGuides", "dg", false, kCached | kKeyable | kWritable | kAffectsAppearance | kStorable);
    m_displayRenderGuides = addBoolAttr("displayRenderGuides", "drg", false, kCached | kKeyable | kWritable | kAffectsAppearance | kStorable);
    m_unloaded = addBoolAttr("unloaded", "ul", false, kCached | kKeyable | kWritable | kAffectsAppearance | kStorable);
    m_serializedTrCtx = addStringAttr("serializedTrCtx", "srtc", kReadable|kWritable|kStorable|kHidden);

    addFrame("USD Timing Information");
    m_time = addTimeAttr("time", "tm", MTime(0.0), kCached | kConnectable | kReadable | kWritable | kStorable | kAffectsAppearance);
    m_timeOffset = addTimeAttr("timeOffset", "tmo", MTime(0.0), kCached | kConnectable | kReadable | kWritable | kStorable | kAffectsAppearance);
    m_timeScalar = addDoubleAttr("timeScalar", "tms", 1.0, kCached | kConnectable | kReadable | kWritable | kStorable | kAffectsAppearance);
    m_outTime = addTimeAttr("outTime", "otm", MTime(0.0), kCached | kConnectable | kReadable | kAffectsAppearance);
    m_layers = addMessageAttr("layers", "lys", kWritable | kReadable | kConnectable | kHidden);

    addFrame("USD Driven Transforms");
    m_drivenPrimPaths = addStringAttr("drivenPrimPaths", "drvpp", kReadable | kWritable | kArray);
    m_drivenRotate = addAngle3Attr("drivenRotate", "drvr", 0, 0, 0, kReadable | kWritable | kInternal | kArray | kConnectable | kKeyable);
    m_drivenRotateOrder = addEnumAttr("drivenRotateOrder", "drvro", kReadable | kWritable | kInternal | kArray | kConnectable | kKeyable, rotate_order_strings, rotate_order_values);
    m_drivenScale = addFloat3Attr("drivenScale", "drvs", 1.0f, 1.0f, 1.0f, kReadable | kWritable | kInternal | kArray | kConnectable | kKeyable);
    m_drivenTranslate = addDistance3Attr("drivenTranslate", "drvt", 0, 0, 0, kReadable | kWritable | kInternal | kArray | kConnectable | kKeyable);
    m_drivenVisibility = addBoolAttr("drivenVisibility", "drvv", true, kReadable | kWritable | kInternal | kArray | kConnectable | kKeyable);
    m_inDrivenTransformsData = addDataAttr("inDrivenTransformsData", "idrvtd", DrivenTransformsData::kTypeId, kWritable | kArray | kConnectable);

    addFrame("OpenGL Display");
    m_ambient = addColourAttr("ambientColour", "amc", MColor(0.1f, 0.1f, 0.1f), kReadable | kWritable | kConnectable | kStorable | kAffectsAppearance);
    m_diffuse = addColourAttr("diffuseColour", "dic", MColor(0.7f, 0.7f, 0.7f), kReadable | kWritable | kConnectable | kStorable | kAffectsAppearance);
    m_specular = addColourAttr("specularColour", "spc", MColor(0.6f, 0.6f, 0.6f), kReadable | kWritable | kConnectable | kStorable | kAffectsAppearance);
    m_emission = addColourAttr("emissionColour", "emc", MColor(0.0f, 0.0f, 0.0f), kReadable | kWritable | kConnectable | kStorable | kAffectsAppearance);
    m_shininess = addFloatAttr("shininess", "shi", 5.0f, kReadable | kWritable | kConnectable | kStorable | kAffectsAppearance);

    m_serializedRefCounts = addStringAttr("serializedRefCounts", "strcs", kReadable | kWritable | kStorable | kHidden);
    m_serializedSchemaPrims = addStringAttr("serializedSchemaPrims", "ssp", kReadable | kWritable | kStorable | kHidden);

    AL_MAYA_CHECK_ERROR(attributeAffects(m_time, m_outTime), errorString);
    AL_MAYA_CHECK_ERROR(attributeAffects(m_timeOffset, m_outTime), errorString);
    AL_MAYA_CHECK_ERROR(attributeAffects(m_timeScalar, m_outTime), errorString);
    AL_MAYA_CHECK_ERROR(attributeAffects(m_filePath, m_outStageData), errorString);
    AL_MAYA_CHECK_ERROR(attributeAffects(m_primPath, m_outStageData), errorString);
    AL_MAYA_CHECK_ERROR(attributeAffects(m_inDrivenTransformsData, m_outStageData), errorString);
  }
  catch (const MStatus& status)
  {
    return status;
  }

  addBaseTemplate("AEsurfaceShapeTemplate");
  generateAETemplate();

  return MS::kSuccess;
}
//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::onEditTargetChanged(UsdNotice::StageEditTargetChanged const& notice, UsdStageWeakPtr const& sender)
{
  Trace("ProxyShape::onEditTargetChanged");
  if (!sender || sender != m_stage)
      return;

  const UsdEditTarget& target = m_stage->GetEditTarget();
  const SdfLayerHandle& layer = target.GetLayer();
  auto layerNode = findLayer(layer);
  if(layerNode)
  {
    layerNode->setHasBeenTheEditTarget(true);
  }
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::onPrimResync(SdfPath primPath, const SdfPathVector& variantPrimsToSwitch)
{
  AL_BEGIN_PROFILE_SECTION(ObjectChanged);
  MFnDagNode fn(thisMObject());
  MDagPath dag_path;
  fn.getPath(dag_path);
  dag_path.pop();

  auto primsToSwitch = huntForNativeNodesUnderPrim(dag_path, primPath, m_schemaNodeDB.translatorManufacture());

  nodes::SchemaNodeRefDB& schemaNodeDB = schemaDB();
  schemaNodeDB.lock();
  schemaNodeDB.removeEntries(variantPrimsToSwitch);
  m_variantSwitchedPrims.clear();

  cleanupTransformRefs();

  MObjectToPrim objsToCreate = filterUpdatablePrims(primsToSwitch);
  schemaNodeDB.context()->updatePrimTypes();

  cmds::ProxyShapePostLoadProcess::createTranformChainsForSchemaPrims(this, primsToSwitch, dag_path, objsToCreate);

  cmds::ProxyShapePostLoadProcess::createSchemaPrims(&schemaNodeDB, objsToCreate);
  schemaNodeDB.unlock();

  // now perform any post-creation fix up
  cmds::ProxyShapePostLoadProcess::connectSchemaPrims(&schemaNodeDB, objsToCreate);

  AL_END_PROFILE_SECTION();

  validateTransforms();
  constructGLImagingEngine();
}

//----------------------------------------------------------------------------------------------------------------------
ProxyShape::MObjectToPrim ProxyShape::filterUpdatablePrims(std::vector<UsdPrim>& variantPrimsToSwitch)
{
  MObjectToPrim objsToCreate;
  fileio::SchemaPrimsUtils schemaPrimUtils(schemaDB().translatorManufacture());
  auto manufacture = schemaDB().translatorManufacture();
  for(auto it = variantPrimsToSwitch.begin(); it != variantPrimsToSwitch.end(); )
  {
    TfToken type = schemaDB().context()->getTypeForPath(it->GetPath());
    fileio::translators::TranslatorRefPtr translator = manufacture.get(type);
    if(type == it->GetTypeName() && translator && translator->supportsUpdate() && translator->needsTransformParent())
    {
      objsToCreate.push_back(std::pair<MObject, UsdPrim>(findRequiredPath(it->GetPath()), *it));
      variantPrimsToSwitch.erase(it);
    }
    else
    {
      ++it;
    }
  }
  return objsToCreate;
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::onObjectsChanged(UsdNotice::ObjectsChanged const& notice, UsdStageWeakPtr const& sender)
{
  if(MFileIO::isOpeningFile())
    return;

  Trace("ProxyShape::onObjectsChanged");
  if (!sender || sender != m_stage)
      return;

  // These paths are subtree-roots representing entire subtrees that may have
  // changed. In this case, we must dump all cached data below these points
  // and repopulate those trees.
  if(m_compositionHasChanged)
  {
    m_compositionHasChanged = false;

    onPrimResync(m_variantChangePath, m_variantSwitchedPrims);
    m_variantSwitchedPrims.clear();
    m_variantChangePath = SdfPath();

    std::stringstream strstr;
    strstr << "Breakdown for Variant Switch:\n";
    maya::Profiler::printReport(strstr);
  }

}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::validateTransforms()
{
  Trace("validateTransforms");
  if(m_stage)
  {
    SdfPathVector pathsToNuke;
    for(auto& it : m_requiredPaths)
    {
      Transform* tm = it.second.m_transform;
      if(!tm)
        continue;

      TransformationMatrix* tmm = tm->transform();
      if(!tmm)
        continue;

      const UsdPrim& prim = tmm->prim();
      if(!prim.IsValid())
      {
        UsdPrim newPrim = m_stage->GetPrimAtPath(it.first);
        if(newPrim)
        {
          std::string transformType;
          newPrim.GetMetadata(Metadata::transformType, &transformType);
          if(newPrim && transformType.empty())
          {
            tm->transform()->setPrim(newPrim);
          }
        }
        else
        {
          pathsToNuke.push_back(it.first);
        }
      }
    }
  }
  Trace("/validateTransforms");
}

//----------------------------------------------------------------------------------------------------------------------
std::vector<UsdPrim> ProxyShape::huntForNativeNodesUnderPrim(
    const MDagPath& proxyTransformPath,
    SdfPath startPath,
    fileio::translators::TranslatorManufacture& manufacture)
{
  Trace("ProxyShape::huntForNativeNodesUnderPrim");
  std::vector<UsdPrim> prims;
  fileio::SchemaPrimsUtils utils(manufacture);

  fileio::TransformIterator it(m_stage->GetPrimAtPath(startPath), proxyTransformPath);
  for(; !it.done(); it.next())
  {
    UsdPrim prim = it.prim();
    if(!prim.IsValid())
    {
      continue;
    }

    if(utils.isSchemaPrim(prim))
    {
      prims.push_back(prim);
    }
  }
  findExcludedGeometry();
  return prims;
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::onPrePrimChanged(const SdfPath& path, SdfPathVector& outPathVector)
{
  Trace("ProxyShape::onPrePrimChanged");
  nodes::SchemaNodeRefDB& db = schemaDB();
  db.preRemoveEntry(path, outPathVector);
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::variantSelectionListener(SdfNotice::LayersDidChange const& notice, UsdStageWeakPtr const& sender)
// In order to detect changes to the variant selection we listen on the SdfNotice::LayersDidChange global notice which is
// sent to indicate that layer contents have changed.  We are then able to access the change list to check if a variant
// selection change happened.  If so, we trigger a ProxyShapePostLoadProcess() which will regenerate the alTransform
// nodes based on the contents of the new variant selection.
{
  if(MFileIO::isOpeningFile())
    return;

  TF_FOR_ALL(itr, notice.GetChangeListMap())
  {
    TF_FOR_ALL(entryIter, itr->second.GetEntryList())
    {
      const SdfPath &path = entryIter->first;
      const SdfChangeList::Entry &entry = entryIter->second;

      Trace("variantSelectionListener, oldPath=" << entry.oldPath.GetString() << ", oldIdentifier=" << entry.oldIdentifier << ", path=" << path.GetText());

      MFnDagNode fn(thisMObject());
      MDagPath dag_path;
      fn.getPath(dag_path);
      dag_path.pop();

      TF_FOR_ALL(it, entry.infoChanged)
      {
        if (it->first == SdfFieldKeys->VariantSelection ||
            it->first == SdfFieldKeys->Active)
        {
          m_compositionHasChanged = true;
          m_variantChangePath = path;
          onPrePrimChanged(m_variantChangePath, m_variantSwitchedPrims);
        }
      }
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::reloadStage(MPlug& plug)
{
  Trace("ProxyShape::reloadStage");

  maya::Profiler::clearAll();
  AL_BEGIN_PROFILE_SECTION(ReloadStage);
  MDataBlock dataBlock = forceCache();
  m_stage = UsdStageRefPtr();

  // Get input attr values
  const MString file = inputStringValue(dataBlock, m_filePath);
  const MString serializedSessionLayer = inputStringValue(dataBlock, m_serializedSessionLayer);
  const MString serializedArCtx = inputStringValue(dataBlock, m_serializedArCtx);

  // TODO initialise the context using the serialised attribute

  // let the usd stage cache deal with caching the usd stage data
  std::string fileString = TfStringTrimRight(file.asChar());

  if (not TfStringStartsWith(fileString, "./"))
  {
    fileString = resolvePath(fileString);
  }

  // Fall back on checking if path is just a standard absolute path
  if (fileString.empty())
  {
    fileString.assign(file.asChar(), file.length());
  }

  TF_DEBUG(ALUSDMAYA_TRANSLATORS).Msg("ProxyShape::reloadStage called for the usd file: %s\n", fileString.c_str());

  // Check path validity
  // Don't try to create a stage for a non-existent file. Some processes
  // such as mbuild may author a file path here does not yet exist until a
  // later operation (e.g., the mayaConvert target will produce the .mb
  // for the USD standin before the usd target runs the usdModelForeman to
  // assemble all the necessary usd files).
  bool isValidPath = (TfStringStartsWith(fileString, "//") ||
                      TfIsFile(fileString, true /*resolveSymlinks*/));

  if (isValidPath)
  {
    AL_BEGIN_PROFILE_SECTION(OpeningUsdStage);
      AL_BEGIN_PROFILE_SECTION(OpeningSessionLayer);

        SdfLayerRefPtr sessionLayer;
        {
          sessionLayer = SdfLayer::CreateAnonymous();
          if(serializedSessionLayer.length() != 0)
          {
            sessionLayer->ImportFromString(convert(serializedSessionLayer));

            auto layer = getLayer();
            if(layer)
            {
              layer->setLayerAndClearAttribute(sessionLayer);
            }
          }
        }

      AL_END_PROFILE_SECTION();

      AL_BEGIN_PROFILE_SECTION(OpenRootLayer);
        SdfLayerRefPtr rootLayer = SdfLayer::FindOrOpen(fileString);
      AL_END_PROFILE_SECTION();

      if(rootLayer)
      {
        AL_BEGIN_PROFILE_SECTION(UsdStageOpen);
        UsdStageCacheContext ctx(StageCache::Get());

        bool unloadedFlag = inputBoolValue(dataBlock, m_unloaded);
        UsdStage::InitialLoadSet loadOperation = unloadedFlag ? UsdStage::LoadNone : UsdStage::LoadAll;

        if (sessionLayer)
        {
          TF_DEBUG(ALUSDMAYA_TRANSLATORS).Msg("ProxyShape::reloadStage is called with extra session layer.\n");
          m_stage = UsdStage::Open(rootLayer, sessionLayer, loadOperation);
        }
        else
        {
          TF_DEBUG(ALUSDMAYA_TRANSLATORS).Msg("ProxyShape::reloadStage is called without any session layer.\n");
          m_stage = UsdStage::Open(rootLayer, loadOperation);
        }

        AL_END_PROFILE_SECTION();
      }
      else
      {
        // file path not valid
        if(file.length())
        {
          TF_DEBUG(ALUSDMAYA_TRANSLATORS).Msg("ProxyShape::reloadStage failed to open the usd file: %s.\n", file.asChar());
          MGlobal::displayWarning(MString("Failed to open usd file \"") + file + "\"");
        }
      }
    AL_END_PROFILE_SECTION();
  }
  else
  if(!fileString.empty())
  {
    TF_DEBUG(ALUSDMAYA_TRANSLATORS).Msg("The usd file is not valid: %s.\n", file.asChar());
    MGlobal::displayWarning(MString("usd file path not valid \"") + file + "\"");
  }

  // Get the prim
  // If no primPath string specified, then use the pseudo-root.
  const SdfPath rootPath(std::string("/"));
  MString primPathStr = inputStringValue(dataBlock, m_primPath);
  if (primPathStr.length())
  {
    m_path = SdfPath(convert(primPathStr));
    UsdPrim prim = m_stage->GetPrimAtPath(m_path);
    if(!prim)
    {
      m_path = rootPath;
    }
  }
  else
  {
    m_path = rootPath;
  }

  if(m_stage && !MFileIO::isOpeningFile())
  {
    AL_BEGIN_PROFILE_SECTION(PostLoadProcess);
      AL_BEGIN_PROFILE_SECTION(FindExcludedGeometry);
        findExcludedGeometry();
      AL_END_PROFILE_SECTION();

      // execute the post load process to import any custom prims
      cmds::ProxyShapePostLoadProcess::initialise(this);

    AL_END_PROFILE_SECTION();
  }

  AL_END_PROFILE_SECTION();

  if(MGlobal::kInteractive == MGlobal::mayaState())
  {
    std::stringstream strstr;
    strstr << "Breakdown for file: " << file << std::endl;
    maya::Profiler::printReport(strstr);
    MGlobal::displayInfo(convert(strstr.str()));
  }
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::constructExcludedPrims()
{
  m_excludedGeometry = getExcludePrimPaths();
  constructGLImagingEngine();
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::onAttributeChanged(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug&, void* clientData)
{
  const SdfPath rootPath(std::string("/"));
  ProxyShape* proxy = (ProxyShape*)clientData;
  if(msg & MNodeMessage::kAttributeSet)
  {
    if(plug == m_filePath)
    {
      proxy->reloadStage(plug);
    }
    else
    if(plug == m_primPath)
    {
      if(proxy->m_stage)
      {
        // Get the prim
        // If no primPath string specified, then use the pseudo-root.
        MString primPathStr = plug.asString();
        if (primPathStr.length())
        {
          proxy->m_path = SdfPath(convert(primPathStr));
          UsdPrim prim = proxy->m_stage->GetPrimAtPath(proxy->m_path);
          if(!prim)
          {
            proxy->m_path = rootPath;
          }
        }
        else
        {
          proxy->m_path = rootPath;
        }
        proxy->constructGLImagingEngine();
      }
    }
    else
    if(plug == m_excludePrimPaths)
    {
      if(proxy->m_stage)
      {
        proxy->constructExcludedPrims();
      }
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::postConstructor()
{
  Trace("ProxyShape::postConstructor");
  setRenderable(true);
  MObject obj = thisMObject();
  m_attributeChanged = MNodeMessage::addAttributeChangedCallback(obj, onAttributeChanged, (void*)this);
}

//----------------------------------------------------------------------------------------------------------------------
bool ProxyShape::primHasExcludedParent(UsdPrim prim)
{
  Trace("ProxyShape::primHasExcludedParent");
  if(prim.IsValid())
  {
    SdfPath primPath = prim.GetPrimPath();
    TF_FOR_ALL(excludedPath, m_excludedTaggedGeometry)
    {
      if (primPath.HasPrefix(*excludedPath))
      {
        return true;
      }
    }
  }

  return false;
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::findExcludedGeometry()
{
  Trace("ProxyShape::findExcludedGeometry");
  if(!m_stage)
    return;

  m_excludedTaggedGeometry.clear();
  MDagPath m_parentPath;

  for(fileio::TransformIterator it(m_stage, m_parentPath); !it.done(); it.next())
  {
    const UsdPrim& prim = it.prim();
    if(!prim.IsValid())
      continue;

    bool excludeGeo = false;
    if(prim.GetMetadata(Metadata::excludeFromProxyShape, &excludeGeo))
    {
      if (excludeGeo)
      {
        m_excludedTaggedGeometry.push_back(prim.GetPrimPath());
      }
    }

    // If prim has exclusion tag or is a descendent of a prim with it, create as Maya geo
    if (excludeGeo or primHasExcludedParent(prim))
    {
      VtValue schemaName(fileio::ALExcludedPrimSchema.GetString());
      prim.SetCustomDataByKey(fileio::ALSchemaType, schemaName);
    }
  }
  constructExcludedPrims();
}

//----------------------------------------------------------------------------------------------------------------------
MStatus ProxyShape::computeOutStageData(const MPlug& plug, MDataBlock& dataBlock)
{
  // create new stage data
  MObject data;
  StageData* usdStageData = createData<StageData>(StageData::kTypeId, data);
  if(!usdStageData)
  {
    return MS::kFailure;
  }

  // Set the output stage data params
  usdStageData->stage = m_stage;
  usdStageData->primPath = m_path;

  // set the cached output value, and flush
  MStatus status = outputDataValue(dataBlock, m_outStageData, usdStageData);
  if(!status)
  {
    return MS::kFailure;
  }
  return status;
}

//----------------------------------------------------------------------------------------------------------------------
bool ProxyShape::isStageValid() const
{
  Trace("ProxyShape::isStageValid");
  MDataBlock dataBlock = const_cast<ProxyShape*>(this)->forceCache();

  StageData* outData = inputDataValue<StageData>(dataBlock, m_outStageData);
  if(outData && outData->stage)
    return true;

  return false;
}

//----------------------------------------------------------------------------------------------------------------------
UsdStageRefPtr ProxyShape::getUsdStage() const
{
  Trace("ProxyShape::getUsdStage");

  MPlug plug(thisMObject(), m_outStageData);
  MObject data;
  plug.getValue(data);
  plug.getValue(data);
  MFnPluginData fnData(data);
  StageData* outData = static_cast<StageData*>(fnData.data());
  if(outData)
  {
    return outData->stage;
  }
  return UsdStageRefPtr();
}

//----------------------------------------------------------------------------------------------------------------------
MStatus ProxyShape::computeOutputTime(const MPlug& plug, MDataBlock& dataBlock, MTime& currentTime)
{
  MTime inTime = inputTimeValue(dataBlock, m_time);
  MTime inTimeOffset = inputTimeValue(dataBlock, m_timeOffset);
  double inTimeScalar = inputDoubleValue(dataBlock, m_timeScalar);
  currentTime.setValue((inTime.as(MTime::uiUnit()) - inTimeOffset.as(MTime::uiUnit())) * inTimeScalar);
  return outputTimeValue(dataBlock, m_outTime, currentTime);
}

//----------------------------------------------------------------------------------------------------------------------
MStatus ProxyShape::compute(const MPlug& plug, MDataBlock& dataBlock)
{
  Trace("ProxyShape::compute " << plug.name().asChar());
  MTime currentTime;
  if(plug == m_outTime)
  {
    return computeOutputTime(plug, dataBlock, currentTime);
  }
  else
  if(plug == m_outStageData)
  {
    MStatus status = computeOutputTime(MPlug(plug.node(), m_outTime), dataBlock, currentTime);
    if (m_drivenTransformsDirty)
    {
      computeDrivenAttributes(plug, dataBlock, currentTime);
    }
    return status == MS::kSuccess ? computeOutStageData(plug, dataBlock) : status;
  }
  return MPxSurfaceShape::compute(plug, dataBlock);
}

//----------------------------------------------------------------------------------------------------------------------
bool ProxyShape::isBounded() const
{
  return true;
}

//----------------------------------------------------------------------------------------------------------------------
MBoundingBox ProxyShape::boundingBox() const
{
  MStatus status;

  // Make sure outStage is up to date
  MDataBlock dataBlock = const_cast<ProxyShape*>(this)->forceCache();

  // This would seem to be superfluous? unless it is actually forcing a DG pull?
  MDataHandle outDataHandle = dataBlock.inputValue(m_outStageData, &status);
  CHECK_MSTATUS_AND_RETURN(status, MBoundingBox() );

  // XXX:aluk
  // If we could cheaply determine whether a stage only has static geometry,
  // we could make this value a constant one for that case, avoiding the
  // memory overhead of a cache entry per frame
  UsdTimeCode currTime = UsdTimeCode(inputDoubleValue(dataBlock, m_outTime));

  // RB: There must be a nicer way of doing this that avoids the map?
  // The time codes are likely to be ranged, so an ordered array + binary search would surely work?
  std::map<UsdTimeCode, MBoundingBox>::const_iterator cacheLookup = m_boundingBoxCache.find(currTime);
  if (cacheLookup != m_boundingBoxCache.end())
  {
    return cacheLookup->second;
  }

  GfBBox3d allBox;
  UsdPrim prim = getUsdPrim(dataBlock);
  if (prim)
  {
    UsdGeomImageable imageablePrim(prim);
    bool showGuides = inputBoolValue(dataBlock, m_displayGuides);
    bool showRenderGuides = inputBoolValue(dataBlock, m_displayRenderGuides);
    if (showGuides and showRenderGuides)
    {
      allBox = imageablePrim.ComputeUntransformedBound(
            currTime,
            UsdGeomTokens->default_,
            UsdGeomTokens->proxy,
            UsdGeomTokens->guide,
            UsdGeomTokens->render);
    }
    else
    if (showGuides and not showRenderGuides)
    {
      allBox = imageablePrim.ComputeUntransformedBound(
            currTime,
            UsdGeomTokens->default_,
            UsdGeomTokens->proxy,
            UsdGeomTokens->guide);
    }
    else if (not showGuides and showRenderGuides)
    {
      allBox = imageablePrim.ComputeUntransformedBound(
            currTime,
            UsdGeomTokens->default_,
            UsdGeomTokens->proxy,
            UsdGeomTokens->render);
    }
    else
    {
      allBox = imageablePrim.ComputeUntransformedBound(
            currTime,
            UsdGeomTokens->default_,
            UsdGeomTokens->proxy);
    }
  }
  else
  {
    return MBoundingBox();
  }

  // insert new cache entry
  MBoundingBox& retval = m_boundingBoxCache[currTime];

  // Convert to GfRange3d to MBoundingBox
  GfRange3d boxRange = allBox.ComputeAlignedBox();
  if (!boxRange.IsEmpty())
  {
    retval = MBoundingBox(MPoint(boxRange.GetMin()[0],
                                 boxRange.GetMin()[1],
                                 boxRange.GetMin()[2]),
                          MPoint(boxRange.GetMax()[0],
                                 boxRange.GetMax()[1],
                                 boxRange.GetMax()[2]));
  }
  else
  {
    retval = MBoundingBox(MPoint(-100000.0f, -100000.0f, -100000.0f), MPoint(100000.0f, 100000.0f, 100000.0f));
  }

  return retval;
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::unloadMayaReferences()
{
  MObjectArray references;
  for(auto it = m_requiredPaths.begin(), e = m_requiredPaths.end(); it != e; ++it)
  {
    MStatus status;
    MFnDependencyNode fn(it->second.m_node, &status);
    if(status)
    {
      MPlug plug = fn.findPlug("message", &status);
      if(status)
      {
        MPlugArray plugs;
        plug.connectedTo(plugs, false, true);
        for(uint32_t i = 0; i < plugs.length(); ++i)
        {
          MObject temp = plugs[i].node();
          if(temp.hasFn(MFn::kReference))
          {
            Trace("unloading reference: " << MFileIO::unloadReferenceByNode(temp));

            MString command = MString("referenceQuery -filename ") + MFnDependencyNode(temp).name();
            MString referenceFilename;
            MStatus returnStatus = MGlobal::executeCommand(command, referenceFilename);
            if (returnStatus != MStatus::kFailure)
            {
              Trace("Removing reference: " << referenceFilename);
              MFileIO::removeReference(referenceFilename);
            }
          }
        }
      }
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------
bool ProxyShape::initPrim(const uint32_t index, MDGContext& ctx)
{
  MStatus status;
  MPlug plug(thisMObject(), m_drivenPrimPaths);
  MPlug element(plug.elementByLogicalIndex(index, &status));
  if(!status)
  {
    std::cout << "element " << index << " not found" << std::endl;
    return false;
  }

  MString path = element.asString();

  if(m_stage)
  {
    if(m_paths.size() >= index)
    {
      m_paths.resize(index + 1);
      m_prims.resize(index + 1);
      drivenTranslatePlug().setNumElements(index + 1);
      drivenScalePlug().setNumElements(index + 1);
      drivenRotatePlug().setNumElements(index + 1);
      drivenRotateOrderPlug().setNumElements(index + 1);
    }
    Trace("ProxyShape::setNumElements" << plug.name());
    m_paths[index] = SdfPath(path.asChar());
    m_prims[index] = m_stage->GetPrimAtPath(m_paths[index]);
    if(m_prims[index])
    {
      UsdGeomXform xform(m_prims[index]);
      bool resetsXformStack = false;
      std::vector<UsdGeomXformOp> xformops = xform.GetOrderedXformOps(&resetsXformStack);
      if(!xformops.empty() && xformops.back().GetOpType() == UsdGeomXformOp::TypeTransform)
      {
        xformops.erase(xformops.end() - 1);
        xform.SetXformOpOrder(xformops, resetsXformStack);
      }
    }
    return !!m_prims[index];
  }
  return false;
}

//----------------------------------------------------------------------------------------------------------------------
bool ProxyShape::getInternalValueInContext(const MPlug& plug, MDataHandle& dataHandle, MDGContext& ctx)
{
  Trace("TRSArrayDriver::getInternalValueInContext " << plug.name());

  bool resetsXformStack = false;
  if(plug.array() == m_drivenVisibility)
  {
    Trace("ProxyShape::setInternalValueInContext visibility");
    uint32_t index = plug.logicalIndex();
    if(m_prims.size() <= index || !m_prims[index])
    {
      if(!initPrim(index, ctx))
      {
        return 0;
      }
    }
    UsdPrim prim = m_prims[index];
    if(prim)
    {
      UsdGeomXform xform(prim);
      TfToken token;
      UsdAttribute attr = xform.GetVisibilityAttr();
      attr.Get(&token);
      dataHandle.set(token == UsdGeomTokens->inherited);
      return true;
    }
  }
  else
  if(plug.array() == m_drivenScale)
  {
    uint32_t index = plug.logicalIndex();
    Trace("TRSArrayDriver::getInternalValueInContext scale " << index);
    if(m_prims.size() <= index || !m_prims[index])
    {
      if(!initPrim(index, ctx))
      {
        return 0;
      }
    }
    UsdPrim prim = m_prims[index];
    if(prim)
    {
      MVector scale(1.0, 1.0, 1.0);
      UsdGeomXform xform(prim);
      std::vector<UsdGeomXformOp> xformops = xform.GetOrderedXformOps(&resetsXformStack);
      for(auto& it : xformops)
      {
        if(it.GetOpType() == UsdGeomXformOp::TypeScale)
        {
          TransformationMatrix::readVector(scale, it);
          break;
        }
      }
      dataHandle.set(scale);
    }
  }
  if(plug.array() == m_drivenScale)
  {
    uint32_t index = plug.logicalIndex();
    Trace("TRSArrayDriver::getInternalValueInContext scale " << index);
    if(m_prims.size() <= index || !m_prims[index])
    {
      if(!initPrim(index, ctx))
      {
        return 0;
      }
    }
    UsdPrim prim = m_prims[index];
    if(prim)
    {
      MVector scale(1.0, 1.0, 1.0);
      UsdGeomXform xform(prim);
      std::vector<UsdGeomXformOp> xformops = xform.GetOrderedXformOps(&resetsXformStack);
      for(auto& it : xformops)
      {
        if(it.GetOpType() == UsdGeomXformOp::TypeScale)
        {
          TransformationMatrix::readVector(scale, it);
          break;
        }
      }
      dataHandle.set(scale);
    }
  }
  else
  if(plug.array() == m_drivenTranslate)
  {
    Trace("TRSArrayDriver::getInternalValueInContext translate");
    uint32_t index = plug.logicalIndex();
    if(m_prims.size() <= index || !m_prims[index])
    {
      if(!initPrim(index, ctx))
      {
        return 0;
      }
    }
    UsdPrim prim = m_prims[index];
    if(prim)
    {
      MVector translate(0.0, 0.0, 0.0);
      UsdGeomXform xform(prim);
      std::vector<UsdGeomXformOp> xformops = xform.GetOrderedXformOps(&resetsXformStack);
      for(auto& it : xformops)
      {
        if(it.GetOpType() == UsdGeomXformOp::TypeTranslate)
        {
          TransformationMatrix::readVector(translate, it);
          break;
        }
      }
      dataHandle.set(translate);
    }
  }
  else
  if(plug.array() == m_drivenRotate)
  {
    Trace("TRSArrayDriver::getInternalValueInContext rotate");
    uint32_t index = plug.logicalIndex();
    if(m_prims.size() <= index || !m_prims[index])
    {
      if(!initPrim(index, ctx))
      {
        return 0;
      }
    }
    UsdPrim prim = m_prims[index];
    const double degToRad = 3.141592654 / 180.0;
    MVector rotation(0, 0, 0);
    if(prim)
    {
      bool done = false;
      UsdGeomXform xform(prim);
      std::vector<UsdGeomXformOp> xformops = xform.GetOrderedXformOps(&resetsXformStack);
      for(auto& it : xformops)
      {
        switch(it.GetOpType())
        {
        case UsdGeomXformOp::TypeRotateX:
          {
            rotation.x = TransformationMatrix::readDouble(it);
            done = true;
          }
          break;
        case UsdGeomXformOp::TypeRotateY:
          {
            rotation.y = TransformationMatrix::readDouble(it);
            done = true;
          }
          break;
        case UsdGeomXformOp::TypeRotateZ:
          {
            rotation.z = TransformationMatrix::readDouble(it);
            done = true;
          }
          break;
        case UsdGeomXformOp::TypeRotateXYZ:
        case UsdGeomXformOp::TypeRotateYZX:
        case UsdGeomXformOp::TypeRotateZXY:
        case UsdGeomXformOp::TypeRotateXZY:
        case UsdGeomXformOp::TypeRotateYXZ:
        case UsdGeomXformOp::TypeRotateZYX:
          {
            TransformationMatrix::readVector(rotation, it);
            done = true;
          }
          break;
        default: break;
        }
        if(done) break;
      }
      dataHandle.set(rotation * degToRad);
    }
  }
  else
  if(plug.array() == m_drivenRotateOrder)
  {
    Trace("TRSArrayDriver::getInternalValueInContext rotateOrder");
    uint32_t index = plug.logicalIndex();
    if(m_prims.size() <= index || !m_prims[index])
    {
      if(!initPrim(index, ctx))
      {
        return 0;
      }
    }
    UsdPrim prim = m_prims[index];
    if(prim)
    {
      int32_t rotateOrder = -1;
      UsdGeomXform xform(prim);
      std::vector<UsdGeomXformOp> xformops = xform.GetOrderedXformOps(&resetsXformStack);
      for(auto it = xformops.end(), end = xformops.end(); it != end && rotateOrder == -1; ++it)
      {
        switch(it->GetOpType())
        {
        case UsdGeomXformOp::TypeRotateX:
        case UsdGeomXformOp::TypeRotateY:
        case UsdGeomXformOp::TypeRotateZ:
        case UsdGeomXformOp::TypeRotateXYZ:
          rotateOrder = 0;
          break;
        case UsdGeomXformOp::TypeRotateYZX:
          rotateOrder = 1;
          break;
        case UsdGeomXformOp::TypeRotateZXY:
          rotateOrder = 2;
          break;
        case UsdGeomXformOp::TypeRotateXZY:
          rotateOrder = 3;
          break;
        case UsdGeomXformOp::TypeRotateYXZ:
          rotateOrder = 4;
          break;
        case UsdGeomXformOp::TypeRotateZYX:
          rotateOrder = 5;
          break;
        default: break;
        }
      }
      dataHandle.set(std::max(rotateOrder, 0));
    }
  }
  else
    return false;

  return true;
}

//----------------------------------------------------------------------------------------------------------------------
UsdGeomXformOp addTranslateOp(UsdGeomXform& xform, std::vector<UsdGeomXformOp>& ops)
{
  UsdGeomXformOp translateOp = xform.AddTranslateOp();
  ops.insert(ops.begin(), translateOp);
  xform.SetXformOpOrder(ops, xform.GetResetXformStack());
  return translateOp;
}

//----------------------------------------------------------------------------------------------------------------------
UsdGeomXformOp addScaleOp(UsdGeomXform& xform, std::vector<UsdGeomXformOp>& ops)
{
  UsdGeomXformOp scaleOp = xform.AddScaleOp();

  auto it = ops.end();
  while(ops.begin() != it)
  {
    --it;
    std::string attrName = it->GetBaseName();
    if(it->IsInverseOp())
      attrName += "INV";

    // if we find somewhere to insert it
    auto type = xformOpToEnum(attrName);
    if(type < kScale)
    {
      ops.insert(it + 1, scaleOp);
      xform.SetXformOpOrder(ops, xform.GetResetXformStack());
      return scaleOp;
    }
  }

  // otherwise if we get here, just insert at the start of the array
  ops.insert(ops.begin(), scaleOp);
  xform.SetXformOpOrder(ops, xform.GetResetXformStack());
  return scaleOp;
}

//----------------------------------------------------------------------------------------------------------------------
UsdGeomXformOp addRotateOp(UsdGeomXform& xform, std::vector<UsdGeomXformOp>& ops)
{
  UsdGeomXformOp rotateOp = xform.AddRotateXYZOp();

  auto it = ops.begin();
  while(ops.end() != it)
  {
    std::string attrName = it->GetBaseName();
    if(it->IsInverseOp())
      attrName += "INV";

    // if we find somewhere to insert it
    auto type = xformOpToEnum(attrName);
    if(type > kRotate)
    {
      ops.insert(it, rotateOp);
      xform.SetXformOpOrder(ops, xform.GetResetXformStack());
      return rotateOp;
    }
    ++it;
  }

  ops.insert(ops.end(), rotateOp);
  xform.SetXformOpOrder(ops, xform.GetResetXformStack());
  return rotateOp;
}

//----------------------------------------------------------------------------------------------------------------------
bool ProxyShape::setInternalValueInContext(const MPlug& plug, const MDataHandle& dataHandle, MDGContext& ctx)
{
  Trace("ProxyShape::setInternalValueInContext " << plug.name());
  if(plug.array() == m_drivenVisibility)
  {
    Trace("ProxyShape::setInternalValueInContext visibility");
    uint32_t index = plug.logicalIndex();
    if(m_prims.size() <= index || !m_prims[index])
    {
      if(!initPrim(index, ctx))
      {
        return 0;
      }
    }
    UsdPrim prim = m_prims[index];
    if(prim)
    {
      UsdGeomXform xform(prim);
      UsdAttribute attr = xform.GetVisibilityAttr();
      attr.Set(dataHandle.asBool() ? UsdGeomTokens->inherited : UsdGeomTokens->invisible);
      return true;
    }
  }
  else
  if(plug.array() == m_drivenTranslate)
  {
    uint32_t index = plug.logicalIndex();
    if(m_prims.size() <= index || !m_prims[index])
    {
      if(!initPrim(index, ctx))
      {
        return 0;
      }
    }
    UsdPrim prim = m_prims[index];
    bool resetsXformStack = false;
    UsdGeomXform xform(prim);
    std::vector<UsdGeomXformOp> xformops = xform.GetOrderedXformOps(&resetsXformStack);
    if(!xformops.empty())
    {
      if(xformops[0].GetOpType() == UsdGeomXformOp::TypeTranslate)
      {
        TransformationMatrix::pushVector(dataHandle.asVector(), xformops[0]);
        return true;
      }
    }

    UsdGeomXformOp translateOp = addTranslateOp(xform, xformops);
    TransformationMatrix::pushVector(dataHandle.asVector(), translateOp);
    return true;
  }
  else
  if(plug.array() == m_drivenRotate)
  {
    uint32_t index = plug.logicalIndex();
    if(m_prims.size() <= index || !m_prims[index])
    {
      if(!initPrim(index, ctx))
      {
        return 0;
      }
    }
    UsdPrim prim = m_prims[index];
    bool resetsXformStack = false;
    UsdGeomXform xform(prim);
    std::vector<UsdGeomXformOp> xformops = xform.GetOrderedXformOps(&resetsXformStack);
    for(auto& it : xformops)
    {
      switch(it.GetOpType())
      {
      case UsdGeomXformOp::TypeRotateX:
        {
          const double radToDeg = 180.0 / 3.141592654;
          MVector rotation = dataHandle.asVector() * radToDeg;
          TransformationMatrix::pushDouble(rotation.x, it);
          return true;
        }
        break;

      case UsdGeomXformOp::TypeRotateY:
        {
          const double radToDeg = 180.0 / 3.141592654;
          MVector rotation = dataHandle.asVector() * radToDeg;
          TransformationMatrix::pushDouble(rotation.y, it);
          return true;
        }
        break;

      case UsdGeomXformOp::TypeRotateZ:
        {
          const double radToDeg = 180.0 / 3.141592654;
          MVector rotation = dataHandle.asVector() * radToDeg;
          TransformationMatrix::pushDouble(rotation.z, it);
          return true;
        }
        break;

      case UsdGeomXformOp::TypeRotateXYZ:
      case UsdGeomXformOp::TypeRotateXZY:
      case UsdGeomXformOp::TypeRotateYXZ:
      case UsdGeomXformOp::TypeRotateYZX:
      case UsdGeomXformOp::TypeRotateZXY:
      case UsdGeomXformOp::TypeRotateZYX:
        {
          const double radToDeg = 180.0 / 3.141592654;
          MVector rotation = dataHandle.asVector() * radToDeg;
          TransformationMatrix::pushVector(rotation, it);
          return true;
        }
        break;
      default: break;
      }
    }


    UsdGeomXformOp rotateOp = addRotateOp(xform, xformops);
    TransformationMatrix::pushVector(dataHandle.asVector(), rotateOp);
    return true;
  }
  else
  if(plug.array() == m_drivenRotateOrder)
  {
    MGlobal::displayError("I'm not sure how to handle changing rotation orders right now. Please bother robb.");
  }
  return false;
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::updateDrivenPrimPaths(uint32_t drivenIndex, std::vector<SdfPath>& drivenPaths,
                                       std::vector<UsdPrim>& drivenPrims, const DrivenTransforms& drivenTransforms)
{
  uint32_t cnt = drivenTransforms.m_drivenPrimPaths.size();
  if (drivenPaths.size() < cnt)
  {
    drivenPaths.resize(cnt);
    drivenPrims.resize(cnt);
  }
  for (uint32_t idx = 0; idx < cnt; ++idx)
  {
    drivenPaths[idx] = SdfPath(drivenTransforms.m_drivenPrimPaths[idx]);
    drivenPrims[idx] = m_stage->GetPrimAtPath(drivenPaths[idx]);
    if (!drivenPrims[idx].IsValid())
    {
      MString warningMsg;
      warningMsg.format("Driven Prim [^1s] at Host [^2s] is not valid.", MString("") + idx, MString("") + drivenIndex);
      MGlobal::displayWarning(warningMsg);
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::updateDrivenTransforms(std::vector<UsdPrim>& drivenPrims,
                                        const DrivenTransforms& drivenTransforms,
                                        const MTime& currentTime)
{
  for (uint32_t i = 0, cnt = drivenTransforms.m_dirtyMatrices.size(); i < cnt; ++i)
  {
    int32_t idx = drivenTransforms.m_dirtyMatrices[i];
    if (idx >= drivenPrims.size())
    {
      continue;
    }
    UsdPrim& usdPrim = drivenPrims[idx];
    if (!usdPrim.IsValid())
    {
      continue;
    }
    UsdGeomXform xform(usdPrim);
    bool resetsXformStack = false;
    std::vector<UsdGeomXformOp> xformops = xform.GetOrderedXformOps(&resetsXformStack);
    bool pushed = false;
    for (auto& it : xformops)
    {
      if (it.GetOpType() == UsdGeomXformOp::TypeTransform)
      {
        TransformationMatrix::pushMatrix(drivenTransforms.m_drivenMatrix[idx], it, currentTime.as(MTime::uiUnit()));
        pushed = true;
        break;
      }
    }
    if (!pushed)
    {
      UsdGeomXformOp xformop = xform.AddTransformOp();
      TransformationMatrix::pushMatrix(drivenTransforms.m_drivenMatrix[idx], xformop, currentTime.as(MTime::uiUnit()));
    }
    Trace("ProxyShape::updateDrivenTransforms t="<<currentTime.as(MTime::uiUnit()) << " "
        << drivenTransforms.m_drivenMatrix[idx][0][0] << " "
        << drivenTransforms.m_drivenMatrix[idx][0][1] << " "
        << drivenTransforms.m_drivenMatrix[idx][0][2] << " "
        << drivenTransforms.m_drivenMatrix[idx][0][3] << " "
        << drivenTransforms.m_drivenMatrix[idx][1][0] << " "
        << drivenTransforms.m_drivenMatrix[idx][1][1] << " "
        << drivenTransforms.m_drivenMatrix[idx][1][2] << " "
        << drivenTransforms.m_drivenMatrix[idx][1][3] << " "
        << drivenTransforms.m_drivenMatrix[idx][2][0] << " "
        << drivenTransforms.m_drivenMatrix[idx][2][1] << " "
        << drivenTransforms.m_drivenMatrix[idx][2][2] << " "
        << drivenTransforms.m_drivenMatrix[idx][2][3] << " "
        << drivenTransforms.m_drivenMatrix[idx][3][0] << " "
        << drivenTransforms.m_drivenMatrix[idx][3][1] << " "
        << drivenTransforms.m_drivenMatrix[idx][3][2] << " "
        << drivenTransforms.m_drivenMatrix[idx][3][3]);
  }
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::updateDrivenVisibility(std::vector<UsdPrim>& drivenPrims,
                                        const DrivenTransforms& drivenTransforms,
                                        const MTime& currentTime)
{
  for (uint32_t i = 0, cnt = drivenTransforms.m_dirtyVisibilities.size(); i < cnt; ++i)
  {
    int32_t idx = drivenTransforms.m_dirtyVisibilities[i];
    if (idx >= drivenPrims.size())
    {
      continue;
    }
    UsdPrim& usdPrim = drivenPrims[idx];
    if (!usdPrim)
    {
      continue;
    }
    UsdGeomXform xform(usdPrim);
    UsdAttribute attr = xform.GetVisibilityAttr();
    attr.Set(drivenTransforms.m_drivenVisibility[idx] ? UsdGeomTokens->inherited : UsdGeomTokens->invisible,
            currentTime.as(MTime::uiUnit()));
  }
}

//----------------------------------------------------------------------------------------------------------------------
MStatus ProxyShape::computeDrivenAttributes(const MPlug& plug, MDataBlock& dataBlock, const MTime& currentTime)
{
  Trace("ProxyShape::computeDrivenAttributes");
  m_drivenTransformsDirty = false;
  MArrayDataHandle drvTransArray = dataBlock.inputArrayValue(m_inDrivenTransformsData);
  uint32_t elemCnt = drvTransArray.elementCount();
  for (uint32_t elemIdx = 0; elemIdx < elemCnt; ++elemIdx)
  {
    drvTransArray.jumpToArrayElement(elemIdx);
    MDataHandle dtHandle = drvTransArray.inputValue();
    DrivenTransformsData* dtData = static_cast<DrivenTransformsData*>(dtHandle.asPluginData());
    if (!dtData)
      continue;
    DrivenTransforms& drivenTransforms = dtData->m_drivenTransforms;
    if (elemIdx >= m_drivenPaths.size())
    {
      m_drivenPaths.resize(elemIdx + 1);
      m_drivenPrims.resize(elemIdx + 1);
    }
    std::vector<SdfPath>& drivenPaths = m_drivenPaths[elemIdx];
    std::vector<UsdPrim>& drivenPrims = m_drivenPrims[elemIdx];

    if (!drivenTransforms.m_drivenPrimPaths.empty())
    {
      updateDrivenPrimPaths(elemIdx, drivenPaths, drivenPrims, drivenTransforms);
    }
    if (!drivenTransforms.m_dirtyMatrices.empty())
    {
      updateDrivenTransforms(drivenPrims, drivenTransforms, currentTime);
      drivenTransforms.m_dirtyMatrices.clear();
    }
    if (!drivenTransforms.m_dirtyVisibilities.empty())
    {
      updateDrivenVisibility(drivenPrims, drivenTransforms, currentTime);
      drivenTransforms.m_dirtyVisibilities.clear();
    }
  }
  return dataBlock.setClean(plug);
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::serialiseTransformRefs()
{
  std::ostringstream oss;
  for(auto iter : m_requiredPaths)
  {
    MFnDagNode fn(iter.second.m_node);
    MDagPath path;
    fn.getPath(path);
    oss << path.fullPathName() << " "
        << iter.first.GetText() << " "
        << uint32_t(iter.second.required()) << " "
        << uint32_t(iter.second.selected()) << " "
        << uint32_t(iter.second.refCount()) << ";";
  }
  serializedRefCountsPlug().setString(oss.str().c_str());
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::deserialiseTransformRefs()
{
  MString str = serializedRefCountsPlug().asString();
  MStringArray strs;
  str.split(';', strs);

  for(uint32_t i = 0, n = strs.length(); i < n; ++i)
  {
    if(strs[i].length())
    {
      MStringArray tstrs;
      strs[i].split(' ', tstrs);
      MString nodeName = tstrs[0];

      MSelectionList sl;
      if(sl.add(nodeName))
      {
        MObject node;
        if(sl.getDependNode(0, node))
        {
          MFnDependencyNode fn(node);
          if(fn.typeId() == AL_USDMAYA_TRANSFORM)
          {
            Transform* ptr = (Transform*)fn.userNode();
            const uint32_t required = tstrs[2].asUnsigned();
            const uint32_t selected = tstrs[3].asUnsigned();
            const uint32_t refCounts = tstrs[4].asUnsigned();
            SdfPath path(tstrs[1].asChar());
            m_requiredPaths.emplace(path, TransformReference(node, ptr, required, selected, refCounts));
          }
          else
          {
            const uint32_t required = tstrs[2].asUnsigned();
            const uint32_t selected = tstrs[3].asUnsigned();
            const uint32_t refCounts = tstrs[4].asUnsigned();
            SdfPath path(tstrs[1].asChar());
            m_requiredPaths.emplace(path, TransformReference(node, 0, required, selected, refCounts));
          }
        }
      }
    }
  }

  serializedRefCountsPlug().setString("");
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::serialiseSchemaPrims()
{
  serializedSchemaPrimsPlug().setString(m_schemaNodeDB.serialize());
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::deserialiseSchemaPrims()
{
  m_schemaNodeDB.deserialize(serializedSchemaPrimsPlug().asString());
  serializedSchemaPrimsPlug().setString("");
}

//----------------------------------------------------------------------------------------------------------------------
ProxyShape::TransformReference::TransformReference(MObject mayaNode, Transform* node, uint32_t r, uint32_t s, uint32_t rc)
  : m_node(mayaNode), m_transform(node)
{
  m_required = r;
  m_selected = s;
  m_refCount = rc;
}

//----------------------------------------------------------------------------------------------------------------------
void ProxyShape::cleanupTransformRefs()
{
  for(auto it = m_requiredPaths.begin(); it != m_requiredPaths.end(); )
  {
    if(!it->second.selected() && !it->second.required() && !it->second.refCount())
    {
      m_requiredPaths.erase(it++);
    }
    else
    {
      ++it;
    }
  }
}

//----------------------------------------------------------------------------------------------------------------------
} // nodes
} // usdmaya
} // AL
//----------------------------------------------------------------------------------------------------------------------
