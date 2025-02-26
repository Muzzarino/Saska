#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <iostream>
#include <nlohmann/json.hpp>

#include "memory.hpp"

#include "vulkan.hpp"
#include "core.hpp"
#include "world.hpp"
#include <stdlib.h>

#include "game.hpp"
#include "vulkan.hpp"

#define DEBUG_FILE ".debug"

Debug_Output output_file;
Window_Data window;

internal bool running;

internal void
open_debug_file(void)
{
    output_file.fp = fopen(DEBUG_FILE, "w+");
    assert(output_file.fp >= NULL);
}

internal void
close_debug_file(void)
{
    fclose(output_file.fp);
}

extern void
output_debug(const char *format
	     , ...)
{
    va_list arg_list;
    
    va_start(arg_list, format);

    fprintf(output_file.fp
	    , format
	    , arg_list);

    va_end(arg_list);

    fflush(output_file.fp);
}

f32
barry_centric(const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec3 &p3, const glm::vec2 &pos)
{
    f32 det = (p2.z - p3.z) * (p1.x - p3.x) + (p3.x - p2.x) * (p1.z - p3.z);
    f32 l1 = ((p2.z - p3.z) * (pos.x - p3.x) + (p3.x - p2.x) * (pos.y - p3.z)) / det;
    f32 l2 = ((p3.z - p1.z) * (pos.x - p3.x) + (p1.x - p3.x) * (pos.y - p3.z)) / det;
    f32 l3 = 1.0f - l1 - l2;
    return l1 * p1.y + l2 * p2.y + l3 * p3.y;
}

// window procs:
#define MAX_KEYS 350
#define MAX_MB 5

internal void
glfw_window_resize_proc(GLFWwindow *win, s32 w, s32 h)
{
    Window_Data *w_data = (Window_Data *)glfwGetWindowUserPointer(win);
    w_data->w = w;
    w_data->h = h;
    w_data->window_resized = true;
}

internal void
glfw_keyboard_input_proc(GLFWwindow *win, s32 key, s32 scancode, s32 action, s32 mods)
{
    if (key < MAX_KEYS)
    {
	Window_Data *w_data = (Window_Data *)glfwGetWindowUserPointer(win);
	if (action == GLFW_PRESS) w_data->key_map[key] = true;
	else if (action == GLFW_RELEASE) w_data->key_map[key] = false;
    }
}

internal void
glfw_mouse_position_proc(GLFWwindow *win, f64 x, f64 y)
{
    Window_Data *w_data = (Window_Data *)glfwGetWindowUserPointer(win);

    w_data->prev_m_x = w_data->m_x;
    w_data->prev_m_y = w_data->m_y;
    
    w_data->m_x = x;
    w_data->m_y = y;
    w_data->m_moved = true;
}

internal void
glfw_mouse_button_proc(GLFWwindow *win, s32 button, s32 action, s32 mods)
{
    if (button < MAX_MB)
    {
	Window_Data *w_data = (Window_Data *)glfwGetWindowUserPointer(win);
	if (action == GLFW_PRESS) w_data->mb_map[button] = true;
	else if (action == GLFW_RELEASE) w_data->mb_map[button] = false;
    }
}

#include <chrono>

internal void
init_free_list_allocator_head(Free_List_Allocator *allocator = &free_list_allocator_global)
{
    allocator->free_block_head = (Free_Block_Header *)allocator->start;
    allocator->free_block_head->free_block_size = allocator->available_bytes;
}

s32
main(s32 argc
     , char * argv[])
{
    try
    {
	open_debug_file();
	
	OUTPUT_DEBUG_LOG("%s\n", "starting session");

	linear_allocator_global.capacity = megabytes(megabytes(10));
	linear_allocator_global.start = linear_allocator_global.current = malloc(linear_allocator_global.capacity);
	
	stack_allocator_global.capacity = megabytes(10);
	stack_allocator_global.start = stack_allocator_global.current = malloc(stack_allocator_global.capacity);

	free_list_allocator_global.available_bytes = megabytes(10);
	free_list_allocator_global.start = malloc(free_list_allocator_global.available_bytes);
	init_free_list_allocator_head(&free_list_allocator_global);
	
	OUTPUT_DEBUG_LOG("stack allocator start address : %p\n", stack_allocator_global.current);
    
	if (!glfwInit())
	{
	    return(-1);
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	allocate_memory_buffer(window.key_map, MAX_KEYS);
	allocate_memory_buffer(window.mb_map, MAX_MB);
	
	window.w = 1280;
	window.h = 720;
	
	window.window = glfwCreateWindow(window.w
					 , window.h
					 , "Game"
					 , NULL
					 , NULL);

	if (!window.window)
	{
	    glfwTerminate();
	    return(-1);
	}

	glfwSetInputMode(window.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwSetWindowUserPointer(window.window, &window);
	glfwSetKeyCallback(window.window, glfw_keyboard_input_proc);
	glfwSetMouseButtonCallback(window.window, glfw_mouse_button_proc);
	glfwSetCursorPosCallback(window.window, glfw_mouse_position_proc);
	glfwSetWindowSizeCallback(window.window, glfw_window_resize_proc);

	Vulkan::State vk = {};

	Vulkan::init_state(&vk, window.window);


	make_game(&vk
		  , &vk.gpu
		  , &vk.swapchain
		  , &window);
	
	

	f32 fps = 0.0f;

        {
            f64 m_x, m_y;
            glfwGetCursorPos(window.window, &m_x, &m_y);
            window.m_x = (f32)m_x;
            window.m_y = (f32)m_y;
        }
        
	auto now = std::chrono::high_resolution_clock::now();
	while(!glfwWindowShouldClose(window.window))
	{
	    glfwPollEvents();
            update_game(&vk.gpu, &vk.swapchain, &window, &vk, window.dt);	   

	    if (glfwGetKey(window.window, GLFW_KEY_F))
	    {
		printf("%f\n", 1.0f / window.dt);
	    }
	    
	    clear_linear();

            window.m_moved = false;
            
	    auto new_now = std::chrono::high_resolution_clock::now();
	    window.dt = std::chrono::duration<f32, std::chrono::seconds::period>(new_now - now).count();

            /*	auto new_now = std::chrono::high_resolution_clock::now();
            window.dt = std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(new_now - now).count();
	    now = new_now;*/
            
	    now = new_now;
	}

	OUTPUT_DEBUG_LOG("stack allocator start address is : %p\n", stack_allocator_global.current);
	OUTPUT_DEBUG_LOG("stack allocator allocated %d bytes\n", (u32)((u8 *)stack_allocator_global.current - (u8 *)stack_allocator_global.start));
	
	OUTPUT_DEBUG_LOG("finished session : FPS : %f\n", fps);

	//	printf("%f\n", fps);

	std::cout << std::endl;
	
	close_debug_file();

	// destroy rnd and vk
        vkDeviceWaitIdle(vk.gpu.logical_device);
        Vulkan::destroy_swapchain(&vk);
	destroy_game(&vk.gpu);
	
	Vulkan::destroy_state(&vk);
	
	glfwDestroyWindow(window.window);
	glfwTerminate();

        std::cout << "Finished session" << std::endl;
    }
    catch(nlohmann::json::parse_error &e)
    {
	std::cout << e.what() << " : " << e.id << " : " << e.byte << std::endl;
	close_debug_file();
    }

    std::cin.get();
    
    return(0);
}

