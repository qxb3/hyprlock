#include "PasswordInputField.hpp"
#include "../Renderer.hpp"
#include "../../core/hyprlock.hpp"
#include "../../core/Auth.hpp"
#include "../../config/ConfigDataValues.hpp"
#include "../../helpers/Log.hpp"
#include <hyprutils/string/String.hpp>
#include <algorithm>
#include <hyprlang.hpp>

using namespace Hyprutils::String;

CPasswordInputField::CPasswordInputField(const Vector2D& viewport_, const std::unordered_map<std::string, std::any>& props, const std::string& output) :
    viewport(viewport_), outputStringPort(output), shadow(this, props, viewport_) {
    try {
        pos                      = CLayoutValueData::fromAnyPv(props.at("position"))->getAbsolute(viewport_);
        size                     = CLayoutValueData::fromAnyPv(props.at("size"))->getAbsolute(viewport_);
        halign                   = std::any_cast<Hyprlang::STRING>(props.at("halign"));
        valign                   = std::any_cast<Hyprlang::STRING>(props.at("valign"));
        outThick                 = std::any_cast<Hyprlang::INT>(props.at("outline_thickness"));
        dots.size                = std::any_cast<Hyprlang::FLOAT>(props.at("dots_size"));
        dots.spacing             = std::any_cast<Hyprlang::FLOAT>(props.at("dots_spacing"));
        dots.center              = std::any_cast<Hyprlang::INT>(props.at("dots_center"));
        dots.rounding            = std::any_cast<Hyprlang::INT>(props.at("dots_rounding"));
        dots.fadeMs              = std::any_cast<Hyprlang::INT>(props.at("dots_fade_time"));
        dots.textFormat          = std::any_cast<Hyprlang::STRING>(props.at("dots_text_format"));
        fadeOnEmpty              = std::any_cast<Hyprlang::INT>(props.at("fade_on_empty"));
        fadeTimeoutMs            = std::any_cast<Hyprlang::INT>(props.at("fade_timeout"));
        hiddenInputState.enabled = std::any_cast<Hyprlang::INT>(props.at("hide_input"));
        rounding                 = std::any_cast<Hyprlang::INT>(props.at("rounding"));
        configPlaceholderText    = std::any_cast<Hyprlang::STRING>(props.at("placeholder_text"));
        configFailText           = std::any_cast<Hyprlang::STRING>(props.at("fail_text"));
        configFailTimeoutMs      = std::any_cast<Hyprlang::INT>(props.at("fail_timeout"));
        fontFamily               = std::any_cast<Hyprlang::STRING>(props.at("font_family"));
        colorConfig.transitionMs = std::any_cast<Hyprlang::INT>(props.at("fail_transition"));
        colorConfig.outer        = CGradientValueData::fromAnyPv(props.at("outer_color"));
        colorConfig.inner        = std::any_cast<Hyprlang::INT>(props.at("inner_color"));
        colorConfig.font         = std::any_cast<Hyprlang::INT>(props.at("font_color"));
        colorConfig.fail         = CGradientValueData::fromAnyPv(props.at("fail_color"));
        colorConfig.check        = CGradientValueData::fromAnyPv(props.at("check_color"));
        colorConfig.both         = CGradientValueData::fromAnyPv(props.at("bothlock_color"));
        colorConfig.caps         = CGradientValueData::fromAnyPv(props.at("capslock_color"));
        colorConfig.num          = CGradientValueData::fromAnyPv(props.at("numlock_color"));
        colorConfig.invertNum    = std::any_cast<Hyprlang::INT>(props.at("invert_numlock"));
        colorConfig.swapFont     = std::any_cast<Hyprlang::INT>(props.at("swap_font_color"));
    } catch (const std::bad_any_cast& e) {
        RASSERT(false, "Failed to construct CPasswordInputField: {}", e.what()); //
    } catch (const std::out_of_range& e) {
        RASSERT(false, "Missing property for CPasswordInputField: {}", e.what()); //
    }

    configPos  = pos;
    configSize = size;

    pos                      = posFromHVAlign(viewport, size, pos, halign, valign);
    dots.size                = std::clamp(dots.size, 0.2f, 0.8f);
    dots.spacing             = std::clamp(dots.spacing, -1.f, 1.f);
    colorConfig.transitionMs = std::clamp(colorConfig.transitionMs, 0, 1000);
    colorConfig.both         = colorConfig.both->m_bIsFallback ? colorConfig.outer : colorConfig.both;
    colorConfig.caps         = colorConfig.caps->m_bIsFallback ? colorConfig.outer : colorConfig.caps;
    colorConfig.num          = colorConfig.num->m_bIsFallback ? colorConfig.outer : colorConfig.num;

    colorState.inner       = colorConfig.inner;
    colorState.outer       = *colorConfig.outer;
    colorState.font        = colorConfig.font;
    colorState.outerSource = colorConfig.outer;

    if (!dots.textFormat.empty()) {
        dots.textResourceID = std::format("input:{}-{}", (uintptr_t)this, dots.textFormat);
        CAsyncResourceGatherer::SPreloadRequest request;
        request.id                   = dots.textResourceID;
        request.asset                = dots.textFormat;
        request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
        request.props["font_family"] = fontFamily;
        request.props["color"]       = colorState.font;
        request.props["font_size"]   = (int)(std::nearbyint(size.y * dots.size * 0.5f) * 2.f);

        g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
    }

    // request the inital placeholder asset
    updatePlaceholder();
}

