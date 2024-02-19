#pragma once

#include "IWidget.hpp"
#include "../../helpers/Vector2D.hpp"
#include "../../helpers/Color.hpp"
#include <chrono>
#include <vector>
#include <any>
#include <unordered_map>

struct SPreloadedAsset;

class CPasswordInputField : public IWidget {
  public:
    CPasswordInputField(const Vector2D& viewport, const std::unordered_map<std::string, std::any>& props);

    virtual bool draw(const SRenderData& data);

  private:
    void     updateDots();
    void     updateFade();

    Vector2D size;
    Vector2D pos;

    int      out_thick;

    CColor   inner, outer, font;

    struct {
        float                                 currentAmount  = 0;
        float                                 speedPerSecond = 5; // actually per... something. I am unsure xD
        std::chrono::system_clock::time_point lastFrame;
    } dots;

    struct {
        std::chrono::system_clock::time_point start;
        float                                 a         = 0;
        bool                                  appearing = true;
        bool                                  animated  = false;
    } fade;

    struct {
        std::string      resourceID = "";
        SPreloadedAsset* asset      = nullptr;
    } placeholder;

    bool fadeOnEmpty;
};