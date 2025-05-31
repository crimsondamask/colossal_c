// colossal_cpp.cpp : This file contains the 'main' function. Program execution
// begins and ends there.
/// @file colossal.cpp

#include <cstdint>
#define _CRT_SECURE_NO_WARNINGS
#include "colossal.h"
#include "curl/curl.h"
#include "curl/easy.h"
#include "imgui/GLFW/glfw3.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "link.h"
#include "snap7/snap7.h"
#include <cstddef>
#include <cstring>
#include <gl/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <wincrypt.h>
#include <windows.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define N_CHANNELS 15
#define N_DEVICES 3
#define N_FRAMES_UNTIL_CONS 60

#define POSTDATA_BUF_STRLEN 2048
#define TAGSDATA_BUF_STRLEN 1024
#define TAGDATA_BUF_STRLEN 128

static int polling_thread(void *arg);

bool load_texture_from_memory(const void *data, size_t data_size, GLuint *out_texture, int *out_width, int *out_height)
{
    int image_width = 0;
    int image_height = 0;

    unsigned char *image_data =
        stbi_load_from_memory((const unsigned char *)data, (int)data_size, &image_width, &image_height, NULL, 4);

    if (image_data == NULL)
    {
        return false;
    }

    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    stbi_image_free(image_data);

    *out_texture = image_texture;
    *out_width = image_width;
    *out_height = image_height;

    return true;
}

bool load_texture_from_file(const char *file_name, GLuint *out_texture, int *out_width, int *out_height)
{
    FILE *f = fopen(file_name, "rb");
    if (f == NULL)
        return false;

    fseek(f, 0, SEEK_END);
    size_t file_size = (size_t)ftell(f);
    if (file_size == -1)
        return false;
    fseek(f, 0, SEEK_SET);
    void *file_data = IM_ALLOC(file_size);
    fread(file_data, 1, file_size, f);
    fclose(f);

    bool ret = load_texture_from_memory(file_data, file_size, out_texture, out_width, out_height);
    IM_FREE(file_data);

    return ret;
}