static void fadeOutCallback(std::shared_ptr<CTimer> self, void* data) {
    CPasswordInputField* p = (CPasswordInputField*)data;

    p->onFadeOutTimer();
}

void CPasswordInputField::onFadeOutTimer() {
    fade.allowFadeOut = true;
    fade.fadeOutTimer.reset();

    g_pHyprlock->renderOutput(outputStringPort);
}

void CPasswordInputField::updateFade() {
    if (!fadeOnEmpty) {
        fade.a = 1.0;
        return;
    }

    const bool INPUTUSED = passwordLength > 0 || checkWaiting;

    if (INPUTUSED && fade.allowFadeOut)
        fade.allowFadeOut = false;

    if (INPUTUSED && fade.fadeOutTimer.get()) {
        fade.fadeOutTimer->cancel();
        fade.fadeOutTimer.reset();
    }

    if (!INPUTUSED && fade.a != 0.0 && (!fade.animated || fade.appearing)) {
        if (fade.allowFadeOut || fadeTimeoutMs == 0) {
            fade.a            = 1.0;
            fade.animated     = true;
            fade.appearing    = false;
            fade.start        = std::chrono::system_clock::now();
            fade.allowFadeOut = false;
        } else if (!fade.fadeOutTimer.get())
            fade.fadeOutTimer = g_pHyprlock->addTimer(std::chrono::milliseconds(fadeTimeoutMs), fadeOutCallback, this);
    }

    if (INPUTUSED && fade.a != 1.0 && (!fade.animated || !fade.appearing)) {
        fade.a         = 0.0;
        fade.animated  = true;
        fade.appearing = true;
        fade.start     = std::chrono::system_clock::now();
    }

    if (fade.animated) {
        if (fade.appearing)
            fade.a = std::clamp(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - fade.start).count() / 100000.0, 0.0, 1.0);
        else
            fade.a = std::clamp(1.0 - std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - fade.start).count() / 100000.0, 0.0, 1.0);

        if ((fade.appearing && fade.a == 1.0) || (!fade.appearing && fade.a == 0.0))
            fade.animated = false;

        redrawShadow = true;
    }
}

void CPasswordInputField::updateDots() {
    if (passwordLength == dots.currentAmount)
        return;

    if (std::abs(passwordLength - dots.currentAmount) > 1) {
        dots.currentAmount = std::clamp(dots.currentAmount, passwordLength - 1.f, passwordLength + 1.f);
        dots.lastFrame     = std::chrono::system_clock::now();
    }

    const auto  DELTA = std::clamp((int)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - dots.lastFrame).count(), 0, 20000);

    const float TOADD = dots.fadeMs > 0 ? ((double)DELTA / 1000000.0) * (1000.0 / (double)dots.fadeMs) : 1;

    if (passwordLength > dots.currentAmount) {
        dots.currentAmount += TOADD;
        if (dots.currentAmount > passwordLength)
            dots.currentAmount = passwordLength;
    } else if (passwordLength < dots.currentAmount) {
        dots.currentAmount -= TOADD;
        if (dots.currentAmount < passwordLength)
            dots.currentAmount = passwordLength;
    }

    dots.lastFrame = std::chrono::system_clock::now();
}

