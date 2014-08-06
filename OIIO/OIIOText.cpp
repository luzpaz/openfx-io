/*
 OIIOText plugin.
 Write text on images using OIIO.

 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.

 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.

 Neither the name of the {organization} nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 INRIA
 Domaine de Voluceau
 Rocquencourt - B.P. 105
 78153 Le Chesnay Cedex - France

 */

#ifdef DEBUG

#include "OIIOText.h"


#include "ofxsProcessing.H"
#include "ofxsCopier.h"
#include "ofxsPositionInteract.h"

#include "IOUtility.h"
#include "ofxNatron.h"
#include <OpenImageIO/imageio.h>
/*
 unfortunately, OpenImageIO/imagebuf.h includes OpenImageIO/thread.h,
 which includes boost/thread.hpp,
 which includes boost/system/error_code.hpp,
 which requires the library boost_system to get the symbol boost::system::system_category().

 the following define prevents including error_code.hpp, which is not used anyway.
 */
#define OPENIMAGEIO_THREAD_H
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>

#define kPluginName "OIIOText"
#define kPluginGrouping "Draw"
#define kPluginDescription  "Use OpenImageIO to write text on images."

#define kPluginIdentifier "fr.inria.openfx:OIIOText"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kPositionParamName "position"
#define kPositionParamLabel "Position"
#define kPositionParamHint "The position where starts the baseline of the first character."

#define kTextParamName "text"
#define kTextParamLabel "Text"
#define kTextParamHint "The text that will be drawn on the image"

#define kFontSizeParamName "fontSize"
#define kFontSizeParamLabel "Size"
#define kFontSizeParamHint "The height of the characters to render in pixels"

#define kFontNameParamName "fontName"
#define kFontNameParamLabel "Font"
#define kFontNameParamHint "The name of the font to be used. Defaults to some reasonable system font."

#define kTextColorParamName "textColor"
#define kTextColorParamLabel "Color"
#define kTextColorParamHint "The color of the text to render"

#define kSupportsTiles 0
#define kSupportsMultiResolution 0
#define kRenderThreadSafety eRenderInstanceSafe

using namespace OFX;

class OIIOTextPlugin : public OFX::ImageEffect
{
public:

    OIIOTextPlugin(OfxImageEffectHandle handle);

    virtual ~OIIOTextPlugin();

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);

    /* override is identity */
    virtual bool isIdentity(const OFX::RenderArguments &args, OFX::Clip * &identityClip, double &identityTime);

    /* override changedParam */
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);

    /* override changed clip */
    //virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName);

    // override the rod call
    //virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod);

    // override the roi call
    //virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois);

private:


private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;

    OFX::Double2DParam *position_;
    OFX::StringParam *text_;
    OFX::IntParam *fontSize_;
    OFX::StringParam *fontName_;
    OFX::RGBAParam *textColor_;
};

OIIOTextPlugin::OIIOTextPlugin(OfxImageEffectHandle handle)
: OFX::ImageEffect(handle)
, dstClip_(0)
, srcClip_(0)
{
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    assert(dstClip_ && (dstClip_->getPixelComponents() == OFX::ePixelComponentRGBA || dstClip_->getPixelComponents() == OFX::ePixelComponentRGB));
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert(srcClip_ && (srcClip_->getPixelComponents() == OFX::ePixelComponentRGBA || srcClip_->getPixelComponents() == OFX::ePixelComponentRGB));

    position_ = fetchDouble2DParam(kPositionParamName);
    text_ = fetchStringParam(kTextParamName);
    fontSize_ = fetchIntParam(kFontSizeParamName);
    fontName_ = fetchStringParam(kFontNameParamName);
    textColor_ = fetchRGBAParam(kTextColorParamName);
    assert(position_ && text_ && fontSize_ && fontName_ && textColor_);
}

OIIOTextPlugin::~OIIOTextPlugin()
{
}

