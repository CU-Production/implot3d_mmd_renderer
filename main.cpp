#define SOKOL_IMPL
#define SOKOL_GLCORE
#define SOKOL_TRACE_HOOKS
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "sokol_glue.h"

#define SOKOL_IMGUI_IMPL
#include "imgui.h"
#include "util/sokol_imgui.h"
#include "util/sokol_gfx_imgui.h"
#include "implot3d.h"

#include "mmd/mmd.hxx"

#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <codecvt>
#include <locale>
#include <fstream>
#include <algorithm>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// application state
static struct {
    sg_pass_action pass_action;

    sgimgui_t sgimgui;

    std::string model_filename;
    std::string motion_filename;

    std::shared_ptr<mmd::Model> model;
    std::shared_ptr<mmd::Motion> motion;
    std::unique_ptr<mmd::Poser> poser;
    std::unique_ptr<mmd::MotionPlayer> motion_player;

    float time = 0.0f;
    bool model_loaded = false;
    bool motion_loaded = false;

    std::vector<ImPlot3DPoint> mmd_vtx;
    std::vector<uint32_t> mmd_idx;
} g_state;

// UTF-8 string conversion helper functions
#ifdef _WIN32
std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}
#else
// Linux/macOS use standard library
std::wstring utf8_to_wstring(const std::string& str) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.from_bytes(str);
}

std::string wstring_to_utf8(const std::wstring& wstr) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.to_bytes(wstr);
}
#endif

// Load PMX model
bool LoadPMXModel(const std::string& filename) {
    try {
        std::wstring wfilename = utf8_to_wstring(filename);
        mmd::FileReader file(wfilename);

        // Read PMX file
        mmd::PmxReader reader(file);
        g_state.model = std::make_shared<mmd::Model>();
        reader.ReadModel(*g_state.model);
        g_state.model_loaded = true;

        // Create poser for the model
        if (g_state.model) {
            g_state.poser = std::make_unique<mmd::Poser>(*g_state.model);
            // Create motion player if motion is already loaded
            if (g_state.motion && g_state.poser) {
                g_state.motion_player = std::make_unique<mmd::MotionPlayer>(*g_state.motion, *g_state.poser);
            }
        }

        std::wstring model_name = g_state.model->GetName();
        std::string model_name_utf8 = wstring_to_utf8(model_name);
        std::cout << "Loaded PMX model: " << model_name_utf8 << std::endl;
        std::cout << "  Vertices: " << g_state.model->GetVertexNum() << std::endl;
        std::cout << "  Triangles: " << g_state.model->GetTriangleNum() << std::endl;
        std::cout << "  Bones: " << g_state.model->GetBoneNum() << std::endl;

        return true;
    } catch (const mmd::exception& e) {
        std::cerr << "Error loading PMX model: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "Unknown error loading PMX model" << std::endl;
        return false;
    }
}

// Load VMD motion file
bool LoadVMDMotion(const std::string& filename) {
    try {
        std::wstring wfilename = utf8_to_wstring(filename);
        mmd::FileReader file(wfilename);

        mmd::VmdReader reader(file);
        g_state.motion = std::make_shared<mmd::Motion>();
        reader.ReadMotion(*g_state.motion);
        g_state.motion_loaded = true;

        // Create motion player if both model and motion are loaded
        if (g_state.model && g_state.motion && g_state.poser) {
            g_state.motion_player = std::make_unique<mmd::MotionPlayer>(*g_state.motion, *g_state.poser);
        }

        std::wstring motion_name = g_state.motion->GetName();
        std::string motion_name_utf8 = wstring_to_utf8(motion_name);
        std::cout << "Loaded VMD motion: " << motion_name_utf8 << std::endl;

        return true;
    } catch (const mmd::exception& e) {
        std::cerr << "Error loading VMD motion: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "Unknown error loading VMD motion" << std::endl;
        return false;
    }
}