static void ui_loggers_window(size_t link_count, Link links[], Link ui_link_buffers[], bool *menu_state,
                              int *logger_selected_link, int config_edit_flags[])
{
    Link *ui_buffer = &ui_link_buffers[*logger_selected_link];
    const char *link_names[N_DEVICES] = {};

    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));

    if (ImGui::Begin("Logging Config", menu_state))
    {
        ImGui::PopStyleColor();

        // Populate the combobox values with device names.
        for (int i = 0; i < N_DEVICES; i++)
        {
            link_names[i] = links[i].name;
        }

        if (ImGui::Combo("Link", logger_selected_link, link_names, IM_ARRAYSIZE(link_names)))
        {
        }
        ImGui::Text("%s Logging Details", links[*logger_selected_link].name);

        if (ImGui::InputText("API Token", ui_buffer->token, IM_ARRAYSIZE(ui_buffer->token),
                             ImGuiInputTextFlags_CharsNoBlank))
        {
            config_edit_flags[*logger_selected_link] |= CONFIG_EDIT_DEVICE_CONFIG;
        }

        if (ImGui::InputText("Database URL", ui_buffer->url, IM_ARRAYSIZE(ui_buffer->url),
                             ImGuiInputTextFlags_CharsNoBlank))
        {
            config_edit_flags[*logger_selected_link] |= CONFIG_EDIT_DEVICE_CONFIG;
        }
        const char *logging_methods[] = {"LOCAL", "REMOTE"};
        if (ImGui::Combo("Logging Method", &ui_buffer->logging_type, logging_methods, IM_ARRAYSIZE(logging_methods)))
        {
            config_edit_flags[*logger_selected_link] |= CONFIG_EDIT_DEVICE_CONFIG;
        }
        ImGui::Text("Logging Count: %lu", links[*logger_selected_link].log_count);
    }
    else
    {
        ImGui::PopStyleColor();
    }
    ImGui::End();
}
static void ui_tag_window(size_t link_count, Link links[], Link ui_link_buffers[], bool *menu_state,
                          int selected_link_index, int selected_tag_index, int config_edit_flags[])
{
    Link *ui_buffer = &ui_link_buffers[selected_link_index];
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    if (ImGui::Begin("Properties", menu_state))
    {
        ImGui::PopStyleColor();

        ImGui::BeginDisabled(!ui_buffer->tags[selected_tag_index].enabled);

        ImGui::Text("%s:%s Details", links[selected_link_index].name,
                    links[selected_link_index].tags[selected_tag_index].name);

        if (ImGui::InputText("Tag", ui_buffer->tags[selected_tag_index].name,
                             IM_ARRAYSIZE(ui_buffer->tags[selected_tag_index].name), ImGuiInputTextFlags_CharsNoBlank))
        {
            config_edit_flags[selected_link_index] |= CONFIG_EDIT_CHANNEL_CONFIG;
        }

        if (ImGui::InputText("Description", ui_buffer->tags[selected_tag_index].description,
                             IM_ARRAYSIZE(ui_buffer->tags[selected_tag_index].description)))
        {
            config_edit_flags[selected_link_index] |= CONFIG_EDIT_CHANNEL_CONFIG;
        }
        if (ImGui::InputText("Unit", ui_buffer->tags[selected_tag_index].unit,
                             IM_ARRAYSIZE(ui_buffer->tags[selected_tag_index].unit)))
        {
            config_edit_flags[selected_link_index] |= CONFIG_EDIT_CHANNEL_CONFIG;
        }

        // Show the tag value options and address depending on the protocol.
        switch (ui_buffer->protocol)
        {
        case MB_SERIAL: {
            // MB_SERIAL and MB_TCP use the same protocol.
            // We let it leak into the next case
        }
        case MB_TCP: {
            const char *value_types[] = {"INT", "REAL - Uses 2 registers", "COIL"};
            if (ImGui::Combo("Value Type", &ui_buffer->tags[selected_tag_index].value_type, value_types,
                             IM_ARRAYSIZE(value_types)))
            {
                config_edit_flags[selected_link_index] |= CONFIG_EDIT_CHANNEL_CONFIG;
            }
            if (ImGui::InputInt("Address", &ui_buffer->tags[selected_tag_index].tag_addr.mb_addr))
            {
                config_edit_flags[selected_link_index] |= CONFIG_EDIT_CHANNEL_CONFIG;
            }
            break;
        }
        case SIEMENS_S7: {

            const char *value_types[] = {"INT (16bit)", "REAL (32bit)", "BIT"};
            if (ImGui::Combo("Value Type", &ui_buffer->tags[selected_tag_index].value_type, value_types,
                             IM_ARRAYSIZE(value_types)))
            {
                config_edit_flags[selected_link_index] |= CONFIG_EDIT_CHANNEL_CONFIG;
            }
            if (ImGui::InputInt("DB", &ui_buffer->tags[selected_tag_index].tag_addr.s7_tag_addr.db_number))
            {
                config_edit_flags[selected_link_index] |= CONFIG_EDIT_CHANNEL_CONFIG;
            }
            if (ImGui::InputInt("Offset", &ui_buffer->tags[selected_tag_index].tag_addr.s7_tag_addr.start))
            {
                config_edit_flags[selected_link_index] |= CONFIG_EDIT_CHANNEL_CONFIG;
            }
            if (ImGui::InputInt("Bit", &ui_buffer->tags[selected_tag_index].tag_addr.s7_tag_addr.start_bit))
            {
                config_edit_flags[selected_link_index] |= CONFIG_EDIT_CHANNEL_CONFIG;
            }
            break;
        }
        default: {
            break;
        }
        }
        ImGui::EndDisabled();

        if (ImGui::Checkbox("Enabled", &ui_buffer->tags[selected_tag_index].enabled))
        {
            config_edit_flags[selected_link_index] |= CONFIG_EDIT_CHANNEL_CONFIG;
        }

        ImGui::SameLine();
        if (ImGui::Checkbox("Logged", &ui_buffer->tags[selected_tag_index].logged))
        {
            config_edit_flags[selected_link_index] |= CONFIG_EDIT_CHANNEL_CONFIG;
        }
    }
    else
    {

        ImGui::PopStyleColor();
    }
    ImGui::End();
}

static void ui_links_window(size_t link_count, Link links[], Link ui_link_buffers[], ConfigUpdate config_update[],
                            bool *menu_state, int *selected_link_index, int *selected_tag_index,
                            int config_edit_flags[])

