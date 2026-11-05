#include "Editor.h"

#include "Controller.h"
#include "Parameters.h"

#include "pluginterfaces/base/ustring.h"
#include "vstgui/vstgui.h"

#include <cmath>

using namespace VSTGUI;

namespace PartialResynth {

namespace {

constexpr int kMargin = 8;
constexpr int kGap = 6;
constexpr int kColumnWidth = 66;
constexpr int kSectionPadding = 6;
constexpr int kSectionHeight = 112;
constexpr int kKnobSize = 44;
constexpr int kTitleRowOffset = 4;
constexpr int kKnobRowOffset = 26;
constexpr int kLabelHeight = 14;
constexpr int kMenuHeight = 20;

constexpr int sectionWidth (int columns)
{
    return columns * kColumnWidth + 2 * kSectionPadding;
}

constexpr int kEditorWidth = 2 * kMargin + 4 * kGap + sectionWidth (2) + sectionWidth (4) + sectionWidth (4) + sectionWidth (5) + sectionWidth (3);
constexpr int kEditorHeight = 2 * kMargin + kSectionHeight;

const CColor kPanelColor (26, 32, 36);
const CColor kSectionColor (40, 49, 54);
const CColor kFrameColor (68, 82, 90);
const CColor kAccentColor (92, 190, 176);
const CColor kTextColor (225, 230, 232);
const CColor kDimTextColor (158, 170, 175);

// A titled background box. Controls are added on top of it.
class SectionView : public CView
{
public:
    SectionView (const CRect& size, const char* title) : CView (size), _title (title)
    {
        setMouseEnabled (false);
    }