// Update model vertex buffers (initial creation)
void UpdateModelBuffers() {
    if (!g_state.model || !g_state.model_loaded) return;

    size_t vertex_num = g_state.model->GetVertexNum();
    g_state.mmd_vtx.resize(vertex_num);

    for (size_t i = 0; i < vertex_num; ++i) {
        mmd::Model::Vertex<mmd::ref> vertex = g_state.model->GetVertex(i);
        mmd::Vector3f pos = vertex.GetCoordinate();
        g_state.mmd_vtx[i] = {pos.p.x, pos.p.z, pos.p.y};
    }

    // Prepare index data
    size_t triangle_num = g_state.model->GetTriangleNum();
    g_state.mmd_idx.resize(triangle_num * 3);

    for (size_t i = 0; i < triangle_num; ++i) {
        const mmd::Vector3D<std::uint32_t>& triangle = g_state.model->GetTriangle(i);
        // workaround for backface culling
        // g_state.mmd_idx[i*3+0] = triangle.v[2];
        // g_state.mmd_idx[i*3+1] = triangle.v[1];
        // g_state.mmd_idx[i*3+2] = triangle.v[0];
        g_state.mmd_idx[i*3+0] = triangle.v[0];
        g_state.mmd_idx[i*3+1] = triangle.v[1];
        g_state.mmd_idx[i*3+2] = triangle.v[2];
    }
}

// Update vertex buffer with deformed vertices (called each frame after Deform())
void UpdateDeformedVertices() {
    if (!g_state.model || !g_state.model_loaded || !g_state.poser || g_state.mmd_vtx.size() <= 0) {
        return;
    }

    size_t vertex_num = g_state.model->GetVertexNum();
    if (vertex_num == 0 || g_state.poser->pose_image.coordinates.size() < vertex_num) {
        return;
    }

    // Prepare vertex data from deformed coordinates
    g_state.mmd_vtx.resize(vertex_num);

    for (size_t i = 0; i < vertex_num; ++i) {
        mmd::Model::Vertex<mmd::ref> vertex = g_state.model->GetVertex(i);
        // Use deformed coordinates and normals from pose_image
        const mmd::Vector3f& pos = g_state.poser->pose_image.coordinates[i];
        g_state.mmd_vtx[i] = {pos.p.x, pos.p.z, pos.p.y};
    }
}


void init(void) {
    sg_desc _sg_desc{};
    _sg_desc.environment = sglue_environment();
    _sg_desc.logger.func = slog_func;
    sg_setup(_sg_desc);

    simgui_desc_t simgui_desc{};
    simgui_desc.max_vertices = 2000000;
    simgui_desc.logger.func = slog_func;
    simgui_setup(&simgui_desc);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    sgimgui_desc_t _sgimgui_desc{};
    sgimgui_init(&g_state.sgimgui, &_sgimgui_desc );

    ImPlot3D::CreateContext();

    g_state.pass_action.colors[0] = { .load_action=SG_LOADACTION_CLEAR, .clear_value={0.0f, 0.0f, 0.0f, 1.0f } };

    // Try to load model and motion files if they exist
    if (!g_state.model_filename.empty()) {
        LoadPMXModel(g_state.model_filename);
        if (g_state.model_loaded) {
            UpdateModelBuffers();
        }
    }
    if (!g_state.motion_filename.empty()) {
        LoadVMDMotion(g_state.motion_filename);
    }

    std::cout << "MMD Renderer initialized" << std::endl;
    std::cout << "Usage: Load PMX and VMD files via code or command line" << std::endl;
}

