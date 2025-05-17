// colossal_cpp.cpp : This file contains the 'main' function. Program execution
// begins and ends there.
/// @file colossal.cpp

#include "colossal.h"
#include "imgui/GLFW/glfw3.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "libmodbus/modbus.h"
#include "mb_device.h"
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>
#include <windows.h>

#define N_CHANNELS 20
#define N_DEVICES 1
#define N_FRAMES_UNTIL_CONS 60

// Win32 Thread function to keep polling the device;
// DWORD WINAPI polling_thread(LPVOID lpParam);
// C11 thread function.
int polling_thread(void *arg);
static void glfw_error_callback(int error, const char *description);

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
    // Arguments to pass to each thread.
    ThreadArg thread_arg[N_DEVICES];
    // Device list
    MbDevice mb_devices[N_DEVICES];
    bool data_ready = false;

    size_t frame_count = 0;
    // Initialize each buffer for 10 products.
    // Can only keep 10 products at a time.
    // This is enough as the main thread will
    // keep consuming the products.
    // If the products are not consumed and the buffer is full,
    // the polling thread will stop polling the device.
    // This will probably change once we implement the logging functionality.
    for (int i = 0; i < N_DEVICES; i++)
    {
        buf_init(&buf[i], 10);
    }

    for (int i = 0; i < N_DEVICES; i++)
    {
        thread_arg[i].id = i + 1;
        thread_arg[i].buf_ptr = &buf[i];

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
        // Consume the data in the buffer

        if (frame_count >= N_FRAMES_UNTIL_CONS)
        {
            for (size_t i = 0; i < N_DEVICES; i++)
            {
                while (buf_get(&buf[i], &mb_devices[i], 1))
                {
                }
                data_ready = true;
            }

            frame_count = 0;
        }
        // Show demo window for tests.
        if (app.show_demo_window)
        {
            ImGui::ShowDemoWindow(&app.show_demo_window);
        }

        {
            static float f = 0.0f;
            static int count = 0;

            ImGui::Begin("Main Window");

            ImGui::Text("Sample text...");
            ImGui::Checkbox("Show Demo", &app.show_demo_window);

            ImGui::SliderFloat("float", &f, 0.0f, 100.0f);
            ImGui::ColorEdit3("Clear Color", (float *)&app.clear_color);

            if (ImGui::Button("Increment"))
            {
                count++;
            }
            ImGui::SameLine();
            ImGui::Text("Counter: %d", count);

            if (data_ready)
            {
                for (size_t i = 0; i < N_DEVICES; i++)
                {
                    for (size_t j = 0; j < mb_devices[i].channel_count; j++)
                    {
                        ImGui::Text("Device ID: %d. CH%d: %f", mb_devices[i].id, mb_devices[i].channels[j].id,
                                    mb_devices[i].channels[j].value);
                    }
                }
            }

            ImGui::End();
        }

        // Rendering
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
    int id = arg_ptr->id;
    Buffer *buf_ptr = arg_ptr->buf_ptr;
    unsigned long timestamp = (unsigned long)time(nullptr);
    int rc;

    modbus_t *ctx;
    MbDevice device = cl_device_init_tcp("PLC_1", N_CHANNELS);
    device.id = id;

    ctx = modbus_new_tcp(device.ip, device.port);

    if (modbus_connect(ctx) == -1)
    {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return EXIT_FAILURE;
    }

    for (;;) // Loop forever
    {
        for (int i = 0; i < device.channel_count; i++)
        {
            uint16_t read_buf[2];
            int read_rc = modbus_read_registers(ctx, device.channels[i].address, 2, read_buf);
            if (read_rc == -1)
            {
                fprintf(stderr, "Read failed: %s\n", modbus_strerror(errno));
                modbus_free(ctx);
                return EXIT_FAILURE;
            }

            device.channels[i].value = modbus_get_float_abcd(read_buf);
            printf("CH%d: %f\n", device.channels[i].id, device.channels[i].value);
        }
        device.timestamp = timestamp;

        if (buf_put(buf_ptr, device))
        {
            // printf("Producer N. %d produced data. timestamp: %lu\n", id, timestamp);
        }
        Sleep(1000);
    }
    modbus_free(ctx);
    cl_device_destroy(&device);
    return EXIT_SUCCESS;
}
