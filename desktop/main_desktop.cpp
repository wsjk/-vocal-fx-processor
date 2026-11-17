// Desktop simulator of the Demonic vocal pedal: PortAudio runs the engine on the microphone, raylib draws the pedal
// enclosure (OLED, twelve encoders, three footswitches, jacks) and turns mouse and keyboard input into control
// changes. The audio and UI threads talk only through std::atomic values.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <portaudio.h>
#include <string>
#include "raylib.h"
#include "demonic_engine.hpp"
#include "sim_controls.hpp"
#include "sim_font.hpp"
#include "sim_layout.hpp"

#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 64

using simctl::PARAM_COUNT;
using simlayout::Circle;
using simlayout::Layout;

DemonicEngineMemory engineMemory;                  // the engine's long buffers (~576 KB)
DemonicEngine demonic(SAMPLE_RATE, engineMemory);

std::atomic<bool>  guiBypass(false);        // true bypass: dry signal only, effects off
std::atomic<float> guiParam[PARAM_COUNT];   // one per encoder, indexed by simctl::Param (set by selectPreset)
std::atomic<int>   guiActivePreset(0);
std::atomic<bool>  guiSpectralFreeze(false);
std::atomic<float> currentMicPeak(0.0f);

constexpr size_t SCOPE_SIZE = 128;
float scopeBuffer[SCOPE_SIZE] = {0};
std::atomic<size_t> scopeHead(0);

// UI thread: move every encoder to the preset's values (pitch is not part of a preset and stays where it is).
// The values are stored before the index, so the callback never applies a new preset with old encoder values.
static void selectPreset(int idx) {
    float v[PARAM_COUNT];
    simctl::preset_values(demonic.getPreset(idx), guiParam[simctl::P_PITCH].load(), v);
    for (int p = 0; p < PARAM_COUNT; ++p) guiParam[p].store(v[p]);
    guiActivePreset.store(idx);
}

static int audioCallback(const void* inputBuffer, void* outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo* timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void* userData) {
    using namespace simctl;
    const float* in = static_cast<const float*>(inputBuffer);
    float* out = static_cast<float*>(outputBuffer);

    static int lastPreset = -1;
    int currentPreset = guiActivePreset.load();
    if (currentPreset != lastPreset) {
        demonic.loadPreset(currentPreset);    // the encoders already hold the preset's values (selectPreset)
        lastPreset = currentPreset;
    }

    demonic.setBypass(guiBypass.load());          // before the freeze setter below: freeze is refused while bypassed
    float values[PARAM_COUNT];
    for (int p = 0; p < PARAM_COUNT; ++p) values[p] = guiParam[p].load();
    simctl::apply(demonic, values);
    demonic.setSpectralFreeze(guiSpectralFreeze.load());

    float peakIn = 0.0f;
    if (in != nullptr) {
        for (unsigned long i = 0; i < framesPerBuffer; ++i) {
            peakIn = std::max(peakIn, std::abs(in[i]));
        }
        demonic.processBlock(in, out, framesPerBuffer);

        size_t head = scopeHead.load();
        for (unsigned long i = 0; i < framesPerBuffer; ++i) {
            scopeBuffer[head % SCOPE_SIZE] = out[i];
            head++;
        }
        scopeHead.store(head);
    } else {
        std::fill(out, out + framesPerBuffer, 0.0f);
    }

    currentMicPeak.store(peakIn);
    return paContinue;
}