bool CPasswordInputField::draw(const SRenderData& data) {
    CBox inputFieldBox = {pos, size};
    CBox outerBox      = {pos - Vector2D{outThick, outThick}, size + Vector2D{outThick * 2, outThick * 2}};

    if (firstRender || redrawShadow) {
        firstRender  = false;
        redrawShadow = false;
        shadow.markShadowDirty();
    }

    bool forceReload = false;

    passwordLength = g_pHyprlock->getPasswordBufferDisplayLen();
    checkWaiting   = g_pAuth->checkWaiting();
    displayFail    = g_pAuth->m_bDisplayFailText;

    updateFade();
    updateDots();
    updateColors();
    updatePlaceholder();
    updateHiddenInputState();

    static auto TIMER = std::chrono::system_clock::now();

    if (placeholder.asset) {
        const auto TARGETSIZEX = placeholder.asset->texture.m_vSize.x + inputFieldBox.h;

        if (size.x < TARGETSIZEX) {
            const auto DELTA = std::clamp((int)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - TIMER).count(), 8000, 20000);
            TIMER            = std::chrono::system_clock::now();
            forceReload      = true;

            size.x += std::clamp((TARGETSIZEX - size.x) * DELTA / 100000.0, 1.0, 1000.0);

            if (size.x > TARGETSIZEX) {
                size.x       = TARGETSIZEX;
                redrawShadow = true;
            }
        }

        pos = posFromHVAlign(viewport, size, configPos, halign, valign);
    } else if (size.x != configSize.x) {
        size.x = configSize.x;
        pos    = posFromHVAlign(viewport, size, configPos, halign, valign);
    }

    SRenderData shadowData = data;
    shadowData.opacity *= fade.a;
    shadow.draw(shadowData);

    CGradientValueData outerGrad = colorState.outer;
    for (auto& c : outerGrad.m_vColors)
        c.a *= fade.a * data.opacity;

    CColor innerCol = colorState.inner;
    innerCol.a *= fade.a * data.opacity;
    CColor fontCol = colorState.font;
    fontCol.a *= fade.a * data.opacity;

    if (outThick > 0) {
        const auto OUTERROUND = rounding == -1 ? outerBox.h / 2.0 : rounding;
        g_pRenderer->renderBorder(outerBox, outerGrad, outThick, OUTERROUND, fade.a * data.opacity);

        if (passwordLength != 0 && hiddenInputState.enabled && !fade.animated && data.opacity == 1.0) {
            CBox     outerBoxScaled = outerBox;
            Vector2D p              = outerBox.pos();
            outerBoxScaled.translate(-p).scale(0.5).translate(p);
            if (hiddenInputState.lastQuadrant > 1)
                outerBoxScaled.y += outerBoxScaled.h;
            if (hiddenInputState.lastQuadrant % 2 == 1)
                outerBoxScaled.x += outerBoxScaled.w;
            glEnable(GL_SCISSOR_TEST);
            glScissor(outerBoxScaled.x, outerBoxScaled.y, outerBoxScaled.w, outerBoxScaled.h);
            g_pRenderer->renderBorder(outerBox, hiddenInputState.lastColor, outThick, OUTERROUND, fade.a * data.opacity);
            glScissor(0, 0, viewport.x, viewport.y);
            glDisable(GL_SCISSOR_TEST);
        }
    }

    g_pRenderer->renderRect(inputFieldBox, innerCol, rounding == -1 ? inputFieldBox.h / 2.0 : rounding - outThick - 1);

    if (!hiddenInputState.enabled && !g_pHyprlock->m_bFadeStarted) {
        const int RECTPASSSIZE = std::nearbyint(inputFieldBox.h * dots.size * 0.5f) * 2.f;
        Vector2D  passSize{RECTPASSSIZE, RECTPASSSIZE};
        int       passSpacing = std::floor(passSize.x * dots.spacing);

        if (!dots.textFormat.empty()) {
            if (!dots.textAsset)
                dots.textAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(dots.textResourceID);

            if (!dots.textAsset)
                forceReload = true;
            else {
                passSize    = dots.textAsset->texture.m_vSize;
                passSpacing = std::floor(passSize.x * dots.spacing);
            }
        }

        const int   DOT_PAD        = (inputFieldBox.h - passSize.y) / 2;
        const int   DOT_AREA_WIDTH = inputFieldBox.w - DOT_PAD * 2;                                 // avail width for dots
        const int   MAX_DOTS       = std::round(DOT_AREA_WIDTH * 1.0 / (passSize.x + passSpacing)); // max amount of dots that can fit in the area
        const int   DOT_FLOORED    = std::floor(dots.currentAmount);
        const float DOT_ALPHA      = fontCol.a;

        // Calculate the total width required for all dots including spaces between them
        const int TOTAL_DOTS_WIDTH = (passSize.x + passSpacing) * dots.currentAmount - passSpacing;

        // Calculate starting x-position to ensure dots stay centered within the input field
        int xstart = dots.center ? (DOT_AREA_WIDTH - TOTAL_DOTS_WIDTH) / 2 + DOT_PAD : DOT_PAD;

        if (dots.currentAmount > MAX_DOTS)
            xstart = (inputFieldBox.w + MAX_DOTS * (passSize.x + passSpacing) - passSpacing - 2 * TOTAL_DOTS_WIDTH) / 2;

        if (dots.rounding == -1)
            dots.rounding = passSize.x / 2.0;
        else if (dots.rounding == -2)
            dots.rounding = rounding == -1 ? passSize.x / 2.0 : rounding * dots.size;

        for (int i = 0; i < dots.currentAmount; ++i) {
            if (i < DOT_FLOORED - MAX_DOTS)
                continue;

            if (dots.currentAmount != DOT_FLOORED) {
                if (i == DOT_FLOORED)
                    fontCol.a *= (dots.currentAmount - DOT_FLOORED) * data.opacity;
                else if (i == DOT_FLOORED - MAX_DOTS)
                    fontCol.a *= (1 - dots.currentAmount + DOT_FLOORED) * data.opacity;
            }

            Vector2D dotPosition =
                inputFieldBox.pos() + Vector2D{xstart + (int)inputFieldBox.w % 2 / 2.f + i * (passSize.x + passSpacing), inputFieldBox.h / 2.f - passSize.y / 2.f};
            CBox box{dotPosition, passSize};
            if (!dots.textFormat.empty()) {
                if (!dots.textAsset)
                    break;

                g_pRenderer->renderTexture(box, dots.textAsset->texture, fontCol.a, dots.rounding);
            } else {
                g_pRenderer->renderRect(box, fontCol, dots.rounding);
            }
            fontCol.a = DOT_ALPHA;
        }
    }

    if (passwordLength == 0 && !placeholder.resourceID.empty()) {
        SPreloadedAsset* currAsset = nullptr;

        if (!placeholder.asset)
            placeholder.asset = g_pRenderer->asyncResourceGatherer->getAssetByID(placeholder.resourceID);

        currAsset = placeholder.asset;

        if (currAsset) {
            Vector2D pos = outerBox.pos() + outerBox.size() / 2.f;
            pos          = pos - currAsset->texture.m_vSize / 2.f;
            CBox textbox{pos, currAsset->texture.m_vSize};
            g_pRenderer->renderTexture(textbox, currAsset->texture, data.opacity * fade.a, 0);
        } else
            forceReload = true;
    }

    return dots.currentAmount != passwordLength || fade.animated || colorState.animated || redrawShadow || data.opacity < 1.0 || forceReload;
}