void frame(void) {
    // Update animation and deformed vertices
    // Ensure Deform() is called before UpdateDeformedVertices() to populate pose_image
    if (g_state.model_loaded && g_state.poser) {
        // Reset posing first (clears all bone poses and morphs)
        g_state.poser->ResetPosing();

        // Then apply motion if available
        if (g_state.motion_loaded && g_state.motion_player) {
            // Calculate current frame (assuming 30 FPS)
            size_t frame = static_cast<size_t>(g_state.time * 30.0f);

            // Seek to current frame and apply motion (sets bone poses and morphs)
            g_state.motion_player->SeekFrame(frame);

            // After setting bone poses, we need to update bone transforms
            // ResetPosing() already called PrePhysicsPosing() and PostPhysicsPosing(),
            // but after SeekFrame() we need to update transforms again
            g_state.poser->PrePhysicsPosing();
            g_state.poser->PostPhysicsPosing();

            // Debug: print frame number every second
            static size_t last_frame = 0;
            if (frame != last_frame && frame % 30 == 0) {
                std::cout << "Animation frame: " << frame << " (time: " << g_state.time << "s)" << std::endl;
                last_frame = frame;
            }
        }

        // Apply deformation (calculates deformed vertex positions)
        g_state.poser->Deform();

        // Update vertex buffer with deformed vertices (only once per frame)
        UpdateDeformedVertices();
    }

    const int width = sapp_width();
    const int height = sapp_height();
    simgui_new_frame({ width, height, sapp_frame_duration(), sapp_dpi_scale() });

    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_None);

    if (ImGui::BeginMainMenuBar()) {
        sgimgui_draw_menu(&g_state.sgimgui, "sokol-gfx");
        ImGui::EndMainMenuBar();
    }

    // draw mmd mesh using implot3d
    if (ImGui::Begin("PMX/VMD viewer")) {
        // Choose fill color
        static bool set_fill_color = true;
        static ImVec4 fill_color = ImVec4(0.8f, 0.8f, 0.2f, 0.6f);
        ImGui::Checkbox("Fill Color", &set_fill_color);
        if (set_fill_color) {
            ImGui::SameLine();
            ImGui::ColorEdit4("##MeshFillColor", (float*)&fill_color);
        }

        // Choose line color
        static bool set_line_color = false;
        static ImVec4 line_color = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);
        ImGui::Checkbox("Line Color", &set_line_color);
        if (set_line_color) {
            ImGui::SameLine();
            ImGui::ColorEdit4("##MeshLineColor", (float*)&line_color);
        }

        // Choose marker color
        static bool set_marker_color = false;
        static ImVec4 marker_color = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);
        ImGui::Checkbox("Marker Color", &set_marker_color);
        if (set_marker_color) {
            ImGui::SameLine();
            ImGui::ColorEdit4("##MeshMarkerColor", (float*)&marker_color);
        }

        if (ImPlot3D::BeginPlot("Mesh Plots")) {
            ImPlot3D::SetupAxesLimits(-1, 1, -1, 1, -1, 1);

            // Set colors
            if (set_fill_color)
                ImPlot3D::SetNextFillStyle(fill_color);
            else {
                // If not set as transparent, the fill color will be determined by the colormap
                ImPlot3D::SetNextFillStyle(ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            }
            if (set_line_color)
                ImPlot3D::SetNextLineStyle(line_color);
            if (set_marker_color)
                ImPlot3D::SetNextMarkerStyle(ImPlot3DMarker_Square, 3, marker_color, IMPLOT3D_AUTO, marker_color);

            // Plot mesh
            ImPlot3D::PlotMesh("MMD", g_state.mmd_vtx.data(), g_state.mmd_idx.data(), g_state.mmd_vtx.size(), g_state.mmd_idx.size());

            ImPlot3D::EndPlot();
        }

        ImGui::End();
    }

    sg_pass _sg_pass{};
    _sg_pass = { .action = g_state.pass_action, .swapchain = sglue_swapchain() };

    sg_begin_pass(&_sg_pass);
    sgimgui_draw(&g_state.sgimgui);
    simgui_render();
    sg_end_pass();
    sg_commit();
}

void cleanup(void) {
    ImPlot3D::DestroyContext();
    sgimgui_discard(&g_state.sgimgui);
    simgui_shutdown();
    sg_shutdown();
}

void input(const sapp_event* ev) {
    simgui_handle_event(ev);
}

sapp_desc sokol_main(int argc, char* argv[]) {
    // Process command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::string lower_arg = arg;
        std::transform(lower_arg.begin(), lower_arg.end(), lower_arg.begin(), ::tolower);

        if (lower_arg.find(".pmx") != std::string::npos) {
            g_state.model_filename = arg;
        } else if (lower_arg.find(".vmd") != std::string::npos) {
            g_state.motion_filename = arg;
        }
    }

    sapp_desc _sapp_desc{};
    _sapp_desc.init_cb = init;
    _sapp_desc.frame_cb = frame;
    _sapp_desc.cleanup_cb = cleanup;
    _sapp_desc.event_cb = input;
    _sapp_desc.width = 1280;
    _sapp_desc.height = 720;
    _sapp_desc.window_title = "implot3d_mmd_renderer";
    _sapp_desc.icon.sokol_default = true;
    _sapp_desc.logger.func = slog_func;
    return _sapp_desc;
}