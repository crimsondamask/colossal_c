// colossal_cpp.cpp : This file contains the 'main' function. Program execution
// begins and ends there.
/// @file colossal.cpp

#include "colossal.h"
#include "curl/curl.h"
#include "curl/easy.h"
#include "imgui/GLFW/glfw3.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "libmodbus/modbus.h"
#include "mb_device.h"
#include <cstdint>
#include <cstring>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <windows.h>

#define N_CHANNELS 15
#define N_DEVICES 3
#define N_FRAMES_UNTIL_CONS 60

#define POSTDATA_BUF_STRLEN 2048
#define TAGSDATA_BUF_STRLEN 1024
#define TAGDATA_BUF_STRLEN 64

int polling_thread(void *arg);
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
    GLFWwindow *window = glfwCreateWindow(1280, 720, "Colossal 0.1", nullptr, nullptr);

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
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup renderer

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load system font.
    io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 24.0f);

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
    MbDevice mb_devices[N_DEVICES];
    // UI buffers to hold the GUI data
    MbDevice ui_device_buffers[N_DEVICES];

    UiMenuState menu_state = {};
    // We use this so we don't lock the mutex each frame.
    bool frames_exceeded = false;
    size_t frame_count = 0;

    bool selected_channel[N_DEVICES][N_CHANNELS] = {};
    int config_edit_flags[N_DEVICES] = {};

    int logger_selected_device = 0;
    int selected_device_index = 0;
    int selected_channel_index = 0;

    // Initialize each buffer for 10 products.
    // Can only keep 10 products at a time.
    // This is enough as the main thread will
    // keep consuming the products.
    // If the products are not consumed and the buffer is full,
    // the polling thread will stop polling the device.
    // This will probably change once we implement the logging functionality.
    for (int i = 0; i < N_DEVICES; i++)
    {
        // Initialize all the devices.
        // This is needed to allocate the required memory for channels
        // so that the GUI can access them.
        mb_devices[i] = cl_device_init_tcp("PLC", i + 1, N_CHANNELS);

        ui_device_buffers[i] = cl_device_init_tcp("PLC", i + 1, N_CHANNELS);
        // Initialize the buffers
        buf_init(&buf[i], 10);
        // and the config update so we can send updates to the threads.
        config_update_init(&config_update[i]);
    }

    for (int i = 0; i < N_DEVICES; i++)
    {

        thread_arg[i].id = i + 1;
        thread_arg[i].buf_ptr = &buf[i];
        thread_arg[i].config_update_ptr = &config_update[i];

        if (thrd_create(&th[i], polling_thread, (void *)&thread_arg[i]) != thrd_success)
        {
            fprintf(stderr, "Could not spawn thread.\n");
            return EXIT_FAILURE;
        }

        // We don't have to wait for the thread to finish.
        thrd_detach(th[i]);
    }

    // The main loop
    while (!glfwWindowShouldClose(window))
    {
        // Main event loop.
        // Poll and handle events.
        glfwPollEvents();

        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        // Start the Imgui frame.
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ++frame_count;

        // Consume the data in the buffer each N_FRAMES...
        if (frame_count >= N_FRAMES_UNTIL_CONS)
        {
            for (size_t i = 0; i < N_DEVICES; i++)
            {
                // Get the device data from the threads buffers and put it in the
                // mb_devices[] for display
                while (buf_get(&buf[i], &mb_devices[i], 1))
                {
                    // do something
                }
                frames_exceeded = true;
            }

            frame_count = 0;
        }
        if (ImGui::BeginMainMenuBar())
        {
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

        if (menu_state.logging_menu)
        {
            if (ImGui::Begin("Logging Config"))
            {

                MbDevice *ui_device_buffer = &ui_device_buffers[logger_selected_device];
                const char *device_names[N_DEVICES] = {};

                // Populate the combobox values with device names.
                for (int i = 0; i < N_DEVICES; i++)
                {
                    device_names[i] = mb_devices[i].name;
                }

                if (ImGui::Combo("Device", &logger_selected_device, device_names, IM_ARRAYSIZE(device_names)))
                {
                }
                ImGui::Text("%s Logging Details", mb_devices[logger_selected_device].name);

                if (ImGui::InputText("API Token", ui_device_buffer->token, IM_ARRAYSIZE(ui_device_buffer->token),
                                     ImGuiInputTextFlags_CharsNoBlank))
                {
                    config_edit_flags[selected_device_index] |= CONFIG_EDIT_DEVICE_CONFIG;
                }

                if (ImGui::InputText("Database URL", ui_device_buffer->url, IM_ARRAYSIZE(ui_device_buffer->url),
                                     ImGuiInputTextFlags_CharsNoBlank))
                {
                    config_edit_flags[selected_device_index] |= CONFIG_EDIT_DEVICE_CONFIG;
                }
                const char *logging_method[] = {"LOCAL", "REMOTE"};
                if (ImGui::Combo("Logging Method", &ui_device_buffer->logging_type, logging_method,
                                 IM_ARRAYSIZE(logging_method)))
                {
                    config_edit_flags[selected_device_index] |= CONFIG_EDIT_DEVICE_CONFIG;
                }
                ImGui::Text("Logging Count: %lu", mb_devices[logger_selected_device].log_count);
            }
            ImGui::End();
        }
        if (menu_state.tag_menu)
        {
            if (ImGui::Begin("Properties"))
            {

                MbDevice *ui_device_buffer = &ui_device_buffers[selected_device_index];

                ImGui::Text("%s:%s Details", mb_devices[selected_device_index].name,
                            mb_devices[selected_device_index].channels[selected_channel_index].tag);

                if (ImGui::InputText("Tag", ui_device_buffer->channels[selected_channel_index].tag,
                                     IM_ARRAYSIZE(ui_device_buffer->channels[selected_channel_index].tag),
                                     ImGuiInputTextFlags_CharsNoBlank))
                {
                    config_edit_flags[selected_device_index] |= CONFIG_EDIT_CHANNEL_CONFIG;
                }

                if (ImGui::InputText("Description", ui_device_buffer->channels[selected_channel_index].description,
                                     IM_ARRAYSIZE(ui_device_buffer->channels[selected_channel_index].description)))
                {
                    config_edit_flags[selected_device_index] |= CONFIG_EDIT_CHANNEL_CONFIG;
                }
                if (ImGui::InputText("Unit", ui_device_buffer->channels[selected_channel_index].unit,
                                     IM_ARRAYSIZE(ui_device_buffer->channels[selected_channel_index].unit)))
                {
                    config_edit_flags[selected_device_index] |= CONFIG_EDIT_CHANNEL_CONFIG;
                }

                const char *value_types[] = {"COIL", "INT", "REAL - Uses 2 registers"};
                static int value_type = 0;
                if (ImGui::Combo("Value Type", &ui_device_buffer->channels[selected_channel_index].channel_type,
                                 value_types, IM_ARRAYSIZE(value_types)))
                {
                    config_edit_flags[selected_device_index] |= CONFIG_EDIT_CHANNEL_CONFIG;
                }
                if (ImGui::InputInt("Address", &ui_device_buffer->channels[selected_channel_index].address))
                {
                    config_edit_flags[selected_device_index] |= CONFIG_EDIT_CHANNEL_CONFIG;
                }
            }
            ImGui::End();
        }

        if (ImGui::Begin("Devices"))
        {
            ImGui::Checkbox("Show Demo", &app.show_demo_window);

            // ImGui::ColorEdit3("Clear Color", (float *)&app.clear_color);

            for (int i = 0; i < IM_ARRAYSIZE(mb_devices); i++)
            {

                ImGui::PushID(i);

                MbDevice *device = &mb_devices[i];
                // Used to hold UI data and persist it across frames.
                // The use of pointers here is important as we don't want
                // to just copy the buffer. We want to mutate the buffer state outside of
                // the event loop.
                MbDevice *ui_device_buffer = &ui_device_buffers[i];

                // A little hack to show an asterics when the config is edited.
                char collapsing_header_title[32];
                if (config_edit_flags[i])
                {
                    sprintf_s(collapsing_header_title, "Device Config *");
                }
                else
                {
                    sprintf_s(collapsing_header_title, "Device Config");
                }

                if (ImGui::CollapsingHeader(collapsing_header_title, ImGuiTreeNodeFlags_Bullet))
                {
                    if (ImGui::InputText("Name", ui_device_buffer->name, IM_ARRAYSIZE(ui_device_buffer->ip),
                                         ImGuiInputTextFlags_CharsNoBlank)

                    )
                    {
                        config_edit_flags[i] |= CONFIG_EDIT_DEVICE_CONFIG;
                    }
                    if (ImGui::InputText("IP Address", ui_device_buffer->ip, IM_ARRAYSIZE(ui_device_buffer->ip)))
                    {
                        config_edit_flags[i] |= CONFIG_EDIT_DEVICE_CONFIG;
                    }

                    if (ImGui::InputInt("Port", &ui_device_buffer->port))
                    {
                        config_edit_flags[i] |= CONFIG_EDIT_DEVICE_CONFIG;
                    }
                }
                // Button to send config update to the threads
                if (ImGui::Button("Reconfigure"))
                {
                    device = ui_device_buffer;
                    if (config_update_put(&config_update[i], device, true))
                    {
                        // Reset the config change indication flags.
                        config_edit_flags[i] = 0;
                    }
                }
                if (device->is_error)
                {
                    ImGui::Text("Device ID: %d. ERROR: %s", device->id, device->error_msg);
                }
                else
                {
                    ImVec2 outer_size = ImVec2(0.0f, 200.0f);
                    if (ImGui::BeginTable("Device Data", 6,
                                          ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders |
                                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_RowBg,
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
                        for (int j = 0; j < device->channel_count; j++)
                        {
                            char selectable_label[32];
                            bool set_selected = false;
                            sprintf_s(selectable_label, "%s", device->channels[j].tag);
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();

                            if (selected_device_index == i && selected_channel_index == j)
                            {
                                set_selected = true;
                            }
                            else
                            {
                                set_selected = false;
                            }
                            if (ImGui::Selectable(selectable_label, set_selected, ImGuiSelectableFlags_SpanAllColumns))
                            {
                                selected_device_index = i;
                                selected_channel_index = j;
                            }
                            // ImGui::Text("CH%d", device->channels[j].id);
                            ImGui::TableNextColumn();
                            ImGui::Text("%0.3f", device->channels[j].value);
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", device->channels[j].unit);
                            ImGui::TableNextColumn();
                            switch (device->channels[j].channel_type)
                            {
                            case 1:
                                ImGui::Text("INT");
                                break;
                            case 2:
                                ImGui::Text("REAL");
                                break;
                            default:
                                ImGui::Text("INT");
                            }
                            ImGui::TableNextColumn();
                            ImGui::Text("%d", device->channels[j].address);
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", device->channels[j].description);
                        }
                        ImGui::EndTable();
                    }
                }

                ImGui::PopID();
            }
        }
        ImGui::End();

        ImGui::Render();

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(app.clear_color.x * app.clear_color.w, app.clear_color.y * app.clear_color.w,
                     app.clear_color.z * app.clear_color.w, app.clear_color.w);
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
    ConfigUpdate *config_update_ptr = arg_ptr->config_update_ptr;
    unsigned long timestamp = (unsigned long)time(nullptr);
    int rc;

    modbus_t *ctx;
    MbDevice device = cl_device_init_tcp("PLC_1", arg_ptr->id, N_CHANNELS);

    CURL *curl;
    CURLcode curl_res;

    char post_data[POSTDATA_BUF_STRLEN];
    curl = curl_easy_init();

    float test_value = 0.0;
    bool reconnect_flag = false;

    for (;;)
    {
        timestamp = (unsigned long)time(nullptr);
        if (config_update_get(config_update_ptr, &device, &reconnect_flag))
        {
            printf("Config updated: Device %d\n", arg_ptr->id);
        }
        ctx = modbus_new_tcp(device.ip, device.port);
        if (modbus_connect(ctx) == -1)
        {

            // Hack to get Windows error message.
            wchar_t *s = NULL;
            char err_buffer[60];
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           NULL, WSAGetLastError(), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_UK), (LPWSTR)&s, 0, NULL);
            fprintf(stderr, "Connection failed: %S\n", s);

            device.timestamp = timestamp;
            device.is_error = true;
            sprintf_s(err_buffer, "Error while trying to connect to device: %S", s);
            device.error_msg = err_buffer;
            // Make sure to put the data in the buffer so that main can get the error message.
            if (buf_put(buf_ptr, device))
            {
                // printf("Producer N. %d produced data. timestamp: %lu\n", id, timestamp);
            }
            Sleep(2000);
            continue; // Restart
        }
        reconnect_flag = false;
        device.is_error = false;
        for (;;) // Loop until error
        {
            char tag_data_str_buf[TAGSDATA_BUF_STRLEN] = {};
            timestamp = (unsigned long)time(nullptr);
            // Check if there is a configuration update and if we need to reconnect the device.
            if (config_update_get(config_update_ptr, &device, &reconnect_flag))
            {
                printf("Config updated: Device %d\n", arg_ptr->id);
            }

            for (int i = 0; i < device.channel_count; i++)
            {
                char tag_str[TAGDATA_BUF_STRLEN] = {};

                uint16_t read_buf[2] = {};
                int read_rc;
                switch (device.channels[i].channel_type)
                {
                case 2:
                    read_rc = modbus_read_registers(ctx, device.channels[i].address, 2, read_buf);
                    device.channels[i].value = modbus_get_float_abcd(read_buf);
                    break;
                case 1:
                    read_rc = modbus_read_registers(ctx, device.channels[i].address, 1, read_buf);
                    device.channels[i].value = (read_buf[0]);
                    break;
                default:
                    read_rc = modbus_read_registers(ctx, device.channels[i].address, 1, read_buf);
                    device.channels[i].value = (read_buf[0]);
                    break;
                }
                if (read_rc == -1)
                {
                    fprintf(stderr, "Read failed: %s\n", modbus_strerror(errno));
                    // Try to reconnect.
                    // Temporary. TODO: we should check errno and only try to reconnect if it is a socket error.
                    reconnect_flag = true;
                    device.is_error = true;
                    device.error_msg = modbus_strerror(errno);
                    break;
                }

                if (i + 1 == device.channel_count)
                {
                    if (sprintf_s(tag_str, "%s=%0.3f %lu", device.channels[i].tag, device.channels[i].value,
                                  timestamp) == -1)
                    {
                    }
                }
                else
                {
                    sprintf_s(tag_str, "%s=%0.3f,", device.channels[i].tag, device.channels[i].value);
                }
                strcat_s(tag_data_str_buf, tag_str);
            }
            device.timestamp = timestamp;

            sprintf_s(post_data, "%s %s", device.name, tag_data_str_buf);

            if (curl)
            {
                struct CurlMemoryStruct chunk;
                chunk.memory = NULL;
                chunk.size = 0;

                struct curl_slist *headers = NULL;
                char token_header[256];

                switch (device.logging_type)
                {
                case 0: {
                    sprintf_s(token_header, "Authorization: Bearer %s", device.token);
                    headers = curl_slist_append(headers, "Content-Type: application/json");
                    break;
                }
                case 1: {
                    sprintf_s(token_header, "Authorization: Token %s", device.token);
                    headers = curl_slist_append(headers, "Content-Type: text/plain; charset=utf-8");
                    break;
                }
                default: {
                    sprintf_s(token_header, "Authorization: Bearer %s", device.token);
                    headers = curl_slist_append(headers, "Content-Type: application/json");
                    break;
                }
                }

                headers = curl_slist_append(headers, token_header);

                curl_easy_setopt(curl, CURLOPT_URL, device.url);
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
                        device.log_count++;
                    }
                    printf("HTTP Status Code: %ld\n", http_code);
                }
                free(chunk.memory);
            }

            if (buf_put(buf_ptr, device))
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

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    modbus_free(ctx);
    cl_device_destroy(&device);
    return EXIT_SUCCESS;
}