static void failTimeoutCallback(std::shared_ptr<CTimer> self, void* data) {
    g_pAuth->m_bDisplayFailText = false;
    g_pHyprlock->renderAllOutputs();
}

void CPasswordInputField::updatePlaceholder() {
    if (passwordLength != 0) {
        if (placeholder.asset && /* keep prompt asset cause it is likely to be used again */ displayFail) {
            std::erase(placeholder.registeredResourceIDs, placeholder.resourceID);
            g_pRenderer->asyncResourceGatherer->unloadAsset(placeholder.asset);
            placeholder.asset      = nullptr;
            placeholder.resourceID = "";
            redrawShadow           = true;
        }
        return;
    }

    const auto AUTHFEEDBACK   = g_pAuth->m_bDisplayFailText ? g_pAuth->getLastFailText().value_or("Ups, no fail text?") : g_pAuth->getLastPrompt().value_or("Ups, no prompt?");
    const auto ALLOWCOLORSWAP = outThick == 0 && colorConfig.swapFont;

    if (!ALLOWCOLORSWAP && placeholder.lastAuthFeedback == AUTHFEEDBACK && g_pHyprlock->getPasswordFailedAttempts() == placeholder.failedAttempts)
        return;

    placeholder.failedAttempts   = g_pHyprlock->getPasswordFailedAttempts();
    placeholder.lastAuthFeedback = AUTHFEEDBACK;

    placeholder.asset = nullptr;

    if (displayFail) {
        g_pHyprlock->addTimer(std::chrono::milliseconds(configFailTimeoutMs), failTimeoutCallback, nullptr);
        placeholder.currentText = configFailText;
        replaceInString(placeholder.currentText, "$FAIL", AUTHFEEDBACK);
        replaceInString(placeholder.currentText, "$ATTEMPTS", std::to_string(placeholder.failedAttempts));
    } else {
        placeholder.currentText = configPlaceholderText;
        replaceInString(placeholder.currentText, "$PROMPT", AUTHFEEDBACK);
    }

    placeholder.resourceID =
        std::format("placeholder:{}{}{}{}{}{}", placeholder.currentText, (uintptr_t)this, colorState.font.r, colorState.font.g, colorState.font.b, colorState.font.a);

    if (std::find(placeholder.registeredResourceIDs.begin(), placeholder.registeredResourceIDs.end(), placeholder.resourceID) != placeholder.registeredResourceIDs.end())
        return;

    placeholder.registeredResourceIDs.push_back(placeholder.resourceID);

    // query
    CAsyncResourceGatherer::SPreloadRequest request;
    request.id                   = placeholder.resourceID;
    request.asset                = placeholder.currentText;
    request.type                 = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
    request.props["font_family"] = fontFamily;
    request.props["color"]       = colorState.font;
    request.props["font_size"]   = (int)size.y / 4;
    g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
}