    void draw (CDrawContext* context) override
    {
        const auto bounds = getViewSize ();
        context->setFillColor (kSectionColor);
        context->setFrameColor (kFrameColor);
        context->setLineWidth (1.0);
        context->drawRect (bounds, kDrawFilledAndStroked);

        auto titleArea = bounds;
        titleArea.top += kTitleRowOffset;
        titleArea.setHeight (18);
        context->setFont (kNormalFont);
        context->setFontColor (kTextColor);
        context->drawString (_title.data (), titleArea);

        setDirty (false);
    }

private:
    UTF8String _title;
};

CTextLabel* createLabel (const CRect& area, const char* text, const CColor& color)
{
    auto* label = new CTextLabel (area, text);
    label->setFont (kNormalFontSmall);
    label->setFontColor (color);
    label->setHoriAlign (kCenterText);
    label->setStyle (CParamDisplay::kNoFrame);
    label->setTransparency (true);
    label->setMouseEnabled (false);
    return label;
}

} // namespace

Editor::Editor (Controller* controller) : VSTGUIEditor (controller), _controller (controller)
{
    Steinberg::ViewRect size (0, 0, kEditorWidth, kEditorHeight);
    setRect (size);
}

void Editor::addKnob (int x, int y, Steinberg::Vst::ParamID id, const char* title, bool bipolar)
{
    frame->addView (createLabel (CRect (x, y, x + kColumnWidth, y + kLabelHeight), title, kDimTextColor));

    const int knobX = x + (kColumnWidth - kKnobSize) / 2;
    const int knobY = y + kLabelHeight + 2;
    const CRect knobArea (knobX, knobY, knobX + kKnobSize, knobY + kKnobSize);

    int32_t style = CKnob::kCoronaDrawing | CKnob::kCoronaOutline;
    if (bipolar)
        style |= CKnob::kCoronaFromCenter;

    auto* knob = new CKnob (knobArea, this, static_cast<int32_t> (id), nullptr, nullptr, CPoint (0, 0), style);
    knob->setCoronaColor (kAccentColor);
    knob->setColorHandle (kTextColor);
    knob->setColorShadowHandle (kFrameColor);
    knob->setHandleLineWidth (2.0);
    knob->setCoronaInset (2.0);

    const auto& spec = getParamSpec (id);
    knob->setDefaultValue (static_cast<float> (plainToNormalized (spec, spec.defaultValue)));
    frame->addView (knob);

    const int valueY = knobY + kKnobSize + 2;
    auto* value = createLabel (CRect (x, valueY, x + kColumnWidth, valueY + kLabelHeight), "", kTextColor);
    frame->addView (value);

    _bindings.push_back ({ knob, value });
}

void Editor::addMenu (int x, int y, Steinberg::Vst::ParamID id, const char* title)
{
    frame->addView (createLabel (CRect (x, y, x + kColumnWidth, y + kLabelHeight), title, kDimTextColor));

    const int menuY = y + kLabelHeight + 2 + (kKnobSize - kMenuHeight) / 2;
    auto* menu = new COptionMenu (CRect (x + 1, menuY, x + kColumnWidth - 1, menuY + kMenuHeight), this, static_cast<int32_t> (id));
    const auto& spec = getParamSpec (id);
    for (int i = 0; i < spec.labelCount; ++i)
        menu->addEntry (spec.labels[i]);
    menu->setCurrent (0); // A new menu has no selection, and would stay blank at a value of 0.

    menu->setFont (kNormalFontSmall);
    menu->setFontColor (kTextColor);
    menu->setBackColor (kPanelColor);
    menu->setFrameColor (kFrameColor);
    menu->setHoriAlign (kCenterText);
    frame->addView (menu);

    _bindings.push_back ({ menu, nullptr });
}

bool PLUGIN_API Editor::open (void* parent, const PlatformType& platformType)
{
    const CRect bounds (0, 0, kEditorWidth, kEditorHeight);

    frame = new CFrame (bounds, this);
    frame->setBackgroundColor (kPanelColor);

    const int top = kMargin;
    const int knobY = top + kKnobRowOffset;
    int x = kMargin;

    const auto section = [&] (int columns, const char* title) {
        const int left = x;
        frame->addView (new SectionView (CRect (left, top, left + sectionWidth (columns), top + kSectionHeight), title));
        x += sectionWidth (columns) + kGap;
        return left + kSectionPadding;
    };

    {
        const int left = section (2, "Analysis");
        addKnob (left, knobY, kThreshold, "Threshold");
        addKnob (left + kColumnWidth, knobY, kGlide, "Glide");
    }

    const auto addOscillator = [&] (const char* title, ParamId wave, ParamId level, ParamId coarse, ParamId fine) {
        const int left = section (4, title);
        addMenu (left, knobY, wave, "Wave");
        addKnob (left + kColumnWidth, knobY, level, "Level");
        addKnob (left + 2 * kColumnWidth, knobY, coarse, "Coarse", true);
        addKnob (left + 3 * kColumnWidth, knobY, fine, "Fine", true);
    };
    addOscillator ("Oscillator 1", kOsc1Wave, kOsc1Level, kOsc1Coarse, kOsc1Fine);
    addOscillator ("Oscillator 2", kOsc2Wave, kOsc2Level, kOsc2Coarse, kOsc2Fine);

    {
        const int left = section (5, "Filter");
        addMenu (left, knobY, kFilterMode, "Mode");
        addKnob (left + kColumnWidth, knobY, kFilterCutoff, "Cutoff");
        addKnob (left + 2 * kColumnWidth, knobY, kFilterResonance, "Resonance");
        addKnob (left + 3 * kColumnWidth, knobY, kFilterKeyFollow, "Key follow");
        addKnob (left + 4 * kColumnWidth, knobY, kFilterEnvAmount, "Env amount", true);
    }
    {
        const int left = section (3, "Output");
        addKnob (left, knobY, kSpread, "Spread");
        addKnob (left + kColumnWidth, knobY, kDryWet, "Dry/wet");
        addKnob (left + 2 * kColumnWidth, knobY, kVolume, "Volume");
    }

    if (!frame->open (parent, platformType))
    {
        _bindings.clear ();
        frame->forget ();
        frame = nullptr;
        return false;
    }

    syncControls ();

    // Keeps the panel up to date with automation and state changes.
    _syncTimer = makeOwned<CVSTGUITimer> ([this] (CVSTGUITimer*) { syncControls (); }, 33);
    return true;
}

void PLUGIN_API Editor::close ()
{
    _syncTimer = nullptr;
    _bindings.clear ();

    if (frame)
    {
        frame->close (); // This also releases the frame.
        frame = nullptr;
    }
}

void Editor::syncControls ()
{
    for (const auto& binding : _bindings)
    {
        const auto id = static_cast<Steinberg::Vst::ParamID> (binding.control->getTag ());
        const auto value = _controller->getParamNormalized (id);

        if (std::abs (binding.control->getValueNormalized () - static_cast<float> (value)) > 1e-5f)
        {
            binding.control->setValueNormalized (static_cast<float> (value));
            binding.control->invalid ();
        }

        if (!binding.valueLabel)
            continue;

        Steinberg::Vst::String128 text {};
        _controller->getParamStringByValue (id, value, text);

        char ascii[128] {};
        Steinberg::UString (text, str16BufferSize (Steinberg::Vst::String128)).toAscii (ascii, sizeof (ascii));

        UTF8String display (ascii);
        const auto& spec = getParamSpec (id);
        if (spec.units[0] != '\0')
        {
            display += " ";
            display += spec.units;
        }
        binding.valueLabel->setText (display);
    }
}

void Editor::valueChanged (CControl* control)
{
    const auto id = static_cast<Steinberg::Vst::ParamID> (control->getTag ());
    const double value = control->getValueNormalized ();

    _controller->setParamNormalized (id, value);
    _controller->performEdit (id, value);
}

void Editor::controlBeginEdit (CControl* control)
{
    beginEdit (control->getTag ());
}

void Editor::controlEndEdit (CControl* control)
{
    endEdit (control->getTag ());
}

} // namespace PartialResynth