{
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    if (ImGui::Begin("Devices"), &menu_state)
    {
        ImGui::PopStyleColor();
        for (int i = 0; i < link_count; i++)
        {

            ImGui::PushID(i);

            Link *link = &links[i];
            // Used to hold UI data and persist it across frames.
            // The use of pointers here is important as we don't want
            // to just copy the buffer. We want to mutate the buffer state outside of
            // the event loop.
            Link *ui_buffer = &ui_link_buffers[i];

            // A little hack to show an asterics when the config is edited.
            char collapsing_header_title[2048];
            if (config_edit_flags[i])
            {

                if (link->protocol == SIEMENS_S7)
                {

                    sprintf_s(collapsing_header_title, "%s %s %s Config *", link->name,
                              link->link_config.s7_config.cpu_info.ModuleTypeName,
                              link->link_config.s7_config.cpu_info.SerialNumber);
                }
                else
                {
                    sprintf_s(collapsing_header_title, "%s Config *", link->name);
                }
            }
            else
            {
                if (link->protocol == SIEMENS_S7)
                {

                    sprintf_s(collapsing_header_title, "%s %s %s Config", link->name,
                              link->link_config.s7_config.cpu_info.ModuleTypeName,
                              link->link_config.s7_config.cpu_info.ModuleName);
                }
                else
                {
                    sprintf_s(collapsing_header_title, "%s Config", link->name);
                }
            }

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
            if (ImGui::CollapsingHeader(collapsing_header_title, ImGuiTreeNodeFlags_Bullet))
            {
                ImGui::PopStyleColor();
                if (ImGui::InputText("Name", ui_buffer->name, IM_ARRAYSIZE(ui_link_buffers->name),
                                     ImGuiInputTextFlags_CharsNoBlank)

                )
                {
                    config_edit_flags[i] |= CONFIG_EDIT_DEVICE_CONFIG;
                }

                const char *link_types[] = {"MODBUS TCP", "MODBUS SERIAL", "ALLEN BRADLEY EIP", "SIEMENS S7",
                                            "IEC61850"};

                if (ImGui::Combo("Link Protocol", &ui_buffer->protocol, link_types, IM_ARRAYSIZE(link_types)))
                {
                    config_edit_flags[i] |= CONFIG_EDIT_DEVICE_CONFIG;

                    for (int tag_i = 0; tag_i < ui_buffer->tag_count; tag_i++)
                    {
                        ui_buffer->tags[tag_i].protocol = ui_buffer->protocol;
                    }
                }

                switch (ui_buffer->protocol)
                {
                case MB_TCP: {

                    if (ImGui::InputText("IP Address", ui_buffer->link_config.mb_tcp_config.ip,
                                         IM_ARRAYSIZE(ui_buffer->link_config.mb_tcp_config.ip)))
                    {
                        config_edit_flags[i] |= CONFIG_EDIT_DEVICE_CONFIG;
                    }

                    if (ImGui::InputInt("Port", &ui_buffer->link_config.mb_tcp_config.port))
                    {
                        config_edit_flags[i] |= CONFIG_EDIT_DEVICE_CONFIG;
                    }
                    break;
                }
                case MB_SERIAL: {

                    if (ImGui::InputText("Serial Port", ui_buffer->link_config.mb_serial_config.com_port,
                                         IM_ARRAYSIZE(ui_buffer->link_config.mb_serial_config.com_port)))
                    {
                        config_edit_flags[i] |= CONFIG_EDIT_DEVICE_CONFIG;
                    }

                    const char *baudrates[] = {"9600", "19200", "38400", "115200"};
                    static int baudrate = 0;
                    if (ImGui::Combo("Baudrate", &baudrate, baudrates, IM_ARRAYSIZE(baudrates)))
                    {
                        config_edit_flags[i] |= CONFIG_EDIT_DEVICE_CONFIG;
                        switch (baudrate)
                        {
                        case 0:
                            ui_buffer->link_config.mb_serial_config.baudrate = BR_9600;
                            break;
                        case 1:
                            ui_buffer->link_config.mb_serial_config.baudrate = BR_19200;
                            break;
                        case 2:
                            ui_buffer->link_config.mb_serial_config.baudrate = BR_38400;
                            break;
                        case 3:
                            ui_buffer->link_config.mb_serial_config.baudrate = BR_115200;
                            break;

                        default:
                            ui_buffer->link_config.mb_serial_config.baudrate = BR_9600;
                            break;
                        }
                    }
                    const char *parities[] = {"NONE", "EVEN", "ODD"};
                    static int parity = 0;
                    if (ImGui::Combo("Parity", &parity, parities, IM_ARRAYSIZE(parities)))
                    {
                        config_edit_flags[i] |= CONFIG_EDIT_DEVICE_CONFIG;
                        switch (parity)
                        {
                        case 0:
                            ui_buffer->link_config.mb_serial_config.parity = 'N';
                            break;
                        case 1:
                            ui_buffer->link_config.mb_serial_config.parity = 'E';
                            break;
                        case 2:
                            ui_buffer->link_config.mb_serial_config.parity = 'O';
                            break;

                        default:
                            ui_buffer->link_config.mb_serial_config.parity = 'N';
                            break;
                        }
                    }
                    break;
                }
                case SIEMENS_S7: {
                    if (ImGui::InputText("IP Address", ui_buffer->link_config.s7_config.ip,
                                         IM_ARRAYSIZE(ui_buffer->link_config.s7_config.ip)))
                    {
                        config_edit_flags[i] |= CONFIG_EDIT_DEVICE_CONFIG;
                    }

                    if (ImGui::InputInt("Rack", &ui_buffer->link_config.s7_config.rack))
                    {
                        config_edit_flags[i] |= CONFIG_EDIT_DEVICE_CONFIG;
                    }
                    if (ImGui::InputInt("Slot", &ui_buffer->link_config.s7_config.slot))
                    {
                        config_edit_flags[i] |= CONFIG_EDIT_DEVICE_CONFIG;
                    }
                    break;
                }
                case EIP: {
                    break;
                }
                case IEC_61850: {
                    break;
                }
                default:
                    break;
                }
            }
            else
            {

                ImGui::PopStyleColor();
            }
            // Button to send config update to the threads
            if (ImGui::Button("Reconfigure"))
            {
                link = ui_buffer;
                if (config_update_put(&config_update[i], link, true))
                {
                    // Reset the config change indication flags.
                    config_edit_flags[i] = 0;
                }
            }

            if (link->is_error)
            {
                ImGui::Text("Link ID: %d. ERROR: %s", link->id, link->err_msg);
            }
            else
            {
                ImVec2 outer_size = ImVec2(0.0f, 250.0f);
                if (ImGui::BeginTable("Tag Data", 6,
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                                          ImGuiTableFlags_ScrollX | ImGuiTableFlags_RowBg,
                                      outer_size))

                {

                    ImGui::TableSetupColumn("Tag");
                    ImGui::TableSetupColumn("Value");
                    ImGui::TableSetupColumn("Unit");
                    ImGui::TableSetupColumn("Type");
                    ImGui::TableSetupColumn("Address");
                    ImGui::TableSetupColumn("Description");
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableHeadersRow();
                    for (int j = 0; j < link->tag_count; j++)
                    {
                        char selectable_label[32];
                        bool set_selected = false;
                        sprintf_s(selectable_label, "%s", link->tags[j].name);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();

                        if (*selected_link_index == i && *selected_tag_index == j)
                        {
                            set_selected = true;
                        }
                        else
                        {
                            set_selected = false;
                        }
                        if (ImGui::Selectable(selectable_label, set_selected, ImGuiSelectableFlags_SpanAllColumns))
                        {
                            *selected_link_index = i;
                            *selected_tag_index = j;
                        }
                        // ImGui::Text("CH%d", device->channels[j].id);
                        ImGui::TableNextColumn();
                        ImGui::BeginDisabled(!link->tags[j].enabled);

                        switch (link->tags[j].value_type)
                        {
                        case VALUE_REAL:
                            ImGui::Text("%0.3f", link->tags[j].tag_value.real_value);
                            break;
                        case VALUE_INT:
                            ImGui::Text("%d", link->tags[j].tag_value.int_value);
                            break;
                        case VALUE_BOOL:
                            ImGui::Text("%d", link->tags[j].tag_value.bool_value);
                            break;
                        }
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", link->tags[j].unit);
                        ImGui::TableNextColumn();

                        switch (link->tags[j].value_type)
                        {
                        case VALUE_REAL:
                            ImGui::Text("REAL");
                            break;
                        case VALUE_INT:
                            ImGui::Text("INT");
                            break;
                        case VALUE_BOOL:
                            ImGui::Text("BOOL");
                            break;
                        default:
                            ImGui::Text("INT");
                            break;
                        }
                        ImGui::TableNextColumn();
                        switch (link->tags[j].protocol)
                        {
                        case MB_TCP:
                        case MB_SERIAL:
                            ImGui::Text("%d", link->tags[j].tag_addr.mb_addr);
                            break;
                        case EIP:
                            ImGui::Text("%s", link->tags[j].tag_addr.eip_tag_addr);
                            break;
                        case SIEMENS_S7:
                            ImGui::Text("DB%d:%d.%d", link->tags[j].tag_addr.s7_tag_addr.db_number,
                                        link->tags[j].tag_addr.s7_tag_addr.start,
                                        link->tags[j].tag_addr.s7_tag_addr.start_bit);
                            break;

                        default:
                            ImGui::Text("%d", link->tags[j].tag_addr.mb_addr);
                            break;
                        }
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", link->tags[j].description);
                        ImGui::EndDisabled();
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::PopID();
        }
    }
    else
    {

        ImGui::PopStyleColor();
    }
    ImGui::End();
}
static void glfw_error_callback(int error, const char *description);

struct CurlMemoryStruct
{
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct CurlMemoryStruct *mem = (struct CurlMemoryStruct *)userp;

    char *ptr = (char *)realloc(mem->memory, mem->size + realsize + 1);
    if (ptr == NULL)
    {
        return 0;
    }
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}
// int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)

int main(int, char **)
{

    glfwSetErrorCallback(glfw_error_callback);

    if (!glfwInit())
    {
        return EXIT_FAILURE;
    }

// Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100 (WebGL 1.0)
    const char *glsl_version = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
    // GL ES 3.0 + GLSL 300 es (WebGL 2.0)
    const char *glsl_version = "#version 300 es";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    // GL 3.2 + GLSL 150
    const char *glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);           // Required on Mac
#else
    // GL 3.0 + GLSL 130
    const char *glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    // Create window with graphics context.

    // Font scaling depending on monitor resolution

    float font_scale_factor = 1.0;

    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    int monitor_xscale, monitor_yscale;

    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

    monitor_xscale = mode->width;
    monitor_yscale = mode->height;

    printf("%d, %d\n", monitor_xscale, monitor_yscale);
    GLFWwindow *window = glfwCreateWindow(mode->width, mode->height, "Colossal 1.0", nullptr, nullptr);

    if (window == nullptr)
    {
        return EXIT_FAILURE;
    }

    // Create GLFW context.
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable Vsync.

    // Setup dear imgui context.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    (void)io;

    // IO configuration flags.
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // Light mode style
    ImGui::StyleColorsLight();

    ImGuiStyle &style = ImGui::GetStyle();

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        // Because I am sick of rounded corners.
        style.WindowRounding = 0.0f;
        style.ChildRounding = 0.0f;
        style.FrameRounding = 0.0f;
        style.TabRounding = 0.0f;
        style.PopupRounding = 0.0f;
        style.ScrollbarRounding = 0.0f;

        style.Colors[ImGuiCol_TitleBg].x = 0.7f;
        style.Colors[ImGuiCol_TitleBg].y = 0.7f;
        style.Colors[ImGuiCol_TitleBg].z = 0.7f;
        style.Colors[ImGuiCol_TitleBg].w = 1.0f;

        // style.Colors[ImGuiCol_Header].x = 0.7f;
        // style.Colors[ImGuiCol_Header].y = 0.7f;
        // style.Colors[ImGuiCol_Header].z = 0.7f;
        // style.Colors[ImGuiCol_Header].w = 1.0f;

        style.Colors[ImGuiCol_Button].x = 0.8f;
        style.Colors[ImGuiCol_Button].y = 0.8f;
        style.Colors[ImGuiCol_Button].z = 0.8f;
        style.Colors[ImGuiCol_Button].w = 1.0f;

        style.Colors[ImGuiCol_TableHeaderBg].x = 0.8f;
        style.Colors[ImGuiCol_TableHeaderBg].y = 0.8f;
        style.Colors[ImGuiCol_TableHeaderBg].z = 0.8f;
        style.Colors[ImGuiCol_TableHeaderBg].w = 1.0f;

        style.Colors[ImGuiCol_TitleBg].x = 0.7f;
        style.Colors[ImGuiCol_TitleBg].y = 0.7f;
        style.Colors[ImGuiCol_TitleBg].z = 0.7f;
        style.Colors[ImGuiCol_TitleBg].w = 1.0f;

        style.Colors[ImGuiCol_TabDimmed].x = 0.7f;
        style.Colors[ImGuiCol_TabDimmed].y = 0.7f;
        style.Colors[ImGuiCol_TabDimmed].z = 0.7f;
        style.Colors[ImGuiCol_TabDimmed].w = 1.0f;

        style.Colors[ImGuiCol_TabDimmedSelected].x = 0.7f;
        style.Colors[ImGuiCol_TabDimmedSelected].y = 0.7f;
        style.Colors[ImGuiCol_TabDimmedSelected].z = 0.7f;
        style.Colors[ImGuiCol_TabDimmedSelected].w = 1.0f;

        style.Colors[ImGuiCol_TabUnfocused].x = 0.7f;
        style.Colors[ImGuiCol_TabUnfocused].y = 0.7f;
        style.Colors[ImGuiCol_TabUnfocused].z = 0.7f;
        style.Colors[ImGuiCol_TabUnfocused].w = 1.0f;

        style.Colors[ImGuiCol_TitleBgActive].x = 0.14f;
        style.Colors[ImGuiCol_TitleBgActive].y = 0.28f;
        style.Colors[ImGuiCol_TitleBgActive].z = 0.56f;
        style.Colors[ImGuiCol_TitleBgActive].w = 1.0f;

        style.Colors[ImGuiCol_HeaderHovered].x = 0.36f;
        style.Colors[ImGuiCol_HeaderHovered].y = 0.52;
        style.Colors[ImGuiCol_HeaderHovered].z = 0.84f;
        style.Colors[ImGuiCol_HeaderHovered].w = 1.0f;

        style.Colors[ImGuiCol_Header].x = 0.36f;
        style.Colors[ImGuiCol_Header].y = 0.52;
        style.Colors[ImGuiCol_Header].z = 0.84f;
        style.Colors[ImGuiCol_Header].w = 1.0f;

        style.Colors[ImGuiCol_TabHovered].x = 0.14f;
        style.Colors[ImGuiCol_TabHovered].y = 0.28f;
        style.Colors[ImGuiCol_TabHovered].z = 0.56f;
        style.Colors[ImGuiCol_TabHovered].w = 1.0f;

        style.Colors[ImGuiCol_TabSelectedOverline].x = 0.14f;
        style.Colors[ImGuiCol_TabSelectedOverline].y = 0.28f;
        style.Colors[ImGuiCol_TabSelectedOverline].z = 0.56f;
        style.Colors[ImGuiCol_TabSelectedOverline].w = 1.0f;

        style.Colors[ImGuiCol_TabSelected].x = 0.14f;
        style.Colors[ImGuiCol_TabSelected].y = 0.28f;
        style.Colors[ImGuiCol_TabSelected].z = 0.56f;
        style.Colors[ImGuiCol_TabSelected].w = 1.0f;
    }

    // style.Colors[ImGuiCol_TitleBg] = ImVec4(0.0f, 0.36f, 0.6f, 1.0f);
    // style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.0f, 0.36f, 0.6f, 1.0f);
    // style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.36f, 0.6f, 1.0f);
    // style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.0f, 0.36f, 0.6f, 1.0f);

    // Setup renderer

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load system font.
    io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 20.0f);

    // Our state:
    Colossal app;
    // app.device_data.device = cl_device_init_tcp("PLC_1", N_CHANNELS);
    // Thread handles.
    thrd_t th[N_DEVICES];
    // Ring buffer for each device.
    Buffer buf[N_DEVICES];
    // Config update buffer
    ConfigUpdate config_update[N_DEVICES];
    // Arguments to pass to each thread.
    ThreadArg thread_arg[N_DEVICES];
    // Device list
    Link links[N_DEVICES];

    // UI buffers to hold the GUI data
    Link ui_link_buffers[N_DEVICES];

    UiMenuState menu_state = {};
    menu_state.devices_menu = true;
    // We use this so we don't lock the mutex each frame.
    bool frames_exceeded = false;
    size_t frame_count = 0;

    bool selected_channel[N_DEVICES][N_CHANNELS] = {};
    int config_edit_flags[N_DEVICES] = {};

    int logger_selected_link = 0;
    int selected_link_index = 0;
    int selected_tag_index = 0;

    bool glfw_close_window_pending = false;
    bool glfw_close_window_confirmed = false;
    // Initialize each buffer for 10 products.
    // Can only keep 10 products at a time.
    // This is enough as the main thread will
    // keep consuming the products.
    // If the products are not consumed and the buffer is full,
    // the polling thread will stop polling the device.
    // This will probably change once we implement the logging functionality.
    // TODO =======================================================================
    // Add the ability to check for config files in the file system and create Links
    // accordingly.
    for (int i = 0; i < N_DEVICES; i++)
    {
        // Initialize all the devices.
        // This is needed to allocate the required memory for channels
        // so that the GUI can access them.
        char link_name_buf[32];
        sprintf_s(link_name_buf, "LINK_%d", i);

        MbTcpConfig mb_tcp_config;
        sprintf_s(mb_tcp_config.ip, "127.0.0.1");
        mb_tcp_config.port = 5502;

        MbSerialConfig mb_serial_config;
        sprintf_s(mb_serial_config.com_port, "COM3");
        mb_serial_config.baudrate = BR_9600;
        mb_serial_config.parity = CL_SERIAL_PARITY_NONE;

        S7Config s7_config;

        sprintf_s(s7_config.ip, "192.168.0.1");
        s7_config.rack = 0;
        s7_config.slot = 2;

        LinkConfig link_config;
        link_config.mb_tcp_config = mb_tcp_config;
        link_config.mb_serial_config = mb_serial_config;
        link_config.s7_config = s7_config;

        Link link = {};
        link = *cl_new_link(link_name_buf, i, MB_TCP, link_config, N_CHANNELS);
        links[i] = link;

        // Link data copy used as a buffer for the UI widgets to write to.
        ui_link_buffers[i] = link;

        // Initialize the buffers
        buf_init(&buf[i], 10);
        // and the config update so we can send updates to the threads.
        config_update_init(&config_update[i]);

        // Spawn the threads.

        thread_arg[i].id = i + 1;
        thread_arg[i].buf_ptr = &buf[i];
        thread_arg[i].config_update_ptr = &config_update[i];

        // the thread argument holds the firstly created link.
        // This is used as the initial values for the thread to try and poll...etc
        thread_arg[i].link = link;

        if (thrd_create(&th[i], polling_thread, (void *)&thread_arg[i]) != thrd_success)
        {
            fprintf(stderr, "Could not spawn thread.\n");
            return EXIT_FAILURE;
        }

        // We don't have to wait for the thread to finish.
        thrd_detach(th[i]);
    }

    int image_width = 0;
    int image_height = 0;
    GLuint image_texture = 0;
    bool ret = load_texture_from_file("colossal.png", &image_texture, &image_width, &image_height);

    // The main loop
    while (!glfwWindowShouldClose(window))
    {
        // Main event loop.
        // Poll and handle events.
        glfwPollEvents();

        glfw_close_window_pending = glfwWindowShouldClose(window);

        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        // Start the Imgui frame.
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // const ImGuiViewport *viewport = ImGui::GetMainViewport();
        //
        ImGuiID dock_id = ImGui::GetID("MyDockSpace");
        ImGuiDockNodeFlags dock_flags = 0;
        dock_flags |= ImGuiDockNodeFlags_PassthruCentralNode;
        ImGui::DockSpaceOverViewport(dock_id, ImGui::GetMainViewport(), dock_flags);

        ++frame_count;

        // Consume the data in the buffer each N_FRAMES...
        if (frame_count >= N_FRAMES_UNTIL_CONS)
        {
            for (size_t i = 0; i < N_DEVICES; i++)
            {
                // Get the device data from the threads buffers and put it in the
                // mb_devices[] for display
                while (buf_get(&buf[i], &links[i], 1))
                {
                    // do something
                }
                frames_exceeded = true;
            }

            frame_count = 0;
        }

        if (ImGui::BeginMainMenuBar())
        {
            // Load our images
            image_width = ImGui::GetContentRegionAvail().x;
            image_height = ImGui::GetContentRegionAvail().y;

            ImGui::Image((ImTextureID)(intptr_t)image_texture, ImVec2(110.0f, image_height + 8.0f));

            if (ImGui::BeginMenu("File"))
            {
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {

                ImGui::MenuItem("Tag Properties", NULL, &menu_state.tag_menu);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Devices"))
            {
                ImGui::MenuItem("Device Details", NULL, &menu_state.devices_menu);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Logging"))
            {
                ImGui::MenuItem("Logger Config", NULL, &menu_state.logging_menu);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help"))
            {
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        // Show demo window for tests.
        if (app.show_demo_window)
        {
            ImGui::ShowDemoWindow(&app.show_demo_window);
        }

        // Logger options window
        if (menu_state.logging_menu)
        {
            ui_loggers_window(N_DEVICES, links, ui_link_buffers, &menu_state.logging_menu, &logger_selected_link,
                              config_edit_flags);
        }
        // Tag options window. Tag selection is done through the links window table.
        if (menu_state.tag_menu)
        {
            ui_tag_window(N_DEVICES, links, ui_link_buffers, &menu_state.tag_menu, selected_link_index,
                          selected_tag_index, config_edit_flags);
        }

        // Window containing the different links configs and their associated tags values.
        if (menu_state.devices_menu)
        {
            ui_links_window(N_DEVICES, links, ui_link_buffers, config_update, &menu_state.devices_menu,
                            &selected_link_index, &selected_tag_index, config_edit_flags);
        }

        ImGui::Render();

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.5, 0.5, 0.5, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow *backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwMakeContextCurrent(window);
        glfwSwapBuffers(window);
    }
    printf("Closing\n");
    // Cleanup

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);

    glfwTerminate();

    return EXIT_SUCCESS;
}

static void glfw_error_callback(int error, const char *description)
{
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

/// Spawn a thread to keep polling the device and does not block main.
int polling_thread(void *arg)
{
    if (!arg)
    {
        fprintf(stderr, "Error: Null pointer passed to the thread function.\n");
        return EXIT_FAILURE;
    }
    ThreadArg *arg_ptr = (ThreadArg *)arg;
    Buffer *buf_ptr = arg_ptr->buf_ptr;
    // Get a first copy of the link data.
    Link link = arg_ptr->link;
    ConfigUpdate *config_update_ptr = arg_ptr->config_update_ptr;
    unsigned long timestamp = (unsigned long)time(nullptr);
    int rc;

    // modbus_t *ctx;
    // MbDevice device = cl_device_init_tcp("PLC_1", arg_ptr->id, N_CHANNELS);

    CURL *curl;
    CURLcode curl_res;

    char post_data[POSTDATA_BUF_STRLEN];
    curl = curl_easy_init();

    bool reconnect_flag = false;

    for (;;)
    {
        timestamp = (unsigned long)time(nullptr);
        if (config_update_get(config_update_ptr, &link, &reconnect_flag))
        {
            printf("Config updated: Device %d\n", arg_ptr->id);
        }
        // ctx = modbus_new_tcp(device.ip, device.port);
        if (cl_connect_link(&link) == -1)
        {

            // Hack to get Windows error message.
            wchar_t *s = NULL;
            char err_buffer[60];
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           NULL, WSAGetLastError(), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_UK), (LPWSTR)&s, 0, NULL);
            fprintf(stderr, "Connection failed: %S\n", s);

            link.timestamp = timestamp;
            link.is_error = true;
            // sprintf_s(link.err_msg, "Error while trying to connect to device: %S", s);

            // Make sure to put the data in the buffer so that main can get the error message.
            if (buf_put(buf_ptr, link))
            {
                // printf("Producer N. %d produced data. timestamp: %lu\n", id, timestamp);
            }
            Sleep(2000);
            continue; // Restart
        }
        reconnect_flag = false;
        link.is_error = false;
        for (;;) // Loop until error
        {
            char tag_data_str_buf[TAGSDATA_BUF_STRLEN] = {};
            timestamp = (unsigned long)time(nullptr);
            // Check if there is a configuration update and if we need to reconnect the device.
            if (config_update_get(config_update_ptr, &link, &reconnect_flag))
            {
                printf("Config updated: Device %d\n", arg_ptr->id);
                break;
            }

            for (int i = 0; i < link.tag_count; i++)
            {
                if (!link.tags[i].enabled)
                {
                    // Skip the channel if disabled
                    continue;
                }

                // Read the tag.
                if (cl_read_tag(&link, i) == -1)
                {
                    link.is_error = true;
                    reconnect_flag = true;
                    // indicate that an error happened.
                    // note that each tag holds its own error flag. So, this is redundant.
                }
                char tag_str[TAGDATA_BUF_STRLEN] = {};

                if (!link.tags[i].logged)
                {
                    // Do not concat this channel value to the POST data if it is not logged.
                    continue;
                }
                if (i + 1 == link.tag_count) // We append the timestamp right after the last tag.
                {
                    switch (link.tags[i].value_type)
                    {
                    case VALUE_REAL:
                        sprintf_s(tag_str, "%s=%0.3f %lu", link.tags[i].name, link.tags[i].tag_value.real_value,
                                  timestamp);
                        break;
                    case VALUE_INT:
                        sprintf_s(tag_str, "%s=%d %lu", link.tags[i].name, link.tags[i].tag_value.int_value, timestamp);
                        break;
                    case VALUE_BOOL:
                        sprintf_s(tag_str, "%s=%d %lu", link.tags[i].name, link.tags[i].tag_value.bool_value,
                                  timestamp);
                        break;
                    }
                }
                else
                {
                    switch (link.tags[i].value_type)
                    {
                    case VALUE_REAL:
                        sprintf_s(tag_str, "%s=%0.3f,", link.tags[i].name, link.tags[i].tag_value.real_value);
                        break;
                    case VALUE_INT:
                        sprintf_s(tag_str, "%s=%d,", link.tags[i].name, link.tags[i].tag_value.int_value);
                        break;
                    case VALUE_BOOL:
                        sprintf_s(tag_str, "%s=%d,", link.tags[i].name, link.tags[i].tag_value.bool_value);
                        break;
                    }
                }
                strcat_s(tag_data_str_buf, tag_str);
            }
            link.timestamp = timestamp;

            sprintf_s(post_data, "%s %s", link.name, tag_data_str_buf);

            if (curl)
            {
                struct CurlMemoryStruct chunk;
                chunk.memory = NULL;
                chunk.size = 0;

                struct curl_slist *headers = NULL;
                char token_header[256];

                switch (link.logging_type)
                {
                case 0: {
                    sprintf_s(token_header, "Authorization: Bearer %s", link.token);
                    headers = curl_slist_append(headers, "Content-Type: application/json");
                    break;
                }
                case 1: {
                    sprintf_s(token_header, "Authorization: Token %s", link.token);
                    headers = curl_slist_append(headers, "Content-Type: text/plain; charset=utf-8");
                    // headers = curl_slist_append(headers, "Content-Type: application/json");
                    break;
                }
                default: {
                    sprintf_s(token_header, "Authorization: Bearer %s", link.token);
                    headers = curl_slist_append(headers, "Content-Type: application/json");
                    break;
                }
                }

                headers = curl_slist_append(headers, token_header);

                curl_easy_setopt(curl, CURLOPT_URL, link.url);
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
                curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);

                curl_res = curl_easy_perform(curl);

                if (curl_res != CURLE_OK)
                {
                    printf("CURL Error: %s\n", curl_easy_strerror(curl_res));
                }
                else
                {
                    printf("Response: %s\n", chunk.memory);
                    long http_code = 0;
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                    if (http_code == 204)
                    {
                        link.log_count++;
                    }
                    printf("HTTP Status Code: %ld\n", http_code);
                }
                free(chunk.memory);
            }

            if (buf_put(buf_ptr, link))
            {
                // printf("Producer N. %d produced data. timestamp: %lu\n", id, timestamp);
            }

            if (reconnect_flag) // In case of an error, break out of the loop and reconnect.
            {
                printf("Reconnect requested\n");

                break;
            }

            Sleep(1000);
        }
    }

    // TODO
    // Should clean the links and tags.
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return EXIT_SUCCESS;
}