void CPasswordInputField::updateHiddenInputState() {
    if (!hiddenInputState.enabled || (size_t)hiddenInputState.lastPasswordLength == passwordLength)
        return;

    // randomize new thang
    hiddenInputState.lastPasswordLength = passwordLength;

    srand(std::chrono::system_clock::now().time_since_epoch().count());
    float r1 = (rand() % 100) / 255.0;
    float r2 = (rand() % 100) / 255.0;
    int   r3 = rand() % 3;
    int   r4 = rand() % 2;
    int   r5 = rand() % 2;

    ((float*)&hiddenInputState.lastColor.r)[r3]            = r1 + 155 / 255.0;
    ((float*)&hiddenInputState.lastColor.r)[(r3 + r4) % 3] = r2 + 155 / 255.0;

    for (int i = 0; i < 3; ++i) {
        if (i != r3 && i != ((r3 + r4) % 3)) {
            ((float*)&hiddenInputState.lastColor.r)[i] = 1.0 - ((float*)&hiddenInputState.lastColor.r)[r5 ? r3 : ((r3 + r4) % 3)];
        }
    }

    hiddenInputState.lastColor.a  = 1.0;
    hiddenInputState.lastQuadrant = (hiddenInputState.lastQuadrant + rand() % 3 + 1) % 4;
}

static void changeChannel(const float& source, const float& target, float& subject, const double& multi, bool& animated) {

    const float DELTA = target - source;

    if (subject != target) {
        subject += DELTA * multi;
        animated = true;

        if ((source < target && subject > target) || (source > target && subject < target))
            subject = target;
    }
}

