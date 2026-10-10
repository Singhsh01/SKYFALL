/* Shims for GLFW entry points that raylib 5.5 calls but older Emscripten GLFW ports
 * (e.g. the Emscripten 3.1.6 packaged by Ubuntu 24.04) do not provide. Both are optional
 * features in a browser (window decoration attributes, OS content-scale notifications). */
#pragma once
#include <GLFW/glfw3.h>
#ifndef SKYFALL_GLFW_COMPAT
#define SKYFALL_GLFW_COMPAT
static inline void skyfall_glfwSetWindowAttrib(GLFWwindow* w, int a, int v) { (void)w; (void)a; (void)v; }
typedef void (*skyfall_GLFWwindowcontentscalefun)(GLFWwindow*, float, float);
static inline skyfall_GLFWwindowcontentscalefun skyfall_glfwSetWindowContentScaleCallback(GLFWwindow* w, skyfall_GLFWwindowcontentscalefun f) { (void)w; (void)f; return 0; }
#define glfwSetWindowAttrib skyfall_glfwSetWindowAttrib
#define glfwSetWindowContentScaleCallback skyfall_glfwSetWindowContentScaleCallback
#endif