namespace {

// ---- palette -------------------------------------------------------------------------------------
const Color kWindowBg   = {18, 19, 24, 255};
const Color kBodyEdge   = {14, 10, 12, 255};
const Color kBody       = {58, 22, 26, 255};     // oxblood powder coat
const Color kBodyLight  = {84, 36, 40, 255};
const Color kPanelLine  = {120, 70, 72, 255};
const Color kSilk       = {232, 222, 214, 255};
const Color kSilkDim    = {178, 150, 146, 255};
const Color kMetal      = {170, 173, 182, 255};
const Color kMetalDark  = {96, 99, 109, 255};
const Color kShadow     = {12, 8, 10, 255};
const Color kRingOff    = {40, 20, 22, 255};
const Color kAmber      = {255, 176, 46, 255};
const Color kRed        = {255, 70, 60, 255};
const Color kBlueLed    = {96, 170, 255, 255};
const Color kGreenLed   = {90, 255, 130, 255};

Rectangle R(const simlayout::Rect& r) { return Rectangle{r.x, r.y, r.w, r.h}; }
Vector2 V(float x, float y) { return Vector2{x, y}; }
const float kPi = 3.14159265f;

// Text: an embedded monospaced font (DejaVu Sans Mono), loaded at the screen's real resolution so it stays sharp on
// Retina displays. Sizes below are nominal; kUiScale makes them comfortably readable.
Font g_font, g_font_bold;
bool g_font_custom = false;
const float kUiScale = 1.2f;

void load_fonts() {
    const float dpi = std::max(1.0f, GetWindowScaleDPI().x);
    const int base = static_cast<int>(15.0f * dpi + 0.5f);
    g_font = LoadFontFromMemory(".ttf", simfont::kRegular, static_cast<int>(sizeof(simfont::kRegular)), base, nullptr, 0);
    g_font_bold = LoadFontFromMemory(".ttf", simfont::kBold, static_cast<int>(sizeof(simfont::kBold)), base, nullptr, 0);
    g_font_custom = g_font.texture.id != 0 && g_font_bold.texture.id != 0;
    if (g_font_custom) {
        SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(g_font_bold.texture, TEXTURE_FILTER_BILINEAR);
    } else {                                   // could not load the embedded font: fall back to raylib's built-in one
        g_font = GetFontDefault();
        g_font_bold = g_font;
    }
}
void unload_fonts() { if (g_font_custom) { UnloadFont(g_font); UnloadFont(g_font_bold); } }

void ui_text(const char* text, float x, float y, float size, Color c, bool bold = false) {
    DrawTextEx(bold ? g_font_bold : g_font, text, V(x, y), size * kUiScale, 0.0f, c);
}
float ui_measure(const char* text, float size, bool bold = false) {
    return MeasureTextEx(bold ? g_font_bold : g_font, text, size * kUiScale, 0.0f).x;
}
void centered_text(const char* text, float cx, float y, float size, Color c, bool bold = false) {
    ui_text(text, cx - ui_measure(text, size, bold) / 2.0f, y, size, c, bold);
}

// raylib angles: 0 = +x, clockwise. Knob angles: 0 = up, clockwise.
float ray_angle(float knob_deg) { return knob_deg - 90.0f; }

void draw_screw(float x, float y) {
    DrawCircleV(V(x, y), 6, kShadow);
    DrawCircleV(V(x, y), 4.5f, kMetal);
    DrawLineEx(V(x - 3, y + 1), V(x + 3, y - 1), 1.6f, kMetalDark);
}

void draw_led(const Circle& c, Color on_color, bool on) {
    if (on) {
        DrawCircleV(V(c.x, c.y), c.r + 7, Fade(on_color, 0.20f));
        DrawCircleV(V(c.x, c.y), c.r + 3.5f, Fade(on_color, 0.45f));
        DrawCircleV(V(c.x, c.y), c.r, on_color);
        DrawCircleV(V(c.x - 2, c.y - 2), 2, Fade(WHITE, 0.75f));
    } else {
        DrawCircleV(V(c.x, c.y), c.r + 1.5f, kShadow);
        DrawCircleV(V(c.x, c.y), c.r, Color{static_cast<unsigned char>(on_color.r / 6), static_cast<unsigned char>(on_color.g / 6),
                                           static_cast<unsigned char>(on_color.b / 6), 255});
    }
}

void draw_jack(const Circle& j) {
    DrawCircleV(V(j.x, j.y), j.r + 5, kShadow);
    DrawCircleV(V(j.x, j.y), j.r, kMetal);
    DrawCircleV(V(j.x, j.y), j.r - 5, Color{8, 8, 10, 255});
    DrawCircleV(V(j.x, j.y), j.r - 9, kMetalDark);
}

void draw_usb(const simlayout::Rect& r) {
    DrawRectangleRounded(Rectangle{r.x - 3, r.y - 3, r.w + 6, r.h + 6}, 0.5f, 8, kShadow);
    DrawRectangleRounded(R(r), 0.5f, 8, kMetal);
    DrawRectangleRounded(Rectangle{r.x + 3, r.y + 3, r.w - 6, r.h - 6}, 0.5f, 8, Color{8, 8, 10, 255});
    DrawRectangle(static_cast<int>(r.x + 10), static_cast<int>(r.y + r.h / 2 - 1), static_cast<int>(r.w - 20), 2, kMetalDark);
}

void draw_footswitch(const Circle& f, bool pressed) {
    const float dy = pressed ? 2.0f : 0.0f;
    DrawCircleV(V(f.x, f.y), f.r + 6, kShadow);
    DrawCircleV(V(f.x, f.y), f.r, kMetalDark);
    DrawCircleV(V(f.x, f.y), f.r - 5, kMetal);
    const float cap = pressed ? f.r - 15 : f.r - 11;
    DrawCircleV(V(f.x, f.y + dy), cap, Color{128, 131, 141, 255});
    for (int i = 1; i <= 4; ++i) DrawCircleLines(static_cast<int>(f.x), static_cast<int>(f.y + dy), cap * i / 5.0f, Fade(kShadow, 0.35f));
    if (!pressed) DrawCircleV(V(f.x - f.r * 0.22f, f.y - f.r * 0.25f), f.r * 0.30f, Fade(WHITE, 0.12f));
}

// An endless encoder: LED ring showing the value over a 270 degree sweep, knurled cap that turns with the value.
void draw_encoder(const Circle& k, int p, float value, bool active) {
    const simctl::ParamSpec& s = simctl::spec(p);
    const Vector2 c = V(k.x, k.y);
    const float ring_in = Layout::KNOB_RING_R - 7, ring_out = Layout::KNOB_RING_R - 2;
    DrawCircleV(c, Layout::KNOB_RING_R, kShadow);
    DrawRing(c, ring_in, ring_out, ray_angle(simctl::MIN_DEGREES), ray_angle(-simctl::MIN_DEGREES), 48, kRingOff);

    const float a0 = simctl::origin_degrees(p), a1 = simctl::degrees(p, value);
    if (std::isfinite(s.warn_above) && value > s.warn_above) {           // the part past the warning point is red
        const float aw = simctl::degrees(p, s.warn_above);
        DrawRing(c, ring_in, ring_out, ray_angle(a0), ray_angle(aw), 48, kAmber);
        DrawRing(c, ring_in, ring_out, ray_angle(aw), ray_angle(a1), 16, kRed);
    } else if (std::fabs(a1 - a0) > 0.5f) {
        DrawRing(c, ring_in, ring_out, ray_angle(std::min(a0, a1)), ray_angle(std::max(a0, a1)), 48, kAmber);
    }
    if (s.bipolar) DrawCircleV(V(k.x, k.y - ring_out - 3), 2, kSilkDim);  // centre mark

    const float cap = Layout::KNOB_CAP_R;
    DrawCircleV(V(k.x, k.y + 2), cap + 2, kShadow);
    DrawCircleV(c, cap, active ? Color{70, 72, 80, 255} : Color{52, 54, 62, 255});
    DrawCircleV(c, cap * 0.78f, active ? Color{92, 95, 105, 255} : Color{74, 77, 86, 255});
    DrawCircleV(V(k.x - cap * 0.2f, k.y - cap * 0.25f), cap * 0.4f, Fade(WHITE, 0.07f));
    for (int i = 0; i < 18; ++i) {                                      // knurling, turning with the value
        const float a = (a1 + i * 20.0f - 90.0f) * kPi / 180.0f;
        DrawLineEx(V(k.x + std::cos(a) * cap * 0.80f, k.y + std::sin(a) * cap * 0.80f),
                   V(k.x + std::cos(a) * cap * 0.98f, k.y + std::sin(a) * cap * 0.98f), 1.5f, Fade(kShadow, 0.6f));
    }

    char buf[32];
    simctl::format(p, value, buf, sizeof buf);
    centered_text(s.label, k.x, k.y + k.r + 4, 10, kSilk, true);
    centered_text(buf, k.x, k.y + k.r + 20, 9, active ? kAmber : kSilkDim);
}

void draw_group(const simlayout::Rect& r, const char* name) {
    DrawRectangleRounded(R(r), 0.06f, 8, kPanelLine);
    DrawRectangleRounded(Rectangle{r.x + 1.5f, r.y + 1.5f, r.w - 3, r.h - 3}, 0.06f, 8, kBody);
    const float w = ui_measure(name, 10, true) + 16;
    DrawRectangle(static_cast<int>(r.x + 14), static_cast<int>(r.y - 2), static_cast<int>(w), 6, kBody);   // gap in the line for the title
    ui_text(name, r.x + 22, r.y - 7, 10, kSilkDim, true);
}

void draw_oled(const Layout& L, const RenderTexture2D& canvas) {
    const simlayout::Rect& b = L.oled_bezel;
    DrawRectangleRounded(Rectangle{b.x - 3, b.y - 3, b.w + 6, b.h + 6}, 0.06f, 8, kShadow);
    DrawRectangleRounded(R(b), 0.06f, 8, Color{12, 13, 17, 255});
    const Rectangle src = {0.0f, 0.0f, static_cast<float>(canvas.texture.width), static_cast<float>(-canvas.texture.height)};
    DrawTexturePro(canvas.texture, src, R(L.oled_screen), V(0, 0), 0.0f, WHITE);
}

// The 128x64 display, drawn at its real resolution: header line, output waveform, mic level bar.
void render_oled(RenderTexture2D& canvas, const char* header, bool bypass, bool freeze) {
    BeginTextureMode(canvas);
    ClearBackground(BLACK);
    DrawRectangleLines(0, 0, 128, 64, BLUE);
    DrawText(header, 3, 2, 10, SKYBLUE);
    DrawLine(0, 12, 128, 12, Fade(BLUE, 0.5f));

    const Color wave = bypass ? GRAY : (freeze ? RED : SKYBLUE);
    const size_t head = scopeHead.load();
    for (int x = 0; x < 128 - 1; ++x) {
        const float y1 = std::clamp(35.0f - scopeBuffer[(head + x) % SCOPE_SIZE] * 20.0f, 14.0f, 56.0f);
        const float y2 = std::clamp(35.0f - scopeBuffer[(head + x + 1) % SCOPE_SIZE] * 20.0f, 14.0f, 56.0f);
        DrawLine(x, static_cast<int>(y1), x + 1, static_cast<int>(y2), wave);
    }
    const char* tag = bypass ? "BYPASS" : (freeze ? "FREEZE" : nullptr);   // under the header, over the scope
    if (tag) {
        const int w = MeasureText(tag, 10);
        DrawRectangle(124 - w, 14, w + 3, 11, BLACK);
        DrawText(tag, 125 - w, 15, 10, RED);
    }
    const float peak = currentMicPeak.load();
    DrawRectangle(2, 58, static_cast<int>(std::min(peak * 124.0f, 124.0f)), 4, freeze ? RED : BLUE);
    EndTextureMode();
}

} // namespace

