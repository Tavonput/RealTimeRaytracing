#pragma once

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"



class Gui {

public: 
	void init();
	void cleanup();
};