static void changeColor(const CColor& source, const CColor& target, CColor& subject, const double& multi, bool& animated) {

    changeChannel(source.r, target.r, subject.r, multi, animated);
    changeChannel(source.g, target.g, subject.g, multi, animated);
    changeChannel(source.b, target.b, subject.b, multi, animated);
    changeChannel(source.a, target.a, subject.a, multi, animated);
}

static void changeGrad(CGradientValueData* psource, CGradientValueData* ptarget, CGradientValueData& subject, const double& multi, bool& animated) {
    if (!psource || !ptarget)
        return;

    subject.m_vColors.resize(ptarget->m_vColors.size(), subject.m_vColors.back());

    for (size_t i = 0; i < subject.m_vColors.size(); ++i) {
        const CColor& sourceCol = (i < psource->m_vColors.size()) ? psource->m_vColors[i] : psource->m_vColors.back();
        const CColor& targetCol = (i < ptarget->m_vColors.size()) ? ptarget->m_vColors[i] : ptarget->m_vColors.back();
        changeColor(sourceCol, targetCol, subject.m_vColors[i], multi, animated);
    }

    if (psource->m_fAngle != ptarget->m_fAngle) {
        const float DELTA = ptarget->m_fAngle - psource->m_fAngle;
        subject.m_fAngle += DELTA * multi;
        animated = true;

        if ((psource->m_fAngle < ptarget->m_fAngle && subject.m_fAngle > ptarget->m_fAngle) || (psource->m_fAngle > ptarget->m_fAngle && subject.m_fAngle < ptarget->m_fAngle))
            subject.m_fAngle = ptarget->m_fAngle;
    }
}

void CPasswordInputField::updateColors() {
    const bool BORDERLESS = outThick == 0;
    const bool NUMLOCK    = (colorConfig.invertNum) ? !g_pHyprlock->m_bNumLock : g_pHyprlock->m_bNumLock;
    const auto MULTI      = colorConfig.transitionMs == 0 ?
             1.0 :
             std::clamp(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - colorState.lastFrame).count() / (double)colorConfig.transitionMs,
                        0.0016, 0.5);

    //
    CGradientValueData* targetGrad = nullptr;

    if (checkWaiting)
        targetGrad = colorConfig.check;
    else if (displayFail)
        targetGrad = colorConfig.fail;

    if (g_pHyprlock->m_bCapsLock && NUMLOCK)
        targetGrad = colorConfig.both;
    else if (g_pHyprlock->m_bCapsLock)
        targetGrad = colorConfig.caps;
    else if (NUMLOCK)
        targetGrad = colorConfig.num;

    CGradientValueData* outerTarget = colorConfig.outer;
    CColor              innerTarget = colorConfig.inner;
    CColor              fontTarget  = (displayFail) ? colorConfig.fail->m_vColors.front() : colorConfig.font;

    if (checkWaiting || displayFail || g_pHyprlock->m_bCapsLock || NUMLOCK) {
        if (BORDERLESS && colorConfig.swapFont) {
            fontTarget = colorConfig.fail->m_vColors.front();
        } else if (BORDERLESS && !colorConfig.swapFont) {
            innerTarget = colorConfig.fail->m_vColors.front();
            // When changing the inner color the font cannot be fail_color
            fontTarget = colorConfig.font;
        } else {
            outerTarget = targetGrad;
        }
    }

    if (targetGrad != colorState.currentTarget) {
        colorState.outerSource = &colorState.outer;
        colorState.innerSource = colorState.inner;

        colorState.currentTarget = targetGrad;
    }

    colorState.animated = false;

    if (!BORDERLESS)
        changeGrad(colorState.outerSource, outerTarget, colorState.outer, MULTI, colorState.animated);
    changeColor(colorState.innerSource, innerTarget, colorState.inner, MULTI, colorState.animated);

    // Font color is only chaned, when `swap_font_color` is set to true and no border is present.
    // It is not animated, because that does not look good and we would need to rerender the text for each frame.
    colorState.font = fontTarget;

    colorState.lastFrame = std::chrono::system_clock::now();
}