int main(int argc, char** argv) {
    // `vocal_desktop --screenshot file.png`: run for about a second, save the window as file.png in the
    // current directory (raylib keeps only the file name), and quit. Used for docs/images/simulator.png.
    const char* screenshotFile = (argc == 3 && std::string(argv[1]) == "--screenshot") ? argv[2] : nullptr;
    int frameCount = 0;

    guiParam[simctl::P_PITCH].store(0.0f);
    selectPreset(0);                         // encoders start on preset 0 (before audio starts: the callback reads them)

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << "\n";
        return 1;
    }

    PaStream* stream;
    err = Pa_OpenDefaultStream(&stream, 1, 1, paFloat32, SAMPLE_RATE, FRAMES_PER_BUFFER, audioCallback, nullptr);
    if (err != paNoError) {
        std::cerr << "Stream open error: " << Pa_GetErrorText(err) << "\n";
        Pa_Terminate();
        return 1;
    }
    Pa_StartStream(stream);

    const Layout L = Layout::make();
    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);   // sharp text and smooth rings on Retina screens
    InitWindow(L.window_w, L.window_h, "Demonic Vocal FX " VFX_VERSION " - Pedal Simulator");
    load_fonts();
    SetTargetFPS(60);

    RenderTexture2D oledCanvas = LoadRenderTexture(Layout::OLED_W, Layout::OLED_H);

    int drag_param = -1;                     // encoder being dragged with the mouse
    float drag_pos = 0.0f;                   // where the hand has taken it (0..1), so snapping does not stick
    simlayout::KnobDrag knob_drag;
    float wheel_acc = 0.0f;
    int wheel_param = -1;
    int touched = -1;                        // last encoder moved: its value is shown on the OLED for a moment
    double touched_until = 0.0, preset_flash_until = 0.0;
    int foot_down = -1;

    while (!WindowShouldClose()) {
        const double now = GetTime();
        auto touch = [&](int p) { touched = p; touched_until = now + 1.5; };
        auto nextPreset = [&]() {
            selectPreset((guiActivePreset.load() + 1) % demonic.getPresetCount());
            preset_flash_until = now + 0.2;
            touched = -1;                    // show the new preset's name
        };
        // Bypass: effects off, your untouched voice straight through. Freeze cannot survive a bypass, so it is
        // switched off at the same time, and cannot be switched on while bypassed.
        auto toggleBypass = [&]() {
            const bool on = !guiBypass.load();
            guiBypass.store(on);
            if (on) guiSpectralFreeze.store(false);
        };
        auto toggleFreeze = [&]() { if (!guiBypass.load()) guiSpectralFreeze.store(!guiSpectralFreeze.load()); };

        // ---- keyboard ----
        if (IsKeyPressed(KEY_P)) nextPreset();
        if (IsKeyPressed(KEY_F)) toggleFreeze();
        if (IsKeyPressed(KEY_B) || IsKeyPressed(KEY_SPACE)) toggleBypass();

        // ---- mouse: encoders (drag or scroll) and footswitches (click) ----
        const Vector2 m = GetMousePosition();
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            for (int p = 0; p < PARAM_COUNT; ++p) {
                if (!simlayout::hit_circle(L.encoders[p], m.x, m.y)) continue;
                drag_param = p;
                drag_pos = simctl::norm(p, guiParam[p].load());
                knob_drag.begin(L.encoders[p], m.x, m.y);
                touch(p);
            }
            for (int i = 0; i < simlayout::FOOTSWITCH_COUNT; ++i) {
                if (!simlayout::hit_circle(L.footswitches[i], m.x, m.y)) continue;
                foot_down = i;
                if (i == simlayout::FS_PRESET) nextPreset();
                if (i == simlayout::FS_FREEZE) toggleFreeze();
                if (i == simlayout::FS_BYPASS) toggleBypass();
            }
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { drag_param = -1; foot_down = -1; knob_drag.end(); }
        if (drag_param >= 0) {
            const float deg = knob_drag.move_degrees(m.x, m.y);
            if (deg != 0.0f) {
                drag_pos = std::clamp(drag_pos + deg / simctl::SWEEP_DEGREES, 0.0f, 1.0f);
                guiParam[drag_param].store(simctl::from_norm(drag_param, drag_pos));
                touch(drag_param);
            }
        }

        int hovered = -1;
        for (int p = 0; p < PARAM_COUNT; ++p) if (simlayout::hit_circle(L.encoders[p], m.x, m.y, 6)) hovered = p;
        if (hovered != wheel_param) { wheel_acc = 0.0f; wheel_param = hovered; }
        const float wheel = GetMouseWheelMove();
        if (hovered >= 0 && wheel != 0.0f) {
            const int clicks = simlayout::clicks_from_wheel(wheel_acc, wheel);
            if (clicks != 0) {
                guiParam[hovered].store(simctl::step(hovered, guiParam[hovered].load(), clicks));
                touch(hovered);
            }
        }

        const int presetIdx = guiActivePreset.load();
        const bool freezeActive = guiSpectralFreeze.load();
        const bool bypassActive = guiBypass.load();

        // ---- OLED ----
        char header[48];
        if (touched >= 0 && now < touched_until) {
            char val[24];
            simctl::format(touched, guiParam[touched].load(), val, sizeof val);
            std::snprintf(header, sizeof header, "%s %s", simctl::spec(touched).label, val);
        } else {
            std::snprintf(header, sizeof header, "%d %s", presetIdx + 1, demonic.getPreset(presetIdx).name);
        }
        render_oled(oledCanvas, header, bypassActive, freezeActive);

        // ================================= drawing =================================
        BeginDrawing();
        ClearBackground(kWindowBg);

        const simlayout::Rect& E = L.enclosure;
        DrawRectangleRounded(Rectangle{E.x - 4, E.y - 4, E.w + 8, E.h + 8}, 0.05f, 16, kBodyEdge);
        DrawRectangleRounded(R(E), 0.045f, 16, kBody);
        DrawRectangleRounded(Rectangle{E.x + 6, E.y + 6, E.w - 12, 30}, 0.5f, 8, Fade(kBodyLight, 0.55f));   // top bevel highlight
        draw_screw(E.x + 16, E.y + 16);            draw_screw(E.x + E.w - 16, E.y + 16);
        draw_screw(E.x + 16, E.y + E.h - 16);      draw_screw(E.x + E.w - 16, E.y + E.h - 16);
        ui_text("DEMONIC", 60, 30, 26, kSilk, true);
        ui_text("VOCAL FX PROCESSOR  /  DAISY SEED", 62, 64, 9, kSilkDim);
        draw_led(L.led_power, kRed, true);

        draw_jack(L.jack_out);
        centered_text("OUTPUT", L.jack_out.x, L.jack_out.y + L.jack_out.r + 6, 9, kSilkDim, true);
        draw_usb(L.usb_port);
        centered_text("USB-C", L.usb_port.x + L.usb_port.w / 2, L.usb_port.y + L.usb_port.h + 6, 9, kSilkDim, true);
        draw_jack(L.jack_in);
        centered_text("MIC IN", L.jack_in.x, L.jack_in.y + L.jack_in.r + 6, 9, kSilkDim, true);

        draw_oled(L, oledCanvas);

        for (int g = 0; g < simctl::GROUP_COUNT; ++g) draw_group(L.groups[g], simctl::group_name(g));
        for (int p = 0; p < PARAM_COUNT; ++p)
            draw_encoder(L.encoders[p], p, guiParam[p].load(), p == drag_param || p == hovered);

        draw_led(L.leds[simlayout::FS_PRESET], kBlueLed, now < preset_flash_until);
        draw_led(L.leds[simlayout::FS_FREEZE], kRed, freezeActive);
        draw_led(L.leds[simlayout::FS_BYPASS], kGreenLed, !bypassActive);           // lit = effects on
        const char* foot_labels[simlayout::FOOTSWITCH_COUNT] = {"PRESET", "FREEZE", "BYPASS"};
        for (int i = 0; i < simlayout::FOOTSWITCH_COUNT; ++i) {
            const Circle& f = L.footswitches[i];
            draw_footswitch(f, foot_down == i);
            centered_text(foot_labels[i], f.x, f.y + f.r + 8, 11, kSilk, true);
        }

        const char* help1 = "Drag or scroll an encoder to turn it   |   Click a footswitch to press it";
        const char* help2 = "P = preset   F = freeze   B / Space = bypass   Esc = quit";
        centered_text(help1, L.window_w / 2.0f, L.help_y, 10, LIGHTGRAY);
        centered_text(help2, L.window_w / 2.0f, L.help_y + 20, 10, GRAY);

        EndDrawing();

        if (screenshotFile && ++frameCount == 60) {   // 60 frames: the meters and LEDs have settled
            TakeScreenshot(screenshotFile);
            break;
        }
    }

    UnloadRenderTexture(oledCanvas);
    unload_fonts();
    CloseWindow();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    return 0;
}
