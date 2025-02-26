#include "world.hpp"
#include "vulkan.hpp"

#include "script.hpp"

global_var struct Window_Rendering_Data
{
    // ---- sync objects ----
    VkSemaphore img_ready [2];
    VkSemaphore render_finish [2];
    VkFence cpu_wait [2];

    // ---- command buffer stuff ----
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer[3];

    // ---- data needed to render ----
    u32 image_index;
    u32 in_flight_fram;
} window_rendering;

void
make_game(Vulkan::State *vk, Vulkan::GPU *gpu, Vulkan::Swapchain *swapchain, Window_Data *window)
{
    Vulkan::allocate_command_pool(gpu->queue_families.graphics_family
				  , gpu
				  , &window_rendering.command_pool);

    Vulkan::allocate_command_buffers(&window_rendering.command_pool
				     , VK_COMMAND_BUFFER_LEVEL_PRIMARY
				     , gpu
				     , Memory_Buffer_View<VkCommandBuffer>{3, window_rendering.command_buffer});

    for (u32 i = 0; i < 2; ++i)
    {
        Vulkan::init_semaphore(gpu, &window_rendering.img_ready[i]);
        Vulkan::init_semaphore(gpu, &window_rendering.render_finish[i]);   
        Vulkan::init_fence(gpu, VK_FENCE_CREATE_SIGNALED_BIT, &window_rendering.cpu_wait[i]);
    }
    

    // ---- initialize game data
    make_world(window, vk, &window_rendering.command_pool);

    make_lua_scripting();

    test_script();
}

void
destroy_game(Vulkan::GPU *gpu)
{
    // ---- destroy world data ----
    destroy_world(gpu);
    
    Vulkan::free_command_buffer(Memory_Buffer_View<VkCommandBuffer>{3, window_rendering.command_buffer}
				, &window_rendering.command_pool, gpu);

    vkDestroyCommandPool(gpu->logical_device, window_rendering.command_pool, nullptr);
    vkDestroyFence(gpu->logical_device, window_rendering.cpu_wait[0], nullptr);
    vkDestroyFence(gpu->logical_device, window_rendering.cpu_wait[1], nullptr);
    
    vkDestroySemaphore(gpu->logical_device, window_rendering.render_finish[0], nullptr);
    vkDestroySemaphore(gpu->logical_device, window_rendering.img_ready[0], nullptr);

    vkDestroySemaphore(gpu->logical_device, window_rendering.render_finish[1], nullptr);
    vkDestroySemaphore(gpu->logical_device, window_rendering.img_ready[1], nullptr);
}

void
update_game(Vulkan::GPU *gpu
	    , Vulkan::Swapchain *swapchain
	    , Window_Data *window
	    , Vulkan::State *vk
	    , f32 dt)
{
    // ---- update different parts of the game (world, gui...)
    persist u32 current_frame = 0;
    persist constexpr u32 MAX_FRAMES_IN_FLIGHT = 2;

    current_frame = 0;
    
    Vulkan::wait_fences(gpu, Memory_Buffer_View<VkFence>{1, &window_rendering.cpu_wait[current_frame]});

    VkFence null_fence = VK_NULL_HANDLE;
    
    auto next_image_data = Vulkan::acquire_next_image(swapchain
						      , gpu
						      , &window_rendering.img_ready[current_frame]
						      , &null_fence);
    
    if (next_image_data.result == VK_ERROR_OUT_OF_DATE_KHR)
    {
	// ---- recreate swapchain ----
	return;
    }
    else if (next_image_data.result != VK_SUCCESS && next_image_data.result != VK_SUBOPTIMAL_KHR)
    {
	OUTPUT_DEBUG_LOG("%s\n", "failed to acquire swapchain image");
    }

    // ---- begin recording instructions into the command buffers ----

    update_world(window, vk, dt, next_image_data.image_index, current_frame, &window_rendering.command_buffer[current_frame]);
    // ---- end recording instructions

    VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;;

    Vulkan::reset_fences(gpu, {1, &window_rendering.cpu_wait[current_frame]});
    Vulkan::submit(Memory_Buffer_View<VkCommandBuffer>{1, &window_rendering.command_buffer[current_frame]}
                               , Memory_Buffer_View<VkSemaphore>{1, &window_rendering.img_ready[current_frame]}
                               , Memory_Buffer_View<VkSemaphore>{1, &window_rendering.render_finish[current_frame]}
                               , Memory_Buffer_View<VkPipelineStageFlags>{1, &wait_stages}
                               , &window_rendering.cpu_wait[current_frame]
                               , &gpu->graphics_queue);
    
    VkSemaphore signal_semaphores[] = {window_rendering.render_finish[current_frame]};

    Vulkan::present(Memory_Buffer_View<VkSemaphore>{1, &window_rendering.render_finish[current_frame]}
                                , Memory_Buffer_View<VkSwapchainKHR>{1, &swapchain->swapchain}
                                , &next_image_data.image_index
                                , &gpu->present_queue);
    
    if (next_image_data.result == VK_ERROR_OUT_OF_DATE_KHR || next_image_data.result == VK_SUBOPTIMAL_KHR)
    {
	// recreate swapchain
    }
    else if (next_image_data.result != VK_SUCCESS)
    {
	OUTPUT_DEBUG_LOG("%s\n", "failed to present swapchain image");
    }    

    current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}