static OIIO::ImageSpec
imageSpecFromOFXImage(const OfxRectI &rod, const OfxRectI &bounds, OFX::PixelComponentEnum pixelComponents, OFX::BitDepthEnum bitDepth)
{
    OIIO::TypeDesc format;
 	switch (bitDepth) {
		case OFX::eBitDepthUByte:
			format = OIIO::TypeDesc::UINT8;
			break;
		case OFX::eBitDepthUShort:
			format = OIIO::TypeDesc::UINT16;
			break;
		case OFX::eBitDepthHalf:
			format = OIIO::TypeDesc::HALF;
			break;
		case OFX::eBitDepthFloat:
			format = OIIO::TypeDesc::FLOAT;
			break;
		default:
            throwSuiteStatusException(kOfxStatErrFormat);
			break;
	}
    int nchannels = 0, alpha_channel = -1;
    switch (pixelComponents) {
        case OFX::ePixelComponentAlpha:
            nchannels = 1;
            alpha_channel = 0;
            break;
        case OFX::ePixelComponentRGB:
            nchannels = 3;
            break;
        case OFX::ePixelComponentRGBA:
            nchannels = 4;
            alpha_channel = 3;
            break;
        default:
            throwSuiteStatusException(kOfxStatErrFormat);
            break;
    }
    OIIO::ImageSpec spec (format);
    spec.x = bounds.x1;
    spec.y = rod.y2 - bounds.y2;
    spec.width = bounds.x2 - bounds.x1;
    spec.height = bounds.y2 - bounds.y1;
    spec.full_x = rod.x1;
    spec.full_y = 0;
    spec.full_width = rod.x2 - rod.x1;
    spec.full_height = rod.y2 - rod.y1;
    spec.nchannels = nchannels;
    return spec;
}


/* Override the render */
void
OIIOTextPlugin::render(const OFX::RenderArguments &args)
{
    if (!srcClip_) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    assert(srcClip_);
    std::auto_ptr<OFX::Image> srcImg(srcClip_->fetchImage(args.time));
    if (!srcImg.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (srcImg->getRenderScale().x != args.renderScale.x ||
        srcImg->getRenderScale().y != args.renderScale.y ||
        srcImg->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    OFX::BitDepthEnum srcBitDepth = srcImg->getPixelDepth();
    OFX::PixelComponentEnum srcComponents = srcImg->getPixelComponents();

    if (!dstClip_) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    assert(dstClip_);
    std::auto_ptr<OFX::Image> dstImg(dstClip_->fetchImage(args.time));
    if (!dstImg.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dstImg->getRenderScale().x != args.renderScale.x ||
        dstImg->getRenderScale().y != args.renderScale.y ||
        dstImg->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    OFX::BitDepthEnum dstBitDepth = dstImg->getPixelDepth();
    if (dstBitDepth != OFX::eBitDepthFloat || dstBitDepth != srcBitDepth) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    OFX::PixelComponentEnum dstComponents  = dstImg->getPixelComponents();
    if ((dstComponents != OFX::ePixelComponentRGBA && dstComponents != OFX::ePixelComponentRGB && dstComponents != OFX::ePixelComponentAlpha) ||
        dstComponents != srcComponents) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    // are we in the image bounds
    OfxRectI dstBounds = dstImg->getBounds();
    if(args.renderWindow.x1 < dstBounds.x1 || args.renderWindow.x1 >= dstBounds.x2 || args.renderWindow.y1 < dstBounds.y1 || args.renderWindow.y1 >= dstBounds.y2 ||
       args.renderWindow.x2 <= dstBounds.x1 || args.renderWindow.x2 > dstBounds.x2 || args.renderWindow.y2 <= dstBounds.y1 || args.renderWindow.y2 > dstBounds.y2) {
        OFX::throwSuiteStatusException(kOfxStatErrValue);
        //throw std::runtime_error("render window outside of image bounds");
    }

    // NOTE THAT ALL ROIs ARE PROBABLY WRONG IN THE FOLLOWING.
    // THIS CODE WAS NEVER TESTED

    OfxRectI srcRod = srcImg->getRegionOfDefinition();
    OfxRectI srcBounds = srcImg->getBounds();
    OFX::PixelComponentEnum pixelComponents = srcImg->getPixelComponents();
    OFX::BitDepthEnum bitDepth = srcImg->getPixelDepth();
    //int rowBytes = img->getRowBytes();
    OIIO::ImageSpec srcSpec = imageSpecFromOFXImage(srcRod, srcBounds, pixelComponents, bitDepth);
    OIIO::ImageBuf srcBuf(srcSpec, srcImg->getPixelData());

    OfxRectI dstRod = dstImg->getRegionOfDefinition();
    if (!kSupportsTiles) {
        // http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsTiles
        //  If a clip or plugin does not support tiled images, then the host should supply full RoD images to the effect whenever it fetches one.
        assert(srcRod.x1 == srcBounds.x1);
        assert(srcRod.x2 == srcBounds.x2);
        assert(srcRod.y1 == srcBounds.y1);
        //assert(srcRod.y2 == srcBounds.y2); // crashes on Natron if kSupportsTiles=0 & kSupportsMultiResolution=1
        assert(dstRod.x1 == dstBounds.x1);
        assert(dstRod.x2 == dstBounds.x2);
        assert(dstRod.y1 == dstBounds.y1);
        //assert(dstRod.y2 == dstBounds.y2); // crashes on Natron if kSupportsTiles=0 & kSupportsMultiResolution=1
    }
    if (!kSupportsMultiResolution) {
        // http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsMultiResolution
        //   Multiple resolution images mean...
        //    input and output images can be of any size
        //    input and output images can be offset from the origin
        assert(args.renderScale.x == 1. && args.renderScale.y == 1.);
        assert(srcRod.x1 == 0);
        assert(srcRod.y1 == 0);
        assert(srcRod.x1 == dstRod.x1);
        assert(srcRod.x2 == dstRod.x2);
        assert(srcRod.y1 == dstRod.y1);
        //assert(srcRod.y2 == dstRod.y2); // crashes on Natron if kSupportsMultiResolution=0
    }

    // allocate temporary image
    int pixelBytes = getPixelBytes(pixelComponents, bitDepth);
    int tmpRowBytes = (args.renderWindow.x2-args.renderWindow.x1) * pixelBytes;
    size_t memSize = (args.renderWindow.y2-args.renderWindow.y1) * tmpRowBytes;
    OFX::ImageMemory mem(memSize,this);
    float *tmpPixelData = (float*)mem.lock();
    const bool flip = true;
    OIIO::ImageSpec tmpSpec = imageSpecFromOFXImage(srcRod, args.renderWindow, pixelComponents, bitDepth);
    assert(tmpSpec.width == args.renderWindow.x2 - args.renderWindow.x1);
    assert(tmpSpec.width == args.renderWindow.x2 - args.renderWindow.x1);
    assert(tmpSpec.height == args.renderWindow.y2 - args.renderWindow.y1);
    if (flip) {
        // can't figure out how to flip...
        tmpSpec.y = ((tmpSpec.full_y+tmpSpec.full_height-1) - tmpSpec.y) - (tmpSpec.height-1);
        tmpSpec.full_y = 0;
    }
    assert(tmpSpec.width == args.renderWindow.x2 - args.renderWindow.x1);
    assert(tmpSpec.height == args.renderWindow.y2 - args.renderWindow.y1);
    OIIO::ImageBuf tmpBuf(tmpSpec, tmpPixelData);

    // do we have to flip the image?
    if (flip) {
        bool ok = OIIO::ImageBufAlgo::flip(tmpBuf, srcBuf);
        if (!ok) {
            setPersistentMessage(OFX::Message::eMessageError, "", tmpBuf.geterror().c_str());
            throwSuiteStatusException(kOfxStatFailed);
        }
    } else {
        // copy the renderWindow from src to a temp buffer
        tmpBuf.copy_pixels(srcBuf);
    }

    // render text in the temp buffer
    double x, y;
    position_->getValueAtTime(args.time, x, y);
    std::string text;
    text_->getValueAtTime(args.time, text);
    int fontSize;
    fontSize_->getValueAtTime(args.time, fontSize);
    std::string fontName;
    fontName_->getValueAtTime(args.time, fontName);
    double r, g, b, a;
    textColor_->getValueAtTime(args.time, r, g, b, a);
    float textColor[4];
    textColor[0] = r;
    textColor[1] = g;
    textColor[2] = b;
    textColor[3] = a;
    {
        int ytext = flip ? (srcRod.y2-1 - int(y*args.renderScale.y)) : int(y*args.renderScale.y);
        bool ok = OIIO::ImageBufAlgo::render_text(tmpBuf, int(x*args.renderScale.x), ytext, text, int(fontSize*args.renderScale.y), fontName, textColor);
        if (!ok) {
            setPersistentMessage(OFX::Message::eMessageError, "", tmpBuf.geterror().c_str());
            //throwSuiteStatusException(kOfxStatFailed);
        }
    }
    OIIO::ImageSpec dstSpec = imageSpecFromOFXImage(dstRod, dstBounds, pixelComponents, bitDepth);
    OIIO::ImageBuf dstBuf(dstSpec, dstImg->getPixelData());
    
    // do we have to flip the image?
    if (flip) {
        bool ok = OIIO::ImageBufAlgo::flip(dstBuf, tmpBuf);
        if (!ok) {
            setPersistentMessage(OFX::Message::eMessageError, "", tmpBuf.geterror().c_str());
            throwSuiteStatusException(kOfxStatFailed);
        }
    } else {
        // copy the temp buffer to dstImg
        //dstBuf.copy_pixels(tmpBuf); // erases everything out of tmpBuf!
        bool ok = OIIO::ImageBufAlgo::paste(dstBuf, args.renderWindow.x1, srcRod.y2 - args.renderWindow.y2, 0, 0, tmpBuf);
        if (!ok) {
            setPersistentMessage(OFX::Message::eMessageError, "", tmpBuf.geterror().c_str());
            throwSuiteStatusException(kOfxStatFailed);
        }
    }

    // TODO: answer questions:
    // - can we support tiling?
    // - can we support multiresolution by just scaling the coordinates and the font size?
}

bool
OIIOTextPlugin::isIdentity(const OFX::RenderArguments &args, OFX::Clip * &identityClip, double &/*identityTime*/)
{
    std::string text;
    text_->getValueAtTime(args.time, text);
    if (text.empty()) {
        identityClip = srcClip_;
        return true;
    }

    double r, g, b, a;
    textColor_->getValueAtTime(args.time, r, g, b, a);
    if (a == 0.) {
        identityClip = srcClip_;
        return true;
    }

    return false;
}

void
OIIOTextPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    clearPersistentMessage();
}


mDeclarePluginFactory(OIIOTextPluginFactory, {}, {});

namespace {
struct PositionInteractParam {
    static const char *name() { return kPositionParamName; }
};
}

/** @brief The basic describe function, passed a plugin descriptor */
void OIIOTextPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGenerator);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthHalf);
    desc.addSupportedBitDepth(eBitDepthFloat);

    desc.setSupportsTiles(kSupportsTiles); // may be switched to true later?
    desc.setSupportsMultiResolution(kSupportsMultiResolution); // may be switch to true later? don't forget to reduce font size too
    desc.setRenderThreadSafety(kRenderThreadSafety);

    desc.setOverlayInteractDescriptor(new PositionOverlayDescriptor<PositionInteractParam>);
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void OIIOTextPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    //gHostIsNatron = (OFX::getImageEffectHostDescription()->hostName == kOfxNatronHostName);
    
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Text");

    Double2DParamDescriptor* position = desc.defineDouble2DParam(kPositionParamName);
    position->setLabels(kPositionParamLabel, kPositionParamLabel, kPositionParamLabel);
    position->setHint(kPositionParamHint);
    position->setDoubleType(eDoubleTypeXYAbsolute);
    position->setDefaultCoordinateSystem(eCoordinatesNormalised);
    position->setDefault(0.5, 0.5);
    position->setAnimates(true);
    page->addChild(*position);

    StringParamDescriptor* text = desc.defineStringParam(kTextParamName);
    text->setLabels(kTextParamLabel, kTextParamLabel, kTextParamLabel);
    text->setHint(kTextParamHint);
    text->setStringType(eStringTypeMultiLine);
    text->setAnimates(true);
    text->setDefault("Enter text");
    page->addChild(*text);

    IntParamDescriptor* fontSize = desc.defineIntParam(kFontSizeParamName);
    fontSize->setLabels(kFontSizeParamLabel, kFontSizeParamLabel, kFontSizeParamLabel);
    fontSize->setHint(kFontSizeParamHint);
    fontSize->setDefault(16);
    fontSize->setAnimates(true);
    page->addChild(*fontSize);

    StringParamDescriptor* fontName = desc.defineStringParam(kFontNameParamName);
    fontName->setLabels(kFontNameParamLabel, kFontNameParamLabel, kFontNameParamLabel);
    fontName->setHint(kFontNameParamHint);
    fontName->setAnimates(true);
    page->addChild(*fontName);

    RGBAParamDescriptor* textColor = desc.defineRGBAParam(kTextColorParamName);
    textColor->setLabels(kTextColorParamLabel, kTextColorParamLabel, kTextColorParamLabel);
    textColor->setHint(kTextColorParamHint);
    textColor->setDefault(1., 1., 1., 1.);
    textColor->setAnimates(true);
    page->addChild(*textColor);
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* OIIOTextPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new OIIOTextPlugin(handle);
}


void getOIIOTextPluginID(OFX::PluginFactoryArray &ids)
{
    static OIIOTextPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

#endif // DEBUG